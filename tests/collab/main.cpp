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

// Console tests of the WebSocket client, the encryption and the relay channel.
// Without arguments the channel is tested against UBCollabTestRelay; with
// --relay <address> also against a running relay (relay/relay.js under
// "wrangler dev"), with --host-key when that relay has HOST_KEY set and
// --grace with its AWAY_GRACE_SECONDS.

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QtEndian>

#include <cstdio>
#include <functional>
#include <memory>

#include "UBCollabCrypto.h"
#include "UBCollabRelayChannel.h"
#include "UBCollabRelayTuning.h"
#include "UBCollabTestRelay.h"
#include "UBCollabWebSocket.h"

namespace
{
    int sChecks = 0;
    int sFailures = 0;

    bool check(bool ok, const char* what, const char* file, int line)
    {
        ++sChecks;
        if (!ok)
        {
            ++sFailures;
            std::printf("   FAIL %s:%d: %s\n", file, line, what);
            std::fflush(stdout);
        }
        return ok;
    }

#define CHECK(condition) check(bool(condition), #condition, __FILE__, __LINE__)

    void section(const char* name)
    {
        std::printf("-- %s\n", name);
        std::fflush(stdout);
    }

    void note(const QString& text)
    {
        std::printf("   %s\n", qPrintable(text));
        std::fflush(stdout);
    }

    void pause(int milliseconds)
    {
        QEventLoop loop;
        QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
        loop.exec();
    }

    bool waitFor(const std::function<bool()>& done, int timeoutMs = 10000)
    {
        QElapsedTimer timer;
        timer.start();
        while (!done())
        {
            if (timer.elapsed() > timeoutMs)
                return false;
            pause(2);
        }
        return true;
    }

    QByteArray randomData(qsizetype size)
    {
        QByteArray data(size, Qt::Uninitialized);
        QRandomGenerator generator{ quint32(size) };
        for (qsizetype i = 0; i < size; ++i)
            data[i] = char(generator.generate() & 0xFF);
        return data;
    }

    QByteArray hex(const char* text)
    {
        return QByteArray::fromHex(text);
    }

    typedef UBCollabChannel::PeerId PeerId;

    struct Environment
    {
        QUrl relay;
        QString hostKey;
        bool external = false;
        int graceMs = 1500;
        UBCollabTestRelay* testRelay = nullptr;
    };

    Environment sEnv;
}

// ---------------------------------------------------------------- codes and invitations

static void testCodes()
{
    section("session codes");

    QSet<QString> seen;
    bool allValid = true;
    for (int i = 0; i < 2000; ++i)
    {
        const QString code = UBCollabRelayChannel::newSessionCode();
        if (code.size() != 29 || UBCollabRelayChannel::normalizedCode(code) != code || seen.contains(code))
            allValid = false;
        seen.insert(code);
    }
    CHECK(allValid);

    const QString code = UBCollabRelayChannel::newSessionCode();
    static const QRegularExpression form(QStringLiteral("^([0-9A-HJKMNP-TV-Z]{5}-){4}[0-9A-HJKMNP-TV-Z]{5}$"));
    CHECK(form.match(code).hasMatch());

    QString plain = code;
    plain.remove(QLatin1Char('-'));
    CHECK(UBCollabRelayChannel::normalizedCode(code.toLower()) == code);
    CHECK(UBCollabRelayChannel::normalizedCode(plain) == code);
    CHECK(UBCollabRelayChannel::normalizedCode(QStringLiteral("  ") + plain.left(7) + QStringLiteral(" ") + plain.mid(7) + QStringLiteral("\n")) == code);

    QString dashes = code;
    dashes.replace(QLatin1Char('-'), QChar(0x2013));
    CHECK(UBCollabRelayChannel::normalizedCode(dashes) == code);

    // look-alikes: O for 0, I and L for 1
    QString withZeroAndOne;
    for (int i = 0; i < 200 && withZeroAndOne.isEmpty(); ++i)
    {
        const QString candidate = UBCollabRelayChannel::newSessionCode();
        if (candidate.contains(QLatin1Char('0')) && candidate.contains(QLatin1Char('1')))
            withZeroAndOne = candidate;
    }
    CHECK(!withZeroAndOne.isEmpty());
    QString lookAlike = withZeroAndOne;
    lookAlike.replace(QLatin1Char('0'), QLatin1Char('O'));
    lookAlike.replace(QLatin1Char('1'), QLatin1Char('l'));
    CHECK(UBCollabRelayChannel::normalizedCode(lookAlike) == withZeroAndOne);

    CHECK(UBCollabRelayChannel::normalizedCode(QString()).isEmpty());
    CHECK(UBCollabRelayChannel::normalizedCode(plain.left(24)).isEmpty());
    CHECK(UBCollabRelayChannel::normalizedCode(plain + QStringLiteral("0")).isEmpty());
    CHECK(UBCollabRelayChannel::normalizedCode(plain.left(24) + QStringLiteral("U")).isEmpty());
    CHECK(UBCollabRelayChannel::normalizedCode(plain.left(24) + QStringLiteral("!")).isEmpty());

    // the check character: every changed character and every swap of neighbours is noticed,
    // except a 0 that became Z or the other way round (their difference is the modulus)
    const char* alphabet = UBCollabCrypto::codeAlphabet();
    int unnoticed = 0;
    int unexplained = 0;
    for (int position = 0; position < 25; ++position)
    {
        for (int value = 0; value < 32; ++value)
        {
            QString changed = plain;
            if (changed[position] == QLatin1Char(alphabet[value]))
                continue;
            const QChar before = changed[position];
            changed[position] = QLatin1Char(alphabet[value]);
            if (!UBCollabRelayChannel::normalizedCode(changed).isEmpty())
            {
                ++unnoticed;
                const bool zeroZ = (before == QLatin1Char('0') && alphabet[value] == 'Z') || (before == QLatin1Char('Z') && alphabet[value] == '0');
                if (!zeroZ)
                    ++unexplained;
            }
        }
    }
    CHECK(unexplained == 0);

    int swapsUnexplained = 0;
    for (int position = 0; position < 24; ++position)
    {
        QString swapped = plain;
        const QChar a = swapped[position];
        const QChar b = swapped[position + 1];
        if (a == b)
            continue;
        swapped[position] = b;
        swapped[position + 1] = a;
        const bool zeroZ = (a == QLatin1Char('0') && b == QLatin1Char('Z')) || (a == QLatin1Char('Z') && b == QLatin1Char('0'));
        if (!UBCollabRelayChannel::normalizedCode(swapped).isEmpty() && !zeroZ)
            ++swapsUnexplained;
    }
    CHECK(swapsUnexplained == 0);
    note(QStringLiteral("2000 codes unique and valid; %1 of 775 single changes unnoticed (all 0<->Z)").arg(unnoticed));
}

