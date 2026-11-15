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

#include "UBCollabSession.h"

#include <QCoreApplication>
#include <QFile>
#include <QTimer>

#include <algorithm>
#include <cmath>

#include "document/UBDocumentProxy.h"

#include "UBCollabBoardLink.h"
#include "UBCollabProtocol.h"
#include "UBCollabSnapshot.h"
#include "UBCollabWire.h"

#include "core/memcheck.h"

namespace
{
    // messages that wait while the user draws or while the board arrives
    constexpr int maxQueuedMessages = 20000;

    // a participant whose messages are refused this often is not listened to anymore
    constexpr int maxRefusals = 20;

    // messages kept from a participant before his greeting
    constexpr int maxEarlyMessages = 2000;
    constexpr qint64 maxEarlyBytes = 128 * 1024 * 1024;

    // the line is held back above this many bytes waiting to be sent
    constexpr qint64 snapshotBacklog = 8 * 1024 * 1024;

    constexpr int pointerIntervalMs = 66;      // at most 15 cursor updates per second
    constexpr int digestIntervalMs = 3000;
    constexpr qint64 digestRepeatMs = 15000;
    constexpr qint64 pageRequestPauseMs = 8000;
    constexpr int joinTimeoutMs = 30000;
    constexpr int snapshotIdleTimeoutMs = 90000;
    constexpr qint64 cursorIdleMs = 10000;
    constexpr qint64 strokeLingerMs = 2500;
    constexpr double maxCoordinate = 1.0e7;

    QString cleanName(const QString& name)
    {
        QString clean;

        for (const QChar c : name)
        {
            if (!c.isNull() && c.category() != QChar::Other_Control && c.category() != QChar::Other_Format)
                clean.append(c);
        }

        return clean.simplified().left(40);
    }
}


UBCollabSession::UBCollabSession(Role role, UBCollabChannel* channel, std::shared_ptr<UBDocumentProxy> document, QObject* parent)
    : QObject(parent)
    , mRole(role)
    , mChannel(channel)
    , mDocument(role == Host ? document : nullptr)
    , mPumpTimer(new QTimer(this))
    , mDigestTimer(new QTimer(this))
    , mPointerTimer(new QTimer(this))
    , mCursorTimer(new QTimer(this))
    , mJoinTimer(new QTimer(this))
{
    mTime.start();

    if (mChannel)
    {
        mChannel->setParent(this);
        connect(mChannel.data(), &UBCollabChannel::stateChanged, this, &UBCollabSession::channelStateChanged);
        connect(mChannel.data(), &UBCollabChannel::peerJoined, this, &UBCollabSession::peerJoined);
        connect(mChannel.data(), &UBCollabChannel::peerLeft, this, &UBCollabSession::peerLeft);
        connect(mChannel.data(), &UBCollabChannel::resumed, this, &UBCollabSession::resumed);
        connect(mChannel.data(), &UBCollabChannel::messageReceived, this, &UBCollabSession::handleMessage);
    }

    mPumpTimer->setInterval(15);
    connect(mPumpTimer, &QTimer::timeout, this, &UBCollabSession::pumpSnapshots);

    mDigestTimer->setInterval(digestIntervalMs);
    connect(mDigestTimer, &QTimer::timeout, this, &UBCollabSession::sendDigest);

    mPointerTimer->setSingleShot(true);
    mPointerTimer->setInterval(pointerIntervalMs);
    connect(mPointerTimer, &QTimer::timeout, this, &UBCollabSession::sendPointerNow);

    mCursorTimer->setInterval(1000);
    connect(mCursorTimer, &QTimer::timeout, this, &UBCollabSession::expireCursors);

    mJoinTimer->setSingleShot(true);
    connect(mJoinTimer, &QTimer::timeout, this, [this]() {
        if (mState == Joining || mState == Connecting)
            end(mHostPeer ? SnapshotFailed : NoAnswer);
    });

    // messages that waited while the user was drawing
    connect(UBCollabBoardLink::instance(), &UBCollabBoardLink::gestureEnded, this, [this]() {
        QTimer::singleShot(0, this, &UBCollabSession::flushQueue);
    });

    if (mChannel && mChannel->state() == UBCollabChannel::Open)
        QTimer::singleShot(0, this, &UBCollabSession::channelStateChanged);
}


