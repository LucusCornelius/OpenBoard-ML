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

#ifndef UBCOLLABSESSION_H_
#define UBCOLLABSESSION_H_

#include <QColor>
#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QQueue>
#include <QUuid>

#include <memory>

#include "UBCollabChannel.h"
#include "UBCollabDocumentSync.h"

class QTimer;
class UBCollabSnapshotReceiver;
class UBDocumentProxy;

/**
 * One session of a shared board on this computer: the participants, the
 * greeting with the protocol version, the initial board for those who join,
 * the digests and the cursors, on top of a channel. The document itself is
 * kept in step by a UBCollabDocumentSync. The host (who started the session)
 * shares the document that was open on the board; a guest gets the board of
 * the host as a new document. When the host leaves, the session ends for
 * everybody and everybody keeps his copy.
 *
 * Nothing here knows the user interface: UBCollabController shows it.
 */
class UBCollabSession : public QObject
{
    Q_OBJECT

    public:
        using PeerId = UBCollabChannel::PeerId;

        enum Role
        {
            Host,
            Guest
        };

        enum State
        {
            Connecting,     // on the way to the relay
            Joining,        // a guest waits for the board of the host
            Active,
            Ended
        };

        enum EndReason
        {
            NotEnded,
            Left,               // this participant left
            HostEnded,          // the host ended the session
            HostGone,           // the host is no longer there
            ConnectionLost,
            RelayUnreachable,
            RelayRefused,
            SessionNotFound,
            SessionFull,
            WrongCode,
            VersionMismatch,
            InvalidData,        // the host sent something that cannot be right
            SnapshotFailed,
            NoAnswer            // nobody answered as host
        };

        struct Participant
        {
            PeerId peer{0};
            QString name;           // as given; empty when the participant gave none
            QColor color;
            bool host{false};
            bool local{false};
        };

        struct Cursor
        {
            QUuid page;
            QPointF position;
            bool visible{false};
            bool penDown{false};
            int tool{0};
            QColor color;
            qreal width{1.0};
            QList<QPointF> stroke;      // the stroke being drawn, in page coordinates
            qint64 updated{0};
            qint64 strokeEnded{-1};     // when the pen was lifted; the stroke follows soon
        };

        /** the session takes the channel; a host shares document, a guest passes null */
        UBCollabSession(Role role, UBCollabChannel* channel, std::shared_ptr<UBDocumentProxy> document, QObject* parent = nullptr);
        ~UBCollabSession() override;

        /** before the channel opens: the name the others see */
        void setDisplayName(const QString& name);

        Role role() const { return mRole; }
        State state() const { return mState; }
        EndReason endReason() const { return mEndReason; }
        QString endDetail() const { return mEndDetail; }

        UBCollabChannel* channel() const { return mChannel; }
        UBCollabDocumentSync* sync() const { return mSync; }
        std::shared_ptr<UBDocumentProxy> document() const { return mDocument; }

        /** everybody in the session, this participant first */
        QList<Participant> participants() const;
        int participantCount() const;
        QColor localColor() const;

        /** the colour of a participant (stable during the session) */
        static QColor colorFor(PeerId peer);

        /** "Participant" when there is no name */
        static QString displayName(const Participant& participant);

        qint64 snapshotReceived() const;
        qint64 snapshotTotal() const;

        const QHash<PeerId, Cursor>& cursors() const { return mCursors; }

        /** the pointer of this participant; sent at most 15 times a second, and only when it changed */
        void setPointer(const QUuid& page, const QPointF& position, bool visible, bool penDown, int tool, const QColor& color, qreal width);

        /** leaves the session; for the host it ends the session for everybody */
        void leave();

        /** for tests: what a participant sends goes through the same handling */
        void handleMessage(PeerId from, const QByteArray& message);

        /** for tests: number of messages that were refused */
        int refusedCount() const { return mRefused; }
        int resyncCount() const { return mResyncs; }

    signals:
        void stateChanged();
        void participantsChanged();
        void ended(UBCollabSession::EndReason reason);

        /** a guest: the board of the host arrived and is a document now */
        void joined(std::shared_ptr<UBDocumentProxy> document);

        void snapshotProgress(qint64 received, qint64 total);
        void cursorsChanged();

        /** someone took the group to another page */
        void remoteCurrentPage(const QUuid& page);

        void localOnlyAdded(int kind);

        /** something of the user could not be sent (too large) */
        void notSent(const QString& what);

    private:
        struct SnapshotSend
        {
            PeerId peer{0};
            UBCollabDocumentSync::SnapshotPlan plan;
            bool beginSent{false};
            int nextPage{0};
            int nextAsset{0};
            qint64 assetOffset{0};
            qint64 assetTotal{-1};
        };

        struct Remote
        {
            Participant participant;
            bool greeted{false};
            int refused{0};
            qint64 lastRequest{0};
            int requests{0};
        };

        struct Queued
        {
            PeerId from{0};
            QByteArray message;
        };

        void channelStateChanged();
        void peerJoined(PeerId peer);
        void peerLeft(PeerId peer);
        void resumed();

        void sendHello(PeerId to);
        void send(const QByteArray& message, PeerId to = 0);
        void sendBye(quint8 reason, PeerId to = 0);

        void handleHello(PeerId from, UBCollabReader& reader);
        void handleBye(PeerId from, UBCollabReader& reader);
        void handleSnapshotPart(PeerId from, quint8 type, UBCollabReader& reader);
        void handleDigest(PeerId from, UBCollabReader& reader);
        void handleCursor(PeerId from, UBCollabReader& reader);
        void handleRequest(PeerId from, quint8 type, UBCollabReader& reader);
        void documentMessage(PeerId from, const QByteArray& message);
        void applyDocumentMessage(PeerId from, const QByteArray& message);
        void flushQueue();
        void refuse(PeerId from, const QString& reason);

        void startSync(std::shared_ptr<UBDocumentProxy> document);
        void pumpSnapshots();
        void sendDigest();
        void requestPage(const QUuid& page);
        void sendPointerNow();
        void expireCursors();
        void end(EndReason reason, const QString& detail = QString());
        void closeChannel();

        Role mRole;
        State mState{Connecting};
        EndReason mEndReason{NotEnded};
        QString mEndDetail;
        QPointer<UBCollabChannel> mChannel;
        std::shared_ptr<UBDocumentProxy> mDocument;
        UBCollabDocumentSync* mSync{nullptr};
        UBCollabSnapshotReceiver* mReceiver{nullptr};
        QString mName;

        QHash<PeerId, Remote> mRemotes;
        PeerId mHostPeer{0};
        bool mSnapshotRequested{false};
        QList<SnapshotSend> mSnapshotSends;
        QQueue<Queued> mQueue;
        bool mFlushing{false};

        QHash<QUuid, int> mDigestStrikes;
        bool mResumedCheck{false};
        quint64 mLastDigest{0};
        qint64 mLastDigestTime{0};
        QHash<QUuid, qint64> mPageRequests;
        int mRefused{0};
        int mResyncs{0};

        QHash<PeerId, Cursor> mCursors;
        Cursor mPointer;
        Cursor mPointerSent;
        QList<QPointF> mPointerStroke;
        bool mPointerDirty{false};

        QTimer* mPumpTimer{nullptr};
        QTimer* mDigestTimer{nullptr};
        QTimer* mPointerTimer{nullptr};
        QTimer* mCursorTimer{nullptr};
        QTimer* mJoinTimer{nullptr};
        QElapsedTimer mTime;
};

#endif /* UBCOLLABSESSION_H_ */