static void testInvitations()
{
    section("invitations");

    const QString code = UBCollabRelayChannel::newSessionCode();
    const QUrl relay(QStringLiteral("https://relay.example.workers.dev"));
    const QString invitation = UBCollabRelayChannel::invitation(relay, code);
    CHECK(invitation == QStringLiteral("https://relay.example.workers.dev/#join=") + code);

    QUrl parsedRelay;
    QString parsedCode;
    CHECK(UBCollabRelayChannel::parseInvitation(invitation, parsedRelay, parsedCode));
    CHECK(parsedCode == code);
    CHECK(parsedRelay == QUrl(QStringLiteral("https://relay.example.workers.dev/")));

    const QStringList wrapped = {
        QStringLiteral("  \n") + invitation + QStringLiteral("\t "),
        QStringLiteral("Join my board: ") + invitation + QStringLiteral(". See you!"),
        QStringLiteral("<") + invitation + QStringLiteral(">"),
        QStringLiteral("\"") + invitation + QStringLiteral("\""),
        QStringLiteral("(") + invitation + QStringLiteral(")"),
        invitation.left(invitation.indexOf(QLatin1Char('#'))) + QStringLiteral("#join=") + code.toLower()
    };
    for (const QString& text : wrapped)
    {
        parsedCode.clear();
        CHECK(UBCollabRelayChannel::parseInvitation(text, parsedRelay, parsedCode) && parsedCode == code);
    }

    // a wss address becomes a web address in the invitation; the path stays
    CHECK(UBCollabRelayChannel::invitation(QUrl(QStringLiteral("wss://relay.example.org/obml")), code)
          == QStringLiteral("https://relay.example.org/obml#join=") + code);
    CHECK(UBCollabRelayChannel::parseInvitation(QStringLiteral("http://127.0.0.1:8787/#join=") + code, parsedRelay, parsedCode));
    CHECK(parsedRelay.port() == 8787);

    QString broken = code;
    broken[3] = broken[3] == QLatin1Char('A') ? QLatin1Char('B') : QLatin1Char('A');
    CHECK(!UBCollabRelayChannel::parseInvitation(QStringLiteral("https://relay.example.workers.dev/"), parsedRelay, parsedCode));
    CHECK(!UBCollabRelayChannel::parseInvitation(QStringLiteral("hello"), parsedRelay, parsedCode));
    CHECK(!UBCollabRelayChannel::parseInvitation(QStringLiteral("https://relay.example.workers.dev/#join=") + broken, parsedRelay, parsedCode));
    CHECK(!UBCollabRelayChannel::parseInvitation(QStringLiteral("http://relay.example.org/#join=") + code, parsedRelay, parsedCode));
    CHECK(!UBCollabRelayChannel::parseInvitation(code, parsedRelay, parsedCode));

    // of two, the first that is a valid invitation counts
    CHECK(UBCollabRelayChannel::parseInvitation(QStringLiteral("https://a.example.org/#join=") + broken
                                                + QStringLiteral(" or https://b.example.org/#join=") + code, parsedRelay, parsedCode));
    CHECK(parsedRelay.host() == QStringLiteral("b.example.org"));

    CHECK(UBCollabRelayChannel::invitation(relay, QStringLiteral("nonsense")).isEmpty());
    CHECK(UBCollabRelayChannel::invitation(QUrl(QStringLiteral("http://relay.example.org")), code).isEmpty());

    CHECK(UBCollabRelayChannel::isRelayAddress(QUrl(QStringLiteral("https://relay.example.workers.dev"))));
    CHECK(UBCollabRelayChannel::isRelayAddress(QUrl(QStringLiteral("wss://relay.example.workers.dev/path"))));
    CHECK(UBCollabRelayChannel::isRelayAddress(QUrl(QStringLiteral("http://localhost:8787"))));
    CHECK(UBCollabRelayChannel::isRelayAddress(QUrl(QStringLiteral("ws://[::1]:8787/"))));
    CHECK(!UBCollabRelayChannel::isRelayAddress(QUrl(QStringLiteral("http://relay.example.org"))));
    CHECK(!UBCollabRelayChannel::isRelayAddress(QUrl(QStringLiteral("ftp://relay.example.org"))));
    CHECK(!UBCollabRelayChannel::isRelayAddress(QUrl(QStringLiteral("relay.example.org"))));
    CHECK(!UBCollabRelayChannel::isRelayAddress(QUrl(QStringLiteral("https://user@relay.example.org"))));
}

// ---------------------------------------------------------------- encryption

static void testCrypto()
{
    section("encryption");

    // RFC 5869, test case 1
    CHECK(UBCollabCrypto::hkdf(QByteArray(22, 0x0b), hex("000102030405060708090a0b0c"), hex("f0f1f2f3f4f5f6f7f8f9"), 42)
          == hex("3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865"));

    // the GCM specification, test cases 13 and 14 (AES-256)
    const QByteArray zeroKey(32, '\0');
    const QByteArray zeroNonce(12, '\0');
    QByteArray out(16, '\0');
    CHECK(UBCollabCrypto::encrypt(zeroKey, zeroNonce, QByteArrayView(), QByteArrayView(), QByteArrayView(), out.data()));
    CHECK(out == hex("530f8afbc74536b9a963b4f1c4cb738b"));
    const QByteArray zeroBlock(16, '\0');
    out = QByteArray(32, '\0');
    CHECK(UBCollabCrypto::encrypt(zeroKey, zeroNonce, QByteArrayView(), zeroBlock.left(5), zeroBlock.mid(5), out.data()));
    CHECK(out == hex("cea7403d4d606b6e074ec5d3baf39d18d0d1c8a799996bf0265b98b5d48ab919"));
    QByteArray plain;
    CHECK(UBCollabCrypto::decrypt(zeroKey, zeroNonce, QByteArrayView(), out, plain) && plain == zeroBlock);
    out[3] = char(out[3] ^ 1);
    CHECK(!UBCollabCrypto::decrypt(zeroKey, zeroNonce, QByteArrayView(), out, plain) && plain.isEmpty());

    const QString code = UBCollabRelayChannel::newSessionCode();
    const QString other = UBCollabRelayChannel::newSessionCode();
    const QByteArray master = UBCollabCrypto::masterKey(code);
    CHECK(master.size() == 32);
    CHECK(UBCollabCrypto::roomId(code).size() == 64);
    CHECK(UBCollabCrypto::roomId(code) == UBCollabCrypto::roomId(code));
    CHECK(UBCollabCrypto::roomId(code) != UBCollabCrypto::roomId(other));
    CHECK(QByteArray::fromHex(UBCollabCrypto::roomId(code)) != master);

    UBCollabSealer sealer(master);
    UBCollabSealer second(master);
    CHECK(sealer.isValid() && second.isValid());
    CHECK(sealer.instance() != second.instance());

    const PeerId from = 7;
    const PeerId to = 9;
    const QByteArray message = QByteArray("hello, ") + randomData(1000);
    const QByteArray sealed1 = sealer.seal(from, to, message.left(3), message.mid(3), 4);
    const QByteArray sealed2 = sealer.seal(from, 0, QByteArrayView(), QByteArrayView("second"), 0);
    const QByteArray sealed3 = sealer.seal(from, 0, QByteArrayView("third"), QByteArrayView(), 0);
    CHECK(sealed1.size() == 4 + UBCollabCrypto::Overhead + message.size());
    CHECK(sealed1.left(4) == QByteArray(4, '\0'));
    CHECK(sealer.lastSequence() == 3);

    {
        UBCollabOpener opener(master);
        QByteArray opened;
        quint32 addressee = 0;
        CHECK(opener.open(from, QByteArrayView(sealed1).sliced(4), opened, addressee) == UBCollabOpener::Opened);
        CHECK(opened == message && addressee == to && opener.isBound());
        CHECK(opener.open(from, QByteArrayView(sealed1).sliced(4), opened, addressee) == UBCollabOpener::Replayed);
        CHECK(opener.open(from, sealed3, opened, addressee) == UBCollabOpener::Opened && opened == "third" && addressee == 0);
        // the second comes after the third: out of order
        CHECK(opener.open(from, sealed2, opened, addressee) == UBCollabOpener::Replayed);
        CHECK(opener.open(from, second.seal(from, 0, QByteArrayView("x"), QByteArrayView()), opened, addressee) == UBCollabOpener::OtherInstance);
    }

    {
        // every changed byte is noticed
        const QByteArray sealed = QByteArrayView(sealed1).sliced(4).toByteArray();
        int unnoticed = 0;
        for (int i = 0; i < sealed.size(); i += (i < 64 ? 1 : 97))
        {
            QByteArray changed = sealed;
            changed[i] = char(changed[i] ^ 0x20);
            UBCollabOpener opener(master);
            QByteArray opened;
            quint32 addressee = 0;
            const UBCollabOpener::Result result = opener.open(from, changed, opened, addressee);
            if (result == UBCollabOpener::Opened || !opened.isEmpty() || (i == 0 && result != UBCollabOpener::OtherVersion))
                ++unnoticed;
        }
        CHECK(unnoticed == 0);

        UBCollabOpener opener(master);
        QByteArray opened;
        quint32 addressee = 0;
        CHECK(opener.open(from + 1, sealed, opened, addressee) == UBCollabOpener::Forged);   // attributed to another sender
        CHECK(opener.open(from, sealed.left(UBCollabCrypto::Overhead - 1), opened, addressee) == UBCollabOpener::Malformed);
        CHECK(opener.open(from, sealed.left(sealed.size() - 1), opened, addressee) == UBCollabOpener::Forged);
        CHECK(!opener.isBound());

        UBCollabOpener wrongCode(UBCollabCrypto::masterKey(other));
        CHECK(wrongCode.open(from, sealed, opened, addressee) == UBCollabOpener::Forged);

        // the addressee is authenticated: turned into a broadcast it does not open
        QByteArray broadcast = sealed;
        qToBigEndian(quint32(0), broadcast.data() + 1 + UBCollabCrypto::InstanceSize + 8);
        CHECK(opener.open(from, broadcast, opened, addressee) == UBCollabOpener::Forged);
    }

    {
        UBCollabSealer empty(master);
        const QByteArray sealed = empty.seal(1, 0, QByteArrayView(), QByteArrayView());
        UBCollabOpener opener(master);
        QByteArray opened("x");
        quint32 addressee = 5;
        CHECK(opener.open(1, sealed, opened, addressee) == UBCollabOpener::Opened && opened.isEmpty() && addressee == 0);
    }
}

