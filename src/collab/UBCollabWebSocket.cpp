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

#include "UBCollabWebSocket.h"

#include <QCryptographicHash>
#include <QHostAddress>
#include <QRandomGenerator>
#include <QSslSocket>
#include <QTcpSocket>
#include <QTimer>
#include <QtEndian>

#include <cstring>

namespace
{
    const char sAcceptGuid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

    // the answer to the upgrade request, headers only; a relay's answer is a few hundred bytes
    const int sMaxHandshakeSize = 16 * 1024;
    const int sMaxRejectBody = 4 * 1024;

    // Qt stops reading from the network when this much is waiting, so a server
    // that sends faster than we read is held back by TCP instead of filling memory
    const qint64 sReadBufferSize = 256 * 1024;

    const int sCloseTimeout = 3000;
    const int sReleaseTimeout = 2000;

    enum Opcode
    {
        Continuation = 0x0,
        Text = 0x1,
        Binary = 0x2,
        Close = 0x8,
        Ping = 0x9,
        Pong = 0xA
    };

    bool isValidCloseCode(quint16 code)
    {
        return (code >= 1000 && code <= 1003) || (code >= 1007 && code <= 1014) || (code >= 3000 && code <= 4999);
    }

    bool hasControlCharacters(const QByteArray& text)
    {
        for (char c : text)
        {
            if (c == '\r' || c == '\n' || c == '\0')
                return true;
        }
        return false;
    }
}

UBCollabWebSocket::UBCollabWebSocket(QObject* parent)
    : QObject(parent)
    , mState(Unconnected)
    , mFailure(NoFailure)
    , mMaxMessageSize(16 * 1024 * 1024)
    , mHandshakeTimeout(15000)
    , mHasSslConfiguration(false)
    , mTimer(new QTimer(this))
    , mHandshakeDone(false)
    , mInFragments(false)
    , mFragmentOpcode(0)
    , mCloseSent(false)
    , mReading(false)
    , mCloseCode(1005)
    , mRejectStatus(0)
{
    mTimer->setSingleShot(true);
    connect(mTimer, &QTimer::timeout, this, &UBCollabWebSocket::onTimeout);
}

UBCollabWebSocket::~UBCollabWebSocket()
{
    if (mSocket)
    {
        if (mState == Open && !mCloseSent)
        {
            QByteArray payload(2, '\0');
            qToBigEndian(quint16(1001), payload.data());
            writeFrame(Close, payload);
        }
        releaseSocket(mHandshakeDone);
    }
}

void UBCollabWebSocket::setMaxMessageSize(qint64 bytes)
{
    mMaxMessageSize = qMax<qint64>(125, bytes);
}

void UBCollabWebSocket::setHandshakeTimeout(int milliseconds)
{
    mHandshakeTimeout = qMax(1, milliseconds);
}

void UBCollabWebSocket::setSslConfiguration(const QSslConfiguration& configuration)
{
    mSslConfiguration = configuration;
    mHasSslConfiguration = true;
}

bool UBCollabWebSocket::isLoopbackHost(const QString& host)
{
    if (host.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0)
        return true;

    QHostAddress address;
    return address.setAddress(host) && address.isLoopback();
}

bool UBCollabWebSocket::isValidUtf8(QByteArrayView bytes)
{
    const qsizetype size = bytes.size();
    qsizetype i = 0;
    while (i < size)
    {
        const quint8 c = quint8(bytes[i]);
        if (c < 0x80)
        {
            ++i;
            continue;
        }

        int extra = 0;
        quint32 value = 0;
        quint32 minimum = 0;
        if ((c & 0xE0) == 0xC0)
        {
            extra = 1;
            value = c & 0x1F;
            minimum = 0x80;
        }
        else if ((c & 0xF0) == 0xE0)
        {
            extra = 2;
            value = c & 0x0F;
            minimum = 0x800;
        }
        else if ((c & 0xF8) == 0xF0)
        {
            extra = 3;
            value = c & 0x07;
            minimum = 0x10000;
        }
        else
        {
            return false;
        }

        if (size - i <= extra)
            return false;

        for (int k = 1; k <= extra; ++k)
        {
            const quint8 next = quint8(bytes[i + k]);
            if ((next & 0xC0) != 0x80)
                return false;
            value = (value << 6) | (next & 0x3F);
        }

        // overlong forms, UTF-16 surrogates and values beyond Unicode are not UTF-8
        if (value < minimum || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF))
            return false;

        i += extra + 1;
    }
    return true;
}

