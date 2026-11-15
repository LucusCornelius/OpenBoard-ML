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

#ifndef UBCOLLABWEBSOCKET_H_
#define UBCOLLABWEBSOCKET_H_

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QObject>
#include <QPair>
#include <QPointer>
#include <QSslConfiguration>
#include <QUrl>

class QTcpSocket;
class QTimer;

/**
 * A small, strict WebSocket client (RFC 6455) on QTcpSocket and QSslSocket,
 * so that the program does not need the Qt WebSockets module.
 *
 * It speaks wss:// with certificate verification as Qt does by default, and
 * plain ws:// only to an address on this computer (for tests). No extensions
 * and no subprotocols; outgoing messages are sent as one frame each. Incoming
 * frames are checked strictly: every length is checked before anything is
 * buffered, a message larger than maxMessageSize() ends the connection, and so
 * does anything the RFC does not allow a server to send.
 */
class UBCollabWebSocket : public QObject
{
    Q_OBJECT

    public:
        enum State
        {
            Unconnected,
            Connecting,     // TCP, TLS and the HTTP upgrade
            Open,
            Closing         // a close frame was sent, waiting for the answer
        };

        enum Failure
        {
            NoFailure,
            AddressRefused,     // not ws or wss, or plain ws to a host that is not this computer
            ConnectFailed,      // host not found, connection refused, no network
            TlsFailed,          // TLS not available, handshake failed or the certificate was not accepted
            Timeout,            // the connection or the handshake took too long
            HandshakeRejected,  // the server answered the upgrade with another HTTP status: see rejectStatus()
            HandshakeInvalid,   // the answer to the upgrade is not a valid WebSocket answer
            ProtocolViolation,  // the server broke RFC 6455
            MessageTooBig,      // the server sent a message larger than maxMessageSize()
            Closed,             // closed with a close handshake: see closeCode()
            Dropped             // the connection ended without a close handshake
        };

        typedef QList<QPair<QByteArray, QByteArray>> Headers;

        explicit UBCollabWebSocket(QObject* parent = nullptr);

        /** a connection that is still open is closed with code 1001 (going away), without waiting for the answer */
        ~UBCollabWebSocket() override;

        void setMaxMessageSize(qint64 bytes);
        qint64 maxMessageSize() const { return mMaxMessageSize; }

        /** time for TCP, TLS and the HTTP upgrade together */
        void setHandshakeTimeout(int milliseconds);

        /** for wss; the default is Qt's default configuration (system certificates, verification on) */
        void setSslConfiguration(const QSslConfiguration& configuration);

        /** connects; \a headers are added to the HTTP upgrade request */
        void open(const QUrl& url, const Headers& headers = Headers());

        /** false when the state is not Open */
        bool sendBinary(const QByteArray& message);
        bool sendText(const QByteArray& utf8);
        bool ping(const QByteArray& payload = QByteArray());

        /** starts the close handshake; disconnected() follows when the server answered, or after a timeout */
        void close(quint16 code = 1000, const QByteArray& reason = QByteArray());

        /** drops the connection at once, without close handshake; disconnected() follows */
        void abort();

        State state() const { return mState; }
        Failure failure() const { return mFailure; }
        QString errorString() const { return mErrorString; }

        /** the code of the close frame of the server, or 1005 when it sent none */
        quint16 closeCode() const { return mCloseCode; }
        QString closeReason() const { return mCloseReason; }

        /** when the handshake was rejected: the HTTP status, a header of the answer (name in lower case), and the start of its body */
        int rejectStatus() const { return mRejectStatus; }
        QByteArray rejectHeader(const QByteArray& name) const { return mRejectHeaders.value(name); }
        QByteArray rejectBody() const { return mRejectBody; }

        /** bytes given to send...() that have not been handed to the network yet */
        qint64 bytesToWrite() const;

        /** true for the loopback addresses and "localhost" */
        static bool isLoopbackHost(const QString& host);

        /** true for a valid, strict UTF-8 sequence */
        static bool isValidUtf8(QByteArrayView bytes);

    signals:
        void connected();
        void textMessageReceived(const QByteArray& utf8);
        void binaryMessageReceived(const QByteArray& message);
        void pongReceived(const QByteArray& payload);
        void bytesWritten(qint64 bytes);

        /** once after open(), however the connection ended; failure() tells why */
        void disconnected();

    private:
        void onSocketConnected();
        void onReadyRead();
        void onSocketDisconnected();
        void onSocketError();
        void onTimeout();

        void sendHandshake();
        bool readHandshake();
        void readFrames();
        bool handleFrame(int opcode, bool fin, QByteArray& payload);
        bool writeFrame(int opcode, QByteArrayView payload);
        void fail(Failure failure, const QString& text, quint16 closeCode = 0);
        void finish(Failure failure, const QString& text, bool graceful);
        void releaseSocket(bool graceful);

        State mState;
        Failure mFailure;
        QString mErrorString;
        qint64 mMaxMessageSize;
        int mHandshakeTimeout;
        QSslConfiguration mSslConfiguration;
        bool mHasSslConfiguration;

        QPointer<QTcpSocket> mSocket;
        QTimer* mTimer;
        QUrl mUrl;
        Headers mExtraHeaders;
        QByteArray mKey;

        QByteArray mIn;
        bool mHandshakeDone;
        bool mInFragments;
        int mFragmentOpcode;
        QByteArray mFragments;
        bool mCloseSent;
        bool mReading;

        quint16 mCloseCode;
        QString mCloseReason;
        int mRejectStatus;
        QMap<QByteArray, QByteArray> mRejectHeaders;
        QByteArray mRejectBody;
};

#endif /* UBCOLLABWEBSOCKET_H_ */
