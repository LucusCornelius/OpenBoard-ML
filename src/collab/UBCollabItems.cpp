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

#include "UBCollabItems.h"

#include <QFile>
#include <QGraphicsItem>
#include <QTextDocument>
#include <QUndoStack>

#include <algorithm>
#include <cstring>

#include "adaptors/UBSvgSubsetAdaptor.h"
#include "board/UBBoardController.h"
#include "core/UBApplication.h"
#include "document/UBDocumentProxy.h"
#include "domain/UBGraphicsGroupContainerItem.h"
#include "domain/UBGraphicsItemUndoCommand.h"
#include "domain/UBGraphicsPixmapItem.h"
#include "domain/UBGraphicsPolygonItem.h"
#include "domain/UBGraphicsScene.h"
#include "domain/UBGraphicsShapeItem.h"
#include "domain/UBGraphicsStrokesGroup.h"
#include "domain/UBGraphicsSvgItem.h"
#include "domain/UBGraphicsTextItem.h"
#include "domain/UBItem.h"

#include "UBCollabSvgFilter.h"

#include "core/memcheck.h"

namespace
{
    /** FNV-1a: a fingerprint is only compared on this computer, it needs no stronger hash */
    class Hasher
    {
        public:
            void bytes(const void* data, size_t length)
            {
                const unsigned char* p = static_cast<const unsigned char*>(data);

                for (size_t i = 0; i < length; ++i)
                {
                    mValue ^= p[i];
                    mValue *= Q_UINT64_C(0x100000001b3);
                }
            }

            void add(quint64 value) { bytes(&value, sizeof(value)); }
            void add(int value) { add(quint64(qint64(value))); }
            void add(bool value) { add(quint64(value ? 1 : 2)); }

            void add(double value)
            {
                quint64 bits = 0;
                std::memcpy(&bits, &value, sizeof(bits));
                add(bits);
            }

            void add(const QPointF& point)
            {
                add(point.x());
                add(point.y());
            }

            void add(const QSizeF& size)
            {
                add(size.width());
                add(size.height());
            }

            void add(const QTransform& t)
            {
                for (double v : {t.m11(), t.m12(), t.m13(), t.m21(), t.m22(), t.m23(), t.m31(), t.m32(), t.m33()})
                    add(v);
            }

            void add(const QColor& color) { add(quint64(color.isValid() ? color.rgba() : 0)); }

            void add(const QString& text) { bytes(text.constData(), size_t(text.size()) * sizeof(QChar)); }

            quint64 value() const { return mValue; }

        private:
            quint64 mValue{Q_UINT64_C(0xcbf29ce484222325)};
    };

    bool isMediaType(int type)
    {
        switch (type)
        {
            case UBGraphicsItemType::MediaItemType:
            case UBGraphicsItemType::VideoItemType:
            case UBGraphicsItemType::AudioItemType:
            case UBGraphicsItemType::PDFItemType:
            case UBGraphicsItemType::GraphicsWidgetItemType:
            case UBGraphicsItemType::ToolWidgetItemType:
                return true;

            default:
                return false;
        }
    }

    bool isToolType(int type)
    {
        switch (type)
        {
            case UBGraphicsItemType::CurtainItemType:
            case UBGraphicsItemType::RulerItemType:
            case UBGraphicsItemType::CompassItemType:
            case UBGraphicsItemType::ProtractorItemType:
            case UBGraphicsItemType::TriangleItemType:
            case UBGraphicsItemType::MagnifierItemType:
            case UBGraphicsItemType::cacheItemType:
            case UBGraphicsItemType::AxesItemType:
                return true;

            default:
                return false;
        }
    }

    bool hasSafeAsset(const UBMediaAssetItem* item)
    {
        const QList<QString> assets = item->mediaAssets();
        return !assets.isEmpty() && UBCollabSvgFilter::isSafeAssetPath(assets.first(), UBCollabSvgFilter::Fragment);
    }

