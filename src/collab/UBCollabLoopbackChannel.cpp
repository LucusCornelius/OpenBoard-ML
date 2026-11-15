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

#include "UBCollabLoopbackChannel.h"

#include <QTimer>

#include "core/memcheck.h"

UBCollabLoopbackHub::UBCollabLoopbackHub(QObject* parent)
    : QObject(parent)
    , mTimer(new QTimer(this))
{
    mClock.start();
    mTimer->setSingleShot(true);
    connect(mTimer, &QTimer::timeout, this, &UBCollabLoopbackHub::deliver);
}


UBCollabLoopbackHub::~UBCollabLoopbackHub()
{
    for (const QPointer<UBCollabLoopbackChannel>& end : std::as_const(mEnds))
    {
        if (end)
            end->mHub = nullptr;
    }
}


UBCollabLoopbackChannel* UBCollabLoopbackHub::createEnd(QObject* parent)
{
    return new UBCollabLoopbackChannel(this, parent);
}


void UBCollabLoopbackHub::dropNext(int count, UBCollabChannel::PeerId from)
{
    mDropCount = qMax(0, count);
    mDropFrom = from;
}


void UBCollabLoopbackHub::duplicateNext(int count, UBCollabChannel::PeerId from)
{
    mDuplicateCount = qMax(0, count);
    mDuplicateFrom = from;
}


void UBCollabLoopbackHub::resumeAll()
{
    const QList<QPointer<UBCollabLoopbackChannel>> ends = mEnds;

    for (const QPointer<UBCollabLoopbackChannel>& end : ends)
    {
        if (end && end->mState == UBCollabChannel::Open)
            emit end->resumed();
    }
}


bool UBCollabLoopbackHub::isIdle() const
{
    return mQueue.isEmpty();
}


void UBCollabLoopbackHub::open(UBCollabLoopbackChannel* end)
{
    if (mFailNextOpen != UBCollabChannel::NoError)
    {
        const UBCollabChannel::Error error = mFailNextOpen;
        mFailNextOpen = UBCollabChannel::NoError;
        QPointer<UBCollabLoopbackChannel> guard(end);

        QTimer::singleShot(0, this, [guard, error]() {
            if (guard)
                guard->setState(UBCollabChannel::Closed, error);
        });
        return;
    }

    end->mPeer = mNextPeer++;
    mEnds.removeAll(end);
    mEnds << end;

    QPointer<UBCollabLoopbackChannel> guard(end);

    // the others learn about the new participant once it is in
    QTimer::singleShot(0, this, [this, guard]() {
        if (!guard || guard->mState != UBCollabChannel::Connecting)
            return;

        guard->setState(UBCollabChannel::Open);

        const QList<QPointer<UBCollabLoopbackChannel>> ends = mEnds;

        for (const QPointer<UBCollabLoopbackChannel>& other : ends)
        {
            if (other && other != guard && other->mState == UBCollabChannel::Open)
                emit other->peerJoined(guard->mPeer);
        }
    });
}


void UBCollabLoopbackHub::leave(UBCollabLoopbackChannel* end, bool lost)
{
    Q_UNUSED(lost);

    if (!mEnds.contains(end))
        return;

    mEnds.removeAll(end);
    const UBCollabChannel::PeerId peer = end->mPeer;

    // what was on its way to the end that left is gone
    for (int i = mQueue.size() - 1; i >= 0; --i)
    {
        if (mQueue.at(i).to == end)
            mQueue.removeAt(i);
    }

    const QList<QPointer<UBCollabLoopbackChannel>> ends = mEnds;

    for (const QPointer<UBCollabLoopbackChannel>& other : ends)
    {
        if (other && other->mState == UBCollabChannel::Open)
        {
            QPointer<UBCollabLoopbackChannel> target = other;

            // after the messages the participant sent before it left
            Pending pending;
            pending.to = target;
            pending.from = peer;
            pending.due = mClock.elapsed() + mDelay;
            // an empty message marks the departure, it is never sent by a participant
            mQueue.enqueue(pending);
        }
    }

    if (!mTimer->isActive())
        mTimer->start(0);
}


