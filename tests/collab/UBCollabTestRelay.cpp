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

#include "UBCollabTestRelay.h"

#include <QCryptographicHash>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSslSocket>
#include <QTcpSocket>
#include <QTimer>
#include <QtEndian>

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

namespace
{
    QByteArray bioBytes(BIO* bio)
    {
        char* data = nullptr;
        const long length = BIO_get_mem_data(bio, &data);
        return QByteArray(data, int(length));
    }

    bool addExtension(X509* certificate, int nid, const char* value)
    {
        X509V3_CTX context;
        X509V3_set_ctx_nodb(&context);
        X509V3_set_ctx(&context, certificate, certificate, nullptr, nullptr, 0);
        X509_EXTENSION* extension = X509V3_EXT_conf_nid(nullptr, &context, nid, value);
        if (!extension)
            return false;
        const bool ok = X509_add_ext(certificate, extension, -1) == 1;
        X509_EXTENSION_free(extension);
        return ok;
    }
}

bool testCertificate(QSslCertificate& certificate, QSslKey& key, const QByteArray& dnsName)
{
    EVP_PKEY* pkey = EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "P-256");
    X509* x509 = X509_new();
    bool ok = pkey && x509
        && X509_set_version(x509, 2) == 1
        && ASN1_INTEGER_set(X509_get_serialNumber(x509), long(QRandomGenerator::global()->bounded(1, 1 << 30))) == 1
        && X509_gmtime_adj(X509_getm_notBefore(x509), -3600)
        && X509_gmtime_adj(X509_getm_notAfter(x509), 24 * 3600)
        && X509_set_pubkey(x509, pkey) == 1;

    if (ok)
    {
        X509_NAME* name = X509_get_subject_name(x509);
        ok = X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char*>("collab-test"), -1, -1, 0) == 1
            && X509_set_issuer_name(x509, name) == 1;
    }
    const QByteArray alternative = dnsName.isEmpty() ? QByteArray("IP:127.0.0.1") : "DNS:" + dnsName;
    ok = ok && addExtension(x509, NID_subject_alt_name, alternative.constData())
        && addExtension(x509, NID_basic_constraints, "critical,CA:TRUE")
        && addExtension(x509, NID_key_usage, "critical,digitalSignature,keyCertSign")
        && addExtension(x509, NID_ext_key_usage, "serverAuth")
        && X509_sign(x509, pkey, EVP_sha256()) > 0;

    if (ok)
    {
        BIO* certificateBio = BIO_new(BIO_s_mem());
        BIO* keyBio = BIO_new(BIO_s_mem());
        ok = PEM_write_bio_X509(certificateBio, x509) == 1
            && PEM_write_bio_PrivateKey(keyBio, pkey, nullptr, nullptr, 0, nullptr, nullptr) == 1;
        if (ok)
        {
            certificate = QSslCertificate(bioBytes(certificateBio), QSsl::Pem);
            key = QSslKey(bioBytes(keyBio), QSsl::Ec, QSsl::Pem);
            ok = !certificate.isNull() && !key.isNull();
        }
        BIO_free(certificateBio);
        BIO_free(keyBio);
    }

    X509_free(x509);
    EVP_PKEY_free(pkey);
    return ok;
}

void UBCollabTestServer::setCertificate(const QSslCertificate& certificate, const QSslKey& key)
{
    mCertificate = certificate;
    mKey = key;
}

void UBCollabTestServer::incomingConnection(qintptr descriptor)
{
    if (!isSecure())
    {
        QTcpServer::incomingConnection(descriptor);
        return;
    }

    QSslSocket* socket = new QSslSocket(this);
    if (!socket->setSocketDescriptor(descriptor))
    {
        delete socket;
        return;
    }
    socket->setLocalCertificate(mCertificate);
    socket->setPrivateKey(mKey);
    socket->setPeerVerifyMode(QSslSocket::VerifyNone);
    socket->startServerEncryption();
    // QTcpServer emits newConnection() itself after this
    addPendingConnection(socket);
}