void UBCollabWebSocket::open(const QUrl& url, const Headers& headers)
{
    if (mState != Unconnected || mSocket)
        return;

    mUrl = url;
    mExtraHeaders = headers;
    mState = Connecting;
    mFailure = NoFailure;
    mErrorString.clear();
    mHandshakeDone = false;
    mCloseSent = false;
    mCloseCode = 1005;
    mCloseReason.clear();
    mRejectStatus = 0;
    mRejectHeaders.clear();
    mRejectBody.clear();
    mIn.clear();
    mInFragments = false;
    mFragments.clear();

    const QString scheme = url.scheme().toLower();
    const QString host = url.host();
    const bool secure = scheme == QLatin1String("wss");

    QString refusal;
    if (!url.isValid() || host.isEmpty() || (scheme != QLatin1String("ws") && !secure))
        refusal = QStringLiteral("%1 is not a ws:// or wss:// address").arg(url.toDisplayString());
    else if (!secure && !isLoopbackHost(host))
        refusal = QStringLiteral("unencrypted ws:// is only allowed to this computer, not to %1").arg(host);
    else if (!url.userInfo().isEmpty())
        refusal = QStringLiteral("addresses with a user name are not supported");
    else if (secure && !QSslSocket::supportsSsl())
        refusal = QStringLiteral("TLS is not available (%1)").arg(QSslSocket::activeBackend());

    if (!refusal.isEmpty())
    {
        // always asynchronous, so that the caller sees the same order of signals in every case
        const Failure failure = refusal.startsWith(QLatin1String("TLS")) ? TlsFailed : AddressRefused;
        QTimer::singleShot(0, this, [this, failure, refusal]()
        {
            finish(failure, refusal, false);
        });
        return;
    }

    QTcpSocket* socket = nullptr;
    if (secure)
    {
        QSslSocket* ssl = new QSslSocket();
        if (mHasSslConfiguration)
            ssl->setSslConfiguration(mSslConfiguration);
        connect(ssl, &QSslSocket::encrypted, this, &UBCollabWebSocket::onSocketConnected);
        socket = ssl;
    }
    else
    {
        socket = new QTcpSocket();
        connect(socket, &QTcpSocket::connected, this, &UBCollabWebSocket::onSocketConnected);
    }
    mSocket = socket;

    socket->setReadBufferSize(sReadBufferSize);
    connect(socket, &QTcpSocket::readyRead, this, &UBCollabWebSocket::onReadyRead);
    connect(socket, &QTcpSocket::disconnected, this, &UBCollabWebSocket::onSocketDisconnected);
    connect(socket, &QTcpSocket::errorOccurred, this, &UBCollabWebSocket::onSocketError);
    connect(socket, &QTcpSocket::bytesWritten, this, &UBCollabWebSocket::bytesWritten);

    mTimer->start(mHandshakeTimeout);

    const quint16 port = quint16(url.port(secure ? 443 : 80));
    if (secure)
        static_cast<QSslSocket*>(socket)->connectToHostEncrypted(host, port);
    else
        socket->connectToHost(host, port);
}

void UBCollabWebSocket::onSocketConnected()
{
    if (!mSocket || mState != Connecting)
        return;

    // small messages (a stroke) should leave at once, not wait for more
    mSocket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    mSocket->setSocketOption(QAbstractSocket::KeepAliveOption, 1);
    sendHandshake();
}