// ---------------------------------------------------------------- WebSocket client

namespace
{
    // one scripted server connection at a time
    class Script : public QObject
    {
        public:
            explicit Script(const QSslCertificate& certificate = QSslCertificate(), const QSslKey& key = QSslKey())
            {
                if (!certificate.isNull())
                    server.setCertificate(certificate, key);
                server.listen(QHostAddress::LocalHost, 0);
                QObject::connect(&server, &QTcpServer::newConnection, this, [this]()
                {
                    QTcpSocket* socket = server.nextPendingConnection();
                    if (!socket)
                        return;
                    connection = new UBCollabTestConnection(socket, this);
                    QObject::connect(connection, &UBCollabTestConnection::requestReceived, this, [this]() { requested = true; });
                    QObject::connect(connection, &UBCollabTestConnection::frameReceived, this,
                                     [this](int opcode, const QByteArray& payload, bool, int) { frames.append(qMakePair(opcode, payload)); });
                    QObject::connect(connection, &UBCollabTestConnection::binaryReceived, this,
                                     [this](const QByteArray& m) { binaries.append(m); });
                });
            }

            QUrl url(const QString& path = QStringLiteral("/")) const
            {
                return QUrl(QStringLiteral("%1://127.0.0.1:%2%3").arg(server.isSecure() ? QStringLiteral("wss") : QStringLiteral("ws"))
                            .arg(server.serverPort()).arg(path));
            }

            quint16 closeFrom() const
            {
                for (const auto& frame : frames)
                {
                    if (frame.first == 0x8 && frame.second.size() >= 2)
                        return qFromBigEndian<quint16>(frame.second.constData());
                }
                return 0;
            }

            UBCollabTestServer server;
            QPointer<UBCollabTestConnection> connection;
            bool requested = false;
            QList<QPair<int, QByteArray>> frames;
            QList<QByteArray> binaries;
    };

    class Client : public QObject
    {
        public:
            Client()
            {
                QObject::connect(&ws, &UBCollabWebSocket::connected, this, [this]() { connected = true; });
                QObject::connect(&ws, &UBCollabWebSocket::disconnected, this, [this]() { ++disconnects; });
                QObject::connect(&ws, &UBCollabWebSocket::textMessageReceived, this, [this](const QByteArray& t) { texts.append(t); });
                QObject::connect(&ws, &UBCollabWebSocket::binaryMessageReceived, this, [this](const QByteArray& b) { binaries.append(b); });
                QObject::connect(&ws, &UBCollabWebSocket::pongReceived, this, [this](const QByteArray& p) { pongs.append(p); });
            }

            UBCollabWebSocket ws;
            bool connected = false;
            int disconnects = 0;
            QList<QByteArray> texts;
            QList<QByteArray> binaries;
            QList<QByteArray> pongs;
    };

    bool openPair(Script& script, Client& client, const UBCollabWebSocket::Headers& headers = {},
                  const QString& path = QStringLiteral("/"))
    {
        client.ws.open(script.url(path), headers);
        if (!waitFor([&]() { return script.requested || client.disconnects > 0; }, 5000) || !script.connection)
            return false;
        script.connection->accept();
        return waitFor([&]() { return client.connected || client.disconnects > 0; }, 5000) && client.connected;
    }

    // a fresh pair; the server then sends \a bytes; returns the failure of the client
    UBCollabWebSocket::Failure afterServerSends(const QByteArray& bytes, Script* keep = nullptr, qint64 maxMessage = 0)
    {
        Script local;
        Script& script = keep ? *keep : local;
        Client client;
        if (maxMessage > 0)
            client.ws.setMaxMessageSize(maxMessage);
        if (!openPair(script, client))
            return UBCollabWebSocket::NoFailure;
        script.connection->writeRaw(bytes);
        waitFor([&]() { return client.disconnects > 0; }, 3000);
        return client.disconnects > 0 ? client.ws.failure() : UBCollabWebSocket::NoFailure;
    }
}

