/*
 * Copyright (C) 2026 OpenBoard-ML contributors
 *
 * This file is part of OpenBoard.
 *
 * OpenBoard is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License,
 * with a specific linking exception for the OpenSSL project's
 * "OpenSSL" library (or with modified versions of it that use the
 * same license as the "OpenSSL" library).
 *
 * OpenBoard is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with OpenBoard. If not, see <http://www.gnu.org/licenses/>.
 */

// The relay for shared OpenBoard-ML sessions. One Durable Object per session
// ("room") passes WebSocket messages on between the participants of that room.
// The messages are encrypted by the participants; the relay only reads the
// four bytes in front of each message that say to whom it goes. The protocol
// is described in README.md.

import { DurableObject } from "cloudflare:workers";

const PROTOCOL_VERSION = 1;

// participants per room, those whose line dropped and who may still come back included
const MAX_PEERS = 8;

// largest data message a participant may send, the four address bytes included;
// far below the 32 MiB that the platform accepts, so one message never keeps the room busy for long
const MAX_MESSAGE = 1024 * 1024;

// a participant whose line dropped keeps its place this long, so it can come back with the same peer id
const DEFAULT_AWAY_GRACE_MS = 90 * 1000;

// a line on which nothing arrived for this long (not even a keep-alive) is treated as dropped
const STALE_MS = 100 * 1000;

// while a room is in use, it checks once an hour for lines that were lost without a close event
const CHECK_INTERVAL_MS = 60 * 60 * 1000;

// at most this many data messages per participant in a window; drawing sends far fewer
const RATE_WINDOW_MS = 10 * 1000;
const RATE_MAX_MESSAGES = 3000;

const ROOM_PATH = /\/v(\d+)\/rooms\/([^/]*)$/;
const ROOM_ID = /^[0-9a-f]{64}$/;
const RESUME_TOKEN = /^[A-Za-z0-9_-]{43}$/;

// close codes of the relay, next to the standard 1000 (left), 1008 (policy) and 1009 (too big)
const CLOSE_ENDED = 4000;       // the session ended: the participant who started it left
const CLOSE_REPLACED = 4001;    // the same participant connected again on a new line
const CLOSE_STALE = 4002;       // nothing arrived for too long

const INFO_PAGE = `<!DOCTYPE html>
<html lang="en">
<head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>OpenBoard-ML relay</title></head>
<body style="font-family: sans-serif; max-width: 36em; margin: 3em auto; padding: 0 1em; line-height: 1.5">
<h1>OpenBoard-ML relay</h1>
<p>This address passes on the messages of shared OpenBoard-ML sessions. There is nothing to see here in a browser.</p>
<p>If somebody sent you this address as an invitation, copy the whole line you received
(including the part after the <code>#</code>) and paste it into OpenBoard-ML to join the session.</p>
</body>
</html>
`;

function refuse(status, reason, text) {
    return new Response(text + "\n", {
        status,
        headers: {
            "Content-Type": "text/plain; charset=utf-8",
            "OBML-Relay": String(PROTOCOL_VERSION),
            "OBML-Reason": reason,
            "Cache-Control": "no-store",
        },
    });
}

async function sha256(text) {
    return crypto.subtle.digest("SHA-256", new TextEncoder().encode(text));
}

// compares two secrets without leaking through timing where they differ (or how long the secret is)
async function sameSecret(a, b) {
    const [x, y] = await Promise.all([sha256(a), sha256(b)]);
    return crypto.subtle.timingSafeEqual(x, y);
}

// for two hex digests of equal length; synchronous, so that nothing else runs in the room in between
function sameDigest(a, b) {
    const encoder = new TextEncoder();
    const x = encoder.encode(a);
    const y = encoder.encode(b);
    return x.byteLength === y.byteLength && crypto.subtle.timingSafeEqual(x, y);
}

async function tokenHash(token) {
    return [...new Uint8Array(await sha256(token))].map((b) => b.toString(16).padStart(2, "0")).join("");
}