UBCollabSession::~UBCollabSession()
{
    if (mState != Ended)
        end(Left);

    delete mReceiver;
}


void UBCollabSession::setDisplayName(const QString& name)
{
    mName = cleanName(name);
}


QColor UBCollabSession::colorFor(PeerId peer)
{
    // distinct on a light and a dark board
    static const QList<QColor> palette = {
        QColor(0x0A, 0x84, 0xFF), QColor(0xFF, 0x9F, 0x0A), QColor(0x30, 0xD1, 0x58), QColor(0xFF, 0x37, 0x5F),
        QColor(0xBF, 0x5A, 0xF2), QColor(0x40, 0xC8, 0xE0), QColor(0xE0, 0xB0, 0x00), QColor(0xA2, 0x84, 0x5E)
    };

    return palette.at(int(peer % quint32(palette.size())));
}


QString UBCollabSession::displayName(const Participant& participant)
{
    return participant.name.isEmpty() ? tr("Participant") : participant.name;
}


QColor UBCollabSession::localColor() const
{
    return colorFor(mChannel ? mChannel->localPeer() : 0);
}


QList<UBCollabSession::Participant> UBCollabSession::participants() const
{
    QList<Participant> list;

    Participant self;
    self.peer = mChannel ? mChannel->localPeer() : 0;
    self.name = mName;
    self.color = localColor();
    self.host = mRole == Host;
    self.local = true;
    list << self;

    QList<PeerId> peers = mRemotes.keys();
    std::sort(peers.begin(), peers.end());

    for (PeerId peer : std::as_const(peers))
    {
        const Remote& remote = mRemotes[peer];

        if (remote.greeted)
            list << remote.participant;
    }

    return list;
}


int UBCollabSession::participantCount() const
{
    int count = 1;

    for (const Remote& remote : mRemotes)
    {
        if (remote.greeted)
            ++count;
    }

    return count;
}


qint64 UBCollabSession::snapshotReceived() const
{
    return mReceiver ? mReceiver->received() : 0;
}


qint64 UBCollabSession::snapshotTotal() const
{
    return mReceiver ? mReceiver->total() : 0;
}


// ---------------------------------------------------------------------------
// the line
// ---------------------------------------------------------------------------

void UBCollabSession::channelStateChanged()
{
    if (!mChannel || mState == Ended)
        return;

    switch (mChannel->state())
    {
        case UBCollabChannel::Open:
        {
            if (mState != Connecting)
                break;

            if (mRole == Host)
            {
                startSync(mDocument);
                mSync->start();
                mState = Active;
                mDigestTimer->start();
            }
            else
            {
                mState = Joining;
                mJoinTimer->start(joinTimeoutMs);
            }

            mCursorTimer->start();
            sendHello(0);

            emit stateChanged();
            emit participantsChanged();
            break;
        }

        case UBCollabChannel::Connecting:
            break;

        case UBCollabChannel::Closed:
        {
            EndReason reason = ConnectionLost;

            switch (mChannel->error())
            {
                case UBCollabChannel::NoError: reason = ConnectionLost; break;
                case UBCollabChannel::RelayUnreachable: reason = RelayUnreachable; break;
                case UBCollabChannel::RelayRefused: reason = RelayRefused; break;
                case UBCollabChannel::SessionNotFound: reason = SessionNotFound; break;
                case UBCollabChannel::SessionFull: reason = SessionFull; break;
                case UBCollabChannel::WrongCode: reason = WrongCode; break;
                case UBCollabChannel::ConnectionLost: reason = ConnectionLost; break;
                case UBCollabChannel::ProtocolError: reason = VersionMismatch; break;
            }

            end(reason, mChannel->errorString());
            break;
        }
    }
}


void UBCollabSession::peerJoined(PeerId peer)
{
    if (mState == Ended || peer == 0)
        return;

    Remote& remote = mRemotes[peer];
    remote.participant.peer = peer;
    remote.participant.color = colorFor(peer);
    sendHello(peer);
}


