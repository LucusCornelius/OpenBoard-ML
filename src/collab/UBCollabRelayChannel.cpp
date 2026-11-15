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

#include "UBCollabRelayChannel.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <QtEndian>

#include <map>
#include <memory>

#include "UBCollabCrypto.h"
#include "UBCollabRelayTuning.h"
#include "UBCollabWebSocket.h"

/*
 * How it works (the full protocol is in relay/README.md):
 *
 * The relay is reached at <relay>/v1/rooms/<room id> as a WebSocket. It answers
 * with a "welcome" that gives this participant its peer id and lists the
 * others, and tells about "join", "leave", "away" and "back" afterwards (JSON
 * text messages). Data goes as binary messages: four bytes with the addressee
 * (0 = all others) on the way in, the sender on the way out, then a message
 * sealed by UBCollabCrypto.
 *
 * Inside the sealed messages: a "hello" that every participant sends to all
 * when it enters (and again when its line came back); a participant only
 * counts (peers(), peerJoined) once something it sealed has been opened,
 * which proves it has the same code. Messages of the board sync are cut into
 * chunks of at most chunkSize bytes, each sealed on its own.
 *
 * Order: everything that one participant sends to another goes out in the
 * order of send(), as the relay keeps the order of one line. Chunks of a
 * message for one participant may go between those of a message for another,
 * so that a large snapshot for somebody who joins does not hold up small
 * messages for the others; a broadcast that would have to wait behind such a
 * snapshot is sent to every participant on its own instead.
 *
 * A dropped line: the channel goes back to Connecting and tries again with
 * the same peer id for reconnectWindowMs (60 s); the relay keeps the place of
 * a participant longer than that (90 s), so the others see "away" and "back"
 * but no leave or join, and peer ids stay the same. What was on the way may
 * be lost in both directions; resumed() tells the board sync. A message that
 * was partly sent is sent again from its start. Participants that left during
 * the gap get peerLeft() before resumed(); those that joined during it get
 * peerJoined() once they answer the hello.
 *
 * Sizes: a message is at most maxMessageSize (256 MiB) both ways; a larger one
 * is not sent, and a peer that announces one ends the session with
 * ProtocolError. All partly received messages together may take maxIncoming
 * (512 MiB), counted by the size their senders announce.
 */

namespace
{
    const int sRelayProtocol = 1;
    const char sInvitationMarker[] = "#join=";

    enum Kind
    {
        HelloKind = 1,
        ChunkKind = 2
    };

    enum HelloFlag
    {
        ReplyWanted = 0x01,
        LineResumed = 0x02      // the sender's line dropped: what it partly sent before is void
    };

    enum ChunkFlag
    {
        FirstChunk = 0x01,
        LastChunk = 0x02
    };

    // kind, flags, index; the first chunk also has the total size
    const int sChunkHeader = 1 + 1 + 4;
    const int sFirstChunkHeader = sChunkHeader + 8;

    // close codes of the relay
    const quint16 sCloseEnded = 4000;
    const quint16 sCloseReplaced = 4001;

    const int sMaxListedPeers = 64;

    QString durationText(int milliseconds)
    {
        return QStringLiteral("%1 s").arg(milliseconds / 1000.0);
    }

    bool toPeer(const QJsonValue& value, quint32& peer)
    {
        if (!value.isDouble())
            return false;
        const double d = value.toDouble();
        if (d < 1 || d > 4294967295.0 || d != double(quint32(d)))
            return false;
        peer = quint32(d);
        return true;
    }
}

UBCollabRelayTuning& UBCollabRelayTuning::current()
{
    static UBCollabRelayTuning tuning;
    return tuning;
}

class UBCollabRelayChannelPrivate
{
    public:
        typedef UBCollabChannel::PeerId PeerId;

        struct Remote
        {
            explicit Remote(const QByteArray& masterKey) : opener(masterKey) {}

            UBCollabOpener opener;
            bool foreign = false;           // its messages do not open with our code: not a participant
            bool away = false;              // the relay says its line dropped
            bool inMessage = false;
            bool skipUntilFirst = false;    // our line dropped: the rest of a message it was sending is lost
            QByteArray partial;
            qint64 total = 0;
            qint64 reserved = 0;            // counted in incoming
            quint32 nextIndex = 0;
        };

        struct Outgoing
        {
            PeerId to = 0;
            QByteArray data;
            qint64 offset = 0;
            quint32 index = 0;
            quint64 served = 0;             // when it last had a turn; 0 = never
        };

        explicit UBCollabRelayChannelPrivate(UBCollabRelayChannel* channel);

        void begin(const QUrl& relay, const QString& code, const QString& key, bool asStarter);
        void connectLine();
        void onText(const QByteArray& text);
        void onWelcome(const QJsonObject& object);
        void onBinary(const QByteArray& frame);
        void onChunk(PeerId from, const QByteArray& plain);
        void onLineEnded(UBCollabWebSocket* line);
        void lineLost();
        void scheduleRetry();
        void onTick();
        void pump();
        int pick() const;
        void enqueue(PeerId to, const QByteArray& message);
        bool writeSealed(PeerId to, QByteArrayView part1, QByteArrayView part2);
        void writeHello(PeerId to, int flags);
        void dropPartial(Remote* remote);
        void removeRemote(PeerId peer);
        Remote* remote(PeerId peer);
        void setState(UBCollabChannel::State newState);
        void teardown();
        void fail(UBCollabChannel::Error newError, const QString& text);
        void violation(const QString& text);

