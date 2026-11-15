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

#ifndef UBCOLLABTESTRELAY_H_
#define UBCOLLABTESTRELAY_H_

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QObject>
#include <QPair>
#include <QPointer>
#include <QSslCertificate>
#include <QSslKey>
#include <QTcpServer>
#include <QUrl>

#include <functional>

class QTcpSocket;
class QTimer;

/** a TLS server certificate for 127.0.0.1 (or for \a dnsName instead) and its key, made on the spot */
bool testCertificate(QSslCertificate& certificate, QSslKey& key, const QByteArray& dnsName = QByteArray());

/** a TCP server that speaks TLS when it has a certificate */
class UBCollabTestServer : public QTcpServer
{
    public:
        explicit UBCollabTestServer(QObject* parent = nullptr) : QTcpServer(parent) {}

        void setCertificate(const QSslCertificate& certificate, const QSslKey& key);
        bool isSecure() const { return !mCertificate.isNull(); }

    protected:
        void incomingConnection(qintptr descriptor) override;

    private:
        QSslCertificate mCertificate;
        QSslKey mKey;
};

/** builds a frame as a server sends it; for the tests of odd frames every field can be chosen */
QByteArray testServerFrame(int opcode, const QByteArray& payload, bool fin = true, int rsv = 0,
                           bool masked = false, int lengthForm = -1);

/**
 * The server end of one WebSocket connection, for the tests only: it reads the
 * HTTP upgrade request, lets the owner accept or reject it, then reads the
 * client's frames (and checks that they are masked) and writes frames.
 */
class UBCollabTestConnection : public QObject
{
    Q_OBJECT

    public:
        explicit UBCollabTestConnection(QTcpSocket* socket, QObject* parent = nullptr);

        QByteArray requestLine() const { return mRequestLine; }
        QByteArray path() const;
        QByteArray header(const QByteArray& lowerCaseName) const { return mHeaders.value(lowerCaseName); }
        bool isUpgraded() const { return mUpgraded; }
        bool isConnected() const;

        /** answers the upgrade; \a acceptValue replaces the right Sec-WebSocket-Accept when not empty */
        void accept(const QList<QPair<QByteArray, QByteArray>>& extraHeaders = {}, const QByteArray& acceptValue = QByteArray(),
                    bool withUpgrade = true);
        void reject(int status, const QList<QPair<QByteArray, QByteArray>>& headers, const QByteArray& body);

        void writeRaw(const QByteArray& bytes);
        void sendText(const QByteArray& text);
        void sendBinary(const QByteArray& message);
        void sendClose(quint16 code, const QByteArray& reason = QByteArray());
        void abort();

        /** a frozen connection reads and answers nothing, like a line that died without the sockets noticing */
        void setFrozen(bool frozen);

        /** every client frame was masked and well formed */
        bool clientFramesValid() const { return mClientFramesValid; }
        QString clientFrameProblem() const { return mClientFrameProblem; }
        quint16 closeCodeReceived() const { return mCloseCode; }

        /** answer pings and close frames by itself (on by default) */
        bool autoAnswer;

    signals:
        void requestReceived();
        void textReceived(const QByteArray& text);
        void binaryReceived(const QByteArray& message);
        void frameReceived(int opcode, const QByteArray& payload, bool fin, int headerLength);
        void closeReceived(quint16 code);
        void gone();

    private:
        void onReadyRead();
        void readFrames();

        QPointer<QTcpSocket> mSocket;
        QByteArray mIn;
        QByteArray mRequestLine;
        QMap<QByteArray, QByteArray> mHeaders;
        bool mRequestDone;
        bool mUpgraded;
        bool mFrozen;
        bool mCloseSent;
        bool mClientFramesValid;
        QString mClientFrameProblem;
        quint16 mCloseCode;
        bool mGone;
};

/**
 * A minimal relay that speaks the protocol of relay/relay.js, so that the
 * channel can be tested with nothing installed. It also has switches the real
 * relay does not have, to play a misbehaving relay.
 */
class UBCollabTestRelay : public QObject
{
    Q_OBJECT

    public:
        struct Options
        {
            QByteArray hostKey;
            int maxPeers = 8;
            int graceMs = 1500;
            int maxMessage = 1024 * 1024;
            bool mergeRooms = false;        // every room id leads to one room: participants with different codes meet
            bool refuseVersion = false;     // answer every request as from an unknown protocol version
        };

        explicit UBCollabTestRelay(QObject* parent = nullptr);
        ~UBCollabTestRelay() override;

        bool listen();
        void stopListening();

        /** before listen(): the relay is then reached with https:// */
        void setCertificate(const QSslCertificate& certificate, const QSslKey& key);
        QUrl url() const;

        Options options;

        /** sees each data message before it is forwarded, with the sender already filled in; may change it and its addressee; false drops it */
        std::function<bool(quint32 from, quint32& to, QByteArray& message)> filter;

        void dropPeer(quint32 peer);
        void freezePeer(quint32 peer);
        void sendTo(quint32 peer, const QByteArray& message);
        void sendTextTo(quint32 peer, const QByteArray& text);

        int roomCount() const { return mRooms.size(); }
        int messagesForwarded() const { return mForwarded; }

    private:
        struct Member
        {
            quint32 peer = 0;
            bool host = false;
            QByteArray token;
            QPointer<UBCollabTestConnection> connection;
            QTimer* graceTimer = nullptr;
        };

        struct Room
        {
            QMap<quint32, Member> members;
        };

        void onNewConnection();
        void onRequest(UBCollabTestConnection* connection);
        void onBinary(UBCollabTestConnection* connection, QByteArray message);
        void onText(UBCollabTestConnection* connection, const QByteArray& text);
        void onGone(UBCollabTestConnection* connection);
        void refuse(UBCollabTestConnection* connection, int status, const QByteArray& reason);
        void leave(const QByteArray& roomId, quint32 peer);
        void endRoom(const QByteArray& roomId);
        void broadcast(Room& room, const QByteArray& text, quint32 except);
        Member* memberFor(UBCollabTestConnection* connection, QByteArray* roomId = nullptr);

        UBCollabTestServer* mServer;
        QMap<QByteArray, Room> mRooms;
        int mForwarded;
};

#endif /* UBCOLLABTESTRELAY_H_ */
