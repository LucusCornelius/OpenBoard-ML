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

#ifndef UBCOLLABSNAPSHOT_H_
#define UBCOLLABSNAPSHOT_H_

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUuid>

#include <memory>

#include "UBCollabDocumentSync.h"
#include "UBCollabWire.h"

class UBDocumentProxy;

/**
 * Receives the board of the host when joining a session and makes a new
 * document of it on this computer. The parts are written to a new folder of
 * the documents as they arrive (pages cleaned by UBCollabSvgFilter, files
 * only in the folders of pictures and PDFs, with plain names and bounded
 * sizes); the document is added to the documents only when everything
 * arrived and is consistent. Anything wrong ends in a refusal and the folder
 * is removed.
 */
class UBCollabSnapshotReceiver
{
    public:
        UBCollabSnapshotReceiver() = default;
        ~UBCollabSnapshotReceiver();

        bool begin(UBCollabReader& reader);
        bool page(UBCollabReader& reader);
        bool asset(UBCollabReader& reader);

        /** the last part: the document is made; null on an error */
        std::shared_ptr<UBDocumentProxy> end(UBCollabReader& reader);

        /** removes what was written of an incomplete board */
        void abort();

        bool hasBegun() const { return !mPath.isEmpty(); }
        qint64 received() const { return mReceived; }
        qint64 total() const { return mTotal; }
        const QString& error() const { return mError; }

        const QList<UBCollabDocumentSync::PageRegisters>& registers() const { return mRegisters; }
        quint64 clock() const { return mClock; }
        QUuid currentPage() const { return mCurrentPage; }
        UBCollabVersion currentPageVersion() const { return mCurrentPageVersion; }
        int droppedElements() const { return mDropped; }

    private:
        bool fail(UBCollabReader& reader, const QString& reason);

        QString mPath;
        QString mName;
        quint32 mPageCount{0};
        quint32 mAssetCount{0};
        qint64 mTotal{0};
        qint64 mReceived{0};
        quint64 mClock{0};
        QUuid mCurrentPage;
        UBCollabVersion mCurrentPageVersion;

        QList<UBCollabDocumentSync::PageRegisters> mRegisters;
        QList<QStringList> mPageAssets;
        QSet<QUuid> mPageUuids;
        QHash<QString, qint64> mAssetSizes;     // expected size of every file begun
        QHash<QString, qint64> mAssetWritten;
        int mDropped{0};
        bool mFinished{false};
        QString mError;
};

#endif /* UBCOLLABSNAPSHOT_H_ */