export default {
    async fetch(request, env) {
        const url = new URL(request.url);
        const match = url.pathname.match(ROOM_PATH);

        if (!match) {
            if ((request.method === "GET" || request.method === "HEAD") && (url.pathname === "/" || url.pathname === "/index.html")) {
                return new Response(request.method === "HEAD" ? null : INFO_PAGE, {
                    headers: { "Content-Type": "text/html; charset=utf-8", "OBML-Relay": String(PROTOCOL_VERSION) },
                });
            }
            return new Response("Not found\n", { status: 404 });
        }

        if (match[1] !== String(PROTOCOL_VERSION)) {
            return refuse(426, "version", `This relay speaks version ${PROTOCOL_VERSION} of the OpenBoard-ML relay protocol.`);
        }
        if (request.method !== "GET" || (request.headers.get("Upgrade") || "").toLowerCase() !== "websocket") {
            return refuse(400, "bad-request", "Expected a WebSocket connection.");
        }

        const roomId = match[2];
        const role = request.headers.get("OBML-Role") || "";
        const token = request.headers.get("OBML-Resume") || "";
        const peer = request.headers.get("OBML-Peer") || "";
        if (!ROOM_ID.test(roomId) || !["start", "join", "resume"].includes(role) || !RESUME_TOKEN.test(token)
            || (role === "resume" && !/^[1-9][0-9]{0,9}$/.test(peer))) {
            return refuse(400, "bad-request", "Malformed request.");
        }

        // checked here, so that a stranger without the key never wakes a room
        if (role === "start" && env.HOST_KEY) {
            const auth = request.headers.get("Authorization") || "";
            const given = auth.startsWith("Bearer ") ? auth.slice(7) : "";
            if (!(await sameSecret(given, env.HOST_KEY))) {
                return refuse(403, "refused", "This relay needs a key to start a session.");
            }
        }

        // only what the room needs goes on: the key stays here
        const headers = new Headers({ "Upgrade": "websocket", "OBML-Role": role, "OBML-Resume": token });
        if (role === "resume") {
            headers.set("OBML-Peer", peer);
        }
        const stub = env.ROOMS.get(env.ROOMS.idFromName(roomId));
        return stub.fetch(new Request(url.toString(), { headers }));
    },
};

// One session. Who belongs to the room is kept in storage, one record per
// participant ("peer:<id>": peer id, host flag, hash of the resume token, and
// until when the place is kept when the line dropped), so that it survives the
// object being evicted from memory or restarted. The lines themselves are
// hibernatable WebSockets, so an idle room costs nothing. Nothing about the
// content of the messages is stored.
export class Room extends DurableObject {
    constructor(ctx, env) {
        super(ctx, env);

        // keep-alives are answered by the runtime without waking the object
        ctx.setWebSocketAutoResponse(new WebSocketRequestResponsePair("ping", "pong"));

        const grace = Number(env.AWAY_GRACE_SECONDS);
        this.awayGraceMs = grace >= 2 && grace <= 600 ? grace * 1000 : DEFAULT_AWAY_GRACE_MS;

        // in memory only: lost when the object is evicted, which is fine for both
        this.rates = new Map();
        this.lastSeen = new Map();
        this.lastSweep = 0;
    }

    records() {
        const map = new Map();
        for (const [, record] of this.ctx.storage.kv.list({ prefix: "peer:" })) {
            map.set(record.peer, record);
        }
        return map;
    }

    store(record) {
        this.ctx.storage.kv.put("peer:" + record.peer, record);
    }

    // the open lines, by peer id
    lines() {
        const map = new Map();
        for (const ws of this.ctx.getWebSockets()) {
            const a = ws.deserializeAttachment();
            if (a && !a.gone) {
                map.set(a.peer, ws);
            }
        }
        return map;
    }

    send(ws, object) {
        try {
            ws.send(typeof object === "string" ? object : JSON.stringify(object));
        } catch (e) {
            // the line is closing; its close event follows
        }
    }

    broadcast(object, exceptPeer) {
        for (const [peer, ws] of this.lines()) {
            if (peer !== exceptPeer) {
                this.send(ws, object);
            }
        }
    }

    newPeerId(taken) {
        const value = new Uint32Array(1);
        for (;;) {
            crypto.getRandomValues(value);
            if (value[0] !== 0 && !taken.has(value[0])) {
                return value[0];
            }
        }
    }