void UBCollabSession::peerLeft(PeerId peer)
{
    mRemotes.remove(peer);
    mEarly.remove(peer);
    mCursors.remove(peer);

    for (int i = int(mSnapshotSends.size()) - 1; i >= 0; --i)
    {
        if (mSnapshotSends.at(i).peer == peer)
            mSnapshotSends.removeAt(i);
    }

    emit participantsChanged();
    emit cursorsChanged();

    if (mRole == Guest && peer == mHostPeer && mState != Ended)
        end(HostGone);
}


void UBCollabSession::resumed()
{
    // messages may have been lost both ways: compare with the host at once
    if (mRole == Guest && mHostPeer && mState == Active)
    {
        // what this participant did may not have arrived: sent again before the comparison
        if (mSync)
            mSync->resendOwnChanges();

        mResumedCheck = true;
        UBCollabWriter writer;
        UBCollabProtocol::writeHeader(writer, UBCollabProtocol::DigestRequest);
        send(writer.data(), mHostPeer);
    }
    else if (mRole == Host && mState == Active)
    {
        mLastDigest = 0;
        sendDigest();
    }
}


void UBCollabSession::send(const QByteArray& message, PeerId to)
{
    if (mChannel && mChannel->state() == UBCollabChannel::Open && mState != Ended)
        mChannel->send(message, to);
}


void UBCollabSession::sendHello(PeerId to)
{
    UBCollabWriter writer;
    UBCollabProtocol::writeHeader(writer, UBCollabProtocol::Hello);
    writer.u32(UBCollabProtocol::version);
    writer.string(QCoreApplication::applicationVersion().left(32));
    writer.u8(mRole == Host ? UBCollabProtocol::Host : UBCollabProtocol::Guest);
    writer.string(mName);
    send(writer.data(), to);
}


void UBCollabSession::sendBye(quint8 reason, PeerId to)
{
    UBCollabWriter writer;
    UBCollabProtocol::writeHeader(writer, UBCollabProtocol::Bye);
    writer.u8(reason);
    send(writer.data(), to);
}


// ---------------------------------------------------------------------------
// messages
// ---------------------------------------------------------------------------

void UBCollabSession::handleMessage(PeerId from, const QByteArray& message)
{
    if (mState == Ended)
        return;

    UBCollabReader reader(message);
    const quint8 type = UBCollabProtocol::readHeader(reader);

    if (!type)
    {
        refuse(from, reader.error());
        return;
    }

    if (type == UBCollabProtocol::Hello)
    {
        handleHello(from, reader);
        return;
    }

    if (!mRemotes.value(from).greeted)
    {
        // the protocol version is not known yet: kept until the greeting (which may come later
        // than the first operations when someone joins while others work)
        QList<QByteArray>& early = mEarly[from];
        qint64 bytes = message.size();

        for (const QByteArray& kept : std::as_const(early))
            bytes += kept.size();

        if (early.size() < maxEarlyMessages && bytes <= maxEarlyBytes)
            early << message;
        else
            refuse(from, "too many messages before the greeting");

        return;
    }

    if (mRemotes.value(from).refused >= maxRefusals)
        return;

    switch (type)
    {
        case UBCollabProtocol::Bye:
            handleBye(from, reader);
            break;

        case UBCollabProtocol::SnapshotRequest:
        {
            if (mRole != Host || !mSync || mState != Active)
            {
                refuse(from, "a request for the board to a guest");
                break;
            }

            for (const SnapshotSend& sending : std::as_const(mSnapshotSends))
            {
                if (sending.peer == from)
                    return;
            }

            SnapshotSend sending;
            sending.peer = from;
            sending.plan = mSync->snapshotPlan();
            mSnapshotSends << sending;
            pumpSnapshots();
            break;
        }

        case UBCollabProtocol::SnapshotBegin:
        case UBCollabProtocol::SnapshotPage:
        case UBCollabProtocol::SnapshotAsset:
        case UBCollabProtocol::SnapshotEnd:
            handleSnapshotPart(from, type, reader);
            break;

        case UBCollabProtocol::Cursor:
            handleCursor(from, reader);
            break;

        case UBCollabProtocol::Digest:
            handleDigest(from, reader);
            break;

        case UBCollabProtocol::DigestRequest:
        case UBCollabProtocol::PageRequest:
        case UBCollabProtocol::DocStateRequest:
            handleRequest(from, type, reader);
            break;

        default:
            if (UBCollabDocumentSync::isDocumentMessage(type))
            {
                // only the host may replace pages or the list of pages
                if ((type == UBCollabProtocol::PageContent || type == UBCollabProtocol::DocState)
                        && (mRole != Guest || from != mHostPeer))
                {
                    refuse(from, "a page or list of pages not from the host");
                    break;
                }

                documentMessage(from, message);
            }
            else
            {
                refuse(from, QString("unexpected message %1").arg(type));
            }
            break;
    }
}