static void testWebSocket()
{
    section("WebSocket client: handshake");
    {
        Script script;
        Client client;
        CHECK(openPair(script, client, { qMakePair(QByteArray("OBML-Test"), QByteArray("yes")) }, QStringLiteral("/some/path?x=1")));
        CHECK(script.connection->header("obml-test") == "yes");
        CHECK(script.connection->header("host") == "127.0.0.1:" + QByteArray::number(script.server.serverPort()));
        CHECK(script.connection->path() == "/some/path?x=1");
        CHECK(script.connection->header("sec-websocket-version") == "13");
        CHECK(QByteArray::fromBase64(script.connection->header("sec-websocket-key")).size() == 16);
        CHECK(!script.connection->header("sec-websocket-extensions").size());
    }

    auto handshakeWith = [](const std::function<void(UBCollabTestConnection*)>& answer, int timeoutMs = 5000)
    {
        Script script;
        Client client;
        client.ws.setHandshakeTimeout(timeoutMs);
        client.ws.open(script.url());
        waitFor([&]() { return script.requested; }, 3000);
        if (script.connection)
            answer(script.connection);
        waitFor([&]() { return client.disconnects > 0 || client.connected; }, timeoutMs + 3000);
        return std::make_pair(client.connected, client.ws.failure());
    };

    auto r = handshakeWith([](UBCollabTestConnection* c) { c->accept({}, "d3JvbmcgYWNjZXB0IHZhbHVlIQ=="); });
    CHECK(!r.first && r.second == UBCollabWebSocket::HandshakeInvalid);
    r = handshakeWith([](UBCollabTestConnection* c) { c->accept({}, QByteArray(), false); });
    CHECK(!r.first && r.second == UBCollabWebSocket::HandshakeInvalid);
    r = handshakeWith([](UBCollabTestConnection* c) { c->accept({ qMakePair(QByteArray("Sec-WebSocket-Extensions"), QByteArray("permessage-deflate")) }); });
    CHECK(!r.first && r.second == UBCollabWebSocket::HandshakeInvalid);
    r = handshakeWith([](UBCollabTestConnection* c) { c->accept({ qMakePair(QByteArray("Sec-WebSocket-Protocol"), QByteArray("chat")) }); });
    CHECK(!r.first && r.second == UBCollabWebSocket::HandshakeInvalid);
    r = handshakeWith([](UBCollabTestConnection* c) { c->writeRaw("FOO BAR\r\n\r\n"); });
    CHECK(!r.first && r.second == UBCollabWebSocket::HandshakeInvalid);
    r = handshakeWith([](UBCollabTestConnection* c) { c->writeRaw("HTTP/1.1 101 OK\r\nX: " + QByteArray(20000, 'a')); });
    CHECK(!r.first && r.second == UBCollabWebSocket::HandshakeInvalid);
    r = handshakeWith([](UBCollabTestConnection*) {}, 300);
    CHECK(!r.first && r.second == UBCollabWebSocket::Timeout);

    {
        Script script;
        Client client;
        client.ws.open(script.url());
        waitFor([&]() { return script.requested; }, 3000);
        script.connection->reject(404, { qMakePair(QByteArray("OBML-Reason"), QByteArray("not-found")) }, "nobody here");
        waitFor([&]() { return client.disconnects > 0; }, 3000);
        CHECK(client.ws.failure() == UBCollabWebSocket::HandshakeRejected);
        CHECK(client.ws.rejectStatus() == 404);
        CHECK(client.ws.rejectHeader("obml-reason") == "not-found");
    }
    {
        Client client;
        client.ws.open(QUrl(QStringLiteral("ws://relay.example.org/")));
        CHECK(waitFor([&]() { return client.disconnects > 0; }, 1000) && client.ws.failure() == UBCollabWebSocket::AddressRefused);
    }
    {
        Client client;
        client.ws.open(QUrl(QStringLiteral("ftp://127.0.0.1/")));
        CHECK(waitFor([&]() { return client.disconnects > 0; }, 1000) && client.ws.failure() == UBCollabWebSocket::AddressRefused);
    }
    {
        // a port where nothing listens
        QTcpServer probe;
        probe.listen(QHostAddress::LocalHost, 0);
        const quint16 port = probe.serverPort();
        probe.close();
        Client client;
        client.ws.open(QUrl(QStringLiteral("ws://127.0.0.1:%1/").arg(port)));
        CHECK(waitFor([&]() { return client.disconnects > 0; }, 5000) && client.ws.failure() == UBCollabWebSocket::ConnectFailed);
    }

    section("WebSocket client: frames from the server");
    {
        Script script;
        Client client;
        CHECK(openPair(script, client));
        const QList<qsizetype> sizes = { 0, 1, 125, 126, 200, 65535, 65536, 70000, (1 << 20) + 3 };
        QByteArray all;
        for (qsizetype size : sizes)
            all += testServerFrame(0x2, randomData(size));
        all += testServerFrame(0x1, "text \xc3\xa9");
        script.connection->writeRaw(all);
        CHECK(waitFor([&]() { return client.binaries.size() == sizes.size() && client.texts.size() == 1; }));
        bool same = client.binaries.size() == sizes.size();
        for (int i = 0; same && i < sizes.size(); ++i)
            same = client.binaries.at(i) == randomData(sizes.at(i));
        CHECK(same);
        CHECK(client.texts.value(0) == "text \xc3\xa9");

        // fragments with control frames between them, a UTF-8 character split over two fragments
        script.frames.clear();
        script.connection->writeRaw(testServerFrame(0x2, "abc", false) + testServerFrame(0x9, "p1")
                                    + testServerFrame(0x0, "def", false) + testServerFrame(0xA, "x")
                                    + testServerFrame(0x0, "ghi", true)
                                    + testServerFrame(0x1, "a\xc3", false) + testServerFrame(0x0, "\xa9", true));
        CHECK(waitFor([&]() { return client.binaries.size() == sizes.size() + 1 && client.texts.size() == 2; }));
        CHECK(client.binaries.last() == "abcdefghi");
        CHECK(client.texts.last() == "a\xc3\xa9");
        CHECK(client.pongs == QList<QByteArray>{ "x" });
        CHECK(waitFor([&]() { return !script.frames.isEmpty(); }) && script.frames.first() == qMakePair(0xA, QByteArray("p1")));

        // one byte at a time
        const QByteArray slow = testServerFrame(0x2, randomData(300)) + testServerFrame(0x1, "slow");
        for (char byte : slow)
        {
            script.connection->writeRaw(QByteArray(1, byte));
            pause(1);
        }
        CHECK(waitFor([&]() { return client.texts.size() == 3; }) && client.binaries.last() == randomData(300) && client.texts.last() == "slow");

        // many in one write
        QByteArray many;
        for (int i = 0; i < 1000; ++i)
            many += testServerFrame(0x2, QByteArray::number(i));
        const int before = client.binaries.size();
        script.connection->writeRaw(many);
        CHECK(waitFor([&]() { return client.binaries.size() == before + 1000; }));
        bool ordered = true;
        for (int i = 0; i < 1000 && ordered; ++i)
            ordered = client.binaries.value(before + i) == QByteArray::number(i);
        CHECK(ordered);
    }

    section("WebSocket client: frames the server must not send");
    QByteArray msbSet = testServerFrame(0x2, QByteArray(), true, 0, false, 2);
    msbSet[2] = char(0x80);
    QByteArray hugeLength = testServerFrame(0x2, QByteArray(), true, 0, false, 2);
    hugeLength[2] = 0x7F;
    hugeLength[3] = char(0xFF);

    const QList<QPair<const char*, QByteArray>> violations = {
        { "length 100 in 16 bits", testServerFrame(0x2, QByteArray(100, 'a'), true, 0, false, 1) },
        { "length 1000 in 64 bits", testServerFrame(0x2, QByteArray(1000, 'a'), true, 0, false, 2) },
        { "64-bit length with the high bit", msbSet },
        { "continuation without start", testServerFrame(0x0, "abc") },
        { "new message inside a fragmented one", testServerFrame(0x2, "a", false) + testServerFrame(0x1, "b") },
        { "fragmented ping", testServerFrame(0x9, "p", false) },
        { "ping of 126 bytes", testServerFrame(0x9, QByteArray(126, 'p')) },
        { "reserved bit", testServerFrame(0x2, "abc", true, 4) },
        { "masked frame", testServerFrame(0x2, "abc", true, 0, true) },
        { "data opcode 3", testServerFrame(0x3, "abc") },
        { "control opcode 11", testServerFrame(0xB, "abc") },
        { "text that is not UTF-8", testServerFrame(0x1, "a\xc3\x28") },
        { "overlong UTF-8", testServerFrame(0x1, "\xc0\xaf") },
        { "UTF-16 surrogate in UTF-8", testServerFrame(0x1, "\xed\xa0\x80") },
        { "close with one byte", testServerFrame(0x8, "a") },
        { "close code 1005 in a frame", testServerFrame(0x8, QByteArray("\x03\xed", 2)) },
        { "close code 999", testServerFrame(0x8, QByteArray("\x03\xe7", 2)) },
    };
    for (const auto& violation : violations)
    {
        Script script;
        const UBCollabWebSocket::Failure failure = afterServerSends(violation.second, &script);
        if (!CHECK(failure == UBCollabWebSocket::ProtocolViolation))
            note(QStringLiteral("not refused: %1 (failure %2)").arg(QLatin1String(violation.first)).arg(int(failure)));
        const bool utf8 = QByteArray(violation.first).contains("UTF");
        if (!utf8 && !QByteArray(violation.first).startsWith("close"))
            CHECK(waitFor([&]() { return script.closeFrom() == 1002; }, 2000));
        if (utf8)
            CHECK(waitFor([&]() { return script.closeFrom() == 1007; }, 2000));
    }

    {
        // too large: refused as soon as the header is there, without waiting for the payload
        Script script;
        Client client;
        client.ws.setMaxMessageSize(1 << 20);
        CHECK(openPair(script, client));
        QElapsedTimer timer;
        timer.start();
        script.connection->writeRaw(testServerFrame(0x2, QByteArray(), true, 0, false, 2).left(2) + QByteArray("\0\0\0\0\0\x20\0\0", 8));
        CHECK(waitFor([&]() { return client.disconnects > 0; }, 2000) && client.ws.failure() == UBCollabWebSocket::MessageTooBig);
        CHECK(timer.elapsed() < 1000);
        CHECK(waitFor([&]() { return script.closeFrom() == 1009; }, 2000));
    }
    CHECK(afterServerSends(hugeLength, nullptr, 1 << 20) == UBCollabWebSocket::MessageTooBig);
    CHECK(afterServerSends(testServerFrame(0x2, QByteArray(600000, 'a'), false) + testServerFrame(0x0, QByteArray(600000, 'b')),
                           nullptr, 1 << 20) == UBCollabWebSocket::MessageTooBig);

    section("WebSocket client: frames to the server, closing");
    {
        Script script;
        Client client;
        CHECK(openPair(script, client));
        const QList<qsizetype> sizes = { 0, 1, 125, 126, 65535, 65536, 1 << 20 };
        for (qsizetype size : sizes)
            CHECK(client.ws.sendBinary(randomData(size)));
        CHECK(client.ws.sendText("hello \xc3\xa9"));
        CHECK(!client.ws.sendText("bad \xc3"));
        CHECK(client.ws.ping("p"));
        CHECK(waitFor([&]() { return script.binaries.size() == sizes.size(); }));
        bool same = true;
        for (int i = 0; i < sizes.size(); ++i)
            same = same && script.binaries.value(i) == randomData(sizes.at(i));
        CHECK(same);
        CHECK(waitFor([&]() { return client.pongs.contains("p"); }));
        CHECK(script.connection->clientFramesValid());

        client.ws.close(1000, "bye");
        CHECK(waitFor([&]() { return client.disconnects > 0; }));
        CHECK(client.ws.failure() == UBCollabWebSocket::Closed && client.ws.closeCode() == 1000);
        CHECK(script.closeFrom() == 1000);
        CHECK(!client.ws.sendBinary("late"));
    }
    {
        Script script;
        Client client;
        CHECK(openPair(script, client));
        script.connection->autoAnswer = false;
        script.connection->sendClose(4000, "ended");
        CHECK(waitFor([&]() { return client.disconnects > 0; }));
        CHECK(client.ws.failure() == UBCollabWebSocket::Closed && client.ws.closeCode() == 4000 && client.ws.closeReason() == QStringLiteral("ended"));
        CHECK(waitFor([&]() { return script.closeFrom() == 4000; }, 2000));
    }
    {
        Script script;
        Client client;
        CHECK(openPair(script, client));
        script.connection->abort();
        CHECK(waitFor([&]() { return client.disconnects > 0; }) && client.ws.failure() == UBCollabWebSocket::Dropped);
        CHECK(client.disconnects == 1);
    }
}

