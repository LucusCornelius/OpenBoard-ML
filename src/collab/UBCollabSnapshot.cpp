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

#include "UBCollabSnapshot.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QXmlStreamReader>

#include "adaptors/UBMetadataDcSubsetAdaptor.h"
#include "core/UBPersistenceManager.h"
#include "core/UBSettings.h"
#include "document/UBDocumentProxy.h"
#include "document/UBDocumentToc.h"
#include "frameworks/UBFileSystemUtils.h"
#include "frameworks/UBStringUtils.h"

#include "UBCollabProtocol.h"
#include "UBCollabSvgFilter.h"

#include "core/memcheck.h"

namespace
{
    /** the uuid of the page, from its root element */
    QUuid pageUuidOf(const QByteArray& svg)
    {
        QXmlStreamReader reader(svg);

        while (!reader.atEnd())
        {
            if (reader.readNext() == QXmlStreamReader::StartElement)
                return QUuid(reader.attributes().value(UBSettings::uniboardDocumentNamespaceUri, "uuid").toString());
        }

        return QUuid();
    }
}


UBCollabSnapshotReceiver::~UBCollabSnapshotReceiver()
{
    if (!mFinished)
        abort();
}


bool UBCollabSnapshotReceiver::fail(UBCollabReader& reader, const QString& reason)
{
    reader.fail(reason);

    if (mError.isEmpty())
        mError = reason;

    return false;
}


bool UBCollabSnapshotReceiver::begin(UBCollabReader& reader)
{
    if (hasBegun())
        return fail(reader, "the board began twice");

    mName = reader.string(UBCollabProtocol::maxNameBytes).simplified().left(120);
    mPageCount = reader.u32();
    mAssetCount = reader.u32();
    const quint64 total = reader.u64();
    mClock = reader.u64();
    mCurrentPage = reader.optionalUuid();
    mCurrentPageVersion = reader.version();

    if (!reader.ok())
        return fail(reader, reader.error());

    if (mPageCount == 0 || mPageCount > UBCollabProtocol::maxPages)
        return fail(reader, QString("a board of %1 pages").arg(mPageCount));

    if (mAssetCount > UBCollabProtocol::maxAssets)
        return fail(reader, QString("a board with %1 files").arg(mAssetCount));

    if (total > quint64(UBCollabProtocol::maxSnapshotBytes))
        return fail(reader, "the board is too large");

    if (mClock > (Q_UINT64_C(1) << 60))
        return fail(reader, "clock value out of range");

    mTotal = qint64(total);

    // a new folder among the documents, made a document once everything is there
    mPath = UBPersistenceManager::persistenceManager()->generateUniqueDocumentPath();

    if (!QDir().mkpath(mPath))
    {
        mPath.clear();
        return fail(reader, "cannot make a folder for the board");
    }

    return true;
}


bool UBCollabSnapshotReceiver::page(UBCollabReader& reader)
{
    if (!hasBegun() || mFinished)
        return fail(reader, "a page outside of a board");

    const quint32 index = reader.u32();
    UBCollabDocumentSync::PageRegisters reg;
    UBCollabDocumentSync::readPageRegisters(reader, reg, true);
    const QByteArray svg = reader.bytes(UBCollabProtocol::maxPageBytes);

    if (!reader.ok())
        return fail(reader, reader.error());

    if (index != quint32(mRegisters.size()) || index >= mPageCount)
        return fail(reader, "pages out of order");

    if (mPageUuids.contains(reg.uuid) || !reg.alive)
        return fail(reader, "a page twice or a removed page");

    mReceived += svg.size();

    if (mReceived > UBCollabProtocol::maxSnapshotBytes)
        return fail(reader, "the board is too large");

    const UBCollabSvgFilter::Result filtered = UBCollabSvgFilter::filter(svg, UBCollabSvgFilter::Page);

    if (!filtered.isValid())
        return fail(reader, "page refused: " + filtered.error);

    // the uuid of the page in the file must be the one of the list of pages
    if (pageUuidOf(filtered.svg) != reg.uuid)
        return fail(reader, "the page does not have its own uuid");

    QFile file(mPath + UBPersistenceManager::sceneFilenameForId(int(index)));

    if (!file.open(QIODevice::WriteOnly) || file.write(filtered.svg) != filtered.svg.size())
        return fail(reader, "cannot write a page");

    mDropped += filtered.dropped;
    mPageUuids.insert(reg.uuid);
    mRegisters << reg;
    mPageAssets << filtered.assets;
    return true;
}


