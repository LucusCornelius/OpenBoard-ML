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

#ifndef UBCOLLABLOOPBACKCHANNEL_H_
#define UBCOLLABLOOPBACKCHANNEL_H_

#include <QElapsedTimer>
#include <QList>
#include <QPointer>
#include <QQueue>

#include "UBCollabChannel.h"

class QTimer;
class UBCollabLoopbackChannel;

/**
 * A session inside one process, for tests: the ends created by one hub are
 * the participants. Messages are delivered through the event loop, after an
 * optional delay, in the order in which they were sent. A test can drop or
 * duplicate messages on the way and then tell the ends that the line came
 * back (resumed()), as a dropped internet connection would.
 */
class UBCollabLoopbackHub : public QObject
{
    Q_OBJECT

    public:
        explicit UBCollabLoopbackHub(QObject* parent = nullptr);
        ~UBCollabLoopbackHub() override;

        /** a new end; it joins the session when it is opened */
        UBCollabLoopbackChannel* createEnd(QObject* parent = nullptr);

        /** delay of every delivery in milliseconds */
        void setDelay(int milliseconds) { mDelay = qMax(0, milliseconds); }

        /** the next count messages sent by from (0: by anybody) are lost */
        void dropNext(int count, UBCollabChannel::PeerId from = 0);

        /** the next count messages sent by from (0: by anybody) are delivered twice */
        void duplicateNext(int count, UBCollabChannel::PeerId from = 0);

        /** every open end reports that its line came back */
        void resumeAll();

        /** the next end that opens fails with this error instead of joining */
        void failNextOpen(UBCollabChannel::Error error) { mFailNextOpen = error; }

        /** messages delivered and dropped so far */
        int deliveredCount() const { return mDelivered; }
        int droppedCount() const { return mDropped; }

        /** true when no message is on its way */
        bool isIdle() const;

    private:
        friend class UBCollabLoopbackChannel;

        struct Pending
        {
            QPointer<UBCollabLoopbackChannel> to;
            UBCollabChannel::PeerId from{0};
            QByteArray message;
            qint64 due{0};
        };

        void open(UBCollabLoopbackChannel* end);
        void leave(UBCollabLoopbackChannel* end, bool lost);
        void post(UBCollabLoopbackChannel* from, const QByteArray& message, UBCollabChannel::PeerId to);
        void deliver();
        QList<UBCollabChannel::PeerId> peersOf(const UBCollabLoopbackChannel* end) const;

        QList<QPointer<UBCollabLoopbackChannel>> mEnds;
        QQueue<Pending> mQueue;
        QTimer* mTimer{nullptr};
        QElapsedTimer mClock;
        UBCollabChannel::PeerId mNextPeer{1};
        int mDelay{0};
        int mDropCount{0};
        UBCollabChannel::PeerId mDropFrom{0};
        int mDuplicateCount{0};
        UBCollabChannel::PeerId mDuplicateFrom{0};
        int mDelivered{0};
        int mDropped{0};
        UBCollabChannel::Error mFailNextOpen{UBCollabChannel::NoError};
};


/** one participant of a UBCollabLoopbackHub */
class UBCollabLoopbackChannel : public UBCollabChannel
{
    Q_OBJECT

    public:
        ~UBCollabLoopbackChannel() override;

        /** joins the session of the hub (Connecting, then Open through the event loop) */
        void open();

        /** the line drops and does not come back */
        void loseLine();

        State state() const override { return mState; }
        Error error() const override { return mError; }
        QString errorString() const override;
        PeerId localPeer() const override { return mState == Open ? mPeer : 0; }
        QList<PeerId> peers() const override;
        void send(const QByteArray& message, PeerId to = 0) override;
        qint64 bytesToSend() const override { return 0; }
        void close() override;

    private:
        friend class UBCollabLoopbackHub;

        UBCollabLoopbackChannel(UBCollabLoopbackHub* hub, QObject* parent);

        void setState(State state, Error error = NoError);

        QPointer<UBCollabLoopbackHub> mHub;
        State mState{Closed};
        Error mError{NoError};
        PeerId mPeer{0};
};

#endif /* UBCOLLABLOOPBACKCHANNEL_H_ */