// ---------------------------------------------------------------- the channel

namespace
{
    class Probe : public QObject
    {
        public:
            Probe()
            {
                QObject::connect(&channel, &UBCollabChannel::messageReceived, this,
                                 [this](PeerId from, const QByteArray& message) { messages.append(qMakePair(from, message)); });
                QObject::connect(&channel, &UBCollabChannel::peerJoined, this, [this](PeerId peer) { joined.append(peer); });
                QObject::connect(&channel, &UBCollabChannel::peerLeft, this, [this](PeerId peer) { left.append(peer); });
                QObject::connect(&channel, &UBCollabChannel::resumed, this, [this]() { ++resumed; });
                QObject::connect(&channel, &UBCollabChannel::stateChanged, this, [this]() { states.append(channel.state()); });
            }

            bool isOpen() const { return channel.state() == UBCollabChannel::Open; }
            bool isClosed() const { return channel.state() == UBCollabChannel::Closed && !states.isEmpty(); }
            PeerId id() const { return channel.localPeer(); }

            QList<QByteArray> from(PeerId peer) const
            {
                QList<QByteArray> list;
                for (const auto& message : messages)
                {
                    if (message.first == peer)
                        list.append(message.second);
                }
                return list;
            }

            UBCollabRelayChannel channel;
            QList<QPair<PeerId, QByteArray>> messages;
            QList<PeerId> joined;
            QList<PeerId> left;
            int resumed = 0;
            QList<int> states;
    };

    typedef std::unique_ptr<Probe> ProbePtr;

    ProbePtr host(const QString& code, const QUrl& relay = QUrl())
    {
        ProbePtr probe = std::make_unique<Probe>();
        probe->channel.start(relay.isEmpty() ? sEnv.relay : relay, code, sEnv.hostKey);
        if (!waitFor([&]() { return probe->isOpen() || probe->isClosed(); }))
            note(QStringLiteral("starting timed out"));
        if (!probe->isOpen())
            note(QStringLiteral("start: %1").arg(probe->channel.errorString()));
        return probe;
    }

    ProbePtr guest(const QString& code, const QUrl& relay = QUrl())
    {
        ProbePtr probe = std::make_unique<Probe>();
        probe->channel.join(relay.isEmpty() ? sEnv.relay : relay, code);
        waitFor([&]() { return probe->isOpen() || probe->isClosed(); });
        return probe;
    }

    bool knowEachOther(const QList<Probe*>& probes)
    {
        return waitFor([&]()
        {
            for (Probe* a : probes)
            {
                for (Probe* b : probes)
                {
                    if (a != b && !a->channel.peers().contains(b->id()))
                        return false;
                }
            }
            return true;
        });
    }
}

static void testTls()
{
    section("WebSocket client: TLS");

    QSslCertificate certificate;
    QSslKey key;
    QSslCertificate otherName;
    QSslKey otherKey;
    if (!CHECK(testCertificate(certificate, key) && testCertificate(otherName, otherKey, "relay.example.invalid")))
        return;
    note(QStringLiteral("TLS backend: %1").arg(QSslSocket::activeBackend()));

    QSslConfiguration trusting = QSslConfiguration::defaultConfiguration();
    trusting.addCaCertificate(certificate);
    trusting.addCaCertificate(otherName);

    {
        // with the certificate trusted: a working wss connection
        Script script(certificate, key);
        Client client;
        client.ws.setSslConfiguration(trusting);
        CHECK(openPair(script, client));
        CHECK(client.ws.sendBinary("over TLS"));
        script.connection->sendText("back over TLS");
        CHECK(waitFor([&]() { return script.binaries.contains("over TLS") && client.texts.contains("back over TLS"); }));
        client.ws.close();
        CHECK(waitFor([&]() { return client.disconnects > 0; }) && client.ws.failure() == UBCollabWebSocket::Closed);
    }
    {
        // the system does not know the certificate: refused
        Script script(certificate, key);
        Client client;
        client.ws.open(script.url());
        CHECK(waitFor([&]() { return client.disconnects > 0; }) && client.ws.failure() == UBCollabWebSocket::TlsFailed);
        note(client.ws.errorString());
        CHECK(!script.requested);
    }
    {
        // a trusted certificate for another name: refused
        Script script(otherName, otherKey);
        Client client;
        client.ws.setSslConfiguration(trusting);
        client.ws.open(script.url());
        CHECK(waitFor([&]() { return client.disconnects > 0; }) && client.ws.failure() == UBCollabWebSocket::TlsFailed);
        note(client.ws.errorString());
        CHECK(!script.requested);
    }

    section("channel: over TLS");
    {
        const QSslConfiguration saved = QSslConfiguration::defaultConfiguration();
        QSslConfiguration::setDefaultConfiguration(trusting);

        UBCollabTestRelay relay;
        relay.setCertificate(certificate, key);
        CHECK(relay.listen() && relay.url().scheme() == QStringLiteral("https"));
        const QString code = UBCollabRelayChannel::newSessionCode();
        ProbePtr a = host(code, relay.url());
        ProbePtr b = guest(code, relay.url());
        CHECK(a->isOpen() && b->isOpen() && knowEachOther({ a.get(), b.get() }));
        const QByteArray big = randomData(3 * 1024 * 1024);
        a->channel.send(big);
        b->channel.send("hello over wss");
        CHECK(waitFor([&]() { return b->from(a->id()).contains(big) && a->from(b->id()).contains("hello over wss"); }));
        a->channel.close();
        b->channel.close();

        QSslConfiguration::setDefaultConfiguration(saved);
    }
}