    /** the kind of an item inside a group or at the top of the page, without the checks of the page */
    UBCollabProtocol::ItemKind kindOfContent(const QGraphicsItem* item)
    {
        if (!item || !item->isVisible() || UBCollabItems::uuidOf(item).isNull())
            return UBCollabProtocol::NoKind;

        switch (item->type())
        {
            case UBGraphicsItemType::StrokeItemType:
            {
                for (const QGraphicsItem* child : item->childItems())
                {
                    if (child->type() == UBGraphicsItemType::PolygonItemType)
                        return UBCollabProtocol::Strokes;
                }

                return UBCollabProtocol::NoKind;
            }

            case UBGraphicsItemType::PixmapItemType:
                return hasSafeAsset(static_cast<const UBGraphicsPixmapItem*>(item)) ? UBCollabProtocol::Pixmap : UBCollabProtocol::NoKind;

            case UBGraphicsItemType::SvgItemType:
                return hasSafeAsset(static_cast<const UBGraphicsSvgItem*>(item)) ? UBCollabProtocol::Svg : UBCollabProtocol::NoKind;

            case UBGraphicsItemType::TextItemType:
                // the page reader drops empty texts: an empty text does not exist yet for the others
                return static_cast<const UBGraphicsTextItem*>(item)->toPlainText().isEmpty() ? UBCollabProtocol::NoKind : UBCollabProtocol::Text;

            case UBGraphicsItemType::ShapeItemType:
                return UBCollabProtocol::Shape;

            case UBGraphicsItemType::groupContainerType:
            {
                // a group of one shared item would be dissolved by the reader
                int shared = 0;

                for (const QGraphicsItem* child : item->childItems())
                {
                    if (kindOfContent(child) != UBCollabProtocol::NoKind)
                        ++shared;
                }

                return shared >= 2 ? UBCollabProtocol::Group : UBCollabProtocol::NoKind;
            }

            default:
                return UBCollabProtocol::NoKind;
        }
    }

    quint64 textHtmlHash(const UBGraphicsTextItem* text)
    {
        // black and white are swapped with the background the page is shown on: the same text
        QString html = text->toHtml();
        html.replace("#ffffff", "#000000", Qt::CaseInsensitive);

        Hasher h;
        h.add(html);
        return h.value();
    }

    void collectAssets(const QGraphicsItem* item, QStringList& assets)
    {
        const UBMediaAssetItem* media = nullptr;

        if (item->type() == UBGraphicsItemType::PixmapItemType)
            media = static_cast<const UBGraphicsPixmapItem*>(item);
        else if (item->type() == UBGraphicsItemType::SvgItemType)
            media = static_cast<const UBGraphicsSvgItem*>(item);

        if (media)
        {
            for (const QString& asset : media->mediaAssets())
            {
                if (UBCollabSvgFilter::isSafeAssetPath(asset, UBCollabSvgFilter::Fragment) && !assets.contains(asset))
                    assets << asset;
            }
        }

        if (item->type() == UBGraphicsItemType::groupContainerType)
        {
            for (const QGraphicsItem* child : item->childItems())
                collectAssets(child, assets);
        }
    }

    void collectUndoItems(const QUndoCommand* command, QSet<const QGraphicsItem*>& items)
    {
        for (int i = 0; i < command->childCount(); ++i)
            collectUndoItems(command->child(i), items);

        const UBGraphicsItemUndoCommand* itemCommand = dynamic_cast<const UBGraphicsItemUndoCommand*>(command);

        if (itemCommand)
        {
            for (QGraphicsItem* item : itemCommand->GetAddedList())
                items.insert(item);

            for (QGraphicsItem* item : itemCommand->GetRemovedList())
                items.insert(item);
        }
    }

    QString kindName(int type)
    {
        switch (type)
        {
            case UBGraphicsItemType::StrokeItemType: return "strokes";
            case UBGraphicsItemType::PixmapItemType: return "pixmaps";
            case UBGraphicsItemType::SvgItemType: return "svgs";
            case UBGraphicsItemType::TextItemType: return "texts";
            case UBGraphicsItemType::ShapeItemType: return "shapes";
            case UBGraphicsItemType::groupContainerType: return "groups";
            default: return "other";
        }
    }
}