void UBCollabSession::refuse(PeerId from, const QString& reason)
{
    ++mRefused;
    qWarning() << "collaboration: refused a message of participant" << from << ":" << reason;

    if (!mRemotes.contains(from))
        return;

    Remote& remote = mRemotes[from];
    ++remote.refused;

    if (mRole == Guest && from == mHostPeer && remote.refused >= maxRefusals)
        end(InvalidData, reason);
}


void UBCollabSession::handleHello(PeerId from, UBCollabReader& reader)
{
    const quint32 version = reader.u32();
    const QString appVersion = reader.string(UBCollabProtocol::maxVersionTextBytes);
    const quint8 role = reader.u8();
    const QString name = reader.string(UBCollabProtocol::maxNameBytes);
    Q_UNUSED(appVersion);

    if (!reader.ok() || role > UBCollabProtocol::Host)
    {
        refuse(from, "invalid greeting");
        return;
    }

    if (version != UBCollabProtocol::version)
    {
        // the other uses a different version of OpenBoard-ML
        sendBye(UBCollabProtocol::VersionMismatch, from);

        if (mRole == Guest && role == UBCollabProtocol::Host)
            end(VersionMismatch);

        return;
    }

    if (role == UBCollabProtocol::Host)
    {
        if (mRole == Host || (mHostPeer && mHostPeer != from))
        {
            refuse(from, "a second host");
            return;
        }

        mHostPeer = from;
    }

    Remote& remote = mRemotes[from];
    const bool wasGreeted = remote.greeted;
    remote.greeted = true;
    remote.participant.peer = from;
    remote.participant.name = cleanName(name);
    remote.participant.color = colorFor(from);
    remote.participant.host = role == UBCollabProtocol::Host;
    remote.participant.local = false;

    if (mRole == Guest && from == mHostPeer && !mSnapshotRequested && mState == Joining)
    {
        mSnapshotRequested = true;
        mJoinTimer->start(snapshotIdleTimeoutMs);

        UBCollabWriter writer;
        UBCollabProtocol::writeHeader(writer, UBCollabProtocol::SnapshotRequest);
        send(writer.data(), mHostPeer);
    }

    if (!wasGreeted)
        emit participantsChanged();

    // what came before the greeting
    if (mEarly.contains(from))
    {
        const QList<QByteArray> early = mEarly.take(from);

        for (const QByteArray& message : early)
            handleMessage(from, message);
    }
}


void UBCollabSession::handleBye(PeerId from, UBCollabReader& reader)
{
    const quint8 reason = reader.u8();

    if (!reader.ok())
    {
        refuse(from, "invalid goodbye");
        return;
    }

    if (mRole == Guest && from == mHostPeer)
    {
        end(reason == UBCollabProtocol::VersionMismatch ? VersionMismatch : HostEnded);
        return;
    }

    peerLeft(from);
}


