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

#ifndef UBCOLLABDOCUMENTSYNC_H_
#define UBCOLLABDOCUMENTSYNC_H_

#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QSize>
#include <QStringList>
#include <QUuid>

#include <memory>

#include "UBCollabChannel.h"
#include "UBCollabItems.h"
#include "UBCollabProtocol.h"
#include "UBCollabRules.h"
#include "UBCollabWire.h"

class QGraphicsItem;
class QTimer;
class UBCollabBoardLink;
class UBDocument;
class UBDocumentProxy;
class UBGraphicsScene;

/**
 * Keeps one document in step with the same document on the computers of the
 * others. It sees the changes of the user by comparing the pages with what
 * it last sent or applied (after every undo step and now and then), and
 * sends them as operations on items and pages; it applies the operations of
 * the others to the document, also to pages that are not on the board.
 *
 * Every item and page has a register: the incarnation (the version of the
 * add that made it exist), the version of its last change, and whether it is
 * alive. Versions are Lamport clock values with the participant as tie
 * break. The rules, applied the same way everywhere, make all computers end
 * in the same state whatever the order in which operations arrive:
 *   - an operation of an older incarnation is ignored;
 *   - within an incarnation a removal wins over every change, and of two
 *     changes the later version wins (last writer wins per item);
 *   - an add of a newer incarnation brings an item back (an undo of a delete).
 * A digest over the registers (not over geometry: floating point output
 * differs between computers) is compared with the host's now and then; a
 * page that differs is fetched again from the host, who is the authority.
 *
 * Items of the initial board have no register (version zero) until they are
 * changed; a page nobody touched has an empty register list everywhere.
 */
class UBCollabDocumentSync : public QObject
{
    Q_OBJECT

    public:
        using PeerId = UBCollabChannel::PeerId;

        using ItemRegister = UBCollabRegister;

        /** the registers of a page, as the initial board and the list of pages carry them */
        struct PageRegisters
        {
            QUuid uuid;
            UBCollabVersion incarnation;
            UBCollabVersion version;
            bool alive{true};
            QByteArray key;
            UBCollabVersion propsVersion;
            QHash<QUuid, ItemRegister> items;
        };

        static void writePageRegisters(UBCollabWriter& writer, const PageRegisters& page, bool withItems);
        static bool readPageRegisters(UBCollabReader& reader, PageRegisters& page, bool withItems);

        UBCollabDocumentSync(std::shared_ptr<UBDocumentProxy> document, QObject* parent = nullptr);
        ~UBCollabDocumentSync() override;

        std::shared_ptr<UBDocumentProxy> document() const { return mProxy; }

        void setLocalPeer(PeerId peer) { mPeer = peer; }
        PeerId localPeer() const { return mPeer; }

        /** the host keeps the authority: it answers requests for pages and sends digests */
        void setHost(bool host) { mHost = host; }
        bool isHost() const { return mHost; }

        /**
         * The state of the document now is the shared state (the host when the
         * session starts); from now on the changes of the user are sent.
         */
        void start();

        /** as start(), for a guest: the registers come with the initial board */
        void startFromSnapshot(const QList<PageRegisters>& pages, quint64 clock,
                               const QUuid& currentPage, const UBCollabVersion& currentPageVersion);

        /** stops sending and applying; the modified pages are saved */
        void stop();
        bool isRunning() const { return mRunning; }

        /** a change of someone else is being applied (page turns of the board then are not the user's) */
        bool isApplying() const { return mApplying; }

        /** looks for changes of the user soon (coalesced) */
        void scheduleScan();

        /** looks for changes of the user and sends them */
        void scanNow();

        /**
         * Looks for changes on a page that is not on the board (a program that
         * changes pages directly); the first call only takes note of the page.
         */
        void scanPageNow(const QUuid& page);

