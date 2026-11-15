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

#include "UBCollabDocumentSync.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsItem>
#include <QTimer>
#include <QUndoStack>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <algorithm>

#include "board/UBBoardController.h"
#include "core/UBApplication.h"
#include "core/UBPersistenceManager.h"
#include "document/UBDocument.h"
#include "document/UBDocumentProxy.h"
#include "document/UBDocumentToc.h"
#include "domain/UBGraphicsScene.h"
#include "domain/UBGraphicsTextItem.h"
#include "gui/UBBackgroundManager.h"
#include "gui/UBBackgroundRuling.h"

#include "UBCollabBoardLink.h"
#include "UBCollabSvgFilter.h"

#include "core/memcheck.h"

namespace
{
    // a text that is being typed is sent after this pause, or when the typing ends
    constexpr qint64 textPauseMs = 1200;

    // the smallest encoded size of an operation, to check counts against the message size
    constexpr qsizetype minItemOpBytes = 1 + 16 + 12 + 12;
    constexpr qsizetype minPageRegisterBytes = 16 + 12 + 12 + 1 + 4 + 12;

    // unknown backgrounds that another participant may add to the list of this computer
    constexpr int maxRulingsPerSession = 16;

    // far below the limit of the reader (2^60), far above what counting can reach
    constexpr quint64 maxObservedClock = Q_UINT64_C(1) << 56;

    QGraphicsItem* findShared(UBGraphicsScene* scene, const QUuid& uuid)
    {
        if (!scene)
            return nullptr;

        QGraphicsItem* item = scene->itemForUuid(uuid);

        if (item && item->scene() == scene && !item->parentItem()
                && UBCollabItems::uuidOf(item) == uuid && UBCollabItems::kindOf(item, scene) != UBCollabProtocol::NoKind)
        {
            return item;
        }

        return UBCollabItems::sharedItems(scene).value(uuid, nullptr);
    }

    bool keyLess(const QByteArray& keyA, const QUuid& a, const QByteArray& keyB, const QUuid& b)
    {
        if (keyA != keyB)
            return keyA < keyB;

        return a < b;
    }

    void writeVersion(QCryptographicHash& hash, const UBCollabVersion& version)
    {
        UBCollabWriter writer;
        writer.version(version);
        hash.addData(writer.data());
    }

    QString deferredKey(const QUuid& page, const QUuid& item)
    {
        return page.toString() + item.toString();
    }
}


UBCollabDocumentSync::UBCollabDocumentSync(std::shared_ptr<UBDocumentProxy> document, QObject* parent)
    : QObject(parent)
    , mProxy(document)
    , mScanTimer(new QTimer(this))
    , mPeriodicTimer(new QTimer(this))
    , mPersistTimer(new QTimer(this))
{
    mTime.start();

    mScanTimer->setSingleShot(true);
    connect(mScanTimer, &QTimer::timeout, this, &UBCollabDocumentSync::scanNow);

    // texts being typed and changes that come without an undo step
    mPeriodicTimer->setInterval(500);
    connect(mPeriodicTimer, &QTimer::timeout, this, &UBCollabDocumentSync::scanNow);

    mPersistTimer->setSingleShot(true);
    connect(mPersistTimer, &QTimer::timeout, this, [this]() { persistModifiedPages(false); });
}


UBCollabDocumentSync::~UBCollabDocumentSync()
{
    if (mRunning)
        stop();
}


void UBCollabDocumentSync::writePageRegisters(UBCollabWriter& writer, const PageRegisters& page, bool withItems)
{
    writer.uuid(page.uuid);
    writer.version(page.incarnation);
    writer.version(page.version);
    writer.boolean(page.alive);
    writer.bytes(page.key);
    writer.version(page.propsVersion);

    if (!withItems)
        return;

    writer.u32(quint32(page.items.size()));

    for (auto it = page.items.constBegin(); it != page.items.constEnd(); ++it)
    {
        writer.uuid(it.key());
        writer.version(it.value().incarnation);
        writer.version(it.value().version);
        writer.boolean(it.value().alive);
    }
}


bool UBCollabDocumentSync::readPageRegisters(UBCollabReader& reader, PageRegisters& page, bool withItems)
{
    page.uuid = reader.uuid();
    page.incarnation = reader.version();
    page.version = reader.version();
    page.alive = reader.boolean();
    page.key = reader.bytes(UBCollabProtocol::maxOrderKeyBytes);
    page.propsVersion = reader.version();

    if (reader.ok() && !UBCollabOrderKey::isValid(page.key))
        reader.fail("invalid page order key");

    if (!withItems || !reader.ok())
        return reader.ok();

    const quint32 count = reader.count(UBCollabProtocol::maxTombstonesPerPage, 16 + 12 + 12 + 1);

    for (quint32 i = 0; i < count && reader.ok(); ++i)
    {
        const QUuid uuid = reader.uuid();
        ItemRegister reg;
        reg.incarnation = reader.version();
        reg.version = reader.version();
        reg.alive = reader.boolean();

        if (reader.ok())
            page.items.insert(uuid, reg);
    }

    return reader.ok();
}


// ---------------------------------------------------------------------------
// state
// ---------------------------------------------------------------------------

UBCollabVersion UBCollabDocumentSync::nextVersion()
{
    UBCollabVersion version;
    version.counter = ++mClock;
    version.peer = mPeer;
    return version;
}


void UBCollabDocumentSync::observe(const UBCollabVersion& version)
{
    // a clock pushed near the end of its range by someone would make the versions of
    // this participant unreadable for the others: such a value does not move the clock
    if (version.counter > maxObservedClock)
        return;

    if (version.counter > mClock)
        mClock = version.counter;
}


int UBCollabDocumentSync::pageIndex(const QUuid& page) const
{
    if (!mDocument || page.isNull())
        return -1;

    return mDocument->toc()->findUuid(page);
}


QList<QUuid> UBCollabDocumentSync::documentOrder() const
{
    QList<QUuid> order;

    if (!mDocument)
        return order;

    UBDocumentToc* toc = mDocument->toc();

    for (int i = 0; i < toc->pageCount(); ++i)
        order << toc->uuid(i);

    return order;
}


std::shared_ptr<UBGraphicsScene> UBCollabDocumentSync::sceneForPage(const QUuid& page)
{
    if (mLink)
    {
        std::shared_ptr<UBGraphicsScene> edited = mLink->editedScene(mProxy);

        if (edited && edited->uuid() == page)
            return edited;
    }

    const int index = pageIndex(page);

    if (index < 0)
        return nullptr;

    return mDocument->loadScene(index, false);
}


UBCollabDocumentSync::PageState& UBCollabDocumentSync::pageState(const QUuid& page)
{
    PageState& state = mPages[page];
    state.reg.uuid = page;
    return state;
}


void UBCollabDocumentSync::registerAllPages()
{
    const QList<QUuid> order = documentOrder();
    const QList<QByteArray> keys = UBCollabOrderKey::sequence(int(order.size()));

    for (int i = 0; i < order.size(); ++i)
    {
        if (order.at(i).isNull())
            continue;

        PageState& page = pageState(order.at(i));
        page.reg.alive = true;
        page.reg.key = keys.at(i);
    }

    mShadowOrder = order;
}


void UBCollabDocumentSync::start()
{
    mDocument = UBDocument::getDocument(mProxy);
    mLink = UBCollabBoardLink::instance();

    registerAllPages();

    mCurrentPage = mLink->shownPage(mProxy);

    if (mCurrentPage.isNull() && !mShadowOrder.isEmpty())
        mCurrentPage = mShadowOrder.first();

    mCurrentPageVersion = UBCollabVersion();

    startRunning();
}


void UBCollabDocumentSync::startFromSnapshot(const QList<PageRegisters>& pages, quint64 clock,
                                             const QUuid& currentPage, const UBCollabVersion& currentPageVersion)
{
    mDocument = UBDocument::getDocument(mProxy);
    mLink = UBCollabBoardLink::instance();

    for (const PageRegisters& reg : pages)
    {
        PageState& page = pageState(reg.uuid);
        page.reg = reg;

        for (const ItemRegister& item : reg.items)
        {
            observe(item.incarnation);
            observe(item.version);
        }

        observe(reg.incarnation);
        observe(reg.version);
        observe(reg.propsVersion);
    }

    mShadowOrder = documentOrder();
    UBCollabVersion snapshotClock;
    snapshotClock.counter = clock;
    observe(snapshotClock);
    observe(currentPageVersion);
    mCurrentPage = currentPage;
    mCurrentPageVersion = currentPageVersion;

    startRunning();
}