void UBCollabWebSocket::sendHandshake()
{
    QByteArray nonce(16, Qt::Uninitialized);
    QRandomGenerator::system()->generate(reinterpret_cast<quint32*>(nonce.data()),
                                         reinterpret_cast<quint32*>(nonce.data() + nonce.size()));
    mKey = nonce.toBase64();

    QByteArray target = mUrl.path(QUrl::FullyEncoded).toLatin1();
    if (target.isEmpty())
        target = "/";
    if (mUrl.hasQuery())
        target += '?' + mUrl.query(QUrl::FullyEncoded).toLatin1();

    QByteArray host = mUrl.host(QUrl::FullyEncoded).toLatin1();
    if (host.contains(':'))
        host = '[' + host + ']';
    const bool secure = mUrl.scheme().toLower() == QLatin1String("wss");
    const int port = mUrl.port();
    if (port > 0 && port != (secure ? 443 : 80))
        host += ':' + QByteArray::number(port);

    QByteArray request;
    request += "GET " + target + " HTTP/1.1\r\n";
    request += "Host: " + host + "\r\n";
    request += "Upgrade: websocket\r\n";
    request += "Connection: Upgrade\r\n";
    request += "Sec-WebSocket-Key: " + mKey + "\r\n";
    request += "Sec-WebSocket-Version: 13\r\n";
    request += "User-Agent: OpenBoard-ML\r\n";
    for (const auto& header : mExtraHeaders)
    {
        // a header that could end the request early is a programming error; it is left out rather than sent
        if (header.first.isEmpty() || hasControlCharacters(header.first) || header.first.contains(':')
            || hasControlCharacters(header.second))
        {
            qWarning("UBCollabWebSocket: header %s left out", header.first.constData());
            continue;
        }
        request += header.first + ": " + header.second + "\r\n";
    }
    request += "\r\n";
    mSocket->write(request);
}

void UBCollabWebSocket::onReadyRead()
{
    // a slot that runs a nested event loop must not make us parse the same buffer twice
    if (mReading)
        return;

    QPointer<UBCollabWebSocket> self(this);
    mReading = true;
    while (mSocket && mSocket->bytesAvailable() > 0)
    {
        mIn.append(mSocket->read(sReadBufferSize));

        if (!mHandshakeDone)
        {
            const bool done = readHandshake();
            if (!self)
                return;
            if (!done)
                continue;
        }
        if (mState == Open || mState == Closing)
        {
            readFrames();
            if (!self)
                return;
        }
    }
    mReading = false;
}

bool UBCollabWebSocket::readHandshake()
{
    const qsizetype end = mIn.indexOf("\r\n\r\n");
    if (end < 0)
    {
        if (mIn.size() > sMaxHandshakeSize)
            fail(HandshakeInvalid, QStringLiteral("the answer of the server has no end of headers"));
        return false;
    }
    if (end > sMaxHandshakeSize)
    {
        fail(HandshakeInvalid, QStringLiteral("the headers of the answer of the server are too long"));
        return false;
    }

    // every line, the last one too, ends with CR LF; the split leaves an empty piece after the last
    QList<QByteArray> lines = mIn.left(end + 2).split('\n');
    lines.removeLast();
    QByteArray statusLine = lines.value(0);
    if (!statusLine.endsWith('\r'))
    {
        fail(HandshakeInvalid, QStringLiteral("the server does not answer in HTTP"));
        return false;
    }
    statusLine.chop(1);

    const QList<QByteArray> status = statusLine.split(' ');
    bool statusOk = false;
    const int code = status.value(1).toInt(&statusOk);
    if (status.size() < 2 || !status.value(0).startsWith("HTTP/1.") || !statusOk || status.value(1).size() != 3)
    {
        fail(HandshakeInvalid, QStringLiteral("the server does not answer in HTTP"));
        return false;
    }

    QMap<QByteArray, QByteArray> headers;
    for (int i = 1; i < lines.size(); ++i)
    {
        QByteArray line = lines.at(i);
        if (!line.endsWith('\r'))
        {
            fail(HandshakeInvalid, QStringLiteral("malformed header line in the answer of the server"));
            return false;
        }
        line.chop(1);
        const qsizetype colon = line.indexOf(':');
        if (colon <= 0 || line.startsWith(' ') || line.startsWith('\t'))
        {
            fail(HandshakeInvalid, QStringLiteral("malformed header line in the answer of the server"));
            return false;
        }
        const QByteArray name = line.left(colon).trimmed().toLower();
        const QByteArray value = line.mid(colon + 1).trimmed();
        headers[name] = headers.contains(name) ? headers[name] + ", " + value : value;
    }

    if (code != 101)
    {
        mRejectStatus = code;
        mRejectHeaders = headers;
        mRejectBody = mIn.mid(end + 4, sMaxRejectBody);
        fail(HandshakeRejected, QStringLiteral("the server answered HTTP %1 instead of opening a WebSocket").arg(code));
        return false;
    }

    const QByteArray expected = QCryptographicHash::hash(mKey + sAcceptGuid, QCryptographicHash::Sha1).toBase64();

    bool connectionUpgrade = false;
    for (const QByteArray& token : headers.value("connection").split(','))
    {
        if (token.trimmed().toLower() == "upgrade")
            connectionUpgrade = true;
    }

    QString problem;
    if (headers.value("upgrade").toLower() != "websocket")
        problem = QStringLiteral("no Upgrade: websocket");
    else if (!connectionUpgrade)
        problem = QStringLiteral("no Connection: Upgrade");
    else if (headers.value("sec-websocket-accept") != expected)
        problem = QStringLiteral("wrong Sec-WebSocket-Accept");
    else if (headers.contains("sec-websocket-extensions"))
        problem = QStringLiteral("an extension that was not asked for");
    else if (headers.contains("sec-websocket-protocol"))
        problem = QStringLiteral("a subprotocol that was not asked for");

    if (!problem.isEmpty())
    {
        fail(HandshakeInvalid, QStringLiteral("invalid WebSocket handshake: %1").arg(problem));
        return false;
    }

    mIn.remove(0, end + 4);
    mHandshakeDone = true;
    mState = Open;
    mTimer->stop();

    QPointer<UBCollabWebSocket> self(this);
    emit connected();
    return self && mState == Open;
}