        /**
         * Applies a message of another participant (the header is read already).
         * False when the message is invalid. An invalid message changes nothing,
         * except operations before the invalid one that are complete on their own.
         */
        bool handleMessage(PeerId from, quint8 type, UBCollabReader& reader);

        /** the messages that change the document (they wait while the user draws) */
        static bool isDocumentMessage(quint8 type);

        /**
         * The line came back: messages of this participant may have been lost.
         * Everything whose last version is his is sent again as it is now; the
         * others ignore what they have already (the versions are the same).
         */
        void resendOwnChanges();

        // digest and resync
        QByteArray digestMessage() const;
        QByteArray docStateMessage() const;
        QByteArray pageContentMessage(const QUuid& page);

        /**
         * Milliseconds since this participant last sent a change of the page (of
         * the list of pages for a null uuid); -1 when never. A difference with the
         * digest of the host is expected while such a change is on its way.
         */
        qint64 sinceLocalChange(const QUuid& page) const;

        /** compares a digest of the host with the state here; false when the digest is invalid */
        bool compareDigest(UBCollabReader& reader, QList<QUuid>& differentPages, bool& docDiffers) const;

        /** what a new participant gets: the board saved first, then the pages as files and the files they refer to */
        struct SnapshotPlan
        {
            QByteArray begin;
            QList<QByteArray> pages;
            QStringList assets;     // relative paths, sent in parts
            QList<qint64> assetSizes;
            QByteArray end;
            qint64 totalBytes{0};
        };

        SnapshotPlan snapshotPlan();

        // the page the group is on
        QUuid currentPage() const { return mCurrentPage; }
        void setLocalCurrentPage(const QUuid& page);

        /** for tests and logs */
        QString describeState() const;
        quint64 documentDigest() const;
        quint64 pageDigest(const QUuid& page) const;
        QList<QUuid> knownPages() const { return mPages.keys(); }
        int deferredCount() const { return int(mDeferredTexts.size()); }
        quint64 clockValue() const { return mClock; }

    signals:
        /** a message for the others (to 0: all) */
        void outgoing(const QByteArray& message, UBCollabChannel::PeerId to);

        /** someone else took the group to another page */
        void remoteCurrentPage(const QUuid& page);

        /** the user added something that is not shared (once per kind and session) */
        void localOnlyAdded(int kind);

        /** a page cannot be applied as it was sent (a file is missing...): it should be fetched again */
        void pageNeedsResync(const QUuid& page);

        /** something of the user could not be sent (too large) */
        void notSent(const QString& what);

    private:
        struct ShadowEntry
        {
            QGraphicsItem* item{nullptr};
            quint64 fingerprint{0};
            UBCollabItems::TextCache text;
        };

        struct PageProps
        {
            UBCollabVersion version;
            bool dark{false};
            QUuid ruling;
            QByteArray rulingXml;
            int gridSize{0};
            QSize size;
        };

        struct PageState
        {
            PageRegisters reg;

            // what this computer last sent or applied, to see the changes of the user
            std::weak_ptr<UBGraphicsScene> shadowScene;
            QHash<QUuid, ShadowEntry> shadow;
            quint64 shadowProps{0};
            QSet<QUuid> localOnly;
        };

        struct ItemOp
        {
            quint8 op{0};
            QUuid item;
            UBCollabVersion incarnation;
            UBCollabVersion version;
            quint8 kind{0};
            QByteArray fragment;
        };

        struct TextEdit
        {
            quint64 fingerprint{0};
            qint64 since{0};
        };

        UBCollabVersion nextVersion();
        void observe(const UBCollabVersion& version);

        std::shared_ptr<UBGraphicsScene> sceneForPage(const QUuid& page);
        int pageIndex(const QUuid& page) const;
        QList<QUuid> documentOrder() const;
        PageState& pageState(const QUuid& page);
        void registerAllPages();
        void startRunning();