void UBCollabDocumentSync::startRunning()
{
    // the others have the files of the document from the initial board
    const QString root = mProxy->persistencePath() + "/";

    for (const QString& folder : {UBPersistenceManager::imageDirectory, UBPersistenceManager::objectDirectory})
    {
        for (const QString& name : QDir(root + folder).entryList(QDir::Files))
            mSentAssets.insert(folder + "/" + name);
    }

    if (UBApplication::undoStack)
        connect(UBApplication::undoStack.data(), &QUndoStack::indexChanged, this, &UBCollabDocumentSync::scheduleScan);

    UBPersistenceManager* persistence = UBPersistenceManager::persistenceManager();
    connect(persistence, &UBPersistenceManager::documentSceneDuplicated, this, &UBCollabDocumentSync::scheduleScan);
    connect(persistence, &UBPersistenceManager::documentSceneMoved, this, &UBCollabDocumentSync::scheduleScan);
    connect(persistence, &UBPersistenceManager::documentSceneDeleted, this, &UBCollabDocumentSync::scheduleScan);

    if (UBApplication::boardController)
        connect(UBApplication::boardController, &UBBoardController::activeSceneChanged, this, &UBCollabDocumentSync::scheduleScan);

    connect(mLink.data(), &UBCollabBoardLink::gestureEnded, this, &UBCollabDocumentSync::scheduleScan);

    mRunning = true;
    mPeriodicTimer->start();

    // the page on the board is the starting point: nothing is sent for it
    scanNow();
}


void UBCollabDocumentSync::stop()
{
    if (!mRunning)
        return;

    // what the user did last is sent before leaving
    scanNow();

    mRunning = false;
    mScanTimer->stop();
    mPeriodicTimer->stop();

    if (UBApplication::undoStack)
        disconnect(UBApplication::undoStack.data(), nullptr, this, nullptr);

    disconnect(UBPersistenceManager::persistenceManager(), nullptr, this, nullptr);

    if (UBApplication::boardController)
        disconnect(UBApplication::boardController, nullptr, this, nullptr);

    if (mLink)
        disconnect(mLink.data(), nullptr, this, nullptr);

    // saved now: the session may end because the application quits
    persistModifiedPages(true);

    if (mLink)
        mLink->saveShownPage(mProxy);
}


bool UBCollabDocumentSync::isDocumentMessage(quint8 type)
{
    switch (type)
    {
        case UBCollabProtocol::Asset:
        case UBCollabProtocol::ItemOps:
        case UBCollabProtocol::PageOp:
        case UBCollabProtocol::PageProps:
        case UBCollabProtocol::CurrentPage:
        case UBCollabProtocol::PageContent:
        case UBCollabProtocol::DocState:
            return true;

        default:
            return false;
    }
}


// ---------------------------------------------------------------------------
// changes of the user
// ---------------------------------------------------------------------------

void UBCollabDocumentSync::scheduleScan()
{
    if (mRunning && !mScanTimer->isActive())
        mScanTimer->start(0);
}


void UBCollabDocumentSync::scanNow()
{
    if (!mRunning || mApplying || !mDocument)
        return;

    scanPages();

    std::shared_ptr<UBGraphicsScene> scene = mLink ? mLink->editedScene(mProxy) : nullptr;

    // the page the user worked on until the board turned to another one: what he did last is sent
    std::shared_ptr<UBGraphicsScene> last = mLastEdited.lock();

    if (last && last != scene && pageIndex(last->uuid()) >= 0)
        scanItems(last);

    mLastEdited = scene;

    if (scene && pageIndex(scene->uuid()) >= 0 && !mLink->isGestureActive())
        scanItems(scene);

    applyDeferredTexts();
}


void UBCollabDocumentSync::scanPageNow(const QUuid& page)
{
    if (!mRunning || mApplying || !mDocument)
        return;

    scanPages();

    std::shared_ptr<UBGraphicsScene> scene = sceneForPage(page);

    if (scene && !(mLink && mLink->editedScene(mProxy) == scene && mLink->isGestureActive()))
        scanItems(scene);
}


void UBCollabDocumentSync::scanPages()
{
    const QList<QUuid> order = documentOrder();

    if (order == mShadowOrder)
        return;

    const QSet<QUuid> before(mShadowOrder.begin(), mShadowOrder.end());
    const QSet<QUuid> now(order.begin(), order.end());

    // removed pages
    for (const QUuid& uuid : std::as_const(mShadowOrder))
    {
        if (now.contains(uuid) || !mPages.contains(uuid))
            continue;

        PageState& page = mPages[uuid];

        if (!page.reg.alive)
            continue;

        page.reg.alive = false;
        page.reg.version = nextVersion();

        UBCollabWriter writer;
        UBCollabProtocol::writeHeader(writer, UBCollabProtocol::PageOp);
        writer.u8(UBCollabProtocol::RemovePage);
        writer.uuid(uuid);
        writer.version(page.reg.incarnation);
        writer.version(page.reg.version);
        writer.bytes(page.reg.key);
        emit outgoing(writer.data(), 0);
    }

    // the pages that keep their keys: the longest run in order, the others get new keys
    QList<QUuid> live;

    for (const QUuid& uuid : order)
    {
        if (uuid.isNull())
            continue;

        if (!before.contains(uuid) || (mPages.contains(uuid) && mPages.value(uuid).reg.alive))
            live << uuid;
    }

    const int n = int(live.size());
    QList<int> length(n, 0);
    QList<int> previous(n, -1);
    int best = -1;

    for (int i = 0; i < n; ++i)
    {
        if (!before.contains(live.at(i)))
            continue;

        const PageRegisters& ri = mPages.value(live.at(i)).reg;
        length[i] = 1;

        for (int j = 0; j < i; ++j)
        {
            if (length.at(j) == 0)
                continue;

            const PageRegisters& rj = mPages.value(live.at(j)).reg;

            if (keyLess(rj.key, rj.uuid, ri.key, ri.uuid) && length.at(j) + 1 > length.at(i))
            {
                length[i] = length.at(j) + 1;
                previous[i] = j;
            }
        }

        if (best < 0 || length.at(i) > length.at(best))
            best = i;
    }

    QSet<int> keep;

    for (int i = best; i >= 0; i = previous.at(i))
        keep.insert(i);

    QByteArray lastKey;

    for (int i = 0; i < n; ++i)
    {
        const QUuid uuid = live.at(i);

        if (keep.contains(i))
        {
            lastKey = mPages.value(uuid).reg.key;
            continue;
        }

        QByteArray nextKey;

        for (int j = i + 1; j < n; ++j)
        {
            if (keep.contains(j))
            {
                nextKey = mPages.value(live.at(j)).reg.key;
                break;
            }
        }

        const QByteArray key = UBCollabOrderKey::between(lastKey, nextKey);
        lastKey = key;

        if (!before.contains(uuid))
        {
            // a new page (added, duplicated, copied from another document): sent with its content
            PageState& page = pageState(uuid);
            page.reg.incarnation = nextVersion();
            page.reg.version = page.reg.incarnation;
            page.reg.alive = true;
            page.reg.key = key;
            page.reg.propsVersion = page.reg.incarnation;
            page.reg.items.clear();

            UBCollabWriter writer;
            UBCollabProtocol::writeHeader(writer, UBCollabProtocol::PageOp);
            writer.u8(UBCollabProtocol::AddPage);
            QStringList assets;

            if (writePageContent(writer, uuid, assets))
            {
                sendAssets(assets);
                emit outgoing(writer.data(), 0);
            }
            else
            {
                emit notSent(tr("a page"));
            }

            std::shared_ptr<UBGraphicsScene> scene = sceneForPage(uuid);

            if (scene)
            {
                baseline(page, scene);

                // a page that brings what is not shared (an imported PDF page, a video): told once
                for (QGraphicsItem* item : scene->items())
                {
                    const UBCollabItems::LocalOnly kind = UBCollabItems::localOnlyKind(item, scene.get());

                    if (kind != UBCollabItems::NotLocalOnly && !mNoticesShown.contains(kind))
                    {
                        mNoticesShown.insert(kind);
                        emit localOnlyAdded(kind);
                    }
                }
            }
        }
        else
        {
            PageState& page = mPages[uuid];
            page.reg.key = key;
            page.reg.version = nextVersion();

            UBCollabWriter writer;
            UBCollabProtocol::writeHeader(writer, UBCollabProtocol::PageOp);
            writer.u8(UBCollabProtocol::MovePage);
            writer.uuid(uuid);
            writer.version(page.reg.incarnation);
            writer.version(page.reg.version);
            writer.bytes(page.reg.key);
            emit outgoing(writer.data(), 0);
        }
    }

    mShadowOrder = order;
}