QUuid UBCollabItems::uuidOf(const QGraphicsItem* item)
{
    const UBItem* ubItem = dynamic_cast<const UBItem*>(item);
    return ubItem ? ubItem->uuid() : QUuid();
}


UBCollabItems::Kind UBCollabItems::kindOf(const QGraphicsItem* item, const UBGraphicsScene* scene)
{
    if (!item || item->parentItem())
        return UBCollabProtocol::NoKind;

    if (scene)
    {
        if (scene->isBackgroundObject(item) || const_cast<UBGraphicsScene*>(scene)->tools().contains(const_cast<QGraphicsItem*>(item)))
            return UBCollabProtocol::NoKind;
    }

    return kindOfContent(item);
}


UBCollabItems::LocalOnly UBCollabItems::localOnlyKind(const QGraphicsItem* item, const UBGraphicsScene* scene)
{
    if (!item || item->parentItem() || uuidOf(item).isNull() || kindOf(item, scene) != UBCollabProtocol::NoKind)
        return NotLocalOnly;

    if (isToolType(item->type()) || (scene && const_cast<UBGraphicsScene*>(scene)->tools().contains(const_cast<QGraphicsItem*>(item))))
        return LocalTool;

    if (isMediaType(item->type()) || (scene && scene->isBackgroundObject(item)))
        return LocalContent;

    return NotLocalOnly;
}


QHash<QUuid, QGraphicsItem*> UBCollabItems::sharedItems(const UBGraphicsScene* scene)
{
    QHash<QUuid, QGraphicsItem*> items;

    if (!scene)
        return items;

    for (QGraphicsItem* item : scene->items())
    {
        if (item->parentItem())
            continue;

        if (kindOf(item, scene) == UBCollabProtocol::NoKind)
            continue;

        const QUuid uuid = uuidOf(item);

        if (!items.contains(uuid))
            items.insert(uuid, item);
    }

    return items;
}


quint64 UBCollabItems::fingerprint(const QGraphicsItem* item, TextCache* cache)
{
    Hasher h;

    if (!item)
        return 0;

    h.add(item->type());
    h.add(item->transform());
    h.add(item->pos());
    h.add(item->data(UBGraphicsItemData::ItemOwnZValue).toReal());
    h.add(item->data(UBGraphicsItemData::ItemLocked).toBool());
    h.add(item->data(UBGraphicsItemData::ItemIsHiddenOnDisplay).toBool());

    const QVariant editable = item->data(UBGraphicsItemData::ItemEditable);
    h.add(editable.isNull() ? 0 : (editable.toBool() ? 1 : 2));

    switch (item->type())
    {
        case UBGraphicsItemType::StrokeItemType:
        {
            // the eraser replaces polygons by new ones, so their addresses tell a change
            for (const QGraphicsItem* child : item->childItems())
            {
                const UBGraphicsPolygonItem* polygon = qgraphicsitem_cast<const UBGraphicsPolygonItem*>(child);

                if (!polygon)
                    continue;

                h.add(quint64(quintptr(polygon)));
                h.add(int(polygon->polygon().size()));
                h.add(polygon->colorOnDarkBackground());
                h.add(polygon->colorOnLightBackground());
                h.add(polygon->transform());
                h.add(polygon->pos());
                h.add(polygon->isVisible());
            }
            break;
        }

        case UBGraphicsItemType::PixmapItemType:
            h.add(static_cast<const UBGraphicsPixmapItem*>(item)->mediaAssets().join('|'));
            break;

        case UBGraphicsItemType::SvgItemType:
            h.add(static_cast<const UBGraphicsSvgItem*>(item)->mediaAssets().join('|'));
            break;

        case UBGraphicsItemType::TextItemType:
        {
            const UBGraphicsTextItem* text = static_cast<const UBGraphicsTextItem*>(item);
            const QTextDocument* document = text->document();
            quint64 html = 0;

            if (cache && cache->document == document && cache->revision == document->revision())
            {
                html = cache->html;
            }
            else
            {
                html = textHtmlHash(text);

                if (cache)
                {
                    cache->document = document;
                    cache->revision = document->revision();
                    cache->html = html;
                }
            }

            h.add(html);
            h.add(text->textWidth());
            h.add(text->textHeight());
            h.add(text->hasBoxBorder());
            h.add(text->hasBoxFill());
            h.add(text->boxFillColor());
            h.add(text->colorOnDarkBackground());
            h.add(text->colorOnLightBackground());
            break;
        }

        case UBGraphicsItemType::ShapeItemType:
        {
            const UBGraphicsShapeItem* shape = static_cast<const UBGraphicsShapeItem*>(item);
            h.add(int(shape->kind()));
            h.add(shape->boxSize());
            h.add(shape->strokeWidth());
            h.add(int(shape->pen().style()));
            h.add(shape->lineStartsLeft());
            h.add(shape->lineStartsTop());
            h.add(shape->hasStartArrow());
            h.add(shape->hasEndArrow());
            h.add(shape->colorOnDarkBackground());
            h.add(shape->colorOnLightBackground());
            h.add(shape->isFilled());
            h.add(shape->fillOnDarkBackground());
            h.add(shape->fillOnLightBackground());
            break;
        }

        case UBGraphicsItemType::groupContainerType:
        {
            QList<QGraphicsItem*> children = item->childItems();
            std::sort(children.begin(), children.end(), [](const QGraphicsItem* a, const QGraphicsItem* b) {
                return uuidOf(a) < uuidOf(b);
            });

            for (const QGraphicsItem* child : std::as_const(children))
            {
                if (kindOfContent(child) == UBCollabProtocol::NoKind)
                    continue;

                h.add(uuidOf(child).toString());
                h.add(fingerprint(child, nullptr));
            }
            break;
        }

        default:
            break;
    }

    return h.value();
}