void UBCollabWebSocket::readFrames()
{
    QPointer<UBCollabWebSocket> self(this);
    qsizetype pos = 0;

    for (;;)
    {
        const qsizetype available = mIn.size() - pos;
        if (available < 2)
            break;

        const uchar* data = reinterpret_cast<const uchar*>(mIn.constData()) + pos;
        const bool fin = data[0] & 0x80;
        const int opcode = data[0] & 0x0F;
        const int length7 = data[1] & 0x7F;

        if (data[0] & 0x70)
        {
            fail(ProtocolViolation, QStringLiteral("frame with reserved bits set (no extension was agreed)"), 1002);
            return;
        }
        if (data[1] & 0x80)
        {
            fail(ProtocolViolation, QStringLiteral("masked frame from the server"), 1002);
            return;
        }

        const int headerSize = length7 == 127 ? 10 : (length7 == 126 ? 4 : 2);
        if (available < headerSize)
            break;

        quint64 length = quint64(length7);
        if (length7 == 126)
        {
            length = qFromBigEndian<quint16>(data + 2);
            if (length < 126)
            {
                fail(ProtocolViolation, QStringLiteral("frame length not in its shortest form"), 1002);
                return;
            }
        }
        else if (length7 == 127)
        {
            length = qFromBigEndian<quint64>(data + 2);
            if (length >> 63)
            {
                fail(ProtocolViolation, QStringLiteral("frame length with the highest bit set"), 1002);
                return;
            }
            if (length <= 0xFFFF)
            {
                fail(ProtocolViolation, QStringLiteral("frame length not in its shortest form"), 1002);
                return;
            }
        }

        if (opcode & 0x8)
        {
            if (opcode != Close && opcode != Ping && opcode != Pong)
            {
                fail(ProtocolViolation, QStringLiteral("unknown control frame %1").arg(opcode), 1002);
                return;
            }
            if (!fin || length > 125)
            {
                fail(ProtocolViolation, QStringLiteral("fragmented or oversized control frame"), 1002);
                return;
            }
        }
        else
        {
            if (opcode != Continuation && opcode != Text && opcode != Binary)
            {
                fail(ProtocolViolation, QStringLiteral("unknown data frame %1").arg(opcode), 1002);
                return;
            }
            if (opcode == Continuation && !mInFragments)
            {
                fail(ProtocolViolation, QStringLiteral("continuation frame without a message to continue"), 1002);
                return;
            }
            if (opcode != Continuation && mInFragments)
            {
                fail(ProtocolViolation, QStringLiteral("new message before the fragmented one ended"), 1002);
                return;
            }

            // checked before the payload is waited for, so a large length never makes us buffer
            const quint64 before = opcode == Continuation ? quint64(mFragments.size()) : 0;
            if (length > quint64(mMaxMessageSize) || before + length > quint64(mMaxMessageSize))
            {
                fail(MessageTooBig, QStringLiteral("the server sent a message larger than %1 bytes").arg(mMaxMessageSize), 1009);
                return;
            }
        }

        if (quint64(available - headerSize) < length)
            break;

        QByteArray payload = mIn.mid(pos + headerSize, qsizetype(length));
        pos += headerSize + qsizetype(length);

        const bool goOn = handleFrame(opcode, fin, payload);
        if (!self)
            return;
        if (!goOn || (mState != Open && mState != Closing))
            return;
    }

    if (pos > 0)
        mIn.remove(0, pos);
}

