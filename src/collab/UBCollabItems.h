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

#ifndef UBCOLLABITEMS_H_
#define UBCOLLABITEMS_H_

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QStringList>
#include <QUuid>

#include <memory>

#include "UBCollabProtocol.h"

class QGraphicsItem;
class UBDocumentProxy;
class UBGraphicsScene;

/**
 * The items of a page as the board sync sees them: which ones are shared,
 * a fingerprint to notice changes, and the way between an item and the text
 * that is sent for it (a page with only that item, written and read by
 * UBSvgSubsetAdaptor, so that it is exactly what a saved page holds).
 */
class UBCollabItems
{
    public:
        using Kind = UBCollabProtocol::ItemKind;

        /** the kind an item at the top of the page is shared as; NoKind: it stays on this board */
        static Kind kindOf(const QGraphicsItem* item, const UBGraphicsScene* scene);

        enum LocalOnly
        {
            NotLocalOnly,
            LocalContent,   // media, widgets, web content, PDF pages, backgrounds
            LocalTool       // ruler, compass, protractor, triangle, axes, curtain, spotlight, magnifier
        };

        /** what an item that is not shared is, to tell the user once */
        static LocalOnly localOnlyKind(const QGraphicsItem* item, const UBGraphicsScene* scene);

        /** the shared items at the top of the page by uuid; of items with the same uuid the first counts */
        static QHash<QUuid, QGraphicsItem*> sharedItems(const UBGraphicsScene* scene);

        /** the uuid of an item, null when it has none */
        static QUuid uuidOf(const QGraphicsItem* item);

        /** what a fingerprint of a text keeps between two calls, to read its html only after it changed */
        struct TextCache
        {
            int revision{-1};
            const void* document{nullptr};
            quint64 html{0};
        };

        /**
         * A value that changes when anything of the item changes that is saved
         * with the page (geometry, order, colours, content). It is only
         * compared on this computer; the way an item is shown (dark appearance)
         * does not change it.
         */
        static quint64 fingerprint(const QGraphicsItem* item, TextCache* cache = nullptr);

        /** the text sent for an item: a page with only this item */
        static QByteArray fragment(std::shared_ptr<UBDocumentProxy> proxy, std::shared_ptr<UBGraphicsScene> scene, QGraphicsItem* item);

        /** the files of the document the item (and what it contains) refers to */
        static QStringList assets(const QGraphicsItem* item);

        /** an item read from a fragment, in a scene of its own until it is put on a page */
        struct Parsed
        {
            std::shared_ptr<UBGraphicsScene> scene;
            QGraphicsItem* item{nullptr};
            QString error;
            bool missingAsset{false};

            bool isValid() const { return item != nullptr; }
        };

        /**
         * Reads a fragment that came from another computer: cleaned by
         * UBCollabSvgFilter, read by the page reader; it must hold exactly one
         * item, of this uuid and kind. The files it refers to must be in the
         * document already.
         */
        static Parsed parse(std::shared_ptr<UBDocumentProxy> proxy, const QByteArray& fragment, const QUuid& uuid, Kind kind);

        /** puts a parsed item on the page, in the colours of the page */
        static void insert(UBGraphicsScene* scene, Parsed& parsed);

        /**
         * Gives an item on the page the content of a parsed one, keeping the
         * item itself (undo commands of the user refer to it). Polygons that a
         * stroke no longer has are added to detached. False when the item cannot
         * be changed in place (other type, other members of a group).
         */
        static bool update(UBGraphicsScene* scene, QGraphicsItem* existing, Parsed& parsed, QList<QGraphicsItem*>& detached);

        /** takes an item off the page as a delete by the user does, without an undo step */
        static void remove(UBGraphicsScene* scene, QGraphicsItem* item);

        /** deletes detached polygons that no undo step of the user refers to (the others are deleted with the undo steps) */
        static void releaseDetached(UBGraphicsScene* scene, const QList<QGraphicsItem*>& detached);

        /** the colours of strokes and texts for the background the page is shown with */
        static void recolor(UBGraphicsScene* scene, QGraphicsItem* item);

        /** for tests and logs: a short description of the content of a page (counts and bounds per kind) */
        static QString describe(const UBGraphicsScene* scene);
};

#endif /* UBCOLLABITEMS_H_ */