        UBCollabRelayChannel* q;
        UBCollabRelayTuning tuning;

        UBCollabChannel::State state;
        UBCollabChannel::Error error;
        QString errorText;
        quint64 generation;         // goes up when the session ends: code after a signal checks it

        bool starter;
        QUrl roomUrl;
        QString relayKey;
        QByteArray masterKey;
        std::unique_ptr<UBCollabSealer> sealer;
        QByteArray resumeToken;
        PeerId local;
        PeerId host;

        std::map<PeerId, std::unique_ptr<Remote>> remotes;     // everybody the relay announced
        QList<PeerId> peers;                                    // those who proved the code, in order of arrival

        UBCollabWebSocket* line;
        bool welcomed;          // the relay let this participant in (the first welcome)
        bool lineWelcomed;      // the current line got its welcome
        bool everOpen;
        bool ended;

        QTimer* tick;
        QTimer* retry;
        QTimer* answer;
        QElapsedTimer sinceReceived;
        QElapsedTimer sincePing;
        QElapsedTimer lostSince;
        int retryDelay;

        QList<Outgoing> queue;
        qint64 queued;
        quint64 turns;
        qint64 chunkSize;
        qint64 incoming;
};

UBCollabRelayChannelPrivate::UBCollabRelayChannelPrivate(UBCollabRelayChannel* channel)
    : q(channel)
    , state(UBCollabChannel::Closed)
    , error(UBCollabChannel::NoError)
    , generation(0)
    , starter(false)
    , local(0)
    , host(0)
    , line(nullptr)
    , welcomed(false)
    , lineWelcomed(false)
    , everOpen(false)
    , ended(false)
    , tick(new QTimer(channel))
    , retry(new QTimer(channel))
    , answer(new QTimer(channel))
    , retryDelay(0)
    , queued(0)
    , turns(0)
    , chunkSize(0)
    , incoming(0)
{
    retry->setSingleShot(true);
    answer->setSingleShot(true);

    QObject::connect(tick, &QTimer::timeout, channel, [this]() { onTick(); });
    QObject::connect(retry, &QTimer::timeout, channel, [this]() { connectLine(); });
    QObject::connect(answer, &QTimer::timeout, channel, [this]()
    {
        fail(UBCollabChannel::ConnectionLost, QStringLiteral("nobody in the session answered within %1")
            .arg(durationText(tuning.answerTimeoutMs)));
    });
}

UBCollabRelayChannelPrivate::Remote* UBCollabRelayChannelPrivate::remote(PeerId peer)
{
    auto it = remotes.find(peer);
    return it == remotes.end() ? nullptr : it->second.get();
}

void UBCollabRelayChannelPrivate::setState(UBCollabChannel::State newState)
{
    if (state == newState)
        return;
    state = newState;
    emit q->stateChanged();
}

void UBCollabRelayChannelPrivate::begin(const QUrl& relay, const QString& code, const QString& key, bool asStarter)
{
    if (state != UBCollabChannel::Closed)
        q->close();

    teardown();
    ++generation;
    tuning = UBCollabRelayTuning::current();
    error = UBCollabChannel::NoError;
    errorText.clear();
    starter = asStarter;
    relayKey = key;

    UBCollabChannel::Error problem = UBCollabChannel::NoError;
    QString problemText;

    const QString normalized = UBCollabRelayChannel::normalizedCode(code);
    if (!UBCollabRelayChannel::isRelayAddress(relay))
    {
        problem = UBCollabChannel::RelayUnreachable;
        problemText = QStringLiteral("%1 is not the address of a relay").arg(relay.toDisplayString());
    }
    else if (normalized.isEmpty())
    {
        problem = UBCollabChannel::SessionNotFound;
        problemText = QStringLiteral("this is not a session code");
    }
    else
    {
        masterKey = UBCollabCrypto::masterKey(normalized);
        const QByteArray roomId = UBCollabCrypto::roomId(normalized);
        sealer = std::make_unique<UBCollabSealer>(masterKey);
        resumeToken = UBCollabCrypto::randomBytes(32).toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);

        if (masterKey.size() != UBCollabCrypto::KeySize || roomId.size() != 64 || !sealer->isValid() || resumeToken.size() != 43)
        {
            problem = UBCollabChannel::ProtocolError;
            problemText = QStringLiteral("the encryption (OpenSSL) does not work");
        }

        roomUrl = relay;
        const QString scheme = relay.scheme().toLower();
        roomUrl.setScheme(scheme == QLatin1String("http") || scheme == QLatin1String("ws") ? QStringLiteral("ws") : QStringLiteral("wss"));
        QString path = relay.path();
        while (path.endsWith(QLatin1Char('/')))
            path.chop(1);
        roomUrl.setPath(path + QStringLiteral("/v%1/rooms/").arg(sRelayProtocol) + QString::fromLatin1(roomId));
        roomUrl.setQuery(QString());
        roomUrl.setFragment(QString());
    }

    setState(UBCollabChannel::Connecting);

    if (problem != UBCollabChannel::NoError)
    {
        // asynchronous like every other failure, so the caller sees Connecting and then Closed
        const quint64 current = generation;
        QTimer::singleShot(0, q, [this, current, problem, problemText]()
        {
            if (generation == current)
                fail(problem, problemText);
        });
        return;
    }

    connectLine();
}