void UBCollabDocumentSync::baseline(PageState& page, const std::shared_ptr<UBGraphicsScene>& scene)
{
    page.shadow.clear();
    page.localOnly.clear();
    page.shadowScene = scene;

    if (!scene)
        return;

    const QHash<QUuid, QGraphicsItem*> items = UBCollabItems::sharedItems(scene.get());

    for (auto it = items.constBegin(); it != items.constEnd(); ++it)
    {
        ShadowEntry entry;
        entry.item = it.value();
        entry.fingerprint = UBCollabItems::fingerprint(it.value(), &entry.text);
        page.shadow.insert(it.key(), entry);
    }

    for (QGraphicsItem* item : scene->items())
    {
        if (UBCollabItems::localOnlyKind(item, scene.get()) != UBCollabItems::NotLocalOnly)
            page.localOnly.insert(UBCollabItems::uuidOf(item));
    }

    page.shadowProps = propsFingerprint(propsOf(scene.get()));
}


void UBCollabDocumentSync::scanItems(const std::shared_ptr<UBGraphicsScene>& scene)
{
    const QUuid pageUuid = scene->uuid();

    if (!mPages.contains(pageUuid) || !mPages.value(pageUuid).reg.alive)
        return;

    PageState& page = mPages[pageUuid];

    if (page.shadowScene.lock() != scene)
    {
        baseline(page, scene);
        return;
    }

    const QHash<QUuid, QGraphicsItem*> current = UBCollabItems::sharedItems(scene.get());
    const qint64 now = mTime.elapsed();

    QList<ItemOp> removes;
    QList<std::pair<ItemOp, QGraphicsItem*>> writes;

    for (auto it = page.shadow.constBegin(); it != page.shadow.constEnd(); ++it)
    {
        if (current.contains(it.key()))
            continue;

        const ItemRegister reg = page.reg.items.value(it.key(), ItemRegister());
        ItemOp op;
        op.op = UBCollabProtocol::RemoveItem;
        op.item = it.key();
        op.incarnation = reg.incarnation;
        op.version = nextVersion();
        removes << op;
    }

    for (auto it = current.constBegin(); it != current.constEnd(); ++it)
    {
        const QUuid uuid = it.key();
        QGraphicsItem* item = it.value();
        auto entry = page.shadow.find(uuid);
        UBCollabItems::TextCache cache = entry != page.shadow.end() ? entry->text : UBCollabItems::TextCache();
        const quint64 fp = UBCollabItems::fingerprint(item, &cache);

        if (entry != page.shadow.end() && entry->item == item && entry->fingerprint == fp)
        {
            entry->text = cache;
            continue;
        }

        // a text being typed: after a pause, or when the typing ends
        UBGraphicsTextItem* text = qgraphicsitem_cast<UBGraphicsTextItem*>(item);

        if (text && text->hasFocus())
        {
            TextEdit& edit = mTextEdits[uuid];

            if (edit.fingerprint != fp)
            {
                edit.fingerprint = fp;
                edit.since = now;
                continue;
            }

            if (now - edit.since < textPauseMs)
                continue;
        }

        mTextEdits.remove(uuid);

        const ItemRegister reg = page.reg.items.value(uuid, ItemRegister());
        const bool known = entry != page.shadow.end() || (page.reg.items.contains(uuid) && reg.alive);

        ItemOp op;
        op.item = uuid;
        op.kind = UBCollabItems::kindOf(item, scene.get());

        if (known)
        {
            op.op = UBCollabProtocol::ChangeItem;
            op.incarnation = reg.incarnation;
            op.version = nextVersion();
        }
        else
        {
            // a new item, or one that comes back (undo of a delete): a new incarnation
            op.op = UBCollabProtocol::AddItem;
            op.incarnation = nextVersion();
            op.version = op.incarnation;
        }

        ShadowEntry updated;
        updated.item = item;
        updated.fingerprint = fp;
        updated.text = cache;
        page.shadow.insert(uuid, updated);

        writes << std::make_pair(op, item);
    }

    // the removed items leave the shadow; their registers say they are gone
    for (const ItemOp& op : std::as_const(removes))
    {
        page.shadow.remove(op.item);
        ItemRegister reg;
        reg.incarnation = op.incarnation;
        reg.version = op.version;
        reg.alive = false;
        page.reg.items.insert(op.item, reg);
    }

    UBCollabWriter ops;
    int count = 0;
    QStringList assets;

    for (const ItemOp& op : std::as_const(removes))
    {
        writeItemOp(ops, op);
        ++count;
    }

    for (auto& write : writes)
    {
        ItemOp& op = write.first;
        op.fragment = UBCollabItems::fragment(mProxy, scene, write.second);

        if (op.fragment.isEmpty() || op.fragment.size() > UBCollabProtocol::maxFragmentBytes)
        {
            emit notSent(tr("an item"));
            continue;
        }

        for (const QString& asset : UBCollabItems::assets(write.second))
        {
            if (!assets.contains(asset))
                assets << asset;
        }

        ItemRegister reg;
        reg.incarnation = op.incarnation;
        reg.version = op.version;
        reg.alive = true;
        page.reg.items.insert(op.item, reg);

        writeItemOp(ops, op);
        ++count;
    }

    if (count > 0)
    {
        // the files first: they must be there when the items are read
        sendAssets(assets);

        UBCollabWriter writer;
        UBCollabProtocol::writeHeader(writer, UBCollabProtocol::ItemOps);
        writer.uuid(pageUuid);
        writer.u32(quint32(count));
        writer.raw(ops.data());
        emit outgoing(writer.data(), 0);
    }

    // background and size of the page
    const PageProps props = propsOf(scene.get());
    const quint64 propsFp = propsFingerprint(props);

    if (propsFp != page.shadowProps)
    {
        page.shadowProps = propsFp;
        page.reg.propsVersion = nextVersion();

        PageProps sent = props;
        sent.version = page.reg.propsVersion;

        UBCollabWriter writer;
        UBCollabProtocol::writeHeader(writer, UBCollabProtocol::PageProps);
        writer.uuid(pageUuid);
        writeProps(writer, sent);
        emit outgoing(writer.data(), 0);
    }

    // what is not shared: told once
    for (QGraphicsItem* item : scene->items())
    {
        const UBCollabItems::LocalOnly kind = UBCollabItems::localOnlyKind(item, scene.get());

        if (kind == UBCollabItems::NotLocalOnly)
            continue;

        const QUuid uuid = UBCollabItems::uuidOf(item);

        if (page.localOnly.contains(uuid))
            continue;

        page.localOnly.insert(uuid);

        if (!mNoticesShown.contains(kind))
        {
            mNoticesShown.insert(kind);
            emit localOnlyAdded(kind);
        }
    }
}


bool UBCollabDocumentSync::sendAssets(const QStringList& assets)
{
    bool complete = true;

    for (const QString& asset : assets)
    {
        if (mSentAssets.contains(asset) || !UBCollabSvgFilter::isSafeAssetPath(asset, UBCollabSvgFilter::Fragment))
            continue;

        QFile file(mProxy->persistencePath() + "/" + asset);

        if (file.size() > UBCollabProtocol::maxAssetBytes || !file.open(QIODevice::ReadOnly))
        {
            emit notSent(asset);
            complete = false;
            continue;
        }

        UBCollabWriter writer;
        UBCollabProtocol::writeHeader(writer, UBCollabProtocol::Asset);
        writer.string(asset);
        writer.bytes(file.readAll());
        emit outgoing(writer.data(), 0);

        mSentAssets.insert(asset);
    }

    return complete;
}


void UBCollabDocumentSync::applyDeferredTexts()
{
    if (mDeferredTexts.isEmpty())
        return;

    const QList<QString> keys = mDeferredTexts.keys();

    for (const QString& key : keys)
    {
        const std::pair<QUuid, ItemOp> deferred = mDeferredTexts.value(key);
        std::shared_ptr<UBGraphicsScene> scene = sceneForPage(deferred.first);

        if (!scene)
        {
            mDeferredTexts.remove(key);
            continue;
        }

        QGraphicsItem* item = findShared(scene.get(), deferred.second.item);

        if (item && item->hasFocus())
            continue;

        mDeferredTexts.remove(key);

        QList<QGraphicsItem*> detached;
        mApplying = true;
        applyItemOp(deferred.first, scene, deferred.second, detached, false);
        mApplying = false;
        UBCollabItems::releaseDetached(scene.get(), detached);
        pageModified(deferred.first, scene);
    }
}


// ---------------------------------------------------------------------------
// page properties
// ---------------------------------------------------------------------------

UBCollabDocumentSync::PageProps UBCollabDocumentSync::propsOf(const UBGraphicsScene* scene) const
{
    PageProps props;

    if (!scene)
        return props;

    props.dark = scene->isPageDarkBackground();
    props.gridSize = scene->backgroundGridSize();
    props.size = const_cast<UBGraphicsScene*>(scene)->nominalSize();

    const UBBackgroundRuling* ruling = scene->background();

    if (ruling)
    {
        props.ruling = ruling->uuid();

        QBuffer buffer;
        buffer.open(QIODevice::WriteOnly);
        QXmlStreamWriter writer(&buffer);
        ruling->toXml(writer);
        props.rulingXml = buffer.data();
    }

    return props;
}