bool UBCollabWebSocket::handleFrame(int opcode, bool fin, QByteArray& payload)
{
    int messageOpcode = 0;
    QByteArray message;

    switch (opcode)
    {
        case Text:
        case Binary:
            if (fin)
            {
                messageOpcode = opcode;
                message = payload;
            }
            else
            {
                mInFragments = true;
                mFragmentOpcode = opcode;
                mFragments = payload;
            }
            break;

        case Continuation:
            mFragments.append(payload);
            if (fin)
            {
                messageOpcode = mFragmentOpcode;
                message = mFragments;
                mFragments.clear();
                mInFragments = false;
            }
            break;

        case Ping:
            if (mState == Open)
                writeFrame(Pong, payload);
            return true;

        case Pong:
            emit pongReceived(payload);
            return true;

        case Close:
        {
            if (payload.size() == 1)
            {
                fail(ProtocolViolation, QStringLiteral("close frame with a one-byte payload"), 1002);
                return false;
            }
            quint16 code = 1005;
            QByteArray reason;
            if (payload.size() >= 2)
            {
                code = qFromBigEndian<quint16>(payload.constData());
                reason = payload.mid(2);
                if (!isValidCloseCode(code) || !isValidUtf8(reason))
                {
                    fail(ProtocolViolation, QStringLiteral("invalid close frame"), 1002);
                    return false;
                }
            }
            mCloseCode = code;
            mCloseReason = QString::fromUtf8(reason);

            if (!mCloseSent)
            {
                QByteArray echo;
                if (code != 1005)
                {
                    echo.resize(2);
                    qToBigEndian(code, echo.data());
                }
                writeFrame(Close, echo);
                mCloseSent = true;
            }
            finish(Closed, mCloseReason.isEmpty()
                ? QStringLiteral("closed by the server (%1)").arg(code)
                : QStringLiteral("closed by the server (%1, %2)").arg(code).arg(mCloseReason), true);
            return false;
        }
    }

    if (messageOpcode == 0 || mState != Open)
        return true;

    if (messageOpcode == Text)
    {
        if (!isValidUtf8(message))
        {
            fail(ProtocolViolation, QStringLiteral("text message that is not UTF-8"), 1007);
            return false;
        }
        emit textMessageReceived(message);
    }
    else
    {
        emit binaryMessageReceived(message);
    }
    return true;
}

bool UBCollabWebSocket::writeFrame(int opcode, QByteArrayView payload)
{
    if (!mSocket)
        return false;

    const qsizetype length = payload.size();
    const int lengthSize = length > 0xFFFF ? 8 : (length >= 126 ? 2 : 0);
    const qsizetype headerSize = 2 + lengthSize + 4;

    QByteArray frame(headerSize + length, Qt::Uninitialized);
    uchar* out = reinterpret_cast<uchar*>(frame.data());
    out[0] = uchar(0x80 | opcode);
    if (lengthSize == 0)
    {
        out[1] = uchar(0x80 | length);
    }
    else if (lengthSize == 2)
    {
        out[1] = 0x80 | 126;
        qToBigEndian(quint16(length), out + 2);
    }
    else
    {
        out[1] = 0x80 | 127;
        qToBigEndian(quint64(length), out + 2);
    }

    // a client masks every frame with a key the server cannot predict
    const quint32 maskValue = QRandomGenerator::system()->generate();
    uchar mask[4];
    std::memcpy(mask, &maskValue, 4);
    std::memcpy(out + 2 + lengthSize, mask, 4);

    const uchar* in = reinterpret_cast<const uchar*>(payload.data());
    uchar* body = out + headerSize;
    for (qsizetype i = 0; i < length; ++i)
        body[i] = in[i] ^ mask[i & 3];

    return mSocket->write(frame) == frame.size();
}

bool UBCollabWebSocket::sendBinary(const QByteArray& message)
{
    return mState == Open && writeFrame(Binary, message);
}

bool UBCollabWebSocket::sendText(const QByteArray& utf8)
{
    return mState == Open && isValidUtf8(utf8) && writeFrame(Text, utf8);
}

bool UBCollabWebSocket::ping(const QByteArray& payload)
{
    return mState == Open && payload.size() <= 125 && writeFrame(Ping, payload);
}