    closeLine(ws, code, reason) {
        const a = ws.deserializeAttachment();
        ws.serializeAttachment({ ...a, gone: true });
        try {
            ws.close(code, reason);
        } catch (e) {
            // already closed
        }
    }

    async fetch(request) {
        // the only wait comes first: from here on until the line is accepted, nothing else runs in the room
        const hash = await tokenHash(request.headers.get("OBML-Resume"));
        const role = request.headers.get("OBML-Role");
        const now = Date.now();
        this.sweep(now);

        const records = this.records();
        const lines = this.lines();
        let record;
        let wasAway = false;

        if (role === "resume") {
            const peer = Number(request.headers.get("OBML-Peer"));
            record = records.get(peer);
            if (!record || !sameDigest(hash, record.token)) {
                return refuse(410, "gone", "This participant is no longer in the session.");
            }
            const old = lines.get(peer);
            if (old) {
                // the old line has not noticed yet that it is dead
                this.closeLine(old, CLOSE_REPLACED, "replaced by a newer connection");
            }
            wasAway = record.until > 0;
            record = { ...record, until: 0 };
        } else if (role === "start") {
            if (records.size > 0) {
                return refuse(409, "exists", "A session with this code is already running.");
            }
            record = { peer: this.newPeerId(new Set()), host: true, token: hash, until: 0 };
        } else {
            if (records.size === 0) {
                return refuse(404, "not-found", "Nobody is in a session with this code.");
            }
            if (records.size >= MAX_PEERS) {
                return refuse(409, "full", `The session is full (at most ${MAX_PEERS} participants).`);
            }
            record = { peer: this.newPeerId(new Set(records.keys())), host: false, token: hash, until: 0 };
        }
        this.store(record);
        records.set(record.peer, record);

        const pair = new WebSocketPair();
        const client = pair[0];
        const server = pair[1];
        this.ctx.acceptWebSocket(server, ["p" + record.peer]);
        server.serializeAttachment({ peer: record.peer, since: now });
        this.lastSeen.set(record.peer, now);

        const others = [...records.values()].filter((x) => x.peer !== record.peer);
        const host = [...records.values()].find((x) => x.host);
        this.send(server, {
            type: "welcome",
            v: PROTOCOL_VERSION,
            peer: record.peer,
            host: host ? host.peer : 0,
            peers: others.map((x) => x.peer),
            away: others.filter((x) => x.until > 0).map((x) => x.peer),
            resumed: role === "resume",
            maxMessage: MAX_MESSAGE,
            maxPeers: MAX_PEERS,
        });

        if (role === "join") {
            this.broadcast({ type: "join", peer: record.peer }, record.peer);
        } else if (wasAway) {
            this.broadcast({ type: "back", peer: record.peer }, record.peer);
        }
        await this.scheduleAlarm();

        return new Response(null, { status: 101, webSocket: client });
    }

    async webSocketMessage(ws, message) {
        const a = ws.deserializeAttachment();
        if (!a || a.gone) {
            return;
        }
        const now = Date.now();
        this.lastSeen.set(a.peer, now);

        if (typeof message === "string") {
            // "ping" is normally answered by the auto-response; nothing else is sent as text
            if (message === "ping") {
                this.send(ws, "pong");
                return;
            }
            await this.drop(ws, a.peer, 1003, "text messages are not part of the protocol", true);
            return;
        }

        if (message.byteLength < 4) {
            await this.drop(ws, a.peer, 1008, "message without address", true);
            return;
        }
        if (message.byteLength > MAX_MESSAGE) {
            await this.drop(ws, a.peer, 1009, "message too big", true);
            return;
        }

        let rate = this.rates.get(a.peer);
        if (!rate || now - rate.start > RATE_WINDOW_MS) {
            rate = { start: now, count: 0 };
            this.rates.set(a.peer, rate);
        }
        if (++rate.count > RATE_MAX_MESSAGES) {
            await this.drop(ws, a.peer, 1008, "too many messages", true);
            return;
        }

        // the four address bytes become the sender's peer id; the rest goes on untouched
        const view = new DataView(message);
        const to = view.getUint32(0);
        view.setUint32(0, a.peer);

        const targets = to === 0 ? this.ctx.getWebSockets() : this.ctx.getWebSockets("p" + to);
        for (const target of targets) {
            if (target === ws) {
                continue;
            }
            const t = target.deserializeAttachment();
            if (t && !t.gone && t.peer !== a.peer) {
                try {
                    target.send(message);
                } catch (e) {
                    // closing; its close event follows
                }
            }
        }

        if (now - this.lastSweep > STALE_MS / 4) {
            this.sweep(now);
        }
    }