void UBCollabRelayChannelPrivate::connectLine()
{
    if (line)
        return;

    lineWelcomed = false;
    line = new UBCollabWebSocket(q);
    line->setHandshakeTimeout(tuning.handshakeTimeoutMs);

    // a data message from the relay is at most a chunk and its headers; more is refused
    line->setMaxMessageSize(qMax<qint64>(4 * 1024 * 1024, tuning.chunkSize + 64 * 1024));

    UBCollabWebSocket::Headers headers;
    const QByteArray role = welcomed ? "resume" : (starter ? "start" : "join");
    headers.append(qMakePair(QByteArray("OBML-Role"), role));
    headers.append(qMakePair(QByteArray("OBML-Resume"), resumeToken));
    if (welcomed)
        headers.append(qMakePair(QByteArray("OBML-Peer"), QByteArray::number(local)));
    if (role == "start" && !relayKey.isEmpty())
        headers.append(qMakePair(QByteArray("Authorization"), "Bearer " + relayKey.toUtf8()));

    UBCollabWebSocket* current = line;
    QObject::connect(line, &UBCollabWebSocket::textMessageReceived, q, [this, current](const QByteArray& text)
    {
        if (current == line)
            onText(text);
    });
    QObject::connect(line, &UBCollabWebSocket::binaryMessageReceived, q, [this, current](const QByteArray& frame)
    {
        if (current == line)
            onBinary(frame);
    });
    QObject::connect(line, &UBCollabWebSocket::bytesWritten, q, [this, current]()
    {
        if (current == line)
            pump();
    });
    QObject::connect(line, &UBCollabWebSocket::disconnected, q, [this, current]()
    {
        if (current == line)
            onLineEnded(current);
    });

    line->open(roomUrl, headers);
}

void UBCollabRelayChannelPrivate::onText(const QByteArray& text)
{
    sinceReceived.restart();

    if (text == "pong")
        return;

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(text, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        violation(QStringLiteral("the relay sent a text that is not part of the protocol"));
        return;
    }

    const QJsonObject object = document.object();
    const QString type = object.value(QStringLiteral("type")).toString();

    if (type == QLatin1String("welcome"))
    {
        onWelcome(object);
        return;
    }
    if (!lineWelcomed)
    {
        violation(QStringLiteral("the relay sent \"%1\" before its welcome").arg(type));
        return;
    }

    PeerId peer = 0;
    const bool hasPeer = toPeer(object.value(QStringLiteral("peer")), peer) && peer != local;

    if (type == QLatin1String("join"))
    {
        if (hasPeer && !remote(peer))
            remotes[peer] = std::make_unique<Remote>(masterKey);
    }
    else if (type == QLatin1String("leave"))
    {
        if (hasPeer)
            removeRemote(peer);
    }
    else if (type == QLatin1String("away") || type == QLatin1String("back"))
    {
        if (Remote* r = hasPeer ? remote(peer) : nullptr)
            r->away = type == QLatin1String("away");
    }
    else if (type == QLatin1String("end"))
    {
        ended = true;
    }
    // other types are left for later versions of the relay that stay compatible
}

