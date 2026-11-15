# OpenBoard-ML relay

Two or more people can work on one board over the internet. Their copies of
OpenBoard-ML do not connect to each other directly. Each one connects to a
**relay**, and the relay passes the messages on to the others in the same
session.

This directory holds that relay: a small [Cloudflare Worker](https://developers.cloudflare.com/workers/)
with one [Durable Object](https://developers.cloudflare.com/durable-objects/)
per session. You can run it on your own Cloudflare account, and the free plan
is enough. The relay cannot read the boards. Everything is encrypted with a key
made from the session code, and the relay never gets the code (see
[What the relay can and cannot see](#what-the-relay-can-and-cannot-see)).

- `relay.js`: the whole relay, plain JavaScript with no build step
- `wrangler.toml`: its configuration for Cloudflare
- `package.json`: only pulls in `wrangler`, Cloudflare's command line tool

## Putting the relay online

You do this once. It takes about ten minutes.

### 1. What you need

- A Cloudflare account. Sign up for free at <https://dash.cloudflare.com/sign-up>.
  The free plan ("Workers Free") is enough, and no credit card is needed.
- [Node.js](https://nodejs.org/) version 22.12 or newer, which comes with
  `npm` and `npx`. Check what you have with `node --version` in a terminal.
- This directory on your computer (it is part of the OpenBoard-ML sources).

### 2. Install wrangler

Open a terminal in this directory (`relay/`) and run:

```sh
npm install
```

This downloads `wrangler` into `relay/node_modules/`. Nothing is installed
system-wide.

### 3. Log in to Cloudflare

```sh
npx wrangler login
```

A browser window opens. Log in to your Cloudflare account and allow wrangler
access. You can then close the window and go back to the terminal.

### 4. Deploy

```sh
npx wrangler deploy
```

If your account has no `workers.dev` subdomain yet, wrangler asks you to pick
one. It can be any free name, for example `example-school`. This name is part
of the address of everything you deploy on that account.

At the end, wrangler prints the address of the relay. It looks like this:

```
https://openboard-ml-relay.example-school.workers.dev
```

You can find it again later in the Cloudflare dashboard, under
**Workers & Pages**, then **openboard-ml-relay**. To get a different first
part than `openboard-ml-relay`, change `name` in `wrangler.toml` before you
deploy.

Open the address in a browser. You should see a short page that says
"OpenBoard-ML relay". A brand-new `workers.dev` subdomain can answer with an
error for a minute or so at first. If it does, wait and try again.

### 5. Optional: a key, so only you can start sessions

Right now anybody who knows the address can start sessions on your relay, and
those count against your free allowance. To stop that, set a key:

```sh
npx wrangler secret put HOST_KEY
```

Type a long password when asked. From then on, starting a session needs this
key. Joining a session never needs it: the people you invite only need the
invitation. To remove the key again, run `npx wrangler secret delete HOST_KEY`.

### 6. Fill it in in OpenBoard-ML

In the settings for shared boards in OpenBoard-ML:

- **Relay address**: the address from step 4, for example
  `https://openboard-ml-relay.example-school.workers.dev`
  (`wss://...` works as well).
- **Relay key**: the key from step 5. Leave it empty if you did not set one.

Only the person who starts a session needs these settings. The invitation that
OpenBoard-ML makes holds the relay address and the session code on one line:

```
https://openboard-ml-relay.example-school.workers.dev/#join=7KQ2M-X4D9R-0PZ6H-T3WBN-C8YJ5
```

The person you invite pastes that line into OpenBoard-ML. If they open it in a
browser by mistake, they see the relay's page, which tells them to paste it
into OpenBoard-ML. The part after `#` stays in the browser and is never sent to
the server.

### Updating and removing

- After a change to `relay.js` or `wrangler.toml`, run `npx wrangler deploy`
  again. Running sessions lose their connection for a moment and then pick up
  again (see [Restarts](#restarts)).
- `npx wrangler delete` removes the relay from your account.

## Costs and limits

The relay is built for the
[Workers Free plan](https://developers.cloudflare.com/workers/platform/pricing/).
These are the free daily allowances that matter, as of November 2026. They are
reset every day at 00:00 UTC.

| What | Free per day | What the relay uses |
| --- | --- | --- |
| Worker requests | 100,000 | 1 per connection (also after a dropped line) |
| Durable Object requests | 100,000 | 1 per connection; incoming WebSocket messages count 1 per 20; outgoing messages are free |
| Durable Object duration | 13,000 GB-s | only while messages are being passed on; an idle session [hibernates](https://developers.cloudflare.com/durable-objects/best-practices/websockets/) and costs nothing |
| Durable Object storage | 5 GB in total; 100,000 rows written | a few rows per participant who joins, drops or leaves, and nothing about the boards |

Some examples, if each message counts as a request. One hour of drawing with
about 10 messages a second is roughly 1,800 Durable Object requests. A session
in which something happens all the time keeps the object awake. At 128 MB that
uses 460 GB-s an hour, so the free duration covers about 28 hours of busy
sessions a day. When you go over an allowance, new requests fail until the next
day. Nothing is billed unless you move to a paid plan yourself.

Sources:
[Workers limits](https://developers.cloudflare.com/workers/platform/limits/),
[Durable Objects pricing](https://developers.cloudflare.com/durable-objects/platform/pricing/),
[Durable Objects limits](https://developers.cloudflare.com/durable-objects/platform/limits/).
On the free plan, Durable Objects must use the SQLite storage backend. That is
why `wrangler.toml` declares the class with `new_sqlite_classes`.

The relay's own limits, against abuse and runaway costs:

- at most **8 participants** per session, counting those whose line dropped
  and who may still come back;
- at most **1 MiB** per message, the 4-byte address included (Cloudflare
  allows 32 MiB). OpenBoard-ML cuts larger messages into pieces of 256 KiB;
- at most **3,000 messages per participant in 10 seconds**;
- text messages other than the keep-alive `ping` are refused.

A participant that breaks these limits is disconnected and leaves the session.

## Running it locally

`wrangler dev` runs the relay on your own computer with a local emulation of
Cloudflare. It needs no account and no login:

```sh
npm install
WRANGLER_SEND_METRICS=false npx wrangler dev --ip 127.0.0.1 --port 8787
```

The relay is then at `http://127.0.0.1:8787`. OpenBoard-ML accepts unencrypted
`http://` and `ws://` addresses only on this computer, so this is for tests.
Variables can be set on the command line:

- `--var HOST_KEY:example-key` sets a host key;
- `--var AWAY_GRACE_SECONDS:3` makes the relay keep the place of a participant
  whose line dropped for 3 seconds instead of 90 (allowed: 2 to 600). This
  exists for tests; leave it unset when you deploy.

The console tests in `tests/collab/` can run against it:

```sh
build/collab-test/collab-test --relay http://127.0.0.1:8787 --host-key example-key --grace 3
```

## What the relay can and cannot see

The relay **can** see:

- the IP addresses of the participants, and when they connect and disconnect;
- the room id of a session: a 64-digit hexadecimal value derived from the
  session code. It cannot be turned back into the code;
- how many participants a session has, how many messages each one sends, to
  whom, and how large the messages are.

The relay **cannot** see:

- the session code or anything derived from it except the room id;
- the content of any message: drawings, pages, files, names.

The relay keeps a little membership data in storage while a session runs: the
peer ids, which participant started the session, a hash of each participant's
resume token, and how long a dropped participant's place is kept. All of it is
deleted when the session ends. The relay logs nothing. `wrangler.toml` turns
Cloudflare's Workers Logs off (`[observability] enabled = false`). Cloudflare
itself, like any provider, can see connection metadata.

A misbehaving relay can drop messages or cut the line, as any network can.
It cannot read, change, invent, replay, reorder or misattribute messages
without the participants noticing. They end the session with a protocol error
instead.

## The protocol

Version 1. This section is for whoever changes the relay or the client
(`src/collab/UBCollabRelayChannel.cpp`).

### Connecting

A participant opens a WebSocket (RFC 6455, no extensions, no subprotocol) to:

```
<relay address>/v1/rooms/<room id>
```

`<room id>` is 64 lowercase hexadecimal digits. The upgrade request carries
these headers:

| Header | Value |
| --- | --- |
| `OBML-Role` | `start` (a new session), `join`, or `resume` (the line dropped and comes back) |
| `OBML-Resume` | a random token of 32 bytes in base64url without padding (43 characters), chosen by the client when it starts or joins and sent again on every resume |
| `OBML-Peer` | only with `resume`: the peer id the participant had |
| `Authorization` | only with `start`, when the relay has `HOST_KEY`: `Bearer <key>` |

The Worker checks the host key itself, so a stranger never wakes a room. It
compares SHA-256 digests with `crypto.subtle.timingSafeEqual`. The key is not
passed on to the room.

The relay refuses a connection with an HTTP status and two headers:
`OBML-Relay: 1` and `OBML-Reason: <reason>`. A client must only believe the
reason when `OBML-Relay` is there; without it, the address leads to something
that is not a relay.

| Status | `OBML-Reason` | Meaning | OpenBoard-ML shows |
| --- | --- | --- | --- |
| 400 | `bad-request` | malformed request | `ProtocolError` |
| 403 | `refused` | `start` with a missing or wrong host key | `RelayRefused` |
| 404 | `not-found` | `join`: nobody started a session with this room id | `SessionNotFound` |
| 409 | `full` | `join`: the session has 8 participants | `SessionFull` |
| 409 | `exists` | `start`: a session with this room id is already running | `RelayRefused` |
| 410 | `gone` | `resume`: the participant is no longer in the session, or the token is wrong | `ConnectionLost` |
| 426 | `version` | the client speaks another protocol version (path `/v<n>/`) | `ProtocolError` |

### Messages from the relay

Right after the upgrade the relay sends a JSON text message:

```json
{"type":"welcome","v":1,"peer":3141592653,"host":2718281828,"peers":[2718281828,1414213562],
 "away":[1414213562],"resumed":false,"maxMessage":1048576,"maxPeers":8}
```

- `peer`: this participant's peer id, an unsigned 32-bit number that is never 0
  and is unique in the room. The relay picks it at random.
- `host`: the peer id of the participant who started the session.
- `peers`: the others in the room, including those in `away`, whose line
  dropped and who may come back.
- `resumed`: true on a `resume`. The peer id then stays the same.

After that, the relay sends these text messages:

- `{"type":"join","peer":N}`: a new participant. It always comes before any
  data message from that participant.
- `{"type":"leave","peer":N}`: the participant left for good.
- `{"type":"away","peer":N}` and `{"type":"back","peer":N}`: its line dropped,
  or came back. No `leave` or `join` is sent for this, and the peer id stays
  the same.
- `{"type":"end","reason":"host-left"}`: the session is over. The relay then
  closes the line with code 4000.
- `pong`: the answer to the keep-alive.

Clients ignore message types they do not know, so a later relay can add some
within version 1.

### Data

Data travels as binary messages. From the client to the relay:

```
addressee (4 bytes, big endian; 0 = all others) | payload
```

The relay replaces the four bytes with the sender's peer id and forwards the
message, either to all others or only to the addressee. It never sends a
message back to its sender. It drops a message for a peer id that is not
connected. The order of one sender's messages is kept. The payload is not
looked at.

### Keep-alive

The client sends the text `ping` every 30 seconds. The runtime answers `pong`
without waking the Durable Object
([auto-response](https://developers.cloudflare.com/durable-objects/api/state/)),
so an idle session stays hibernated. If nothing arrives on a line for 50
seconds, the client treats it as dropped. A line on which the relay received
nothing for 100 seconds counts as dropped too, but the relay only checks that
when it is awake anyway.

### Closing, dropped lines and the host

- A close with code **1000** (the participant left) or **1001** (its program
  quit) means the participant leaves at once: the others get `leave`.
- Any other end of the line (no close frame, a network error) counts as a
  dropped line. The relay keeps the participant's place for **90 seconds** and
  tells the others `away`. If the participant comes back with `resume`, its
  matching token and its old peer id, it keeps that id and the others get
  `back`. Otherwise its place expires (the Durable Object sets an alarm) and the
  others get `leave`.
- OpenBoard-ML tries again for 60 seconds, which is less than the 90 seconds
  the relay waits. Only after that does it give up with `ConnectionLost`.
- **The participant who started the session is the host.** When the host
  leaves for good, by closing or because its place expired, the relay sends
  `leave` for the host and then `end` to everybody, closes all lines with code
  4000, and deletes the room. A later `join` gets `not-found`. A short drop of
  the host's line ends nothing.
- Other close codes from the relay: 1003 (a text message that is not part of
  the protocol), 1008 (malformed message or too many messages), 1009 (message
  too large), 4001 (the same participant connected again on a new line, so this
  old line is closed), 4002 (nothing arrived for too long).

### Restarts

Membership is kept in the Durable Object's storage, not only in memory. If
the object restarts while a session runs, for example after a deploy or a
Cloudflare runtime update, the lines drop. The participants come back with
`resume` and carry on with the same peer ids. The relay gives participants
whose line it lost without a close event the same 90 seconds. While a room is
in use, an alarm checks for such participants once an hour, so a room whose
participants all vanished is still cleaned up.

### Inside the data: encryption

The relay does not need this part, but anyone writing another client does.

- **Session code**: 24 random characters (120 bits, from OpenSSL's random
  generator) and one check character, from the 32 characters
  `0123456789ABCDEFGHJKMNPQRSTVWXYZ` (no I, L, O, U), in five groups of five:
  `XXXXX-XXXXX-XXXXX-XXXXX-XXXXX`. When reading a code, lowercase letters are
  accepted, O is read as 0, I and L as 1, and spaces and dashes are ignored.
  The check character is `sum((i+1) * value[i] for i in 0..23) mod 31`.
- **Keys**: with HKDF-SHA256, the input is the 25 characters without dashes
  and the salt is `OpenBoard-ML collab v1`:
  - master key = HKDF(info `message key`, 32 bytes);
  - room id = hex(HKDF(info `relay room id`, 32 bytes)).
- **Sender keys**: each participant picks a random 16-byte *instance* when it
  enters a session. Its sending key is HKDF-SHA256(master key, no salt, info
  `sender key ` followed by the instance, 32 bytes). It numbers its messages
  1, 2, 3, ... with one counter for everything it sends.
- **A sealed message**:
  `version (1) = 1 | instance (16) | number (8) | addressee (4) | AES-256-GCM ciphertext | tag (16)`.
  The nonce is 4 zero bytes followed by the 8-byte number, so nonces never
  repeat under one key, whatever the relay does. The additional data is the
  ASCII text `OBML`, then the sender's peer id as the relay reports it
  (4 bytes), then the 29 header bytes.
- **The receiver** binds each peer id to the first instance that authenticates
  under it. It requires the number to go up and the addressee to be 0 or
  itself. An altered, replayed, reordered, misattributed or misdirected message
  therefore does not open, and the receiver ends the session with
  `ProtocolError`. That never happens on a working line.
- **Inside**: the first byte is the kind. `1` is a *hello* (one flags byte:
  1 = please answer, 2 = my line came back). `2` is a *chunk*: flags (1 = first,
  2 = last), a 4-byte index, on the first chunk the 8-byte total size, then the
  data. A participant only counts as present once one of its messages has
  opened. That proves it has the same code. Every participant sends a hello to
  all when it enters and when its line came back.
- **Sizes**: OpenBoard-ML sends messages of up to 256 MiB, in chunks of at
  most 256 KiB (less if the relay's `maxMessage` asks for it). A receiver keeps
  at most 512 MiB of partly received messages, counted by the sizes their
  senders announce, and ends the session with `ProtocolError` beyond that.
- **A wrong code** normally leads to a different room id, so `join` fails with
  `SessionNotFound`. If a relay puts participants with different codes in one
  room anyway, the newcomer gets a hello it cannot open and ends with
  `WrongCode`. The others ignore it.
- **A code is meant for one session.** A relay that recorded an earlier
  session with the same code could replay that whole session's traffic, so
  OpenBoard-ML makes a new code for every session.