QByteArray UBCollabItems::fragment(std::shared_ptr<UBDocumentProxy> proxy, std::shared_ptr<UBGraphicsScene> scene, QGraphicsItem* item)
{
    if (!proxy || !scene || !item)
        return QByteArray();

    return UBSvgSubsetAdaptor::itemsToSvg(proxy, scene, {item});
}


QStringList UBCollabItems::assets(const QGraphicsItem* item)
{
    QStringList list;

    if (item)
        collectAssets(item, list);

    return list;
}


UBCollabItems::Parsed UBCollabItems::parse(std::shared_ptr<UBDocumentProxy> proxy, const QByteArray& fragment, const QUuid& uuid, Kind kind)
{
    Parsed parsed;

    if (!proxy || uuid.isNull() || kind == UBCollabProtocol::NoKind || kind > UBCollabProtocol::Group)
    {
        parsed.error = "invalid item";
        return parsed;
    }

    const UBCollabSvgFilter::Result filtered = UBCollabSvgFilter::filter(fragment, UBCollabSvgFilter::Fragment);

    if (!filtered.isValid())
    {
        parsed.error = filtered.error;
        return parsed;
    }

    for (const QString& asset : filtered.assets)
    {
        if (!QFile::exists(proxy->persistencePath() + "/" + asset))
        {
            parsed.error = "missing file " + asset;
            parsed.missingAsset = true;
            return parsed;
        }
    }

    parsed.scene = UBSvgSubsetAdaptor::loadScene(proxy, filtered.svg);

    if (!parsed.scene)
    {
        parsed.error = "unreadable item";
        return parsed;
    }

    QGraphicsItem* found = nullptr;
    int others = 0;

    for (QGraphicsItem* item : parsed.scene->items())
    {
        if (item->parentItem() || uuidOf(item).isNull())
            continue;

        if (uuidOf(item) == uuid && kindOf(item, parsed.scene.get()) == kind && !found)
            found = item;
        else
            ++others;
    }

    if (!found)
    {
        parsed.error = "the fragment does not hold the announced item";
        return parsed;
    }

    if (others > 0)
    {
        parsed.error = "the fragment holds more than the announced item";
        return parsed;
    }

    // a picture that could not be decoded would be an empty frame here and a picture elsewhere
    if (kind == UBCollabProtocol::Pixmap && static_cast<UBGraphicsPixmapItem*>(found)->pixmap().isNull())
    {
        parsed.error = "the picture cannot be read";
        return parsed;
    }

    parsed.item = found;
    return parsed;
}