quint64 UBCollabDocumentSync::propsFingerprint(const PageProps& props)
{
    UBCollabWriter writer;
    writer.boolean(props.dark);
    writer.uuid(props.ruling);
    writer.i32(props.gridSize);
    writer.i32(props.size.width());
    writer.i32(props.size.height());
    return digestOf(writer.data());
}


void UBCollabDocumentSync::writeProps(UBCollabWriter& writer, const PageProps& props)
{
    writer.version(props.version);
    writer.boolean(props.dark);
    writer.uuid(props.ruling);
    writer.bytes(props.rulingXml);
    writer.i32(props.gridSize);
    writer.i32(props.size.width());
    writer.i32(props.size.height());
}


bool UBCollabDocumentSync::readProps(UBCollabReader& reader, PageProps& props)
{
    props.version = reader.version();
    props.dark = reader.boolean();
    props.ruling = reader.optionalUuid();
    props.rulingXml = reader.bytes(UBCollabProtocol::maxRulingBytes);
    props.gridSize = reader.i32();
    const int width = reader.i32();
    const int height = reader.i32();

    if (!reader.ok())
        return false;

    if (props.gridSize < 0 || props.gridSize > UBCollabProtocol::maxGridSize)
        reader.fail("invalid grid size");

    if (width < 0 || height < 0 || width > UBCollabProtocol::maxPageSide || height > UBCollabProtocol::maxPageSide)
        reader.fail("invalid page size");

    props.size = QSize(width, height);
    return reader.ok();
}


void UBCollabDocumentSync::applyProps(const std::shared_ptr<UBGraphicsScene>& scene, const PageProps& props)
{
    if (!scene || !UBApplication::boardController)
        return;

    UBBackgroundManager* backgrounds = UBApplication::boardController->backgroundManager();
    const UBBackgroundRuling* ruling = backgrounds ? backgrounds->background(props.ruling) : nullptr;

    if (!ruling && backgrounds && !props.ruling.isNull() && !props.rulingXml.isEmpty() && mRulingsAdded < maxRulingsPerSession)
    {
        // a background this computer does not know yet, as a page that is opened brings it
        QXmlStreamReader reader(props.rulingXml);

        if (reader.readNextStartElement())
        {
            UBBackgroundRuling added;

            if (added.parseXml(reader) && added.isValid() && added.uuid() == props.ruling)
            {
                backgrounds->addBackground(added);
                ++mRulingsAdded;
                ruling = backgrounds->background(props.ruling);
            }
        }
    }

    if (!ruling)
        ruling = scene->background();

    if (mLink)
    {
        mLink->backgroundChanged(scene.get(), props.dark, ruling, props.gridSize);

        if (props.size.isValid() && !props.size.isEmpty() && props.size != scene->nominalSize())
            mLink->pageSizeChanged(scene.get(), props.size);
    }

    scene->setModified(true);
}


// ---------------------------------------------------------------------------
// changes of the others
// ---------------------------------------------------------------------------

bool UBCollabDocumentSync::handleMessage(PeerId from, quint8 type, UBCollabReader& reader)
{
    Q_UNUSED(from);

    if (!mRunning)
        return true;

    // what the user did is sent first, then the others' changes are applied over it
    if (isDocumentMessage(type))
        scanNow();

    mApplying = true;
    bool ok = true;

    switch (type)
    {
        case UBCollabProtocol::Asset:
            ok = handleAsset(reader);
            break;
        case UBCollabProtocol::ItemOps:
            ok = handleItemOps(reader);
            break;
        case UBCollabProtocol::PageOp:
            ok = handlePageOp(reader);
            break;
        case UBCollabProtocol::PageProps:
            ok = handlePageProps(reader);
            break;
        case UBCollabProtocol::CurrentPage:
            ok = handleCurrentPage(reader);
            break;
        case UBCollabProtocol::PageContent:
            ok = readPageContent(reader, true);
            break;
        case UBCollabProtocol::DocState:
            ok = handleDocState(reader);
            break;
        default:
            reader.fail("not a message about the document");
            ok = false;
            break;
    }

    mApplying = false;

    if (ok && !reader.atEnd())
    {
        reader.fail("unexpected data at the end of the message");
        ok = false;
    }

    return ok;
}


bool UBCollabDocumentSync::handleAsset(UBCollabReader& reader)
{
    const QString path = reader.string(UBCollabProtocol::maxAssetPathBytes);
    const QByteArray data = reader.bytes(UBCollabProtocol::maxAssetBytes);

    if (!reader.ok())
        return false;

    if (!UBCollabSvgFilter::isSafeAssetPath(path, UBCollabSvgFilter::Fragment))
    {
        reader.fail("file name not allowed: " + path.left(80));
        return false;
    }

    storeAsset(path, data);
    mSentAssets.insert(path);
    return true;
}


bool UBCollabDocumentSync::storeAsset(const QString& path, const QByteArray& data)
{
    const QString target = mProxy->persistencePath() + "/" + path;

    // file names are made from the content: a file that is there already is the same file
    if (QFile::exists(target))
        return true;

    QDir().mkpath(QFileInfo(target).absolutePath());
    QFile file(target);

    if (!file.open(QIODevice::WriteOnly))
    {
        qWarning() << "collaboration: cannot write" << target;
        return false;
    }

    file.write(data);
    return true;
}


bool UBCollabDocumentSync::readItemOp(UBCollabReader& reader, ItemOp& op, bool withContent)
{
    op.op = reader.u8();
    op.item = reader.uuid();
    op.incarnation = reader.version();
    op.version = reader.version();

    if (!reader.ok())
        return false;

    if (op.op < UBCollabProtocol::AddItem || op.op > UBCollabProtocol::RemoveItem)
    {
        reader.fail(QString("unknown item operation %1").arg(op.op));
        return false;
    }

    if (op.op != UBCollabProtocol::RemoveItem && withContent)
    {
        op.kind = reader.u8();
        op.fragment = reader.bytes(UBCollabProtocol::maxFragmentBytes);

        if (reader.ok() && (op.kind == UBCollabProtocol::NoKind || op.kind > UBCollabProtocol::Group))
            reader.fail(QString("unknown item kind %1").arg(op.kind));
    }

    return reader.ok();
}


void UBCollabDocumentSync::writeItemOp(UBCollabWriter& writer, const ItemOp& op)
{
    writer.u8(op.op);
    writer.uuid(op.item);
    writer.version(op.incarnation);
    writer.version(op.version);

    if (op.op != UBCollabProtocol::RemoveItem)
    {
        writer.u8(op.kind);
        writer.bytes(op.fragment);
    }
}


bool UBCollabDocumentSync::handleItemOps(UBCollabReader& reader)
{
    const QUuid pageUuid = reader.uuid();
    const quint32 count = reader.count(UBCollabProtocol::maxOpsPerMessage, minItemOpBytes);

    QList<ItemOp> ops;

    for (quint32 i = 0; i < count && reader.ok(); ++i)
    {
        ItemOp op;

        if (readItemOp(reader, op, true))
            ops << op;
    }

    if (!reader.ok())
        return false;

    for (const ItemOp& op : std::as_const(ops))
    {
        observe(op.incarnation);
        observe(op.version);
    }

    if (!mPages.contains(pageUuid))
    {
        // a page this computer does not have (its add was lost): ask for it
        emit pageNeedsResync(pageUuid);
        return true;
    }

    // a removed page: removal wins over the changes on it
    if (!mPages.value(pageUuid).reg.alive)
        return true;

    std::shared_ptr<UBGraphicsScene> scene = sceneForPage(pageUuid);

    if (!scene)
    {
        emit pageNeedsResync(pageUuid);
        return true;
    }

    QList<QGraphicsItem*> detached;
    bool ok = true;

    for (const ItemOp& op : std::as_const(ops))
    {
        if (!applyItemOp(pageUuid, scene, op, detached, false))
        {
            reader.fail("invalid item");
            ok = false;
            break;
        }
    }

    UBCollabItems::releaseDetached(scene.get(), detached);

    if (mLink && mLink->editedScene(mProxy) == scene)
        scene->updateSelectionFrame();

    pageModified(pageUuid, scene);
    return ok;
}