static void testChannelBasics()
{
    section("channel: start, join, peers, messages, order");

    const QString code = UBCollabRelayChannel::newSessionCode();
    ProbePtr a = host(code);
    CHECK(a->isOpen());
    CHECK(a->id() != 0 && a->channel.peers().isEmpty());
    CHECK(a->states == QList<int>({ UBCollabChannel::Connecting, UBCollabChannel::Open }));

    ProbePtr b = guest(code);
    CHECK(b->isOpen());
    ProbePtr c = guest(code);
    CHECK(c->isOpen());
    CHECK(knowEachOther({ a.get(), b.get(), c.get() }));
    CHECK(a->id() != b->id() && b->id() != c->id() && a->id() != c->id());
    CHECK(a->joined.size() == 2 && b->joined.size() == 2 && c->joined.size() == 2);
    CHECK(a->channel.error() == UBCollabChannel::NoError && b->channel.errorString().isEmpty());

    a->channel.send("to all");
    a->channel.send("to c", c->id());
    CHECK(waitFor([&]() { return b->messages.size() == 1 && c->messages.size() == 2; }));
    pause(100);
    CHECK(a->messages.isEmpty());
    CHECK(b->messages.size() == 1 && b->messages.first() == qMakePair(a->id(), QByteArray("to all")));
    CHECK(c->from(a->id()) == QList<QByteArray>({ "to all", "to c" }));

    // sent to oneself or to nobody in the session: dropped
    a->channel.send("self", a->id());
    a->channel.send("nobody", 12345);
    a->channel.send(QByteArray());
    CHECK(waitFor([&]() { return b->messages.size() == 2 && c->messages.size() == 3; }));
    CHECK(b->messages.last().second.isEmpty());

    // order: a mix of broadcasts and messages for one participant, from two senders at once
    QList<QByteArray> forB;
    QList<QByteArray> forC;
    QList<QByteArray> forA;
    for (int i = 0; i < 300; ++i)
    {
        const QByteArray m = "n" + QByteArray::number(i);
        if (i % 3 == 0)
        {
            a->channel.send(m, b->id());
            forB.append(m);
        }
        else
        {
            a->channel.send(m);
            forB.append(m);
            forC.append(m);
        }
        b->channel.send("b" + QByteArray::number(i), a->id());
        forA.append("b" + QByteArray::number(i));
    }
    CHECK(waitFor([&]() { return b->messages.size() == 2 + 300 && c->messages.size() == 3 + 200 && a->messages.size() == 300; }));
    CHECK(b->from(a->id()).mid(2) == forB);
    CHECK(c->from(a->id()).mid(3) == forC);
    CHECK(a->from(b->id()) == forA);
    CHECK(waitFor([&]() { return a->channel.bytesToSend() == 0 && b->channel.bytesToSend() == 0; }));

    section("channel: large messages");
    const QByteArray big = randomData(20 * 1024 * 1024 + 123);
    const int bBefore = b->messages.size();
    const int cBefore = c->messages.size();
    a->channel.send(big, b->id());
    CHECK(a->channel.bytesToSend() >= big.size());
    for (int i = 0; i < 20; ++i)
        a->channel.send("small " + QByteArray::number(i));

    QElapsedTimer timer;
    timer.start();
    qint64 smallDone = -1;
    CHECK(waitFor([&]()
    {
        if (smallDone < 0 && c->messages.size() == cBefore + 20)
            smallDone = timer.elapsed();
        return b->messages.size() == bBefore + 21;
    }, 120000));
    const qint64 bigDone = timer.elapsed();
    CHECK(b->messages.value(bBefore).second == big);
    bool smallInOrder = true;
    for (int i = 0; i < 20; ++i)
    {
        smallInOrder = smallInOrder && b->messages.value(bBefore + 1 + i).second == "small " + QByteArray::number(i)
            && c->messages.value(cBefore + i).second == "small " + QByteArray::number(i);
    }
    CHECK(smallInOrder);
    note(QStringLiteral("20 MiB to one participant in %1 ms; the small broadcasts reached the other after %2 ms").arg(bigDone).arg(smallDone));
    CHECK(smallDone >= 0 && smallDone < bigDone);
    CHECK(waitFor([&]() { return a->channel.bytesToSend() == 0; }));

    const QByteArray bigAll = randomData(21 * 1024 * 1024);
    c->channel.send(bigAll);
    CHECK(waitFor([&]() { return a->from(c->id()).contains(bigAll) && b->from(c->id()).contains(bigAll); }, 120000));

    section("channel: leaving");
    const PeerId cId = c->id();
    c->channel.close();
    CHECK(c->channel.state() == UBCollabChannel::Closed && c->channel.error() == UBCollabChannel::NoError);
    CHECK(waitFor([&]() { return a->left.contains(cId) && b->left.contains(cId); }, 5000));
    CHECK(!a->channel.peers().contains(cId));

    // the participant who started leaves: the session ends for the others, without error
    const PeerId aId = a->id();
    a->channel.close();
    CHECK(waitFor([&]() { return b->isClosed(); }, 5000));
    CHECK(b->left.contains(aId));
    CHECK(b->channel.error() == UBCollabChannel::NoError);
    note(QStringLiteral("ended for the others: %1").arg(b->channel.errorString()));
    CHECK(b->channel.errorString().contains(QStringLiteral("ended")));

    // and the code can no longer be joined
    ProbePtr late = guest(code);
    CHECK(late->channel.error() == UBCollabChannel::SessionNotFound);
}

static void testChannelRefusals()
{
    section("channel: refusals");

    ProbePtr none = guest(UBCollabRelayChannel::newSessionCode());
    CHECK(none->isClosed() && none->channel.error() == UBCollabChannel::SessionNotFound);
    CHECK(none->states == QList<int>({ UBCollabChannel::Connecting, UBCollabChannel::Closed }));

    ProbePtr notACode = guest(QStringLiteral("not a code"));
    CHECK(notACode->channel.error() == UBCollabChannel::SessionNotFound);

    {
        const QString code = UBCollabRelayChannel::newSessionCode();
        ProbePtr first = host(code);
        ProbePtr second = host(code);
        CHECK(first->isOpen() && second->channel.error() == UBCollabChannel::RelayRefused);
    }

    {
        // a full room: eight participants at most
        const QString code = UBCollabRelayChannel::newSessionCode();
        std::vector<ProbePtr> room;
        room.push_back(host(code));
        for (int i = 1; i < 8; ++i)
            room.push_back(guest(code));
        bool allOpen = true;
        for (const ProbePtr& p : room)
            allOpen = allOpen && p->isOpen();
        CHECK(allOpen);
        ProbePtr ninth = guest(code);
        CHECK(ninth->channel.error() == UBCollabChannel::SessionFull);
        CHECK(waitFor([&]() { return room.front()->channel.peers().size() == 7; }));
        for (int i = int(room.size()) - 1; i >= 0; --i)
            room[i]->channel.close();
    }

    {
        QTcpServer probe;
        probe.listen(QHostAddress::LocalHost, 0);
        const quint16 port = probe.serverPort();
        probe.close();
        ProbePtr unreachable = guest(UBCollabRelayChannel::newSessionCode(), QUrl(QStringLiteral("http://127.0.0.1:%1/").arg(port)));
        CHECK(unreachable->channel.error() == UBCollabChannel::RelayUnreachable);
        note(unreachable->channel.errorString());

        ProbePtr plain = guest(UBCollabRelayChannel::newSessionCode(), QUrl(QStringLiteral("http://relay.example.org/")));
        CHECK(plain->channel.error() == UBCollabChannel::RelayUnreachable);

        // something that answers HTTP but is not a relay
        Script script;
        QObject::connect(&script.server, &QTcpServer::newConnection, &script, [&script]()
        {
            UBCollabTestConnection* connection = script.connection;
            QObject::connect(connection, &UBCollabTestConnection::requestReceived, connection, [connection]()
            {
                connection->reject(404, {}, "Not found");
            });
        });
        ProbePtr notRelay = guest(UBCollabRelayChannel::newSessionCode(), QUrl(QStringLiteral("http://127.0.0.1:%1/").arg(script.server.serverPort())));
        CHECK(notRelay->channel.error() == UBCollabChannel::RelayUnreachable);
        note(notRelay->channel.errorString());
    }
}

static void testHostKey()
{
    section("channel: host key");

    QString key = sEnv.hostKey;
    if (!sEnv.external)
    {
        key = QStringLiteral("example-host-key");
        sEnv.testRelay->options.hostKey = key.toUtf8();
    }
    if (key.isEmpty())
    {
        note(QStringLiteral("skipped: the relay has no host key (pass --host-key)"));
        return;
    }

    const QString code = UBCollabRelayChannel::newSessionCode();
    for (const QString& wrong : { QString(), QStringLiteral("wrong-key") })
    {
        ProbePtr p = std::make_unique<Probe>();
        p->channel.start(sEnv.relay, code, wrong);
        waitFor([&]() { return p->isClosed(); });
        CHECK(p->channel.error() == UBCollabChannel::RelayRefused);
    }

    ProbePtr p = std::make_unique<Probe>();
    p->channel.start(sEnv.relay, code, key);
    waitFor([&]() { return p->isOpen() || p->isClosed(); });
    CHECK(p->isOpen());

    // joining needs no key
    ProbePtr g = guest(code);
    CHECK(g->isOpen());
    g->channel.close();
    p->channel.close();

    if (!sEnv.external)
        sEnv.testRelay->options.hostKey.clear();
}