void UBCollabSession::handleSnapshotPart(PeerId from, quint8 type, UBCollabReader& reader)
{
    if (mRole != Guest || from != mHostPeer || mState != Joining)
    {
        refuse(from, "a part of a board that was not asked for");
        return;
    }

    if (!mReceiver)
        mReceiver = new UBCollabSnapshotReceiver();

    mJoinTimer->start(snapshotIdleTimeoutMs);

    bool ok = false;
    std::shared_ptr<UBDocumentProxy> document;

    switch (type)
    {
        case UBCollabProtocol::SnapshotBegin:
            ok = mReceiver->begin(reader);
            break;
        case UBCollabProtocol::SnapshotPage:
            ok = mReceiver->page(reader) && reader.atEnd();
            break;
        case UBCollabProtocol::SnapshotAsset:
            ok = mReceiver->asset(reader) && reader.atEnd();
            break;
        case UBCollabProtocol::SnapshotEnd:
            document = mReceiver->end(reader);
            ok = document != nullptr;
            break;
        default:
            break;
    }

    if (!ok)
    {
        const QString reason = mReceiver->error().isEmpty() ? reader.error() : mReceiver->error();
        qWarning() << "collaboration: the board of the host was refused:" << reason;
        end(SnapshotFailed, reason);
        return;
    }

    emit snapshotProgress(mReceiver->received(), mReceiver->total());

    if (!document)
        return;

    mDocument = document;
    startSync(document);
    mSync->startFromSnapshot(mReceiver->registers(), mReceiver->clock(),
                             mReceiver->currentPage(), mReceiver->currentPageVersion());

    if (mReceiver->droppedElements() > 0)
        emit localOnlyAdded(-1);

    delete mReceiver;
    mReceiver = nullptr;

    mJoinTimer->stop();
    mState = Active;

    emit joined(document);
    emit stateChanged();

    // what the others did while the board was on its way
    QTimer::singleShot(0, this, &UBCollabSession::flushQueue);
}


void UBCollabSession::startSync(std::shared_ptr<UBDocumentProxy> document)
{
    mSync = new UBCollabDocumentSync(document, this);
    mSync->setLocalPeer(mChannel ? mChannel->localPeer() : 0);
    mSync->setHost(mRole == Host);

    connect(mSync, &UBCollabDocumentSync::outgoing, this, &UBCollabSession::send);
    connect(mSync, &UBCollabDocumentSync::remoteCurrentPage, this, &UBCollabSession::remoteCurrentPage);
    connect(mSync, &UBCollabDocumentSync::localOnlyAdded, this, &UBCollabSession::localOnlyAdded);
    connect(mSync, &UBCollabDocumentSync::pageNeedsResync, this, &UBCollabSession::requestPage);
    connect(mSync, &UBCollabDocumentSync::notSent, this, &UBCollabSession::notSent);
}


void UBCollabSession::documentMessage(PeerId from, const QByteArray& message)
{
    UBCollabBoardLink* link = UBCollabBoardLink::instance();

    // in order: while the board arrives, while the user draws, behind those that wait already
    if (mState != Active || !mSync || link->isGestureActive() || !mQueue.isEmpty())
    {
        if (mQueue.size() >= maxQueuedMessages)
        {
            // too much at once: the digest will bring the pages back in step
            qWarning() << "collaboration: too many waiting messages, one dropped";
            return;
        }

        Queued queued;
        queued.from = from;
        queued.message = message;
        mQueue.enqueue(queued);
        return;
    }

    applyDocumentMessage(from, message);
}


void UBCollabSession::applyDocumentMessage(PeerId from, const QByteArray& message)
{
    if (!mSync || mState != Active)
        return;

    UBCollabReader reader(message);
    const quint8 type = UBCollabProtocol::readHeader(reader);

    if (!mSync->handleMessage(from, type, reader))
    {
        refuse(from, reader.error());
        return;
    }

    // the stroke arrived: the line drawn while it was being drawn goes
    if (type == UBCollabProtocol::ItemOps && mCursors.contains(from) && mCursors[from].strokeEnded >= 0)
    {
        mCursors[from].stroke.clear();
        mCursors[from].strokeEnded = -1;
        emit cursorsChanged();
    }
}


void UBCollabSession::flushQueue()
{
    if (mFlushing || mState != Active || !mSync)
        return;

    mFlushing = true;

    while (!mQueue.isEmpty() && mState == Active && !UBCollabBoardLink::instance()->isGestureActive())
    {
        // also of someone who left meanwhile: what he did before he left counts
        const Queued queued = mQueue.dequeue();
        applyDocumentMessage(queued.from, queued.message);
    }

    mFlushing = false;
}


// ---------------------------------------------------------------------------
// the initial board
// ---------------------------------------------------------------------------