void UBCollabRelayChannelPrivate::onWelcome(const QJsonObject& object)
{
    if (lineWelcomed)
    {
        violation(QStringLiteral("the relay sent a second welcome"));
        return;
    }
    if (object.value(QStringLiteral("v")).toInt() != sRelayProtocol)
    {
        fail(UBCollabChannel::ProtocolError, QStringLiteral("the relay speaks another version of the protocol"));
        return;
    }

    PeerId peer = 0;
    const bool resumed = object.value(QStringLiteral("resumed")).toBool();
    if (!toPeer(object.value(QStringLiteral("peer")), peer) || resumed != welcomed || (welcomed && peer != local))
    {
        violation(QStringLiteral("the relay sent an inconsistent welcome"));
        return;
    }

    QList<PeerId> listed;
    const QJsonArray list = object.value(QStringLiteral("peers")).toArray();
    if (list.size() > sMaxListedPeers)
    {
        violation(QStringLiteral("the relay lists too many participants"));
        return;
    }
    for (const QJsonValue& value : list)
    {
        PeerId other = 0;
        if (!toPeer(value, other) || other == peer || listed.contains(other))
        {
            violation(QStringLiteral("the relay sent an inconsistent list of participants"));
            return;
        }
        listed.append(other);
    }
    QSet<PeerId> awayPeers;
    for (const QJsonValue& value : object.value(QStringLiteral("away")).toArray())
    {
        PeerId other = 0;
        if (toPeer(value, other))
            awayPeers.insert(other);
    }

    // the relay tells how large a message may be; a chunk and its headers must fit
    const double maxMessage = object.value(QStringLiteral("maxMessage")).toDouble(1024 * 1024);
    const qint64 room = qint64(qMin(maxMessage, 1.0e12)) - 4 - UBCollabCrypto::Overhead - sFirstChunkHeader;
    if (room < 1024)
    {
        violation(QStringLiteral("the relay allows too small messages"));
        return;
    }
    chunkSize = qMin(tuning.chunkSize, room);

    PeerId hostPeer = 0;
    toPeer(object.value(QStringLiteral("host")), hostPeer);
    host = hostPeer;

    lineWelcomed = true;
    sinceReceived.start();
    sincePing.start();
    tick->start(qMax(50, qMin(tuning.keepAliveMs, tuning.silenceLimitMs) / 4));

    const quint64 current = generation;

    if (!welcomed)
    {
        welcomed = true;
        local = peer;
        for (PeerId other : listed)
        {
            remotes[other] = std::make_unique<Remote>(masterKey);
            remotes[other]->away = awayPeers.contains(other);
        }

        // the hello goes first, before anything the board sync sends on Open
        if (!listed.isEmpty())
            writeHello(0, ReplyWanted);
        if (generation != current)
            return;

        if (starter)
        {
            everOpen = true;
            setState(UBCollabChannel::Open);
        }
        else
        {
            // Open once a participant answered: that proves the code
            answer->start(tuning.answerTimeoutMs);
        }
        return;
    }

    // the line came back
    lostSince.invalidate();
    retry->stop();
    writeHello(0, ReplyWanted | LineResumed);
    if (generation != current)
        return;

    QList<PeerId> gone;
    for (auto it = remotes.begin(); it != remotes.end(); ++it)
    {
        if (!listed.contains(it->first))
            gone.append(it->first);
        else
            it->second->away = awayPeers.contains(it->first);
    }
    for (PeerId other : listed)
    {
        if (!remote(other))
            remotes[other] = std::make_unique<Remote>(masterKey);
    }

    if (everOpen)
    {
        setState(UBCollabChannel::Open);
        if (generation != current)
            return;
    }
    for (PeerId other : gone)
    {
        removeRemote(other);
        if (generation != current)
            return;
    }
    if (everOpen)
    {
        emit q->resumed();
        if (generation != current)
            return;
    }
    pump();
}

void UBCollabRelayChannelPrivate::onBinary(const QByteArray& frame)
{
    sinceReceived.restart();

    if (!lineWelcomed)
    {
        violation(QStringLiteral("the relay sent data before its welcome"));
        return;
    }
    if (frame.size() < 4)
    {
        violation(QStringLiteral("the relay sent a message without sender"));
        return;
    }

    const PeerId from = qFromBigEndian<quint32>(frame.constData());
    Remote* r = remote(from);
    if (!r)
    {
        // the relay always announces a participant before its messages; this one is ignored
        qWarning("UBCollabRelayChannel: message from unknown participant %u ignored", from);
        return;
    }
    if (r->foreign)
        return;

    QByteArray plain;
    quint32 to = 0;
    const bool wasBound = r->opener.isBound();
    const UBCollabOpener::Result result = r->opener.open(from, QByteArrayView(frame).sliced(4), plain, to);

    switch (result)
    {
        case UBCollabOpener::Opened:
            break;

        case UBCollabOpener::OtherVersion:
            fail(UBCollabChannel::ProtocolError,
                 QStringLiteral("participant %1 uses another version of the shared-board protocol").arg(from));
            return;

        case UBCollabOpener::Malformed:
        case UBCollabOpener::Forged:
            if (wasBound)
            {
                violation(QStringLiteral("a message from participant %1 was altered or forged on the way").arg(from));
            }
            else if (!everOpen)
            {
                fail(UBCollabChannel::WrongCode,
                     QStringLiteral("the messages of the others in the session cannot be read with this code"));
            }
            else
            {
                // Somebody the relay let in with another code; not a participant of
                // this session. One hello tells it so: it cannot open that either
                // and ends with WrongCode instead of waiting in silence.
                r->foreign = true;
                qWarning("UBCollabRelayChannel: participant %u does not have the code of this session; ignored", from);
                writeHello(from, 0);
            }
            return;

        case UBCollabOpener::OtherInstance:
            violation(QStringLiteral("participant %1 suddenly sends as somebody else").arg(from));
            return;

        case UBCollabOpener::Replayed:
            violation(QStringLiteral("a message from participant %1 was replayed or put out of order").arg(from));
            return;
    }

    if (to != 0 && to != local)
    {
        violation(QStringLiteral("a message of participant %1 for somebody else was delivered here").arg(from));
        return;
    }
    if (plain.isEmpty())
    {
        violation(QStringLiteral("empty message from participant %1").arg(from));
        return;
    }

    const quint64 current = generation;
    if (!wasBound)
    {
        peers.append(from);
        if (!everOpen)
        {
            everOpen = true;
            answer->stop();
            setState(UBCollabChannel::Open);
            if (generation != current)
                return;
        }
        emit q->peerJoined(from);
        if (generation != current)
            return;
        r = remote(from);
        if (!r)
            return;
    }

    const int kind = quint8(plain.at(0));
    if (kind == HelloKind)
    {
        if (plain.size() < 2)
        {
            violation(QStringLiteral("malformed hello from participant %1").arg(from));
            return;
        }
        const int flags = quint8(plain.at(1));
        if (flags & LineResumed)
        {
            dropPartial(r);
            r->skipUntilFirst = false;
        }
        if (flags & ReplyWanted)
            writeHello(from, 0);
    }
    else if (kind == ChunkKind)
    {
        onChunk(from, plain);
    }
    else
    {
        violation(QStringLiteral("unknown kind of message %1 from participant %2").arg(kind).arg(from));
    }
}