bool UBCollabSnapshotReceiver::asset(UBCollabReader& reader)
{
    if (!hasBegun() || mFinished)
        return fail(reader, "a file outside of a board");

    const QString path = reader.string(UBCollabProtocol::maxAssetPathBytes);
    const quint64 total = reader.u64();
    const quint64 offset = reader.u64();
    const QByteArray chunk = reader.bytes(UBCollabProtocol::maxSnapshotPartBytes);

    if (!reader.ok())
        return fail(reader, reader.error());

    if (!UBCollabSvgFilter::isSafeAssetPath(path, UBCollabSvgFilter::Page))
        return fail(reader, "file name not allowed: " + path.left(80));

    if (total > quint64(UBCollabProtocol::maxAssetBytes))
        return fail(reader, "file too large: " + path);

    if (offset == 0)
    {
        // a new file: never one that was sent already
        if (mAssetSizes.contains(path) || mAssetSizes.size() >= int(mAssetCount))
            return fail(reader, "a file twice or too many files");

        mAssetSizes.insert(path, qint64(total));
        mAssetWritten.insert(path, 0);
        QDir().mkpath(QFileInfo(mPath + "/" + path).absolutePath());
    }
    else if (!mAssetSizes.contains(path) || mAssetSizes.value(path) != qint64(total)
             || mAssetWritten.value(path) != qint64(offset))
    {
        return fail(reader, "parts of a file out of order: " + path);
    }

    if (offset + quint64(chunk.size()) > total)
        return fail(reader, "a file larger than announced: " + path);

    mReceived += chunk.size();

    if (mReceived > UBCollabProtocol::maxSnapshotBytes)
        return fail(reader, "the board is too large");

    QFile file(mPath + "/" + path);

    if (!file.open(offset == 0 ? QIODevice::WriteOnly : QIODevice::Append) || file.write(chunk) != chunk.size())
        return fail(reader, "cannot write a file");

    mAssetWritten[path] += chunk.size();
    return true;
}


std::shared_ptr<UBDocumentProxy> UBCollabSnapshotReceiver::end(UBCollabReader& reader)
{
    if (!hasBegun() || mFinished)
    {
        fail(reader, "the end of a board that did not begin");
        return nullptr;
    }

    const quint32 deadCount = reader.count(UBCollabProtocol::maxPages * 4, 16 + 12 + 12 + 1 + 4 + 12);

    for (quint32 i = 0; i < deadCount && reader.ok(); ++i)
    {
        UBCollabDocumentSync::PageRegisters reg;

        if (UBCollabDocumentSync::readPageRegisters(reader, reg, false))
        {
            if (reg.alive || mPageUuids.contains(reg.uuid))
                reader.fail("a removed page that is not removed");
            else
                mRegisters << reg;
        }
    }

    if (!reader.ok() || !reader.atEnd())
    {
        fail(reader, reader.error().isEmpty() ? QString("unexpected data") : reader.error());
        return nullptr;
    }

    if (quint32(mPageUuids.size()) != mPageCount)
    {
        fail(reader, "pages are missing");
        return nullptr;
    }

    for (auto it = mAssetSizes.constBegin(); it != mAssetSizes.constEnd(); ++it)
    {
        if (mAssetWritten.value(it.key()) != it.value())
        {
            fail(reader, "a file is incomplete: " + it.key());
            return nullptr;
        }
    }

    // the list of pages, with the uuids of the pages and the files they refer to
    UBDocumentToc toc(mPath);

    for (int index = 0; index < int(mPageCount); ++index)
    {
        const int pageId = toc.insert(index);

        if (pageId != index)
        {
            fail(reader, "cannot make the list of pages");
            return nullptr;
        }

        toc.setUuid(index, mRegisters.at(index).uuid);

        QStringList present;

        for (const QString& asset : mPageAssets.at(index))
        {
            if (QFile::exists(mPath + "/" + asset))
                present << asset;
        }

        toc.setAssets(index, present);
    }

    toc.save();

    const QString name = mName.isEmpty()
            ? QCoreApplication::translate("UBCollabSnapshotReceiver", "Shared board %1")
                  .arg(QLocale().toString(QDateTime::currentDateTime(), QLocale::ShortFormat))
            : QCoreApplication::translate("UBCollabSnapshotReceiver", "Shared board %1 (%2)")
                  .arg(QLocale().toString(QDateTime::currentDateTime(), QLocale::ShortFormat), mName);

    // the description of the document, as a new document has it: its own identity, dates of
    // today, and this version (its list of pages is complete, no scan needed)
    {
        std::shared_ptr<UBDocumentProxy> description = std::make_shared<UBDocumentProxy>(mPath);
        const QString now = UBStringUtils::toUtcIsoDateTime(QDateTime::currentDateTime());
        description->setUuid(QUuid::createUuid());
        description->setMetaData(UBSettings::documentName, name);
        description->setMetaData(UBSettings::documentVersion, UBSettings::currentFileVersion);
        description->setMetaData(UBSettings::documentDate, now);
        description->setMetaData(UBSettings::documentUpdatedAt, now);
        UBMetadataDcSubsetAdaptor::persist(description);
    }

    std::shared_ptr<UBDocumentProxy> proxy = UBPersistenceManager::persistenceManager()->createDocumentFromDir(mPath, "", name, false, false, false);

    if (!proxy)
    {
        fail(reader, "cannot make the document");
        return nullptr;
    }

    mFinished = true;
    return proxy;
}


void UBCollabSnapshotReceiver::abort()
{
    if (!mPath.isEmpty() && !mFinished)
        UBFileSystemUtils::deleteDir(mPath);

    mPath.clear();
    mFinished = true;
}