void UBCollabItems::insert(UBGraphicsScene* scene, Parsed& parsed)
{
    if (!scene || !parsed.item)
        return;

    QGraphicsItem* item = parsed.item;

    if (parsed.scene)
    {
        parsed.scene->removeItem(item);
        parsed.scene->removeItemFromDeletion(item);
    }

    scene->addItem(item);
    recolor(scene, item);

    parsed.item = nullptr;
}


bool UBCollabItems::update(UBGraphicsScene* scene, QGraphicsItem* existing, Parsed& parsed, QList<QGraphicsItem*>& detached)
{
    QGraphicsItem* incoming = parsed.item;

    if (!scene || !existing || !incoming || existing->type() != incoming->type())
        return false;

    UBItem* source = dynamic_cast<UBItem*>(incoming);

    if (!source)
        return false;

    switch (existing->type())
    {
        case UBGraphicsItemType::StrokeItemType:
        {
            UBGraphicsStrokesGroup* group = static_cast<UBGraphicsStrokesGroup*>(existing);
            UBGraphicsStrokesGroup* other = static_cast<UBGraphicsStrokesGroup*>(incoming);

            // the old polygons leave the group as the eraser takes them out: they keep their group,
            // so that an undo of the user can put them back
            const QList<QGraphicsItem*> oldChildren = group->childItems();

            for (QGraphicsItem* child : oldChildren)
            {
                UBGraphicsPolygonItem* polygon = qgraphicsitem_cast<UBGraphicsPolygonItem*>(child);

                if (!polygon)
                    continue;

                group->removeFromGroup(polygon);
                scene->removeItem(polygon);
                detached << polygon;
            }

            other->copyItemParameters(group);

            const QList<QGraphicsItem*> newChildren = other->childItems();

            for (QGraphicsItem* child : newChildren)
            {
                UBGraphicsPolygonItem* polygon = qgraphicsitem_cast<UBGraphicsPolygonItem*>(child);

                if (!polygon)
                    continue;

                other->removeFromGroup(polygon);

                if (polygon->scene())
                    polygon->scene()->removeItem(polygon);

                polygon->setStrokesGroup(group);
                group->addToGroup(polygon);
            }

            recolor(scene, group);
            break;
        }

        case UBGraphicsItemType::TextItemType:
        {
            UBGraphicsTextItem* text = static_cast<UBGraphicsTextItem*>(existing);
            UBGraphicsTextItem* other = static_cast<UBGraphicsTextItem*>(incoming);

            // copying the size gives the text the keyboard focus: give it back to where it was
            QGraphicsItem* focus = scene->focusItem();

            other->copyItemParameters(text);
            text->setColorOnDarkBackground(other->colorOnDarkBackground());
            text->setColorOnLightBackground(other->colorOnLightBackground());
            text->activateTextEditor(false);
            recolor(scene, text);

            if (scene->focusItem() != focus)
            {
                if (focus)
                    focus->setFocus();
                else
                    text->clearFocus();
            }
            break;
        }

        case UBGraphicsItemType::PixmapItemType:
        case UBGraphicsItemType::SvgItemType:
        case UBGraphicsItemType::ShapeItemType:
            source->copyItemParameters(dynamic_cast<UBItem*>(existing));
            break;

        case UBGraphicsItemType::groupContainerType:
        {
            // the members by uuid; the same members or the group is replaced as a whole
            QHash<QUuid, QGraphicsItem*> mine;
            QHash<QUuid, QGraphicsItem*> theirs;

            for (QGraphicsItem* child : existing->childItems())
            {
                if (kindOfContent(child) != UBCollabProtocol::NoKind)
                    mine.insert(uuidOf(child), child);
            }

            for (QGraphicsItem* child : incoming->childItems())
            {
                if (kindOfContent(child) != UBCollabProtocol::NoKind)
                    theirs.insert(uuidOf(child), child);
            }

            if (mine.size() != existing->childItems().size())
                return false;

            const QList<QUuid> mineKeys = mine.keys();

            if (QSet<QUuid>(mineKeys.begin(), mineKeys.end()) != QSet<QUuid>(theirs.keyBegin(), theirs.keyEnd()))
                return false;

            for (auto it = mine.constBegin(); it != mine.constEnd(); ++it)
            {
                if (it.value()->type() != theirs.value(it.key())->type())
                    return false;
            }

            // the read group has no transform of its own, its members are in page coordinates
            source->copyItemParameters(dynamic_cast<UBItem*>(existing));

            for (auto it = mine.constBegin(); it != mine.constEnd(); ++it)
            {
                Parsed member;
                member.item = theirs.value(it.key());

                if (!update(scene, it.value(), member, detached))
                    return false;
            }
            break;
        }

        default:
            return false;
    }

    return true;
}