void UBCollabRelayChannelPrivate::onChunk(PeerId from, const QByteArray& plain)
{
    Remote* r = remote(from);
    if (!r)
        return;

    if (plain.size() < sChunkHeader)
    {
        violation(QStringLiteral("malformed chunk from participant %1").arg(from));
        return;
    }

    const int flags = quint8(plain.at(1));
    const quint32 index = qFromBigEndian<quint32>(plain.constData() + 2);
    const bool first = flags & FirstChunk;
    const bool last = flags & LastChunk;
    qsizetype offset = sChunkHeader;

    if (first)
    {
        if (plain.size() < sFirstChunkHeader)
        {
            violation(QStringLiteral("malformed chunk from participant %1").arg(from));
            return;
        }
        const quint64 total = qFromBigEndian<quint64>(plain.constData() + sChunkHeader);
        offset = sFirstChunkHeader;

        if (total > quint64(tuning.maxMessageSize))
        {
            violation(QStringLiteral("participant %1 sends a message larger than %2 bytes").arg(from).arg(tuning.maxMessageSize));
            return;
        }
        if (index != 0 || r->inMessage)
        {
            violation(QStringLiteral("participant %1 started a message in the middle of another").arg(from));
            return;
        }

        r->inMessage = true;
        r->skipUntilFirst = false;
        r->total = qint64(total);
        r->nextIndex = 0;
        r->partial = QByteArray();

        // A message in more than one chunk gets its full size at once, counted
        // against what all partly received messages together may take; so a
        // sender cannot make us hold more than maxIncoming, however it lies.
        if (!last)
        {
            if (incoming + r->total > tuning.maxIncoming)
            {
                violation(QStringLiteral("too much partly received data (more than %1 bytes)").arg(tuning.maxIncoming));
                return;
            }
            incoming += r->total;
            r->reserved = r->total;
            r->partial.reserve(r->total);
        }
    }
    else if (!r->inMessage)
    {
        if (r->skipUntilFirst)
            return;
        violation(QStringLiteral("participant %1 continued a message that it never started").arg(from));
        return;
    }

    const qint64 size = plain.size() - offset;
    if (index != r->nextIndex || r->partial.size() + size > r->total || (last && r->partial.size() + size != r->total))
    {
        violation(QStringLiteral("the chunks of a message from participant %1 do not fit together").arg(from));
        return;
    }

    QByteArray message;
    if (first && last)
    {
        message = plain.mid(offset);
    }
    else
    {
        r->partial.append(plain.constData() + offset, size);
        ++r->nextIndex;
        if (!last)
            return;

        message = r->partial;
        r->partial = QByteArray();
        incoming -= r->reserved;
        r->reserved = 0;
    }

    r->inMessage = false;
    r->total = 0;
    r->nextIndex = 0;
    emit q->messageReceived(from, message);
}