        // changes of the user
        void scanPages();
        void scanItems(const std::shared_ptr<UBGraphicsScene>& scene);
        void baseline(PageState& page, const std::shared_ptr<UBGraphicsScene>& scene);
        bool sendAssets(const QStringList& assets);
        void sendAssetsAgain(const QStringList& assets);
        void applyDeferredTexts();

        // page properties
        PageProps propsOf(const UBGraphicsScene* scene) const;
        static quint64 propsFingerprint(const PageProps& props);
        static void writeProps(UBCollabWriter& writer, const PageProps& props);
        static bool readProps(UBCollabReader& reader, PageProps& props);
        void applyProps(const std::shared_ptr<UBGraphicsScene>& scene, const PageProps& props);

        // changes of the others
        bool handleAsset(UBCollabReader& reader);
        bool handleItemOps(UBCollabReader& reader);
        bool handlePageOp(UBCollabReader& reader);
        bool handlePageProps(UBCollabReader& reader);
        bool handleCurrentPage(UBCollabReader& reader);
        bool handleDocState(UBCollabReader& reader);

        static bool readItemOp(UBCollabReader& reader, ItemOp& op, bool withContent);
        static void writeItemOp(UBCollabWriter& writer, const ItemOp& op);
        bool applyItemOp(const QUuid& pageUuid, const std::shared_ptr<UBGraphicsScene>& scene, const ItemOp& op,
                         QList<QGraphicsItem*>& detached, bool authoritative);
        enum ContentResult
        {
            ContentApplied,
            ContentWaiting,     // a file it needs is missing: the page is fetched again
            ContentInvalid
        };

        ContentResult applyContent(const QUuid& pageUuid, const std::shared_ptr<UBGraphicsScene>& scene, const ItemOp& op,
                                   QList<QGraphicsItem*>& detached);
        void shadowUpdate(PageState& page, const std::shared_ptr<UBGraphicsScene>& scene, const QUuid& uuid, QGraphicsItem* item);

        bool writePageContent(UBCollabWriter& writer, const QUuid& pageUuid, QStringList& assets);
        bool readPageContent(UBCollabReader& reader, bool authoritative);
        bool readAssetList(UBCollabReader& reader);
        bool storeAsset(const QString& path, const QByteArray& data);

        /** puts the pages of the document in the order of the registers and removes dead pages */
        void reconcilePages(const QUuid& shownBefore);
        std::shared_ptr<UBGraphicsScene> createPage(const QUuid& pageUuid, const PageRegisters& reg);

        void pageModified(const QUuid& page, const std::shared_ptr<UBGraphicsScene>& scene);
        void persistModifiedPages(bool immediately = false);

        static quint64 digestOf(const QByteArray& data);

        std::shared_ptr<UBDocumentProxy> mProxy;
        std::shared_ptr<UBDocument> mDocument;
        QPointer<UBCollabBoardLink> mLink;
        PeerId mPeer{0};
        bool mHost{false};
        bool mRunning{false};
        bool mApplying{false};
        quint64 mClock{0};

        QHash<QUuid, PageState> mPages;
        QList<QUuid> mShadowOrder;
        std::weak_ptr<UBGraphicsScene> mLastEdited;
        QUuid mCurrentPage;
        UBCollabVersion mCurrentPageVersion;

        QSet<QString> mSentAssets;
        QHash<QUuid, qint64> mLocalChanges;     // page (null: the list of pages) -> time of the last change sent
        QHash<QString, std::pair<QUuid, ItemOp>> mDeferredTexts;
        QHash<QUuid, TextEdit> mTextEdits;
        QHash<QUuid, std::shared_ptr<UBGraphicsScene>> mModifiedPages;
        QSet<int> mNoticesShown;
        int mRulingsAdded{0};

        QTimer* mScanTimer{nullptr};
        QTimer* mPeriodicTimer{nullptr};
        QTimer* mPersistTimer{nullptr};
        QElapsedTimer mTime;
};

#endif /* UBCOLLABDOCUMENTSYNC_H_ */