bool UBCollabDocumentSync::applyItemOp(const QUuid& pageUuid, const std::shared_ptr<UBGraphicsScene>& scene,
                                       const ItemOp& op, QList<QGraphicsItem*>& detached, bool authoritative)
{
    PageState& page = pageState(pageUuid);
    QGraphicsItem* existing = findShared(scene.get(), op.item);

    const UBCollabRules::Decision decision = UBCollabRules::decide(page.reg.items.contains(op.item), page.reg.items.value(op.item),
                                                                   existing != nullptr, op.op, op.incarnation, op.version,
                                                                   authoritative);

    switch (decision.action)
    {
        case UBCollabRules::Ignore:
            return true;

        case UBCollabRules::Store:
            page.reg.items.insert(op.item, decision.reg);
            return true;

        case UBCollabRules::Remove:
        {
            if (existing)
            {
                if (existing->hasFocus())
                    existing->clearFocus();

                UBCollabItems::remove(scene.get(), existing);
            }

            shadowUpdate(page, scene, op.item, nullptr);
            page.reg.items.insert(op.item, decision.reg);
            return true;
        }

        case UBCollabRules::Apply:
            break;
    }

    // a text the user is typing in keeps his text until he is done; the last writer wins then
    if (existing && existing->hasFocus() && qgraphicsitem_cast<UBGraphicsTextItem*>(existing)
            && mLink && mLink->editedScene(mProxy) == scene)
    {
        const QString key = deferredKey(pageUuid, op.item);

        if (!mDeferredTexts.contains(key) || mDeferredTexts.value(key).second.version < op.version)
            mDeferredTexts.insert(key, std::make_pair(pageUuid, op));

        return true;
    }

    const ContentResult result = applyContent(pageUuid, scene, op, detached);

    if (result == ContentInvalid)
        return false;

    // the item could not be made yet (a file is on its way again): its register stays as it was
    if (result == ContentWaiting)
        return true;

    if (!decision.reg.incarnation.isZero() || !decision.reg.version.isZero())
        page.reg.items.insert(op.item, decision.reg);
    else
        page.reg.items.remove(op.item);

    return true;
}


UBCollabDocumentSync::ContentResult UBCollabDocumentSync::applyContent(const QUuid& pageUuid, const std::shared_ptr<UBGraphicsScene>& scene,
                                                                       const ItemOp& op, QList<QGraphicsItem*>& detached)
{
    PageState& page = pageState(pageUuid);
    QGraphicsItem* existing = findShared(scene.get(), op.item);

    UBCollabItems::Parsed parsed = UBCollabItems::parse(mProxy, op.fragment, op.item,
                                                        static_cast<UBCollabProtocol::ItemKind>(op.kind));

    if (!parsed.isValid())
    {
        if (parsed.missingAsset)
        {
            // a file did not arrive: the page is fetched again with its files
            qWarning() << "collaboration: item" << op.item << "waits for a file:" << parsed.error;
            emit pageNeedsResync(pageUuid);
            return ContentWaiting;
        }

        qWarning() << "collaboration: item refused:" << parsed.error;
        return ContentInvalid;
    }

    QGraphicsItem* item = parsed.item;

    if (existing && UBCollabItems::update(scene.get(), existing, parsed, detached))
    {
        item = existing;
        scene->notifyZChanged(existing, existing->data(UBGraphicsItemData::ItemOwnZValue).toReal());
    }
    else
    {
        if (existing)
            UBCollabItems::remove(scene.get(), existing);

        UBCollabItems::insert(scene.get(), parsed);
    }

    shadowUpdate(page, scene, op.item, item);
    return ContentApplied;
}


void UBCollabDocumentSync::shadowUpdate(PageState& page, const std::shared_ptr<UBGraphicsScene>& scene, const QUuid& uuid, QGraphicsItem* item)
{
    if (page.shadowScene.lock() != scene)
        return;

    if (!item)
    {
        page.shadow.remove(uuid);
        return;
    }

    ShadowEntry entry;
    entry.item = item;
    entry.fingerprint = UBCollabItems::fingerprint(item, &entry.text);
    page.shadow.insert(uuid, entry);
    mTextEdits.remove(uuid);
}


bool UBCollabDocumentSync::handlePageOp(UBCollabReader& reader)
{
    const quint8 kind = reader.u8();

    if (!reader.ok())
        return false;

    if (kind == UBCollabProtocol::AddPage)
        return readPageContent(reader, false);

    if (kind != UBCollabProtocol::RemovePage && kind != UBCollabProtocol::MovePage)
    {
        reader.fail(QString("unknown page operation %1").arg(kind));
        return false;
    }

    const QUuid uuid = reader.uuid();
    const UBCollabVersion incarnation = reader.version();
    const UBCollabVersion version = reader.version();
    const QByteArray key = reader.bytes(UBCollabProtocol::maxOrderKeyBytes);

    if (!reader.ok())
        return false;

    if (!UBCollabOrderKey::isValid(key))
    {
        reader.fail("invalid page order key");
        return false;
    }

    observe(incarnation);
    observe(version);

    const QUuid shownBefore = mLink ? mLink->shownPage(mProxy) : QUuid();
    const bool known = mPages.contains(uuid);
    PageState& page = pageState(uuid);

    // the same rules as for items: the key is the content of a page in the list
    UBCollabRegister here;
    here.incarnation = page.reg.incarnation;
    here.version = page.reg.version;
    here.alive = page.reg.alive;

    const UBCollabRules::Decision decision = UBCollabRules::decide(known, here, known && here.alive,
                                                                   kind == UBCollabProtocol::RemovePage ? UBCollabProtocol::RemoveItem
                                                                                                        : UBCollabProtocol::ChangeItem,
                                                                   incarnation, version, false);

    switch (decision.action)
    {
        case UBCollabRules::Ignore:
            return true;

        case UBCollabRules::Store:
        case UBCollabRules::Remove:
            if (decision.reg.incarnation != page.reg.incarnation)
                page.reg.items.clear();

            page.reg.incarnation = decision.reg.incarnation;
            page.reg.version = decision.reg.version;
            page.reg.alive = false;

            if (page.reg.key.isEmpty())
                page.reg.key = key;
            break;

        case UBCollabRules::Apply:
        {
            const bool newIncarnation = !known || decision.reg.incarnation != page.reg.incarnation;

            page.reg.incarnation = decision.reg.incarnation;
            page.reg.version = decision.reg.version;
            page.reg.alive = true;
            page.reg.key = key;

            // a page of which this computer missed the add: it is fetched
            if (newIncarnation || pageIndex(uuid) < 0)
                emit pageNeedsResync(uuid);
            break;
        }
    }

    reconcilePages(shownBefore);
    return true;
}


bool UBCollabDocumentSync::handlePageProps(UBCollabReader& reader)
{
    const QUuid pageUuid = reader.uuid();
    PageProps props;

    if (!readProps(reader, props))
        return false;

    observe(props.version);

    if (!mPages.contains(pageUuid) || !mPages.value(pageUuid).reg.alive)
        return true;

    PageState& page = mPages[pageUuid];

    if (props.version <= page.reg.propsVersion)
        return true;

    std::shared_ptr<UBGraphicsScene> scene = sceneForPage(pageUuid);

    if (!scene)
        return true;

    applyProps(scene, props);
    page.reg.propsVersion = props.version;

    if (page.shadowScene.lock() == scene)
        page.shadowProps = propsFingerprint(propsOf(scene.get()));

    pageModified(pageUuid, scene);
    return true;
}


bool UBCollabDocumentSync::handleCurrentPage(UBCollabReader& reader)
{
    const QUuid page = reader.uuid();
    const UBCollabVersion version = reader.version();

    if (!reader.ok())
        return false;

    observe(version);

    if (version > mCurrentPageVersion)
    {
        mCurrentPage = page;
        mCurrentPageVersion = version;
        emit remoteCurrentPage(page);
    }

    return true;
}


void UBCollabDocumentSync::setLocalCurrentPage(const QUuid& page)
{
    if (!mRunning || page.isNull() || page == mCurrentPage)
        return;

    // a page that was just added goes first, so that the others can turn to it
    if (!mApplying)
        scanPages();

    mCurrentPage = page;
    mCurrentPageVersion = nextVersion();

    UBCollabWriter writer;
    UBCollabProtocol::writeHeader(writer, UBCollabProtocol::CurrentPage);
    writer.uuid(page);
    writer.version(mCurrentPageVersion);
    emit outgoing(writer.data(), 0);
}


bool UBCollabDocumentSync::handleDocState(UBCollabReader& reader)
{
    const quint32 count = reader.count(UBCollabProtocol::maxPages * 4, minPageRegisterBytes);
    QList<PageRegisters> pages;

    for (quint32 i = 0; i < count && reader.ok(); ++i)
    {
        PageRegisters reg;

        if (readPageRegisters(reader, reg, false))
            pages << reg;
    }

    const QUuid current = reader.optionalUuid();
    const UBCollabVersion currentVersion = reader.version();

    if (!reader.ok())
        return false;

    const QUuid shownBefore = mLink ? mLink->shownPage(mProxy) : QUuid();

    for (const PageRegisters& reg : std::as_const(pages))
    {
        observe(reg.incarnation);
        observe(reg.version);
        observe(reg.propsVersion);

        const bool known = mPages.contains(reg.uuid);
        PageState& page = pageState(reg.uuid);

        const bool otherIncarnation = !known || page.reg.incarnation != reg.incarnation;
        const bool otherProps = page.reg.propsVersion != reg.propsVersion;

        page.reg.incarnation = reg.incarnation;
        page.reg.version = reg.version;
        page.reg.alive = reg.alive;
        page.reg.key = reg.key;

        if (reg.alive && (otherIncarnation || otherProps || pageIndex(reg.uuid) < 0))
            emit pageNeedsResync(reg.uuid);
    }

    observe(currentVersion);

    if (currentVersion > mCurrentPageVersion && !current.isNull())
    {
        mCurrentPage = current;
        mCurrentPageVersion = currentVersion;
        emit remoteCurrentPage(current);
    }

    reconcilePages(shownBefore);
    return true;
}