void UBCollabItems::remove(UBGraphicsScene* scene, QGraphicsItem* item)
{
    if (!scene || !item || item->scene() != scene)
        return;

    item->setSelected(false);

    UBGraphicsItemDelegate* delegate = UBGraphicsItem::Delegate(item);

    if (delegate)
        delegate->remove(false);
    else
        scene->removeItem(item);
}


void UBCollabItems::releaseDetached(UBGraphicsScene* scene, const QList<QGraphicsItem*>& detached)
{
    if (detached.isEmpty())
        return;

    // the undo steps of the user only refer to the page on the board
    QSet<const QGraphicsItem*> referenced;

    if (UBApplication::undoStack && UBApplication::boardController
            && UBApplication::boardController->activeScene().get() == scene)
    {
        for (int i = 0; i < UBApplication::undoStack->count(); ++i)
            collectUndoItems(UBApplication::undoStack->command(i), referenced);
    }

    for (QGraphicsItem* item : detached)
    {
        if (!referenced.contains(item) && !item->scene() && !item->parentItem())
            delete item;
    }
}


void UBCollabItems::recolor(UBGraphicsScene* scene, QGraphicsItem* item)
{
    if (!scene || !item)
        return;

    const bool dark = scene->isDarkBackground();

    switch (item->type())
    {
        case UBGraphicsItemType::StrokeItemType:
        {
            for (QGraphicsItem* child : item->childItems())
            {
                UBGraphicsPolygonItem* polygon = qgraphicsitem_cast<UBGraphicsPolygonItem*>(child);

                if (polygon)
                    polygon->setColor(dark ? polygon->colorOnDarkBackground() : polygon->colorOnLightBackground());
            }
            break;
        }

        case UBGraphicsItemType::TextItemType:
            static_cast<UBGraphicsTextItem*>(item)->recolor();
            break;

        case UBGraphicsItemType::groupContainerType:
        {
            for (QGraphicsItem* child : item->childItems())
                recolor(scene, child);
            break;
        }

        default:
            break;
    }
}


QString UBCollabItems::describe(const UBGraphicsScene* scene)
{
    if (!scene)
        return "no page";

    QMap<QString, int> counts;
    QMap<QString, QRectF> bounds;
    int polygons = 0;

    const QHash<QUuid, QGraphicsItem*> items = sharedItems(scene);

    for (QGraphicsItem* item : items)
    {
        const QString name = kindName(item->type());
        counts[name] += 1;
        bounds[name] |= item->sceneBoundingRect();

        if (item->type() == UBGraphicsItemType::StrokeItemType)
            polygons += int(item->childItems().size());
    }

    QStringList parts;

    for (auto it = counts.constBegin(); it != counts.constEnd(); ++it)
    {
        const QRectF r = bounds.value(it.key());
        parts << QString("%1=%2 [%3,%4 %5x%6]").arg(it.key()).arg(it.value())
                     .arg(r.x(), 0, 'f', 0).arg(r.y(), 0, 'f', 0).arg(r.width(), 0, 'f', 0).arg(r.height(), 0, 'f', 0);
    }

    parts << QString("polygons=%1").arg(polygons);
    return parts.join(' ');
}