static void testReconnect()
{
    section("channel: a dropped line");

    const QString code = UBCollabRelayChannel::newSessionCode();
    ProbePtr a = host(code);
    ProbePtr b = guest(code);
    ProbePtr c = guest(code);
    CHECK(knowEachOther({ a.get(), b.get(), c.get() }));
    const PeerId bId = b->id();

    b->states.clear();
    UBCollabRelayTuning::dropLine(&b->channel);
    CHECK(b->channel.state() == UBCollabChannel::Connecting);
    CHECK(waitFor([&]() { return b->resumed == 1; }));
    CHECK(b->isOpen() && b->id() == bId);
    CHECK(b->states == QList<int>({ UBCollabChannel::Connecting, UBCollabChannel::Open }));
    CHECK(b->channel.peers().size() == 2);

    a->channel.send("after the drop");
    b->channel.send("b is back");
    CHECK(waitFor([&]() { return b->from(a->id()).contains("after the drop") && a->from(bId).contains("b is back")
                                 && c->from(bId).contains("b is back"); }));
    CHECK(a->left.isEmpty() && c->left.isEmpty() && b->left.isEmpty());

    // the line of the participant who started drops: nobody leaves
    const PeerId aId = a->id();
    UBCollabRelayTuning::dropLine(&a->channel);
    CHECK(waitFor([&]() { return a->resumed == 1; }));
    CHECK(a->id() == aId && b->left.isEmpty() && c->left.isEmpty());

    // a large message that was on the way when the sender's line dropped arrives whole, once
    const QByteArray big = randomData(8 * 1024 * 1024);
    a->channel.send(big, b->id());
    CHECK(waitFor([&]() { return a->channel.bytesToSend() < big.size() / 2; }, 30000));
    UBCollabRelayTuning::dropLine(&a->channel);
    CHECK(waitFor([&]() { return a->resumed == 2; }));
    CHECK(waitFor([&]() { return b->from(aId).contains(big); }, 60000));
    pause(200);
    CHECK(b->from(aId).count(big) == 1);

    // the receiver's line drops while a large message comes in: that one may be lost, the next arrive
    const QByteArray big2 = randomData(8 * 1024 * 1024 + 1);
    a->channel.send(big2, b->id());
    CHECK(waitFor([&]() { return a->channel.bytesToSend() < big2.size() / 2; }, 30000));
    UBCollabRelayTuning::dropLine(&b->channel);
    CHECK(waitFor([&]() { return b->resumed == 2; }));
    a->channel.send("next", b->id());
    CHECK(waitFor([&]() { return b->from(aId).contains("next"); }, 60000));
    note(QStringLiteral("the message in flight when the receiver's line dropped %1")
         .arg(b->from(aId).contains(big2) ? QStringLiteral("still arrived") : QStringLiteral("was lost, as allowed")));
    CHECK(b->isOpen() && a->left.isEmpty());

    section("channel: a participant that does not come back");
    const PeerId cId = c->id();
    UBCollabRelayTuning::dropLine(&c->channel);
    c.reset();
    QElapsedTimer timer;
    timer.start();
    CHECK(waitFor([&]() { return a->left.contains(cId) && b->left.contains(cId); }, sEnv.graceMs + 15000));
    note(QStringLiteral("left after %1 ms (the relay keeps the place %2 ms)").arg(timer.elapsed()).arg(sEnv.graceMs));
    CHECK(timer.elapsed() >= sEnv.graceMs - 500);

    a->channel.close();
    b->channel.close();
}

static void testKeepAlive()
{
    section("channel: keep-alive on an idle line");

    const QString code = UBCollabRelayChannel::newSessionCode();
    ProbePtr a = host(code);
    ProbePtr b = guest(code);
    CHECK(knowEachOther({ a.get(), b.get() }));
    const int silence = UBCollabRelayTuning::current().silenceLimitMs;
    pause(silence * 2 + 500);
    CHECK(a->isOpen() && b->isOpen() && a->resumed == 0 && b->resumed == 0);

    if (!sEnv.external)
    {
        // the relay stops answering without closing the line: the silence is noticed and the line taken up again
        sEnv.testRelay->freezePeer(b->id());
        CHECK(waitFor([&]() { return b->resumed == 1; }, silence * 3 + 5000));
        a->channel.send("still there?");
        CHECK(waitFor([&]() { return b->from(a->id()).contains("still there?"); }));
    }
    a->channel.close();
    b->channel.close();
}

// the tests below need a relay that misbehaves on purpose: only UBCollabTestRelay

static void testWrongCode()
{
    section("channel: wrong code");

    UBCollabTestRelay relay;
    relay.listen();
    relay.options.mergeRooms = true;

    const QString code = UBCollabRelayChannel::newSessionCode();
    ProbePtr a = host(code, relay.url());
    ProbePtr b = guest(UBCollabRelayChannel::newSessionCode(), relay.url());
    CHECK(b->isClosed() && b->channel.error() == UBCollabChannel::WrongCode);
    note(b->channel.errorString());
    CHECK(b->states == QList<int>({ UBCollabChannel::Connecting, UBCollabChannel::Closed }));
    pause(100);
    CHECK(a->isOpen() && a->channel.peers().isEmpty() && a->joined.isEmpty());

    // the session goes on for those with the right code
    ProbePtr right = guest(code, relay.url());
    CHECK(right->isOpen() && knowEachOther({ a.get(), right.get() }));
}

static void testTampering()
{
    section("channel: a relay that tampers with messages");

    struct Case
    {
        const char* name;
        std::function<bool(UBCollabTestRelay&, PeerId, PeerId, PeerId, quint32 from, quint32& to, QByteArray& m)> filter;
        bool cVictim;
    };

    // each case arms after the session is set up; a, b and c are the peer ids
    QByteArray held;
    int seen = 0;
    const QList<Case> cases = {
        { "a changed byte", [](UBCollabTestRelay&, PeerId a, PeerId, PeerId, quint32 from, quint32&, QByteArray& m)
            {
                if (from == a)
                    m[m.size() - 20] = char(m[m.size() - 20] ^ 1);
                return true;
            }, false },
        { "a replayed message", [](UBCollabTestRelay& r, PeerId a, PeerId b, PeerId, quint32 from, quint32&, QByteArray& m)
            {
                if (from == a)
                    r.sendTo(b, m);
                return true;
            }, false },
        { "two messages swapped", [&held, &seen](UBCollabTestRelay& r, PeerId a, PeerId b, PeerId, quint32 from, quint32&, QByteArray& m)
            {
                if (from != a)
                    return true;
                if (++seen == 1)
                {
                    held = m;
                    return false;
                }
                r.sendTo(b, m);
                r.sendTo(b, held);
                return false;
            }, false },
        { "a message attributed to somebody else", [](UBCollabTestRelay&, PeerId a, PeerId, PeerId c, quint32 from, quint32&, QByteArray& m)
            {
                if (from == a)
                    qToBigEndian(quint32(c), m.data());
                return true;
            }, false },
        { "a message for b delivered to c", [](UBCollabTestRelay&, PeerId a, PeerId, PeerId c, quint32 from, quint32& to, QByteArray&)
            {
                if (from == a)
                    to = c;
                return true;
            }, true },
        { "another protocol version", [](UBCollabTestRelay&, PeerId a, PeerId, PeerId, quint32 from, quint32&, QByteArray& m)
            {
                if (from == a)
                    m[4] = 2;
                return true;
            }, false },
    };

    for (const Case& test : cases)
    {
        held.clear();
        seen = 0;
        UBCollabTestRelay relay;
        relay.listen();
        const QString code = UBCollabRelayChannel::newSessionCode();
        ProbePtr a = host(code, relay.url());
        ProbePtr b = guest(code, relay.url());
        ProbePtr c = guest(code, relay.url());
        knowEachOther({ a.get(), b.get(), c.get() });

        const PeerId aId = a->id();
        const PeerId bId = b->id();
        const PeerId cId = c->id();
        relay.filter = [&relay, &test, aId, bId, cId](quint32 from, quint32& to, QByteArray& m)
        {
            return test.filter(relay, aId, bId, cId, from, to, m);
        };
        a->channel.send("first", bId);
        a->channel.send("second", bId);

        Probe* victim = test.cVictim ? c.get() : b.get();
        const bool failed = waitFor([&]() { return victim->isClosed(); }, 3000);
        if (!CHECK(failed && victim->channel.error() == UBCollabChannel::ProtocolError))
            note(QStringLiteral("not noticed: %1").arg(QLatin1String(test.name)));
        else
            note(QStringLiteral("%1: %2").arg(QLatin1String(test.name), victim->channel.errorString()));
        CHECK(!victim->from(aId).contains("second") || QByteArray(test.name) == "two messages swapped");
        relay.filter = nullptr;
    }

    {
        // a stranger the relay announces and whose messages do not open: ignored, the session goes on
        UBCollabTestRelay relay;
        relay.listen();
        const QString code = UBCollabRelayChannel::newSessionCode();
        ProbePtr a = host(code, relay.url());
        ProbePtr b = guest(code, relay.url());
        knowEachOther({ a.get(), b.get() });
        relay.sendTextTo(a->id(), "{\"type\":\"join\",\"peer\":4242}");
        QByteArray forged(4 + UBCollabCrypto::Overhead + 10, 'x');
        qToBigEndian(quint32(4242), forged.data());
        forged[4] = char(UBCollabCrypto::Version);
        relay.sendTo(a->id(), forged);
        pause(200);
        b->channel.send("still fine");
        CHECK(waitFor([&]() { return a->from(b->id()).contains("still fine"); }));
        CHECK(a->isOpen() && !a->channel.peers().contains(4242u));
    }

    {
        UBCollabTestRelay relay;
        relay.listen();
        relay.options.refuseVersion = true;
        ProbePtr p = host(UBCollabRelayChannel::newSessionCode(), relay.url());
        CHECK(p->channel.error() == UBCollabChannel::ProtocolError);
        note(p->channel.errorString());
    }
}