// ---------------------------------------------------------------------------
// whole pages
// ---------------------------------------------------------------------------

bool UBCollabDocumentSync::writePageContent(UBCollabWriter& writer, const QUuid& pageUuid, QStringList& assets)
{
    PageState& page = pageState(pageUuid);
    std::shared_ptr<UBGraphicsScene> scene = page.reg.alive ? sceneForPage(pageUuid) : nullptr;

    writePageRegisters(writer, page.reg, false);

    PageProps props = scene ? propsOf(scene.get()) : PageProps();
    props.version = page.reg.propsVersion;
    writeProps(writer, props);

    UBCollabWriter items;
    quint32 itemCount = 0;

    if (scene)
    {
        const QHash<QUuid, QGraphicsItem*> shared = UBCollabItems::sharedItems(scene.get());

        for (auto it = shared.constBegin(); it != shared.constEnd(); ++it)
        {
            const ItemRegister reg = page.reg.items.value(it.key(), ItemRegister());
            ItemOp op;
            op.op = UBCollabProtocol::AddItem;
            op.item = it.key();
            op.incarnation = reg.incarnation;
            op.version = reg.version;
            op.kind = UBCollabItems::kindOf(it.value(), scene.get());
            op.fragment = UBCollabItems::fragment(mProxy, scene, it.value());

            if (op.fragment.isEmpty() || op.fragment.size() > UBCollabProtocol::maxFragmentBytes)
                continue;

            for (const QString& asset : UBCollabItems::assets(it.value()))
            {
                if (!assets.contains(asset))
                    assets << asset;
            }

            writeItemOp(items, op);
            ++itemCount;
        }
    }

    writer.u32(itemCount);
    writer.raw(items.data());

    // the registers of removed items, so that a late operation on them is not taken for a new item
    UBCollabWriter tombstones;
    quint32 tombCount = 0;

    for (auto it = page.reg.items.constBegin(); it != page.reg.items.constEnd(); ++it)
    {
        if (it.value().alive)
            continue;

        tombstones.uuid(it.key());
        tombstones.version(it.value().incarnation);
        tombstones.version(it.value().version);
        ++tombCount;
    }

    writer.u32(tombCount);
    writer.raw(tombstones.data());

    // the files go with the page, for a computer that has none of them yet
    UBCollabWriter files;
    quint32 fileCount = 0;

    for (const QString& asset : std::as_const(assets))
    {
        QFile file(mProxy->persistencePath() + "/" + asset);

        if (file.size() > UBCollabProtocol::maxAssetBytes || !file.open(QIODevice::ReadOnly))
            continue;

        files.string(asset);
        files.bytes(file.readAll());
        ++fileCount;
    }

    writer.u32(fileCount);
    writer.raw(files.data());
    return true;
}


bool UBCollabDocumentSync::readAssetList(UBCollabReader& reader)
{
    const quint32 count = reader.count(UBCollabProtocol::maxAssets, 8);
    QList<std::pair<QString, QByteArray>> files;

    for (quint32 i = 0; i < count && reader.ok(); ++i)
    {
        const QString path = reader.string(UBCollabProtocol::maxAssetPathBytes);
        const QByteArray data = reader.bytes(UBCollabProtocol::maxAssetBytes);

        if (reader.ok() && !UBCollabSvgFilter::isSafeAssetPath(path, UBCollabSvgFilter::Fragment))
            reader.fail("file name not allowed: " + path.left(80));

        if (reader.ok())
            files << std::make_pair(path, data);
    }

    if (!reader.ok())
        return false;

    for (const auto& file : std::as_const(files))
    {
        storeAsset(file.first, file.second);
        mSentAssets.insert(file.first);
    }

    return true;
}


bool UBCollabDocumentSync::readPageContent(UBCollabReader& reader, bool authoritative)
{
    PageRegisters reg;
    PageProps props;

    readPageRegisters(reader, reg, false);
    readProps(reader, props);

    const quint32 itemCount = reader.count(UBCollabProtocol::maxItemsPerPage, minItemOpBytes);
    QList<ItemOp> items;

    for (quint32 i = 0; i < itemCount && reader.ok(); ++i)
    {
        ItemOp op;

        if (readItemOp(reader, op, true))
        {
            if (op.op != UBCollabProtocol::AddItem)
                reader.fail("a page holds only items");
            else
                items << op;
        }
    }

    const quint32 tombCount = reader.count(UBCollabProtocol::maxTombstonesPerPage, 16 + 12 + 12);
    QHash<QUuid, ItemRegister> tombstones;

    for (quint32 i = 0; i < tombCount && reader.ok(); ++i)
    {
        const QUuid uuid = reader.uuid();
        ItemRegister dead;
        dead.incarnation = reader.version();
        dead.version = reader.version();
        dead.alive = false;

        if (reader.ok())
            tombstones.insert(uuid, dead);
    }

    if (!reader.ok())
        return false;

    // the files before the items that need them
    if (!readAssetList(reader))
        return false;

    observe(reg.incarnation);
    observe(reg.version);
    observe(props.version);

    for (const ItemOp& op : std::as_const(items))
    {
        observe(op.incarnation);
        observe(op.version);
    }

    for (const ItemRegister& dead : std::as_const(tombstones))
    {
        observe(dead.incarnation);
        observe(dead.version);
    }

    const bool known = mPages.contains(reg.uuid);
    PageState& page = pageState(reg.uuid);

    if (!authoritative && known)
    {
        if (reg.incarnation < page.reg.incarnation)
            return true;

        if (reg.incarnation == page.reg.incarnation)
        {
            // the same page again: at most a newer place in the list
            if (page.reg.alive && reg.version > page.reg.version)
            {
                const QUuid shownBefore = mLink ? mLink->shownPage(mProxy) : QUuid();
                page.reg.key = reg.key;
                page.reg.version = reg.version;
                reconcilePages(shownBefore);
            }

            return true;
        }
    }

    const QUuid shownBefore = mLink ? mLink->shownPage(mProxy) : QUuid();

    page.reg.incarnation = reg.incarnation;
    page.reg.version = reg.version;
    page.reg.alive = reg.alive;
    page.reg.key = reg.key;

    if (!reg.alive)
    {
        page.reg.items = tombstones;
        reconcilePages(shownBefore);
        return true;
    }

    const bool created = pageIndex(reg.uuid) < 0;
    std::shared_ptr<UBGraphicsScene> scene = created ? createPage(reg.uuid, page.reg) : sceneForPage(reg.uuid);

    if (!scene)
    {
        qWarning() << "collaboration: cannot make page" << reg.uuid;
        return true;
    }

    if (authoritative || props.version > page.reg.propsVersion || !known)
    {
        applyProps(scene, props);
        page.reg.propsVersion = props.version;

        if (page.shadowScene.lock() == scene)
            page.shadowProps = propsFingerprint(propsOf(scene.get()));
    }

    QList<QGraphicsItem*> detached;
    QSet<QUuid> inContent;
    bool ok = true;

    for (const ItemOp& op : std::as_const(items))
    {
        inContent.insert(op.item);

        if (!applyItemOp(reg.uuid, scene, op, detached, true))
        {
            reader.fail("invalid item in a page");
            ok = false;
            break;
        }
    }

    if (ok)
    {
        // what the page of the sender does not have is not on this page either; a text the user
        // is typing in stays (it is sent when he is done)
        const QHash<QUuid, QGraphicsItem*> current = UBCollabItems::sharedItems(scene.get());

        for (auto it = current.constBegin(); it != current.constEnd(); ++it)
        {
            if (inContent.contains(it.key()) || it.value()->hasFocus())
                continue;

            UBCollabItems::remove(scene.get(), it.value());
            shadowUpdate(page, scene, it.key(), nullptr);
        }

        // the registers of the sender, exactly, for the items that are here now
        QHash<QUuid, ItemRegister> registers = tombstones;

        for (const ItemOp& op : std::as_const(items))
        {
            if (!findShared(scene.get(), op.item))
                continue;

            if (!op.incarnation.isZero() || !op.version.isZero())
            {
                ItemRegister alive;
                alive.incarnation = op.incarnation;
                alive.version = op.version;
                alive.alive = true;
                registers.insert(op.item, alive);
            }
        }

        // texts the user is typing in keep their registers until they are applied
        for (const auto& deferred : std::as_const(mDeferredTexts))
        {
            if (deferred.first == reg.uuid && page.reg.items.contains(deferred.second.item))
                registers.insert(deferred.second.item, page.reg.items.value(deferred.second.item));
        }

        page.reg.items = registers;
    }

    UBCollabItems::releaseDetached(scene.get(), detached);

    pageModified(reg.uuid, scene);
    reconcilePages(shownBefore);

    // the group may have turned to this page before it was here
    if (created && reg.uuid == mCurrentPage)
        emit remoteCurrentPage(mCurrentPage);

    return ok;
}


