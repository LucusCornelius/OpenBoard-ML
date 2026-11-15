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

#ifndef UBCOLLABPROTOCOL_H_
#define UBCOLLABPROTOCOL_H_

#include <QByteArray>
#include <QtGlobal>

class UBCollabReader;
class UBCollabWriter;

/**
 * What the participants of a shared board send each other through the
 * channel. Every message starts with the magic number and its type; the
 * first message to every participant is Hello, with the protocol version.
 * See docs/COLLABORATION.md for the meaning of the messages.
 */
namespace UBCollabProtocol
{
    /** raised whenever a message changes in a way older versions cannot read */
    constexpr quint32 version = 1;

    constexpr quint32 magic = 0x4C43424F; // "OBCL" in the byte order of the wire

    enum MessageType : quint8
    {
        Hello = 1,              // protocol version, role, name
        Bye,                    // leaves the session (from the host: ends it)
        SnapshotRequest,        // a guest asks the host for the board
        SnapshotBegin,          // the board follows: size, pages, clock
        SnapshotPage,           // one page file
        SnapshotAsset,          // a part of a file of the document (image, PDF)
        SnapshotEnd,
        Asset,                  // a file an item refers to, before the item
        ItemOps,                // items of a page added, changed or removed
        PageOp,                 // a page added, removed or moved
        PageProps,              // background and size of a page
        CurrentPage,            // the page the group is on
        Cursor,                 // the pointer of a participant (and the stroke being drawn)
        Digest,                 // summary of the state, from the host
        DigestRequest,          // a guest asks for a digest (after the line came back)
        PageRequest,            // a guest asks for a page as the host has it
        PageContent,            // a whole page, replaces the page of the guest
        DocStateRequest,        // a guest asks for the list of pages as the host has it
        DocState,
        LastMessageType = DocState
    };

    enum Role : quint8
    {
        Guest = 0,
        Host = 1
    };

    enum ByeReason : quint8
    {
        Leaving = 0,            // the participant leaves
        HostEnded = 1,          // the host ended the session for everybody
        VersionMismatch = 2,    // the participant refuses a different protocol version
        InvalidData = 3         // the participant refuses the data that was sent
    };

    enum ItemOpKind : quint8
    {
        AddItem = 1,
        ChangeItem = 2,
        RemoveItem = 3
    };

    enum PageOpKind : quint8
    {
        AddPage = 1,            // with the content of the page
        RemovePage = 2,
        MovePage = 3
    };

    /** the kinds of items that are shared; anything else stays on the board where it was made */
    enum ItemKind : quint8
    {
        NoKind = 0,
        Strokes = 1,            // pen, marker, lines, ink of formulas
        Pixmap = 2,             // pictures
        Svg = 3,                // svg pictures
        Text = 4,
        Shape = 5,              // shapes of the shapes tool
        Group = 6               // a group of the above
    };

    // limits for everything that arrives: a message that goes beyond them is refused
    constexpr qsizetype maxNameBytes = 256;
    constexpr qsizetype maxVersionTextBytes = 64;
    constexpr qsizetype maxFragmentBytes = 16 * 1024 * 1024;
    constexpr qsizetype maxPageBytes = 64 * 1024 * 1024;
    constexpr qsizetype maxAssetBytes = 64 * 1024 * 1024;
    constexpr qsizetype maxAssetPathBytes = 200;
    constexpr qsizetype maxSnapshotPartBytes = 1024 * 1024;
    constexpr qint64 maxSnapshotBytes = Q_INT64_C(1024) * 1024 * 1024;
    constexpr quint32 maxPages = 2000;
    constexpr quint32 maxAssets = 20000;
    constexpr quint32 maxOpsPerMessage = 5000;
    constexpr quint32 maxItemsPerPage = 50000;
    constexpr quint32 maxTombstonesPerPage = 200000;
    constexpr qsizetype maxOrderKeyBytes = 1024;
    constexpr qsizetype maxRulingBytes = 256 * 1024;
    constexpr int maxPageSide = 30000;
    constexpr int maxGridSize = 5000;
    constexpr quint32 maxCursorPoints = 64;

    /** the header of every message */
    void writeHeader(UBCollabWriter& writer, MessageType type);

    /** reads the header; 0 when the data is not a message of this protocol */
    quint8 readHeader(UBCollabReader& reader);
}


/**
 * Keys that order the pages of a shared document: strings compared byte by
 * byte, so that a page can always be put between two others without changing
 * their keys. Two people who move pages at the same moment get keys that put
 * the pages in the same order on every computer.
 */
class UBCollabOrderKey
{
    public:
        /** a key after a and before b; an empty a is the start, an empty b the end */
        static QByteArray between(const QByteArray& a, const QByteArray& b);

        /** keys for count pages in their order */
        static QList<QByteArray> sequence(int count);

        /** true when the key consists of key digits only and does not end in the lowest digit */
        static bool isValid(const QByteArray& key);
};

#endif /* UBCOLLABPROTOCOL_H_ */