    async webSocketClose(ws, code, reason, wasClean) {
        const a = ws.deserializeAttachment();
        if (!a || a.gone) {
            return;
        }
        // 1000: the participant left the session; 1001: its program quit. Anything else is a dropped line.
        await this.drop(ws, a.peer, 1000, "", code === 1000 || code === 1001);
    }

    async webSocketError(ws, error) {
        const a = ws.deserializeAttachment();
        if (a && !a.gone) {
            await this.drop(ws, a.peer, 1011, "connection error", false);
        }
    }

    // a line ends: either the participant left (\a left), or it may come back on a new line
    async drop(ws, peer, code, reason, left) {
        this.closeLine(ws, code, reason);
        this.rates.delete(peer);
        this.lastSeen.delete(peer);

        if (left) {
            await this.leave(peer);
            return;
        }
        const record = this.ctx.storage.kv.get("peer:" + peer);
        if (record) {
            this.store({ ...record, until: Date.now() + this.awayGraceMs });
            this.broadcast({ type: "away", peer }, peer);
        }
        await this.scheduleAlarm();
    }

    async leave(peer) {
        const record = this.ctx.storage.kv.get("peer:" + peer);
        if (!record) {
            return;
        }
        this.ctx.storage.kv.delete("peer:" + peer);
        const line = this.lines().get(peer);
        if (line) {
            this.closeLine(line, 1000, "left");
        }
        this.broadcast({ type: "leave", peer }, peer);
        if (record.host) {
            await this.end();
        } else {
            await this.scheduleAlarm();
        }
    }

    // the person who started the session is gone for good: the session ends for everybody
    async end() {
        this.broadcast({ type: "end", reason: "host-left" });
        for (const ws of this.lines().values()) {
            this.closeLine(ws, CLOSE_ENDED, "the session ended");
        }
        for (const [key] of this.ctx.storage.kv.list({ prefix: "peer:" })) {
            this.ctx.storage.kv.delete(key);
        }
        await this.ctx.storage.deleteAlarm();
    }

    // Lines that went silent are treated as dropped, and participants who have
    // a record but no line (the lines were lost when the object restarted) get
    // the same grace time. Runs only when the room is awake anyway.
    sweep(now) {
        this.lastSweep = now;
        const lines = this.lines();
        for (const [peer, ws] of lines) {
            const a = ws.deserializeAttachment();
            const pinged = this.ctx.getWebSocketAutoResponseTimestamp(ws);
            const last = Math.max(a.since || 0, this.lastSeen.get(peer) || 0, pinged ? pinged.getTime() : 0);
            if (now - last > STALE_MS) {
                // not awaited: what it schedules may follow a moment later
                this.drop(ws, peer, CLOSE_STALE, "nothing arrived for too long", false);
            }
        }
        for (const record of this.records().values()) {
            if (!record.until && !lines.has(record.peer)) {
                this.store({ ...record, until: now + this.awayGraceMs });
                this.broadcast({ type: "away", peer: record.peer }, record.peer);
            }
        }
    }

    // The alarm expires the places of those who did not come back. While the
    // room is in use it also runs once an hour, so a room whose lines were all
    // lost without a close event is still cleaned up.
    async scheduleAlarm() {
        const records = [...this.records().values()];
        if (records.length === 0) {
            await this.ctx.storage.deleteAlarm();
            return;
        }
        const due = records.filter((x) => x.until > 0).map((x) => x.until);
        await this.ctx.storage.setAlarm(due.length > 0 ? Math.min(...due) : Date.now() + CHECK_INTERVAL_MS);
    }

    async alarm() {
        const now = Date.now();
        this.sweep(now);
        for (const record of this.records().values()) {
            if (record.until > 0 && record.until <= now) {
                await this.leave(record.peer);
            }
        }
        await this.scheduleAlarm();
    }
}