QByteArray UBCollabDocumentSync::pageContentMessage(const QUuid& page)
{
    if (!mPages.contains(page))
        return QByteArray();

    // the page as it is now, with what the user just did
    if (!mApplying)
        scanNow();

    UBCollabWriter writer;
    UBCollabProtocol::writeHeader(writer, UBCollabProtocol::PageContent);
    QStringList assets;
    writePageContent(writer, page, assets);
    return writer.data();
}


std::shared_ptr<UBGraphicsScene> UBCollabDocumentSync::createPage(const QUuid& pageUuid, const PageRegisters& reg)
{
    if (!mDocument)
        return nullptr;

    // after the pages that come before it
    const QList<QUuid> order = documentOrder();
    int index = 0;

    for (int i = 0; i < order.size(); ++i)
    {
        const QUuid other = order.at(i);

        if (other == pageUuid || !mPages.contains(other) || !mPages.value(other).reg.alive)
            continue;

        const PageRegisters& r = mPages.value(other).reg;

        if (keyLess(r.key, r.uuid, reg.key, pageUuid))
            index = i + 1;
    }

    // the board saves its page with the index it has: before the list changes under it
    if (mLink)
        mLink->saveShownPage(mProxy);

    std::shared_ptr<UBGraphicsScene> scene = mDocument->createPage(index, true, true, true, true);

    if (!scene)
        return nullptr;

    scene->setUuid(pageUuid);
    mDocument->toc()->setUuid(index, pageUuid);
    mDocument->toc()->save();

    mShadowOrder = documentOrder();
    return scene;
}


void UBCollabDocumentSync::reconcilePages(const QUuid& shownBefore)
{
    if (!mDocument)
        return;

    UBDocumentToc* toc = mDocument->toc();

    // The board saves the page it shows with the index it had when the list of pages
    // changes (the board follows the change afterwards): with that page saved before,
    // nothing is written to the page that now has that index.
    bool saved = false;

    auto saveShown = [this, &saved]() {
        if (!saved && mLink)
            mLink->saveShownPage(mProxy);

        saved = true;
    };

    // removed pages that are still here; the last page of a document cannot go
    for (int i = toc->pageCount() - 1; i >= 0; --i)
    {
        const QUuid uuid = toc->uuid(i);

        if (mPages.contains(uuid) && !mPages.value(uuid).reg.alive && toc->pageCount() > 1)
        {
            saveShown();
            mModifiedPages.remove(uuid);
            mDocument->deletePages({i}, false);
        }
    }

    // the order of the registers
    QList<QUuid> desired;

    for (const QUuid& uuid : documentOrder())
    {
        if (mPages.contains(uuid) && mPages.value(uuid).reg.alive)
            desired << uuid;
    }

    std::sort(desired.begin(), desired.end(), [this](const QUuid& a, const QUuid& b) {
        return keyLess(mPages.value(a).reg.key, a, mPages.value(b).reg.key, b);
    });

    for (int k = 0; k < desired.size(); ++k)
    {
        const QList<QUuid> order = documentOrder();
        QList<QUuid> knownNow;

        for (const QUuid& uuid : order)
        {
            if (mPages.contains(uuid) && mPages.value(uuid).reg.alive)
                knownNow << uuid;
        }

        if (k >= knownNow.size() || knownNow.at(k) == desired.at(k))
            continue;

        const int from = int(order.indexOf(desired.at(k)));
        const int to = int(order.indexOf(knownNow.at(k)));

        if (from >= 0 && to >= 0 && from != to)
        {
            saveShown();
            mDocument->movePage(from, to);
        }
    }

    mShadowOrder = documentOrder();

    // the host keeps a document with a page: when all pages were removed at once
    if (mHost)
    {
        bool anyAlive = false;

        for (const QUuid& uuid : std::as_const(mShadowOrder))
        {
            if (!mPages.contains(uuid) || mPages.value(uuid).reg.alive)
                anyAlive = true;
        }

        if (!anyAlive)
        {
            saveShown();
            mDocument->createPage(toc->pageCount(), true, true, true, true);
            scheduleScan();
        }
    }

    if (mLink)
        mLink->pagesChanged(mProxy, shownBefore);
}


void UBCollabDocumentSync::pageModified(const QUuid& page, const std::shared_ptr<UBGraphicsScene>& scene)
{
    if (!scene)
        return;

    scene->setModified(true);

    if (mLink && mLink->editedScene(mProxy) == scene)
        return;

    // pages that are not on the board are saved soon, so that the scene cache may let them go
    mModifiedPages.insert(page, scene);

    if (!mPersistTimer->isActive())
        mPersistTimer->start(250);
}


void UBCollabDocumentSync::persistModifiedPages(bool immediately)
{
    const QHash<QUuid, std::shared_ptr<UBGraphicsScene>> pages = mModifiedPages;
    mModifiedPages.clear();

    if (!mDocument)
        return;

    for (auto it = pages.constBegin(); it != pages.constEnd(); ++it)
    {
        const int index = pageIndex(it.key());

        if (index < 0)
            continue;

        if (mLink && mLink->editedScene(mProxy) == it.value())
            continue;

        mDocument->persistPage(it.value(), index, true, immediately, true, true);
    }
}


void UBCollabDocumentSync::resendOwnChanges()
{
    if (!mRunning || mApplying || !mDocument)
        return;

    // what the user did just now first
    scanNow();

    for (const QUuid& pageUuid : documentOrder())
    {
        if (!mPages.contains(pageUuid))
            continue;

        PageState& page = mPages[pageUuid];

        // a page this participant added or moved last
        if (page.reg.alive && page.reg.version.peer == mPeer && !page.reg.version.isZero())
        {
            UBCollabWriter writer;
            UBCollabProtocol::writeHeader(writer, UBCollabProtocol::PageOp);

            if (page.reg.incarnation == page.reg.version)
            {
                writer.u8(UBCollabProtocol::AddPage);
                QStringList assets;
                writePageContent(writer, pageUuid, assets);
                sendAssetsAgain(assets);
            }
            else
            {
                writer.u8(UBCollabProtocol::MovePage);
                writer.uuid(pageUuid);
                writer.version(page.reg.incarnation);
                writer.version(page.reg.version);
                writer.bytes(page.reg.key);
            }

            emit outgoing(writer.data(), 0);
        }

        std::shared_ptr<UBGraphicsScene> scene;
        UBCollabWriter ops;
        quint32 count = 0;
        QStringList assets;

        for (auto it = page.reg.items.constBegin(); it != page.reg.items.constEnd(); ++it)
        {
            if (it.value().version.peer != mPeer || it.value().version.isZero())
                continue;

            ItemOp op;
            op.item = it.key();
            op.incarnation = it.value().incarnation;
            op.version = it.value().version;

            if (!it.value().alive)
            {
                op.op = UBCollabProtocol::RemoveItem;
            }
            else
            {
                if (!scene)
                    scene = sceneForPage(pageUuid);

                QGraphicsItem* item = scene ? findShared(scene.get(), op.item) : nullptr;

                if (!item)
                    continue;

                op.op = op.incarnation == op.version ? UBCollabProtocol::AddItem : UBCollabProtocol::ChangeItem;
                op.kind = UBCollabItems::kindOf(item, scene.get());
                op.fragment = UBCollabItems::fragment(mProxy, scene, item);

                if (op.fragment.isEmpty() || op.fragment.size() > UBCollabProtocol::maxFragmentBytes)
                    continue;

                for (const QString& asset : UBCollabItems::assets(item))
                {
                    if (!assets.contains(asset))
                        assets << asset;
                }
            }

            writeItemOp(ops, op);
            ++count;
        }

        if (count > 0)
        {
            sendAssetsAgain(assets);

            UBCollabWriter writer;
            UBCollabProtocol::writeHeader(writer, UBCollabProtocol::ItemOps);
            writer.uuid(pageUuid);
            writer.u32(count);
            writer.raw(ops.data());
            emit outgoing(writer.data(), 0);
        }

        // the background and size of the page
        if (page.reg.alive && page.reg.propsVersion.peer == mPeer && !page.reg.propsVersion.isZero())
        {
            if (!scene)
                scene = sceneForPage(pageUuid);

            if (scene)
            {
                PageProps props = propsOf(scene.get());
                props.version = page.reg.propsVersion;

                UBCollabWriter writer;
                UBCollabProtocol::writeHeader(writer, UBCollabProtocol::PageProps);
                writer.uuid(pageUuid);
                writeProps(writer, props);
                emit outgoing(writer.data(), 0);
            }
        }
    }

    // pages this participant removed
    for (auto it = mPages.constBegin(); it != mPages.constEnd(); ++it)
    {
        const PageRegisters& reg = it.value().reg;

        if (reg.alive || reg.version.peer != mPeer || reg.version.isZero())
            continue;

        UBCollabWriter writer;
        UBCollabProtocol::writeHeader(writer, UBCollabProtocol::PageOp);
        writer.u8(UBCollabProtocol::RemovePage);
        writer.uuid(it.key());
        writer.version(reg.incarnation);
        writer.version(reg.version);
        writer.bytes(reg.key.isEmpty() ? UBCollabOrderKey::between(QByteArray(), QByteArray()) : reg.key);
        emit outgoing(writer.data(), 0);
    }

    if (mCurrentPageVersion.peer == mPeer && !mCurrentPageVersion.isZero() && !mCurrentPage.isNull())
    {
        UBCollabWriter writer;
        UBCollabProtocol::writeHeader(writer, UBCollabProtocol::CurrentPage);
        writer.uuid(mCurrentPage);
        writer.version(mCurrentPageVersion);
        emit outgoing(writer.data(), 0);
    }
}