QByteArray testServerFrame(int opcode, const QByteArray& payload, bool fin, int rsv, bool masked, int lengthForm)
{
    const qsizetype length = payload.size();
    int form = lengthForm;
    if (form < 0)
        form = length > 0xFFFF ? 2 : (length >= 126 ? 1 : 0);

    QByteArray frame;
    frame.append(char((fin ? 0x80 : 0) | ((rsv & 7) << 4) | (opcode & 0x0F)));
    const char maskBit = masked ? char(0x80) : char(0);
    if (form == 0)
    {
        frame.append(char(maskBit | char(length & 0x7F)));
    }
    else if (form == 1)
    {
        frame.append(char(maskBit | 126));
        char bytes[2];
        qToBigEndian(quint16(length), bytes);
        frame.append(bytes, 2);
    }
    else
    {
        frame.append(char(maskBit | 127));
        char bytes[8];
        qToBigEndian(quint64(length), bytes);
        frame.append(bytes, 8);
    }

    if (masked)
    {
        const char mask[4] = { 0x11, 0x22, 0x33, 0x44 };
        frame.append(mask, 4);
        QByteArray body = payload;
        for (qsizetype i = 0; i < body.size(); ++i)
            body[i] = char(body[i] ^ mask[i & 3]);
        frame.append(body);
    }
    else
    {
        frame.append(payload);
    }
    return frame;
}

UBCollabTestConnection::UBCollabTestConnection(QTcpSocket* socket, QObject* parent)
    : QObject(parent)
    , autoAnswer(true)
    , mSocket(socket)
    , mRequestDone(false)
    , mUpgraded(false)
    , mFrozen(false)
    , mCloseSent(false)
    , mClientFramesValid(true)
    , mCloseCode(0)
    , mGone(false)
{
    socket->setParent(this);
    connect(socket, &QTcpSocket::readyRead, this, &UBCollabTestConnection::onReadyRead);
    connect(socket, &QTcpSocket::disconnected, this, [this]()
    {
        if (!mGone)
        {
            mGone = true;
            emit gone();
        }
    });
}

QByteArray UBCollabTestConnection::path() const
{
    const QList<QByteArray> parts = mRequestLine.split(' ');
    return parts.value(1);
}

bool UBCollabTestConnection::isConnected() const
{
    return mSocket && mSocket->state() == QAbstractSocket::ConnectedState;
}