void UBCollabRelayChannelPrivate::onLineEnded(UBCollabWebSocket* endedLine)
{
    const UBCollabWebSocket::Failure failure = endedLine->failure();
    const quint16 closeCode = endedLine->closeCode();
    const QString lineError = endedLine->errorString();
    const int status = endedLine->rejectStatus();
    const bool fromRelay = !endedLine->rejectHeader("obml-relay").isEmpty();
    const QByteArray reason = endedLine->rejectHeader("obml-reason");

    QObject::disconnect(endedLine, nullptr, q, nullptr);
    endedLine->deleteLater();
    line = nullptr;
    lineWelcomed = false;
    tick->stop();

    if (failure == UBCollabWebSocket::Closed && (closeCode == sCloseEnded || ended))
    {
        teardown();
        ++generation;
        error = UBCollabChannel::NoError;
        errorText = QStringLiteral("the session ended: the participant who started it left");
        setState(UBCollabChannel::Closed);
        return;
    }
    if (failure == UBCollabWebSocket::Closed && (closeCode == 1003 || closeCode == 1008 || closeCode == 1009))
    {
        fail(UBCollabChannel::ProtocolError, QStringLiteral("the relay closed the line: %1").arg(lineError));
        return;
    }
    if (failure == UBCollabWebSocket::Closed && closeCode == sCloseReplaced)
    {
        fail(UBCollabChannel::ConnectionLost, QStringLiteral("another connection took the place of this participant"));
        return;
    }

    if (failure == UBCollabWebSocket::HandshakeRejected && fromRelay)
    {
        if (reason == "version")
        {
            fail(UBCollabChannel::ProtocolError, QStringLiteral("the relay speaks another version of the protocol"));
            return;
        }
        if (welcomed)
        {
            fail(UBCollabChannel::ConnectionLost, QStringLiteral("the line dropped and the session is no longer on the relay (%1)")
                .arg(QString::fromLatin1(reason)));
            return;
        }
        if (reason == "not-found")
            fail(UBCollabChannel::SessionNotFound, QStringLiteral("nobody is in a session with this code"));
        else if (reason == "full")
            fail(UBCollabChannel::SessionFull, QStringLiteral("the session is full"));
        else if (reason == "refused")
            fail(UBCollabChannel::RelayRefused, QStringLiteral("the relay asks for a key to start a session, and the key is missing or wrong"));
        else if (reason == "exists")
            fail(UBCollabChannel::RelayRefused, QStringLiteral("a session with this code is already running"));
        else
            fail(UBCollabChannel::ProtocolError, QStringLiteral("the relay refused the connection (HTTP %1, %2)")
                .arg(status).arg(QString::fromLatin1(reason)));
        return;
    }

    if (!welcomed)
    {
        switch (failure)
        {
            case UBCollabWebSocket::HandshakeRejected:
                fail(UBCollabChannel::RelayUnreachable,
                     QStringLiteral("the address does not lead to an OpenBoard-ML relay (HTTP %1)").arg(status));
                break;
            case UBCollabWebSocket::ProtocolViolation:
            case UBCollabWebSocket::MessageTooBig:
                fail(UBCollabChannel::ProtocolError, QStringLiteral("the relay broke the WebSocket protocol: %1").arg(lineError));
                break;
            default:
                fail(UBCollabChannel::RelayUnreachable, QStringLiteral("no connection with the relay: %1").arg(lineError));
                break;
        }
        return;
    }

    // a relay that breaks the protocol does not get better by trying again
    if (failure == UBCollabWebSocket::ProtocolViolation || failure == UBCollabWebSocket::MessageTooBig)
    {
        fail(UBCollabChannel::ProtocolError, QStringLiteral("the relay broke the WebSocket protocol: %1").arg(lineError));
        return;
    }

    lineLost();
}

void UBCollabRelayChannelPrivate::lineLost()
{
    // what was partly received can no longer be completed
    for (auto& entry : remotes)
    {
        dropPartial(entry.second.get());
        entry.second->skipUntilFirst = true;
    }

    // what was partly sent starts again from its beginning on the next line
    for (Outgoing& outgoing : queue)
    {
        queued += outgoing.offset;
        outgoing.offset = 0;
        outgoing.index = 0;
        outgoing.served = 0;
    }

    if (!lostSince.isValid())
    {
        lostSince.start();
        retryDelay = tuning.firstRetryDelayMs;
    }

    const quint64 current = generation;
    setState(UBCollabChannel::Connecting);
    if (generation != current)
        return;

    scheduleRetry();
}

void UBCollabRelayChannelPrivate::scheduleRetry()
{
    const qint64 left = tuning.reconnectWindowMs - lostSince.elapsed();
    if (left <= 0)
    {
        fail(UBCollabChannel::ConnectionLost, QStringLiteral("the line to the relay dropped and did not come back within %1")
            .arg(durationText(tuning.reconnectWindowMs)));
        return;
    }
    retry->start(int(qMin<qint64>(retryDelay, left)));
    retryDelay = qMin(retryDelay * 2, tuning.maxRetryDelayMs);
}

void UBCollabRelayChannelPrivate::onTick()
{
    if (!line || !lineWelcomed)
        return;

    if (sinceReceived.elapsed() > tuning.silenceLimitMs)
    {
        // a line on which nothing arrives any more (not even the answer to a keep-alive) is as good as dropped
        line->abort();
        return;
    }
    if (sincePing.elapsed() >= tuning.keepAliveMs)
    {
        sincePing.restart();
        line->sendText("ping");
    }
}

int UBCollabRelayChannelPrivate::pick() const
{
    // A message may go before an older one only when the two have no
    // recipient in common; among those, the one that waited longest goes.
    QSet<PeerId> blocked;
    int best = -1;
    quint64 bestServed = 0;
    for (int i = 0; i < queue.size(); ++i)
    {
        const Outgoing& outgoing = queue.at(i);
        const bool eligible = outgoing.to == 0 ? blocked.isEmpty() : !blocked.contains(outgoing.to);
        if (eligible && (best < 0 || outgoing.served < bestServed))
        {
            best = i;
            bestServed = outgoing.served;
        }
        if (outgoing.to == 0 || (best >= 0 && bestServed == 0))
            break;
        blocked.insert(outgoing.to);
    }
    return best;
}