void UBCollabDocumentSync::sendAssetsAgain(const QStringList& assets)
{
    // files that were sent may have been lost with the operations
    for (const QString& asset : assets)
        mSentAssets.remove(asset);

    sendAssets(assets);
}


// ---------------------------------------------------------------------------
// digest
// ---------------------------------------------------------------------------

quint64 UBCollabDocumentSync::digestOf(const QByteArray& data)
{
    const QByteArray hash = QCryptographicHash::hash(data, QCryptographicHash::Sha1);
    quint64 value = 0;

    for (int i = 0; i < 8; ++i)
        value = (value << 8) | quint8(hash.at(i));

    return value;
}


quint64 UBCollabDocumentSync::pageDigest(const QUuid& pageUuid) const
{
    if (!mPages.contains(pageUuid))
        return 0;

    const PageRegisters& reg = mPages.value(pageUuid).reg;
    QCryptographicHash hash(QCryptographicHash::Sha1);

    hash.addData(pageUuid.toRfc4122());
    writeVersion(hash, reg.incarnation);
    writeVersion(hash, reg.version);
    hash.addData(reg.alive ? QByteArray("a") : QByteArray("d"));

    if (!reg.alive)
        return digestOf(hash.result());

    hash.addData(reg.key);
    writeVersion(hash, reg.propsVersion);

    QList<QUuid> uuids = reg.items.keys();
    std::sort(uuids.begin(), uuids.end());

    for (const QUuid& uuid : std::as_const(uuids))
    {
        const ItemRegister& item = reg.items[uuid];
        hash.addData(uuid.toRfc4122());
        writeVersion(hash, item.incarnation);
        writeVersion(hash, item.version);
        hash.addData(item.alive ? QByteArray("a") : QByteArray("d"));
    }

    return digestOf(hash.result());
}


quint64 UBCollabDocumentSync::documentDigest() const
{
    QList<QUuid> uuids = mPages.keys();
    std::sort(uuids.begin(), uuids.end());

    QCryptographicHash hash(QCryptographicHash::Sha1);

    for (const QUuid& uuid : std::as_const(uuids))
    {
        const PageRegisters& reg = mPages[uuid].reg;
        hash.addData(uuid.toRfc4122());
        writeVersion(hash, reg.incarnation);
        writeVersion(hash, reg.version);
        hash.addData(reg.alive ? QByteArray("a") : QByteArray("d"));

        // where a removed page was in the list does not matter, and differs with the order of arrival
        if (reg.alive)
            hash.addData(reg.key);
    }

    return digestOf(hash.result());
}


QByteArray UBCollabDocumentSync::digestMessage() const
{
    UBCollabWriter writer;
    UBCollabProtocol::writeHeader(writer, UBCollabProtocol::Digest);
    writer.u64(documentDigest());
    writer.u32(quint32(mPages.size()));

    for (auto it = mPages.constBegin(); it != mPages.constEnd(); ++it)
    {
        writer.uuid(it.key());
        writer.u64(pageDigest(it.key()));
    }

    return writer.data();
}


bool UBCollabDocumentSync::compareDigest(UBCollabReader& reader, QList<QUuid>& differentPages, bool& docDiffers) const
{
    const quint64 document = reader.u64();
    const quint32 count = reader.count(UBCollabProtocol::maxPages * 4, 24);
    QSet<QUuid> seen;

    for (quint32 i = 0; i < count && reader.ok(); ++i)
    {
        const QUuid uuid = reader.uuid();
        const quint64 digest = reader.u64();

        if (!reader.ok())
            break;

        seen.insert(uuid);

        if (!mPages.contains(uuid) || pageDigest(uuid) != digest)
            differentPages << uuid;
    }

    if (!reader.ok() || !reader.atEnd())
        return false;

    docDiffers = document != documentDigest();
    return true;
}


QByteArray UBCollabDocumentSync::docStateMessage() const
{
    UBCollabWriter writer;
    UBCollabProtocol::writeHeader(writer, UBCollabProtocol::DocState);
    writer.u32(quint32(mPages.size()));

    for (const PageState& page : mPages)
        writePageRegisters(writer, page.reg, false);

    writer.uuid(mCurrentPage);
    writer.version(mCurrentPageVersion);
    return writer.data();
}


// ---------------------------------------------------------------------------
// the initial board
// ---------------------------------------------------------------------------

UBCollabDocumentSync::SnapshotPlan UBCollabDocumentSync::snapshotPlan()
{
    SnapshotPlan plan;

    if (!mDocument)
        return plan;

    // the board as it is now, saved
    scanNow();

    if (mLink)
        mLink->saveShownPage(mProxy);

    persistModifiedPages();
    UBPersistenceManager::persistenceManager()->waitForAllSaved();

    UBDocumentToc* toc = mDocument->toc();
    const QString root = mProxy->persistencePath();

    for (int i = 0; i < toc->pageCount(); ++i)
    {
        const QUuid uuid = toc->uuid(i);
        QFile file(root + UBPersistenceManager::sceneFilenameForId(toc->pageId(i)));

        if (uuid.isNull() || !file.open(QIODevice::ReadOnly))
            continue;

        const QByteArray svg = file.readAll();

        // the files the page refers to, as the receiver will see them
        const UBCollabSvgFilter::Result filtered = UBCollabSvgFilter::filter(svg, UBCollabSvgFilter::Page);

        for (const QString& asset : filtered.assets)
        {
            if (!plan.assets.contains(asset) && QFile::exists(root + "/" + asset))
            {
                plan.assets << asset;
                plan.assetSizes << QFileInfo(root + "/" + asset).size();
                plan.totalBytes += plan.assetSizes.last();
            }
        }

        UBCollabWriter writer;
        UBCollabProtocol::writeHeader(writer, UBCollabProtocol::SnapshotPage);
        writer.u32(quint32(plan.pages.size()));
        writePageRegisters(writer, pageState(uuid).reg, true);
        writer.bytes(svg);
        plan.pages << writer.data();
        plan.totalBytes += svg.size();
    }

    UBCollabWriter begin;
    UBCollabProtocol::writeHeader(begin, UBCollabProtocol::SnapshotBegin);
    begin.string(mProxy->name().left(200));
    begin.u32(quint32(plan.pages.size()));
    begin.u32(quint32(plan.assets.size()));
    begin.u64(quint64(plan.totalBytes));
    begin.u64(mClock);
    begin.uuid(mCurrentPage);
    begin.version(mCurrentPageVersion);
    plan.begin = begin.data();

    // the registers of removed pages, so that a late operation on them is not taken for a new page
    UBCollabWriter end;
    UBCollabProtocol::writeHeader(end, UBCollabProtocol::SnapshotEnd);
    QList<PageRegisters> dead;

    for (const PageState& page : std::as_const(mPages))
    {
        if (!page.reg.alive)
            dead << page.reg;
    }

    end.u32(quint32(dead.size()));

    for (const PageRegisters& reg : std::as_const(dead))
        writePageRegisters(end, reg, false);

    plan.end = end.data();
    return plan;
}


QString UBCollabDocumentSync::describeState() const
{
    int items = 0;
    int dead = 0;

    for (const PageState& page : mPages)
    {
        for (const ItemRegister& reg : page.reg.items)
        {
            if (reg.alive)
                ++items;
            else
                ++dead;
        }
    }

    return QString("pages=%1 registers=%2 removed=%3 clock=%4 digest=%5")
            .arg(mPages.size()).arg(items).arg(dead).arg(mClock).arg(documentDigest(), 16, 16, QLatin1Char('0'));
}