void UBCollabSession::pumpSnapshots()
{
    if (!mChannel || mChannel->state() != UBCollabChannel::Open || mState != Active)
    {
        mSnapshotSends.clear();
        mPumpTimer->stop();
        return;
    }

    const QList<PeerId> peers = mChannel->peers();
    const QString root = mDocument ? mDocument->persistencePath() + "/" : QString();

    for (int i = int(mSnapshotSends.size()) - 1; i >= 0; --i)
    {
        SnapshotSend& sending = mSnapshotSends[i];
        bool done = false;

        if (!peers.contains(sending.peer))
        {
            mSnapshotSends.removeAt(i);
            continue;
        }

        while (mChannel->bytesToSend() < snapshotBacklog)
        {
            if (!sending.beginSent)
            {
                send(sending.plan.begin, sending.peer);
                sending.beginSent = true;
                continue;
            }

            if (sending.nextPage < sending.plan.pages.size())
            {
                send(sending.plan.pages.at(sending.nextPage++), sending.peer);
                continue;
            }

            if (sending.nextAsset < sending.plan.assets.size())
            {
                const QString path = sending.plan.assets.at(sending.nextAsset);
                QFile file(root + path);

                if (sending.assetTotal < 0)
                    sending.assetTotal = file.size();

                if (sending.assetTotal > UBCollabProtocol::maxAssetBytes || !file.open(QIODevice::ReadOnly)
                        || !file.seek(sending.assetOffset))
                {
                    // a file that cannot be sent: the item stays without its picture there
                    ++sending.nextAsset;
                    sending.assetOffset = 0;
                    sending.assetTotal = -1;
                    continue;
                }

                const QByteArray chunk = file.read(qMin<qint64>(UBCollabProtocol::maxSnapshotPartBytes,
                                                                sending.assetTotal - sending.assetOffset));

                UBCollabWriter writer;
                UBCollabProtocol::writeHeader(writer, UBCollabProtocol::SnapshotAsset);
                writer.string(path);
                writer.u64(quint64(sending.assetTotal));
                writer.u64(quint64(sending.assetOffset));
                writer.bytes(chunk);
                send(writer.data(), sending.peer);

                sending.assetOffset += chunk.size();

                if (chunk.isEmpty() || sending.assetOffset >= sending.assetTotal)
                {
                    ++sending.nextAsset;
                    sending.assetOffset = 0;
                    sending.assetTotal = -1;
                }

                continue;
            }

            send(sending.plan.end, sending.peer);
            done = true;
            break;
        }

        if (done)
            mSnapshotSends.removeAt(i);
    }

    if (mSnapshotSends.isEmpty())
        mPumpTimer->stop();
    else if (!mPumpTimer->isActive())
        mPumpTimer->start();
}


// ---------------------------------------------------------------------------
// digests
// ---------------------------------------------------------------------------

void UBCollabSession::sendDigest()
{
    if (mRole != Host || !mSync || mState != Active || participantCount() < 2)
        return;

    const qint64 now = mTime.elapsed();
    const QByteArray message = mSync->digestMessage();
    const quint64 digest = qHash(message);

    if (digest == mLastDigest && now - mLastDigestTime < digestRepeatMs)
        return;

    mLastDigest = digest;
    mLastDigestTime = now;
    send(message, 0);
}


void UBCollabSession::handleDigest(PeerId from, UBCollabReader& reader)
{
    if (mRole != Guest || from != mHostPeer)
    {
        refuse(from, "a digest not from the host");
        return;
    }

    // not while operations wait: they would make a difference that is about to go
    if (!mSync || mState != Active || !mQueue.isEmpty() || mSync->deferredCount() > 0)
        return;

    QList<QUuid> different;
    bool docDiffers = false;

    if (!mSync->compareDigest(reader, different, docDiffers))
    {
        refuse(from, reader.error());
        return;
    }

    // a difference has to last for two digests: operations may be on their way
    const int strikesNeeded = mResumedCheck ? 1 : 2;
    QHash<QUuid, int> strikes;

    for (const QUuid& page : std::as_const(different))
    {
        const int count = mDigestStrikes.value(page) + 1;

        if (count >= strikesNeeded)
            requestPage(page);
        else
            strikes.insert(page, count);
    }

    const QUuid documentKey;
    const int docCount = docDiffers ? mDigestStrikes.value(documentKey) + 1 : 0;

    if (docDiffers && docCount >= strikesNeeded)
    {
        UBCollabWriter writer;
        UBCollabProtocol::writeHeader(writer, UBCollabProtocol::DocStateRequest);
        send(writer.data(), mHostPeer);
        ++mResyncs;
    }
    else if (docDiffers)
    {
        strikes.insert(documentKey, docCount);
    }

    mDigestStrikes = strikes;
    mResumedCheck = false;
}