void UBCollabRelayChannelPrivate::pump()
{
    if (state != UBCollabChannel::Open || !line || !lineWelcomed || line->state() != UBCollabWebSocket::Open)
        return;

    while (!queue.isEmpty() && line->bytesToWrite() < tuning.writeBuffer)
    {
        const int i = pick();
        if (i < 0)
            break;

        Outgoing& outgoing = queue[i];
        const qint64 size = qMin(chunkSize, outgoing.data.size() - outgoing.offset);
        const bool first = outgoing.index == 0;
        const bool last = outgoing.offset + size == outgoing.data.size();

        QByteArray header(first ? sFirstChunkHeader : sChunkHeader, '\0');
        header[0] = char(ChunkKind);
        header[1] = char((first ? FirstChunk : 0) | (last ? LastChunk : 0));
        qToBigEndian(outgoing.index, header.data() + 2);
        if (first)
            qToBigEndian(quint64(outgoing.data.size()), header.data() + sChunkHeader);

        if (!writeSealed(outgoing.to, header, QByteArrayView(outgoing.data).sliced(outgoing.offset, size)))
            return;

        outgoing.offset += size;
        ++outgoing.index;
        outgoing.served = ++turns;
        queued -= size;
        if (last)
            queue.removeAt(i);
    }
}

void UBCollabRelayChannelPrivate::enqueue(PeerId to, const QByteArray& message)
{
    Outgoing outgoing;
    outgoing.to = to;
    outgoing.data = message;
    queue.append(outgoing);
    queued += message.size();
}

bool UBCollabRelayChannelPrivate::writeSealed(PeerId to, QByteArrayView part1, QByteArrayView part2)
{
    if (!line || !sealer)
        return false;

    QByteArray frame = sealer->seal(local, to, part1, part2, 4);
    if (frame.isEmpty())
    {
        fail(UBCollabChannel::ProtocolError, QStringLiteral("a message could not be encrypted"));
        return false;
    }
    qToBigEndian(to, frame.data());
    return line->sendBinary(frame);
}

void UBCollabRelayChannelPrivate::writeHello(PeerId to, int flags)
{
    const char hello[2] = { char(HelloKind), char(flags) };
    writeSealed(to, QByteArrayView(hello, 2), QByteArrayView());
}

void UBCollabRelayChannelPrivate::dropPartial(Remote* r)
{
    incoming -= r->reserved;
    r->reserved = 0;
    r->partial = QByteArray();
    r->inMessage = false;
    r->total = 0;
    r->nextIndex = 0;
}

void UBCollabRelayChannelPrivate::removeRemote(PeerId peer)
{
    auto it = remotes.find(peer);
    if (it == remotes.end())
        return;

    dropPartial(it->second.get());
    remotes.erase(it);

    for (int i = queue.size() - 1; i >= 0; --i)
    {
        if (queue.at(i).to == peer)
        {
            queued -= queue.at(i).data.size() - queue.at(i).offset;
            queue.removeAt(i);
        }
    }

    if (peers.removeAll(peer) > 0)
        emit q->peerLeft(peer);
}

void UBCollabRelayChannelPrivate::teardown()
{
    if (line)
    {
        QObject::disconnect(line, nullptr, q, nullptr);

        // the close frame tells the relay that this participant left, rather than that its line dropped
        line->close(1000);
        line->deleteLater();
        line = nullptr;
    }

    tick->stop();
    retry->stop();
    answer->stop();
    lostSince.invalidate();

    remotes.clear();
    peers.clear();
    queue.clear();
    queued = 0;
    incoming = 0;
    sealer.reset();
    masterKey.clear();
    resumeToken.clear();
    relayKey.clear();
    local = 0;
    host = 0;
    welcomed = false;
    lineWelcomed = false;
    everOpen = false;
    ended = false;
}

void UBCollabRelayChannelPrivate::fail(UBCollabChannel::Error newError, const QString& text)
{
    teardown();
    ++generation;
    error = newError;
    errorText = text;
    qWarning("UBCollabRelayChannel: %s", qPrintable(text));

    if (state == UBCollabChannel::Closed)
        emit q->stateChanged();
    else
        setState(UBCollabChannel::Closed);
}

void UBCollabRelayChannelPrivate::violation(const QString& text)
{
    // the relay or a participant misbehaves; this is not something that happens on a working line
    fail(UBCollabChannel::ProtocolError, text);
}

void UBCollabRelayTuning::dropLine(UBCollabRelayChannel* channel)
{
    // the line is a child of the channel; one that is already closing is left alone
    if (!channel)
        return;
    const QList<UBCollabWebSocket*> lines = channel->findChildren<UBCollabWebSocket*>(Qt::FindDirectChildrenOnly);
    for (UBCollabWebSocket* line : lines)
    {
        if (line->state() == UBCollabWebSocket::Open || line->state() == UBCollabWebSocket::Connecting)
            line->abort();
    }
}

UBCollabRelayChannel::UBCollabRelayChannel(QObject* parent)
    : UBCollabChannel(parent)
    , d(new UBCollabRelayChannelPrivate(this))
{
}

UBCollabRelayChannel::~UBCollabRelayChannel()
{
    if (d->line)
    {
        QObject::disconnect(d->line, nullptr, this, nullptr);
        d->line->close(1000);
        delete d->line;
        d->line = nullptr;
    }
    delete d;
}

QString UBCollabRelayChannel::newSessionCode()
{
    return UBCollabCrypto::newCode();
}

QString UBCollabRelayChannel::normalizedCode(const QString& text)
{
    return UBCollabCrypto::normalizedCode(text);
}