void UBCollabWebSocket::close(quint16 code, const QByteArray& reason)
{
    if (mState == Connecting)
    {
        abort();
        return;
    }
    if (mState != Open)
        return;

    QByteArray payload(2, '\0');
    qToBigEndian(isValidCloseCode(code) ? code : quint16(1000), payload.data());
    QByteArray shortReason = reason.left(123);
    while (!shortReason.isEmpty() && !isValidUtf8(shortReason))
        shortReason.chop(1);
    payload += shortReason;

    writeFrame(Close, payload);
    mCloseSent = true;
    mState = Closing;
    mTimer->start(sCloseTimeout);
}

void UBCollabWebSocket::abort()
{
    finish(Dropped, QStringLiteral("connection dropped by this side"), false);
}

qint64 UBCollabWebSocket::bytesToWrite() const
{
    if (!mSocket)
        return 0;

    qint64 bytes = mSocket->bytesToWrite();
    if (const QSslSocket* ssl = qobject_cast<const QSslSocket*>(mSocket.data()))
        bytes += ssl->encryptedBytesToWrite();
    return bytes;
}

void UBCollabWebSocket::onSocketDisconnected()
{
    if (mState == Unconnected)
        return;

    if (mState == Closing)
        finish(Closed, QStringLiteral("closed without an answer from the server"), false);
    else if (mHandshakeDone)
        finish(Dropped, QStringLiteral("the connection ended without a close handshake"), false);
    else
        finish(HandshakeInvalid, QStringLiteral("the server closed the connection during the handshake"), false);
}

void UBCollabWebSocket::onSocketError()
{
    if (mState == Unconnected || !mSocket)
        return;

    const QAbstractSocket::SocketError error = mSocket->error();
    QString text = mSocket->errorString();

    // the disconnected() that follows tells it better
    if (error == QAbstractSocket::RemoteHostClosedError && mSocket->state() != QAbstractSocket::UnconnectedState)
        return;

    if (QSslSocket* ssl = qobject_cast<QSslSocket*>(mSocket.data()))
    {
        const QList<QSslError> errors = ssl->sslHandshakeErrors();
        if (!errors.isEmpty() && !text.contains(errors.first().errorString()))
            text += QStringLiteral(" (%1)").arg(errors.first().errorString());
    }

    switch (error)
    {
        case QAbstractSocket::SslHandshakeFailedError:
        case QAbstractSocket::SslInternalError:
        case QAbstractSocket::SslInvalidUserDataError:
            finish(TlsFailed, text, false);
            break;

        default:
            if (mHandshakeDone)
                finish(Dropped, text, false);
            else
                finish(mSocket->state() == QAbstractSocket::ConnectedState ? HandshakeInvalid : ConnectFailed, text, false);
            break;
    }
}

void UBCollabWebSocket::onTimeout()
{
    if (mState == Connecting)
        finish(Timeout, QStringLiteral("no WebSocket connection within %1 s").arg(mHandshakeTimeout / 1000.0), false);
    else if (mState == Closing)
        finish(Closed, QStringLiteral("the server did not answer the close in time"), false);
}

void UBCollabWebSocket::fail(Failure failure, const QString& text, quint16 closeCode)
{
    bool graceful = false;
    if (closeCode != 0 && mState == Open && !mCloseSent)
    {
        QByteArray payload(2, '\0');
        qToBigEndian(closeCode, payload.data());
        writeFrame(Close, payload);
        mCloseSent = true;
        graceful = true;
    }
    finish(failure, text, graceful);
}

void UBCollabWebSocket::finish(Failure failure, const QString& text, bool graceful)
{
    if (mState == Unconnected)
        return;

    mState = Unconnected;
    mFailure = failure;
    mErrorString = text;
    mTimer->stop();
    releaseSocket(graceful);
    mIn.clear();
    mFragments.clear();
    mInFragments = false;

    emit disconnected();
}

void UBCollabWebSocket::releaseSocket(bool graceful)
{
    QTcpSocket* socket = mSocket.data();
    mSocket = nullptr;
    if (!socket)
        return;

    disconnect(socket, nullptr, this, nullptr);

    if (graceful && socket->state() == QAbstractSocket::ConnectedState)
    {
        // let what was written (a close frame) leave, then close; never wait long for it
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        QTimer::singleShot(sReleaseTimeout, socket, [socket]()
        {
            socket->abort();
            socket->deleteLater();
        });
        socket->disconnectFromHost();
    }
    else
    {
        socket->abort();
        socket->deleteLater();
    }
}