void UBCollabSession::requestPage(const QUuid& page)
{
    if (mRole != Guest || !mHostPeer || mState != Active || page.isNull())
        return;

    const qint64 now = mTime.elapsed();

    if (mPageRequests.contains(page) && now - mPageRequests.value(page) < pageRequestPauseMs)
        return;

    mPageRequests.insert(page, now);
    ++mResyncs;

    UBCollabWriter writer;
    UBCollabProtocol::writeHeader(writer, UBCollabProtocol::PageRequest);
    writer.uuid(page);
    send(writer.data(), mHostPeer);
}


void UBCollabSession::handleRequest(PeerId from, quint8 type, UBCollabReader& reader)
{
    if (mRole != Host || !mSync || mState != Active)
    {
        refuse(from, "a request to a guest");
        return;
    }

    // a participant that asks too much gets no more than this
    Remote& remote = mRemotes[from];
    const qint64 now = mTime.elapsed();

    if (now - remote.lastRequest > 10000)
    {
        remote.lastRequest = now;
        remote.requests = 0;
    }

    if (++remote.requests > 200)
        return;

    switch (type)
    {
        case UBCollabProtocol::DigestRequest:
            if (reader.atEnd())
                send(mSync->digestMessage(), from);
            else
                refuse(from, "invalid request");
            break;

        case UBCollabProtocol::PageRequest:
        {
            const QUuid page = reader.uuid();

            if (!reader.ok() || !reader.atEnd())
            {
                refuse(from, "invalid request");
                break;
            }

            const QByteArray content = mSync->pageContentMessage(page);

            if (!content.isEmpty())
                send(content, from);
            break;
        }

        case UBCollabProtocol::DocStateRequest:
            if (reader.atEnd())
                send(mSync->docStateMessage(), from);
            else
                refuse(from, "invalid request");
            break;

        default:
            break;
    }
}


// ---------------------------------------------------------------------------
// cursors
// ---------------------------------------------------------------------------

void UBCollabSession::setPointer(const QUuid& page, const QPointF& position, bool visible, bool penDown,
                                 int tool, const QColor& color, qreal width)
{
    if (mState != Active)
        return;

    // nothing is sent while the pointer does not move
    if (visible == mPointer.visible && page == mPointer.page && position == mPointer.position
            && penDown == mPointer.penDown)
    {
        return;
    }

    if (!visible && !mPointerSent.visible)
        return;

    if (penDown && visible)
        mPointerStroke << position;

    mPointer.page = page;
    mPointer.position = position;
    mPointer.visible = visible;
    mPointer.penDown = penDown;
    mPointer.tool = tool;
    mPointer.color = color;
    mPointer.width = width;
    mPointerDirty = true;

    if (!mPointerTimer->isActive())
        mPointerTimer->start();

    // the points of a stroke are not lost when the pen is fast
    if (mPointerStroke.size() >= int(UBCollabProtocol::maxCursorPoints))
        sendPointerNow();
}


void UBCollabSession::sendPointerNow()
{
    if (!mPointerDirty || mState != Active)
        return;

    mPointerDirty = false;

    UBCollabWriter writer;
    UBCollabProtocol::writeHeader(writer, UBCollabProtocol::Cursor);
    writer.uuid(mPointer.page);
    writer.f64(mPointer.position.x());
    writer.f64(mPointer.position.y());
    writer.u8((mPointer.visible ? 1 : 0) | (mPointer.penDown ? 2 : 0));
    writer.u8(quint8(qBound(0, mPointer.tool, 255)));
    writer.u32(mPointer.color.rgba());
    writer.f64(mPointer.width);

    const QList<QPointF> points = mPointerStroke.mid(0, int(UBCollabProtocol::maxCursorPoints));
    mPointerStroke = mPointerStroke.mid(points.size());
    writer.u32(quint32(points.size()));

    for (const QPointF& point : points)
    {
        writer.f64(point.x());
        writer.f64(point.y());
    }

    send(writer.data(), 0);
    mPointerSent = mPointer;

    // the next update not before the interval
    if (mPointerDirty || !mPointerStroke.isEmpty())
        mPointerTimer->start();
}