void UBCollabTestConnection::accept(const QList<QPair<QByteArray, QByteArray>>& extraHeaders, const QByteArray& acceptValue,
                                    bool withUpgrade)
{
    const QByteArray key = header("sec-websocket-key");
    const QByteArray value = acceptValue.isEmpty()
        ? QCryptographicHash::hash(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11", QCryptographicHash::Sha1).toBase64()
        : acceptValue;

    QByteArray answer = "HTTP/1.1 101 Switching Protocols\r\n";
    if (withUpgrade)
        answer += "Upgrade: websocket\r\n";
    answer += "Connection: Upgrade\r\nSec-WebSocket-Accept: " + value + "\r\n";
    for (const auto& h : extraHeaders)
        answer += h.first + ": " + h.second + "\r\n";
    answer += "\r\n";
    writeRaw(answer);
    mUpgraded = true;

    if (!mIn.isEmpty())
        readFrames();
}

void UBCollabTestConnection::reject(int status, const QList<QPair<QByteArray, QByteArray>>& headers, const QByteArray& body)
{
    QByteArray answer = "HTTP/1.1 " + QByteArray::number(status) + " Refused\r\n";
    for (const auto& h : headers)
        answer += h.first + ": " + h.second + "\r\n";
    answer += "Content-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
    writeRaw(answer);
    if (mSocket)
        mSocket->disconnectFromHost();
}

void UBCollabTestConnection::writeRaw(const QByteArray& bytes)
{
    if (mSocket && !mFrozen && mSocket->state() == QAbstractSocket::ConnectedState)
        mSocket->write(bytes);
}

void UBCollabTestConnection::sendText(const QByteArray& text)
{
    writeRaw(testServerFrame(0x1, text));
}

void UBCollabTestConnection::sendBinary(const QByteArray& message)
{
    writeRaw(testServerFrame(0x2, message));
}

void UBCollabTestConnection::sendClose(quint16 code, const QByteArray& reason)
{
    QByteArray payload(2, '\0');
    qToBigEndian(code, payload.data());
    writeRaw(testServerFrame(0x8, payload + reason));
    mCloseSent = true;
}

void UBCollabTestConnection::abort()
{
    if (mSocket)
        mSocket->abort();
    if (!mGone)
    {
        mGone = true;
        emit gone();
    }
}

void UBCollabTestConnection::setFrozen(bool frozen)
{
    mFrozen = frozen;
}

void UBCollabTestConnection::onReadyRead()
{
    if (!mSocket || mFrozen)
        return;

    mIn.append(mSocket->readAll());

    if (!mRequestDone)
    {
        const qsizetype end = mIn.indexOf("\r\n\r\n");
        if (end < 0)
            return;

        const QList<QByteArray> lines = mIn.left(end).split('\n');
        mRequestLine = lines.value(0).trimmed();
        for (int i = 1; i < lines.size(); ++i)
        {
            const QByteArray line = lines.at(i).trimmed();
            const qsizetype colon = line.indexOf(':');
            if (colon > 0)
                mHeaders[line.left(colon).trimmed().toLower()] = line.mid(colon + 1).trimmed();
        }
        mIn.remove(0, end + 4);
        mRequestDone = true;
        emit requestReceived();
        return;
    }

    if (mUpgraded)
        readFrames();
}

void UBCollabTestConnection::readFrames()
{
    QPointer<UBCollabTestConnection> self(this);
    for (;;)
    {
        if (mIn.size() < 2)
            return;

        const uchar* d = reinterpret_cast<const uchar*>(mIn.constData());
        const bool fin = d[0] & 0x80;
        const int opcode = d[0] & 0x0F;
        const bool masked = d[1] & 0x80;
        int length7 = d[1] & 0x7F;
        int header = 2 + (length7 == 126 ? 2 : (length7 == 127 ? 8 : 0)) + (masked ? 4 : 0);
        if (mIn.size() < header)
            return;

        quint64 length = quint64(length7);
        if (length7 == 126)
            length = qFromBigEndian<quint16>(d + 2);
        else if (length7 == 127)
            length = qFromBigEndian<quint64>(d + 2);

        if (!masked)
        {
            mClientFramesValid = false;
            mClientFrameProblem = QStringLiteral("unmasked client frame");
        }
        if (d[0] & 0x70)
        {
            mClientFramesValid = false;
            mClientFrameProblem = QStringLiteral("reserved bits in a client frame");
        }
        if ((length7 == 126 && length < 126) || (length7 == 127 && length <= 0xFFFF))
        {
            mClientFramesValid = false;
            mClientFrameProblem = QStringLiteral("client frame length not in its shortest form");
        }

        if (quint64(mIn.size() - header) < length)
            return;

        QByteArray payload = mIn.mid(header, qsizetype(length));
        if (masked)
        {
            const uchar* mask = d + header - 4;
            for (qsizetype i = 0; i < payload.size(); ++i)
                payload[i] = char(payload[i] ^ mask[i & 3]);
        }
        mIn.remove(0, header + qsizetype(length));

        emit frameReceived(opcode, payload, fin, header);
        if (!self)
            return;

        if (opcode == 0x9 && autoAnswer)
        {
            writeRaw(testServerFrame(0xA, payload));
        }
        else if (opcode == 0x8)
        {
            mCloseCode = payload.size() >= 2 ? qFromBigEndian<quint16>(payload.constData()) : 1005;
            emit closeReceived(mCloseCode);
            if (!self)
                return;
            if (autoAnswer)
            {
                if (!mCloseSent)
                    writeRaw(testServerFrame(0x8, payload.left(2)));
                mCloseSent = true;
                if (mSocket)
                    mSocket->disconnectFromHost();
            }
            return;
        }
        else if (opcode == 0x1 && fin)
        {
            emit textReceived(payload);
        }
        else if (opcode == 0x2 && fin)
        {
            emit binaryReceived(payload);
        }
        if (!self)
            return;
    }
}

UBCollabTestRelay::UBCollabTestRelay(QObject* parent)
    : QObject(parent)
    , mServer(new UBCollabTestServer(this))
    , mForwarded(0)
{
    connect(mServer, &QTcpServer::newConnection, this, &UBCollabTestRelay::onNewConnection);
}

UBCollabTestRelay::~UBCollabTestRelay()
{
    for (Room& room : mRooms)
    {
        for (Member& member : room.members)
            delete member.graceTimer;
    }
}

bool UBCollabTestRelay::listen()
{
    return mServer->listen(QHostAddress::LocalHost, 0);
}

void UBCollabTestRelay::setCertificate(const QSslCertificate& certificate, const QSslKey& key)
{
    mServer->setCertificate(certificate, key);
}

void UBCollabTestRelay::stopListening()
{
    mServer->close();
}

QUrl UBCollabTestRelay::url() const
{
    return QUrl(QStringLiteral("%1://127.0.0.1:%2/").arg(mServer->isSecure() ? QStringLiteral("https") : QStringLiteral("http"))
                .arg(mServer->serverPort()));
}

void UBCollabTestRelay::onNewConnection()
{
    while (QTcpSocket* socket = mServer->nextPendingConnection())
    {
        UBCollabTestConnection* connection = new UBCollabTestConnection(socket, this);
        connect(connection, &UBCollabTestConnection::requestReceived, this, [this, connection]() { onRequest(connection); });
        connect(connection, &UBCollabTestConnection::binaryReceived, this, [this, connection](const QByteArray& m) { onBinary(connection, m); });
        connect(connection, &UBCollabTestConnection::textReceived, this, [this, connection](const QByteArray& t) { onText(connection, t); });
        connect(connection, &UBCollabTestConnection::gone, this, [this, connection]() { onGone(connection); });
    }
}

void UBCollabTestRelay::refuse(UBCollabTestConnection* connection, int status, const QByteArray& reason)
{
    connection->reject(status, { qMakePair(QByteArray("OBML-Relay"), QByteArray("1")),
                                 qMakePair(QByteArray("OBML-Reason"), reason) }, reason + "\n");
}

void UBCollabTestRelay::broadcast(Room& room, const QByteArray& text, quint32 except)
{
    for (Member& member : room.members)
    {
        if (member.peer != except && member.connection)
            member.connection->sendText(text);
    }
}

UBCollabTestRelay::Member* UBCollabTestRelay::memberFor(UBCollabTestConnection* connection, QByteArray* roomId)
{
    const QByteArray id = connection->property("room").toByteArray();
    const quint32 peer = connection->property("peer").toUInt();
    auto room = mRooms.find(id);
    if (room == mRooms.end())
        return nullptr;
    auto member = room->members.find(peer);
    if (member == room->members.end() || member->connection != connection)
        return nullptr;
    if (roomId)
        *roomId = id;
    return &member.value();
}

void UBCollabTestRelay::onRequest(UBCollabTestConnection* connection)
{
    static const QRegularExpression roomPath(QStringLiteral("/v(\\d+)/rooms/([^/]*)$"));
    const QRegularExpressionMatch match = roomPath.match(QString::fromLatin1(connection->path()));
    if (!match.hasMatch())
    {
        connection->reject(404, {}, "Not found\n");
        return;
    }
    if (match.captured(1) != QLatin1String("1") || options.refuseVersion)
    {
        refuse(connection, 426, "version");
        return;
    }

    const QByteArray roomId = match.captured(2).toLatin1();
    const QByteArray role = connection->header("obml-role");
    const QByteArray token = connection->header("obml-resume");
    static const QRegularExpression roomPattern(QStringLiteral("^[0-9a-f]{64}$"));
    static const QRegularExpression tokenPattern(QStringLiteral("^[A-Za-z0-9_-]{43}$"));
    if (connection->header("upgrade").toLower() != "websocket" || !roomPattern.match(QString::fromLatin1(roomId)).hasMatch()
        || !tokenPattern.match(QString::fromLatin1(token)).hasMatch() || (role != "start" && role != "join" && role != "resume"))
    {
        refuse(connection, 400, "bad-request");
        return;
    }

    if (role == "start" && !options.hostKey.isEmpty() && connection->header("authorization") != "Bearer " + options.hostKey)
    {
        refuse(connection, 403, "refused");
        return;
    }

    const QByteArray id = options.mergeRooms ? QByteArray("merged") : roomId;
    const QByteArray hash = QCryptographicHash::hash(token, QCryptographicHash::Sha256).toHex();
    const bool exists = mRooms.contains(id) && !mRooms[id].members.isEmpty();

    Member record;
    bool wasAway = false;

    if (role == "resume")
    {
        const quint32 peer = connection->header("obml-peer").toUInt();
        if (!exists || !mRooms[id].members.contains(peer) || mRooms[id].members[peer].token != hash)
        {
            refuse(connection, 410, "gone");
            return;
        }
        Member& member = mRooms[id].members[peer];
        if (member.connection)
        {
            UBCollabTestConnection* old = member.connection;
            member.connection = nullptr;
            old->sendClose(4001, "replaced");
            old->abort();
        }
        else
        {
            wasAway = true;
        }
        delete member.graceTimer;
        member.graceTimer = nullptr;
        member.connection = connection;
        record = member;
    }
    else if (role == "start")
    {
        if (exists)
        {
            refuse(connection, 409, "exists");
            return;
        }
        record.peer = QRandomGenerator::global()->bounded(1u, 0xFFFFFFFFu);
        record.host = true;
        record.token = hash;
        record.connection = connection;
        mRooms[id].members.insert(record.peer, record);
    }
    else
    {
        if (!exists)
        {
            refuse(connection, 404, "not-found");
            return;
        }
        if (mRooms[id].members.size() >= options.maxPeers)
        {
            refuse(connection, 409, "full");
            return;
        }
        do
        {
            record.peer = QRandomGenerator::global()->bounded(1u, 0xFFFFFFFFu);
        }
        while (mRooms[id].members.contains(record.peer));
        record.token = hash;
        record.connection = connection;
        mRooms[id].members.insert(record.peer, record);
    }

    connection->setProperty("room", id);
    connection->setProperty("peer", record.peer);

    Room& room = mRooms[id];
    if (role == "join")
        broadcast(room, QJsonDocument(QJsonObject{ { "type", "join" }, { "peer", double(record.peer) } }).toJson(QJsonDocument::Compact), record.peer);

    QJsonArray peers;
    QJsonArray away;
    quint32 host = 0;
    for (const Member& member : room.members)
    {
        if (member.host)
            host = member.peer;
        if (member.peer == record.peer)
            continue;
        peers.append(double(member.peer));
        if (!member.connection)
            away.append(double(member.peer));
    }

    connection->accept();
    connection->sendText(QJsonDocument(QJsonObject{
        { "type", "welcome" }, { "v", 1 }, { "peer", double(record.peer) }, { "host", double(host) },
        { "peers", peers }, { "away", away }, { "resumed", role == "resume" },
        { "maxMessage", options.maxMessage }, { "maxPeers", options.maxPeers } }).toJson(QJsonDocument::Compact));

    if (wasAway)
        broadcast(room, QJsonDocument(QJsonObject{ { "type", "back" }, { "peer", double(record.peer) } }).toJson(QJsonDocument::Compact), record.peer);
}

void UBCollabTestRelay::onText(UBCollabTestConnection* connection, const QByteArray& text)
{
    QByteArray roomId;
    Member* member = memberFor(connection, &roomId);
    if (!member)
        return;

    if (text == "ping")
    {
        connection->sendText("pong");
        return;
    }
    connection->sendClose(1003, "text");
    leave(roomId, member->peer);
}

void UBCollabTestRelay::onBinary(UBCollabTestConnection* connection, QByteArray message)
{
    QByteArray roomId;
    Member* member = memberFor(connection, &roomId);
    if (!member)
        return;

    if (message.size() < 4 || message.size() > options.maxMessage)
    {
        connection->sendClose(message.size() < 4 ? 1008 : 1009, "bad message");
        leave(roomId, member->peer);
        return;
    }

    const quint32 from = member->peer;
    quint32 to = qFromBigEndian<quint32>(message.constData());
    qToBigEndian(from, message.data());

    if (filter && !filter(from, to, message))
        return;

    Room& room = mRooms[roomId];
    for (Member& target : room.members)
    {
        if (target.peer != from && target.connection && (to == 0 || target.peer == to))
            target.connection->sendBinary(message);
    }
    ++mForwarded;
}

void UBCollabTestRelay::onGone(UBCollabTestConnection* connection)
{
    connection->deleteLater();

    QByteArray roomId;
    Member* member = memberFor(connection, &roomId);
    if (!member)
        return;

    const quint16 code = connection->closeCodeReceived();
    if (code == 1000 || code == 1001)
    {
        leave(roomId, member->peer);
        return;
    }

    // the line dropped: the place is kept for a while
    const quint32 peer = member->peer;
    member->connection = nullptr;
    member->graceTimer = new QTimer();
    member->graceTimer->setSingleShot(true);
    connect(member->graceTimer, &QTimer::timeout, this, [this, roomId, peer]() { leave(roomId, peer); });
    member->graceTimer->start(options.graceMs);
    broadcast(mRooms[roomId], QJsonDocument(QJsonObject{ { "type", "away" }, { "peer", double(peer) } }).toJson(QJsonDocument::Compact), peer);
}

void UBCollabTestRelay::leave(const QByteArray& roomId, quint32 peer)
{
    auto room = mRooms.find(roomId);
    if (room == mRooms.end() || !room->members.contains(peer))
        return;

    Member member = room->members.take(peer);
    if (member.graceTimer)
        member.graceTimer->deleteLater();
    if (member.connection)
    {
        member.connection->setProperty("room", QByteArray());
        member.connection->sendClose(1000, "left");
    }

    broadcast(room.value(), QJsonDocument(QJsonObject{ { "type", "leave" }, { "peer", double(peer) } }).toJson(QJsonDocument::Compact), peer);
    if (member.host)
        endRoom(roomId);
    else if (room->members.isEmpty())
        mRooms.erase(room);
}

void UBCollabTestRelay::endRoom(const QByteArray& roomId)
{
    auto room = mRooms.find(roomId);
    if (room == mRooms.end())
        return;

    broadcast(room.value(), QJsonDocument(QJsonObject{ { "type", "end" }, { "reason", "host-left" } }).toJson(QJsonDocument::Compact), 0);
    for (Member& member : room->members)
    {
        if (member.graceTimer)
            member.graceTimer->deleteLater();
        if (member.connection)
        {
            member.connection->setProperty("room", QByteArray());
            member.connection->sendClose(4000, "the session ended");
        }
    }
    mRooms.erase(room);
}

void UBCollabTestRelay::dropPeer(quint32 peer)
{
    for (Room& room : mRooms)
    {
        if (room.members.contains(peer) && room.members[peer].connection)
        {
            room.members[peer].connection->abort();
            return;
        }
    }
}

void UBCollabTestRelay::freezePeer(quint32 peer)
{
    for (Room& room : mRooms)
    {
        if (room.members.contains(peer) && room.members[peer].connection)
        {
            room.members[peer].connection->setFrozen(true);
            return;
        }
    }
}

void UBCollabTestRelay::sendTo(quint32 peer, const QByteArray& message)
{
    for (Room& room : mRooms)
    {
        if (room.members.contains(peer) && room.members[peer].connection)
        {
            room.members[peer].connection->sendBinary(message);
            return;
        }
    }
}

void UBCollabTestRelay::sendTextTo(quint32 peer, const QByteArray& text)
{
    for (Room& room : mRooms)
    {
        if (room.members.contains(peer) && room.members[peer].connection)
        {
            room.members[peer].connection->sendText(text);
            return;
        }
    }
}