static void testLimits()
{
    section("channel: limits");

    UBCollabTestRelay relay;
    relay.listen();
    const QString code = UBCollabRelayChannel::newSessionCode();
    ProbePtr a = host(code, relay.url());

    UBCollabRelayTuning saved = UBCollabRelayTuning::current();
    UBCollabRelayTuning::current().maxIncoming = 1024 * 1024;
    ProbePtr b = guest(code, relay.url());
    UBCollabRelayTuning::current().maxIncoming = saved.maxIncoming;
    UBCollabRelayTuning::current().maxMessageSize = 2 * 1024 * 1024;
    ProbePtr c = guest(code, relay.url());
    UBCollabRelayTuning::current() = saved;
    CHECK(knowEachOther({ a.get(), b.get(), c.get() }));

    // more partly received data than the receiver allows
    a->channel.send(randomData(1536 * 1024), b->id());
    CHECK(waitFor([&]() { return b->isClosed(); }) && b->channel.error() == UBCollabChannel::ProtocolError);
    note(b->channel.errorString());

    // a message larger than the receiver allows
    a->channel.send(randomData(3 * 1024 * 1024), c->id());
    CHECK(waitFor([&]() { return c->isClosed(); }) && c->channel.error() == UBCollabChannel::ProtocolError);
    note(c->channel.errorString());

    // the sender refuses what is larger than its own limit
    UBCollabRelayTuning::current().maxMessageSize = 1024 * 1024;
    ProbePtr d = host(UBCollabRelayChannel::newSessionCode(), relay.url());
    UBCollabRelayTuning::current() = saved;
    d->channel.send(randomData(1024 * 1024 + 1));
    CHECK(d->channel.bytesToSend() == 0);

    // not in a session: nothing is sent
    Probe idle;
    idle.channel.send("nothing");
    CHECK(idle.channel.bytesToSend() == 0 && idle.channel.state() == UBCollabChannel::Closed && idle.states.isEmpty());

    section("channel: the relay does not come back");
    ProbePtr e = guest(code, relay.url());
    CHECK(e->isOpen());
    relay.stopListening();
    relay.dropPeer(e->id());
    QElapsedTimer timer;
    timer.start();
    CHECK(waitFor([&]() { return e->isClosed(); }, UBCollabRelayTuning::current().reconnectWindowMs + 10000));
    CHECK(e->channel.error() == UBCollabChannel::ConnectionLost);
    note(QStringLiteral("%1 after %2 ms").arg(e->channel.errorString()).arg(timer.elapsed()));
    CHECK(a->isOpen());
}

// run by hand around a restart of the relay: the session must survive it
static void testRelayRestart()
{
    section("channel: the relay restarts during a session");

    UBCollabRelayTuning& tuning = UBCollabRelayTuning::current();
    tuning.reconnectWindowMs = 60000;
    tuning.maxRetryDelayMs = 1000;

    const QString code = UBCollabRelayChannel::newSessionCode();
    ProbePtr a = host(code);
    ProbePtr b = guest(code);
    CHECK(knowEachOther({ a.get(), b.get() }));
    const PeerId aId = a->id();
    const PeerId bId = b->id();

    std::printf("   restart the relay now\n");
    std::fflush(stdout);
    CHECK(waitFor([&]() { return a->channel.state() == UBCollabChannel::Connecting; }, 60000));
    CHECK(waitFor([&]() { return a->resumed == 1 && b->resumed == 1; }, 90000));
    CHECK(a->id() == aId && b->id() == bId && a->left.isEmpty() && b->left.isEmpty());
    a->channel.send("after the restart");
    b->channel.send("me too");
    CHECK(waitFor([&]() { return b->from(aId).contains("after the restart") && a->from(bId).contains("me too"); }));
    a->channel.close();
    CHECK(waitFor([&]() { return b->isClosed(); }));
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    const QStringList arguments = app.arguments();
    bool restart = false;
    for (int i = 1; i < arguments.size(); ++i)
    {
        if (arguments.at(i) == QLatin1String("--relay") && i + 1 < arguments.size())
        {
            sEnv.relay = QUrl(arguments.at(++i));
            sEnv.external = true;
        }
        else if (arguments.at(i) == QLatin1String("--host-key") && i + 1 < arguments.size())
        {
            sEnv.hostKey = arguments.at(++i);
        }
        else if (arguments.at(i) == QLatin1String("--restart"))
        {
            restart = true;
        }
        else if (arguments.at(i) == QLatin1String("--grace") && i + 1 < arguments.size())
        {
            sEnv.graceMs = arguments.at(++i).toInt() * 1000;
        }
        else
        {
            std::printf("usage: collab-test [--relay <address> [--host-key <key>] [--grace <seconds>] [--restart]]\n");
            return 2;
        }
    }

    // short times, so a dropped line is noticed and taken up again quickly
    UBCollabRelayTuning& tuning = UBCollabRelayTuning::current();
    tuning.handshakeTimeoutMs = 5000;
    tuning.keepAliveMs = 500;
    tuning.silenceLimitMs = 2500;
    tuning.reconnectWindowMs = 4000;
    tuning.firstRetryDelayMs = 50;
    tuning.maxRetryDelayMs = 500;
    tuning.answerTimeoutMs = 5000;

    if (restart && sEnv.external)
    {
        testRelayRestart();
        std::printf("== %d checks, %d failed\n", sChecks, sFailures);
        return sFailures == 0 ? 0 : 1;
    }

    testCodes();
    testInvitations();
    testCrypto();
    testWebSocket();
    testTls();

    UBCollabTestRelay testRelay;
    if (!sEnv.external)
    {
        if (!testRelay.listen())
        {
            std::printf("cannot listen on the loopback address\n");
            return 1;
        }
        testRelay.options.graceMs = sEnv.graceMs;
        sEnv.testRelay = &testRelay;
        sEnv.relay = testRelay.url();
    }
    std::printf("== channel tests against %s (%s)\n", qPrintable(sEnv.relay.toString()),
                sEnv.external ? "external relay" : "built-in test relay");

    testChannelBasics();
    testChannelRefusals();
    testHostKey();
    testReconnect();
    testKeepAlive();
    if (!sEnv.external)
    {
        testWrongCode();
        testTampering();
        testLimits();
    }

    std::printf("== %d checks, %d failed\n", sChecks, sFailures);
    return sFailures == 0 ? 0 : 1;
}