void UBCollabSession::handleCursor(PeerId from, UBCollabReader& reader)
{
    Cursor cursor = mCursors.value(from);
    const bool wasDown = cursor.penDown;

    cursor.page = reader.optionalUuid();
    const double x = reader.f64();
    const double y = reader.f64();
    const quint8 flags = reader.u8();
    cursor.tool = reader.u8();
    const quint32 color = reader.u32();
    const double width = reader.f64();
    const quint32 count = reader.count(UBCollabProtocol::maxCursorPoints, 16);

    QList<QPointF> points;

    for (quint32 i = 0; i < count && reader.ok(); ++i)
    {
        const double px = reader.f64();
        const double py = reader.f64();

        if (std::fabs(px) > maxCoordinate || std::fabs(py) > maxCoordinate)
            reader.fail("cursor out of range");

        points << QPointF(px, py);
    }

    if (!reader.ok() || !reader.atEnd() || std::fabs(x) > maxCoordinate || std::fabs(y) > maxCoordinate
            || width < 0 || width > 500 || flags > 3)
    {
        refuse(from, reader.error().isEmpty() ? QString("invalid cursor") : reader.error());
        return;
    }

    cursor.position = QPointF(x, y);
    cursor.visible = flags & 1;
    cursor.penDown = flags & 2;
    cursor.color = QColor::fromRgba(color);
    cursor.width = width;
    cursor.updated = mTime.elapsed();

    if (cursor.penDown && !wasDown)
    {
        cursor.stroke.clear();
        cursor.strokeEnded = -1;
    }

    // a stroke on the page that is shown, bounded like everything else
    if (cursor.stroke.size() + points.size() <= 20000)
        cursor.stroke << points;

    if (!cursor.penDown && wasDown)
        cursor.strokeEnded = cursor.updated;

    mCursors.insert(from, cursor);
    emit cursorsChanged();
}


void UBCollabSession::expireCursors()
{
    const qint64 now = mTime.elapsed();
    bool changed = false;

    for (auto it = mCursors.begin(); it != mCursors.end(); ++it)
    {
        if (it->visible && now - it->updated > cursorIdleMs)
        {
            it->visible = false;
            changed = true;
        }

        if (it->strokeEnded >= 0 && now - it->strokeEnded > strokeLingerMs)
        {
            it->stroke.clear();
            it->strokeEnded = -1;
            changed = true;
        }
    }

    if (changed)
        emit cursorsChanged();
}


// ---------------------------------------------------------------------------
// the end
// ---------------------------------------------------------------------------

void UBCollabSession::leave()
{
    if (mState == Ended)
        return;

    // what the user did last goes before the goodbye
    if (mSync && mState == Active)
        mSync->scanNow();

    sendBye(mRole == Host ? UBCollabProtocol::HostEnded : UBCollabProtocol::Leaving);

    // the goodbye has a moment to leave before the line is closed
    QPointer<UBCollabChannel> channel = mChannel;
    end(Left);

    if (channel)
    {
        QTimer::singleShot(300, channel, [channel]() {
            if (channel && channel->state() != UBCollabChannel::Closed)
                channel->close();
        });
    }
}


void UBCollabSession::end(EndReason reason, const QString& detail)
{
    if (mState == Ended)
        return;

    mEndReason = reason;

    if (!detail.isEmpty())
        mEndDetail = detail;

    // what the user did last is saved; everybody keeps his copy
    if (mSync)
        mSync->stop();

    if (mReceiver)
    {
        mReceiver->abort();
        delete mReceiver;
        mReceiver = nullptr;
    }

    mState = Ended;
    mQueue.clear();
    mEarly.clear();
    mSnapshotSends.clear();
    mCursors.clear();
    mPumpTimer->stop();
    mDigestTimer->stop();
    mPointerTimer->stop();
    mCursorTimer->stop();
    mJoinTimer->stop();

    if (reason != Left)
        closeChannel();

    emit stateChanged();
    emit cursorsChanged();
    emit ended(reason);
}


void UBCollabSession::closeChannel()
{
    if (!mChannel)
        return;

    disconnect(mChannel.data(), nullptr, this, nullptr);

    if (mChannel->state() != UBCollabChannel::Closed)
        mChannel->close();
}