void UBCollabLoopbackHub::post(UBCollabLoopbackChannel* from, const QByteArray& message, UBCollabChannel::PeerId to)
{
    if (message.isEmpty())
        return;

    if (mDropCount > 0 && (mDropFrom == 0 || mDropFrom == from->mPeer))
    {
        --mDropCount;
        ++mDropped;
        return;
    }

    int copies = 1;

    if (mDuplicateCount > 0 && (mDuplicateFrom == 0 || mDuplicateFrom == from->mPeer))
    {
        --mDuplicateCount;
        copies = 2;
    }

    for (const QPointer<UBCollabLoopbackChannel>& end : std::as_const(mEnds))
    {
        if (!end || end == from || end->mState != UBCollabChannel::Open)
            continue;

        if (to != 0 && end->mPeer != to)
            continue;

        for (int i = 0; i < copies; ++i)
        {
            Pending pending;
            pending.to = end;
            pending.from = from->mPeer;
            pending.message = message;
            pending.due = mClock.elapsed() + mDelay;
            mQueue.enqueue(pending);
        }
    }

    if (!mTimer->isActive())
        mTimer->start(mDelay);
}


void UBCollabLoopbackHub::deliver()
{
    // a handful at a time, so that the event loop (and the board) keeps running
    int budget = 64;

    while (!mQueue.isEmpty() && budget-- > 0)
    {
        if (mQueue.head().due > mClock.elapsed())
            break;

        const Pending pending = mQueue.dequeue();

        if (!pending.to || pending.to->mState != UBCollabChannel::Open)
            continue;

        if (pending.message.isEmpty())
        {
            emit pending.to->peerLeft(pending.from);
            continue;
        }

        ++mDelivered;
        emit pending.to->messageReceived(pending.from, pending.message);
    }

    if (!mQueue.isEmpty())
    {
        const qint64 wait = qMax<qint64>(0, mQueue.head().due - mClock.elapsed());
        mTimer->start(int(wait));
    }
}


QList<UBCollabChannel::PeerId> UBCollabLoopbackHub::peersOf(const UBCollabLoopbackChannel* end) const
{
    QList<UBCollabChannel::PeerId> peers;

    for (const QPointer<UBCollabLoopbackChannel>& other : mEnds)
    {
        if (other && other != end && other->mState == UBCollabChannel::Open)
            peers << other->mPeer;
    }

    return peers;
}


UBCollabLoopbackChannel::UBCollabLoopbackChannel(UBCollabLoopbackHub* hub, QObject* parent)
    : UBCollabChannel(parent)
    , mHub(hub)
{
}


UBCollabLoopbackChannel::~UBCollabLoopbackChannel()
{
    if (mHub)
        mHub->leave(this, false);
}


void UBCollabLoopbackChannel::open()
{
    if (!mHub || mState != Closed)
        return;

    mError = NoError;
    setState(Connecting);
    mHub->open(this);
}


void UBCollabLoopbackChannel::loseLine()
{
    if (mState == Closed)
        return;

    if (mHub)
        mHub->leave(this, true);

    setState(Closed, ConnectionLost);
}


QString UBCollabLoopbackChannel::errorString() const
{
    switch (mError)
    {
        case NoError: return QString();
        case RelayUnreachable: return "loopback: relay unreachable";
        case RelayRefused: return "loopback: relay refused";
        case SessionNotFound: return "loopback: session not found";
        case SessionFull: return "loopback: session full";
        case WrongCode: return "loopback: wrong code";
        case ConnectionLost: return "loopback: connection lost";
        case ProtocolError: return "loopback: protocol error";
    }

    return QString();
}


QList<UBCollabChannel::PeerId> UBCollabLoopbackChannel::peers() const
{
    if (!mHub || mState != Open)
        return {};

    return mHub->peersOf(this);
}


void UBCollabLoopbackChannel::send(const QByteArray& message, PeerId to)
{
    if (mHub && mState == Open)
        mHub->post(this, message, to);
}


void UBCollabLoopbackChannel::close()
{
    if (mState == Closed)
        return;

    if (mHub)
        mHub->leave(this, false);

    setState(Closed);
}


void UBCollabLoopbackChannel::setState(State state, Error error)
{
    if (mState == state && mError == error)
        return;

    mState = state;
    mError = error;
    emit stateChanged();
}