QString UBCollabRelayChannel::invitation(const QUrl& relay, const QString& code)
{
    const QString normalized = normalizedCode(code);
    if (!isRelayAddress(relay) || normalized.isEmpty())
        return QString();

    // as a web address, so that opening it in a browser shows the page of the relay;
    // the code is in the fragment, which a browser never sends to the server
    QUrl url = relay;
    const QString scheme = relay.scheme().toLower();
    if (scheme == QLatin1String("ws"))
        url.setScheme(QStringLiteral("http"));
    else if (scheme == QLatin1String("wss"))
        url.setScheme(QStringLiteral("https"));
    url.setQuery(QString());
    url.setFragment(QString());
    if (url.path().isEmpty())
        url.setPath(QStringLiteral("/"));

    return url.toString(QUrl::FullyEncoded) + QLatin1String(sInvitationMarker) + normalized;
}

bool UBCollabRelayChannel::parseInvitation(const QString& text, QUrl& relay, QString& code)
{
    // the invitation may come with other text around it (a chat message, quotes, a full stop)
    static const QRegularExpression pattern(
        QStringLiteral("((?:https?|wss?)://[^\\s<>\"'#]+)#join=([0-9A-Za-z][0-9A-Za-z-]*)"),
        QRegularExpression::CaseInsensitiveOption);

    QRegularExpressionMatchIterator matches = pattern.globalMatch(text);
    while (matches.hasNext())
    {
        const QRegularExpressionMatch match = matches.next();
        const QUrl url(match.captured(1), QUrl::StrictMode);
        const QString normalized = normalizedCode(match.captured(2));
        if (isRelayAddress(url) && !normalized.isEmpty())
        {
            relay = url;
            code = normalized;
            return true;
        }
    }
    return false;
}

bool UBCollabRelayChannel::isRelayAddress(const QUrl& relay)
{
    if (!relay.isValid() || relay.host().isEmpty() || !relay.userInfo().isEmpty())
        return false;

    const QString scheme = relay.scheme().toLower();
    if (scheme == QLatin1String("wss") || scheme == QLatin1String("https"))
        return true;

    // without encryption only to this computer, for tests and a local relay
    return (scheme == QLatin1String("ws") || scheme == QLatin1String("http")) && UBCollabWebSocket::isLoopbackHost(relay.host());
}

void UBCollabRelayChannel::start(const QUrl& relay, const QString& code, const QString& relayKey)
{
    d->begin(relay, code, relayKey, true);
}

void UBCollabRelayChannel::join(const QUrl& relay, const QString& code)
{
    d->begin(relay, code, QString(), false);
}

UBCollabChannel::State UBCollabRelayChannel::state() const
{
    return d->state;
}

UBCollabChannel::Error UBCollabRelayChannel::error() const
{
    return d->error;
}

QString UBCollabRelayChannel::errorString() const
{
    if (!d->errorText.isEmpty())
        return d->errorText;

    switch (d->error)
    {
        case NoError: return QString();
        case RelayUnreachable: return QStringLiteral("no connection with the relay");
        case RelayRefused: return QStringLiteral("the relay does not let this participant start a session");
        case SessionNotFound: return QStringLiteral("nobody is in a session with this code");
        case SessionFull: return QStringLiteral("the session is full");
        case WrongCode: return QStringLiteral("the messages of the others cannot be read with this code");
        case ConnectionLost: return QStringLiteral("the line dropped and did not come back");
        case ProtocolError: return QStringLiteral("the other side does not speak this protocol");
    }
    return QString();
}

UBCollabChannel::PeerId UBCollabRelayChannel::localPeer() const
{
    return d->state == Open ? d->local : 0;
}

QList<UBCollabChannel::PeerId> UBCollabRelayChannel::peers() const
{
    return d->peers;
}

void UBCollabRelayChannel::send(const QByteArray& message, PeerId to)
{
    if (d->state != Open || to == d->local || (to != 0 && !d->peers.contains(to)))
        return;

    if (message.size() > d->tuning.maxMessageSize)
    {
        qWarning("UBCollabRelayChannel: message of %lld bytes not sent, the limit is %lld",
                 qlonglong(message.size()), qlonglong(d->tuning.maxMessageSize));
        return;
    }

    // a small broadcast that would wait behind a large message for one participant goes to everybody on its own
    if (to == 0 && message.size() <= d->chunkSize && !d->peers.isEmpty())
    {
        bool directed = false;
        bool broadcast = false;
        for (const auto& outgoing : d->queue)
        {
            if (outgoing.to == 0)
                broadcast = true;
            else
                directed = true;
        }
        if (directed && !broadcast)
        {
            for (PeerId peer : d->peers)
                d->enqueue(peer, message);
            d->pump();
            return;
        }
    }

    d->enqueue(to, message);
    d->pump();
}

qint64 UBCollabRelayChannel::bytesToSend() const
{
    return d->queued + (d->line ? d->line->bytesToWrite() : 0);
}

void UBCollabRelayChannel::close()
{
    if (d->state == Closed)
        return;

    d->teardown();
    ++d->generation;
    d->error = NoError;
    d->errorText.clear();
    d->setState(Closed);
}
