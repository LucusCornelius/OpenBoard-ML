/*
 * Copyright (C) 2015-2022 Département de l'Instruction Publique (DIP-SEM)
 *
 * Copyright (C) 2013 Open Education Foundation
 *
 * Copyright (C) 2010-2013 Groupement d'Intérêt Public pour
 * l'Education Numérique en Afrique (GIP ENA)
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




#include "UBGraphicsShapeItem.h"

#include <QtMath>
#include <QStyleOptionGraphicsItem>
#include <QGraphicsSceneMouseEvent>

#include "domain/UBGraphicsScene.h"
#include "domain/UBGraphicsShapeItemDelegate.h"
#include "domain/UBGraphicsDelegateFrame.h"

#include "core/memcheck.h"

namespace
{
    const char* const kindNames[] = {"rectangle", "rounded-rectangle", "ellipse", "triangle", "line", "arrow"};

    // minimal distance from the outline used for hit testing of thin shapes
    constexpr qreal minHitWidth = 12.;
}

UBGraphicsShapeItem::UBGraphicsShapeItem(Kind kind, QGraphicsItem* parent)
    : QAbstractGraphicsShapeItem(parent)
    , mKind(kind)
    , mLineStartsLeft(true)
    , mLineStartsTop(true)
    , mStartArrow(false)
    , mEndArrow(kind == Arrow)
    , mColorOnDarkBackground(Qt::white)
    , mColorOnLightBackground(Qt::black)
    , mFilled(false)
{
    QPen p(Qt::black, 3.);
    p.setCapStyle(Qt::RoundCap);
    p.setJoinStyle(Qt::RoundJoin);
    setPen(p);
    setBrush(Qt::NoBrush);

    setDelegate(new UBGraphicsShapeItemDelegate(this));

    setData(UBGraphicsItemData::ItemLayerType, UBItemLayerType::Object);
    setData(UBGraphicsItemData::itemLayerType, QVariant(itemLayerType::ObjectItem)); // necessary to have the z value assigned correctly

    setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);
    setFlag(QGraphicsItem::ItemIsSelectable, true);
    setFlag(QGraphicsItem::ItemIsMovable, true);

    setUuid(QUuid::createUuid());
}

UBGraphicsShapeItem::~UBGraphicsShapeItem()
{
}

QString UBGraphicsShapeItem::kindToString(Kind kind)
{
    if (kind < 0 || kind >= KindCount)
        kind = Rectangle;

    return QString::fromLatin1(kindNames[kind]);
}

UBGraphicsShapeItem::Kind UBGraphicsShapeItem::kindFromString(const QString& name, bool* ok)
{
    for (int i = 0; i < KindCount; ++i)
    {
        if (name == QLatin1String(kindNames[i]))
        {
            if (ok)
                *ok = true;

            return Kind(i);
        }
    }

    if (ok)
        *ok = false;

    return Rectangle;
}

void UBGraphicsShapeItem::setKind(Kind kind)
{
    if (kind < 0 || kind >= KindCount)
        kind = Rectangle;

    prepareGeometryChange();
    mKind = kind;
    geometryChanged();
}

void UBGraphicsShapeItem::setBoxSize(const QSizeF& size)
{
    QSizeF newSize(qMax(0., size.width()), qMax(0., size.height()));

    if (newSize != mBoxSize)
    {
        prepareGeometryChange();
        mBoxSize = newSize;
        geometryChanged();
    }
}

void UBGraphicsShapeItem::setGeometryFromPoints(const QPointF& p1, const QPointF& p2)
{
    QRectF rect = QRectF(p1, p2).normalized();

    setPos(rect.topLeft());
    setLineDirection(p1.x() <= p2.x(), p1.y() <= p2.y());
    setBoxSize(rect.size());
}

QLineF UBGraphicsShapeItem::line() const
{
    const qreal w = mBoxSize.width();
    const qreal h = mBoxSize.height();

    QPointF start(mLineStartsLeft ? 0 : w, mLineStartsTop ? 0 : h);
    QPointF end(mLineStartsLeft ? w : 0, mLineStartsTop ? h : 0);

    return QLineF(start, end);
}

void UBGraphicsShapeItem::setLineDirection(bool startsLeft, bool startsTop)
{
    if (startsLeft != mLineStartsLeft || startsTop != mLineStartsTop)
    {
        mLineStartsLeft = startsLeft;
        mLineStartsTop = startsTop;
        geometryChanged();
    }
}

void UBGraphicsShapeItem::setArrowHeads(bool start, bool end)
{
    prepareGeometryChange();
    mStartArrow = start;
    mEndArrow = end;
    geometryChanged();
}

void UBGraphicsShapeItem::setStrokeWidth(qreal width)
{
    prepareGeometryChange();
    QPen p = pen();
    p.setWidthF(qMax(0.5, width));
    setPen(p);
    geometryChanged();
}

void UBGraphicsShapeItem::setColorOnDarkBackground(const QColor& color)
{
    mColorOnDarkBackground = color;
    update();
}

void UBGraphicsShapeItem::setColorOnLightBackground(const QColor& color)
{
    mColorOnLightBackground = color;
    update();
}

void UBGraphicsShapeItem::setFilled(bool filled)
{
    mFilled = filled;
    update();
}

void UBGraphicsShapeItem::setFillOnDarkBackground(const QColor& color)
{
    mFillOnDarkBackground = color;
    update();
}

void UBGraphicsShapeItem::setFillOnLightBackground(const QColor& color)
{
    mFillOnLightBackground = color;
    update();
}

QColor UBGraphicsShapeItem::strokeColor(bool darkBackground) const
{
    return darkBackground ? mColorOnDarkBackground : mColorOnLightBackground;
}

QColor UBGraphicsShapeItem::fillColor(bool darkBackground) const
{
    QColor color = darkBackground ? mFillOnDarkBackground : mFillOnLightBackground;

    if (!color.isValid())
    {
        // default fill: stroke color with 25% alpha
        color = strokeColor(darkBackground);
        color.setAlphaF(0.25);
    }

    return color;
}

qreal UBGraphicsShapeItem::cornerRadius() const
{
    const qreal smallerSide = qMin(mBoxSize.width(), mBoxSize.height());
    return qMin(smallerSide / 4., qMax(12., strokeWidth() * 3.));
}

QPolygonF UBGraphicsShapeItem::trianglePolygon() const
{
    const qreal w = mBoxSize.width();
    const qreal h = mBoxSize.height();

    QPolygonF polygon;
    polygon << QPointF(w / 2., 0) << QPointF(w, h) << QPointF(0, h);
    return polygon;
}

qreal UBGraphicsShapeItem::arrowHeadLength() const
{
    return qMax(12., strokeWidth() * 4.);
}

QPolygonF UBGraphicsShapeItem::arrowHead(const QPointF& tip, const QPointF& from) const
{
    QLineF direction(tip, from);

    if (direction.length() < 0.001)
        direction = QLineF(tip, tip - QPointF(1, 0));

    const qreal length = arrowHeadLength();
    const QLineF unit = direction.unitVector();
    const QPointF back = tip + (unit.p2() - unit.p1()) * length;
    const QPointF normal(-(unit.p2() - unit.p1()).y(), (unit.p2() - unit.p1()).x());
    const qreal halfWidth = length * 0.45;

    QPolygonF polygon;
    polygon << tip << back + normal * halfWidth << back - normal * halfWidth;
    return polygon;
}

QLineF UBGraphicsShapeItem::visibleLine() const
{
    QLineF l = line();
    const qreal length = l.length();

    if (length < 0.001)
        return l;

    // shorten the line under the arrow heads, so that the round cap does not show at the tip
    const qreal shortening = qMin(arrowHeadLength() * 0.6, length / 2.);
    const QPointF unit = (l.p2() - l.p1()) / length;

    QPointF p1 = l.p1();
    QPointF p2 = l.p2();

    if (mKind == Arrow || mKind == Line)
    {
        if (mStartArrow)
            p1 += unit * shortening;

        if (mEndArrow)
            p2 -= unit * shortening;
    }

    return QLineF(p1, p2);
}

QPainterPath UBGraphicsShapeItem::outlinePath() const
{
    QPainterPath path;
    const QRectF rect = box();

    switch (mKind)
    {
    case Rectangle:
        path.addRect(rect);
        break;

    case RoundedRectangle:
    {
        const qreal radius = cornerRadius();
        path.addRoundedRect(rect, radius, radius);
        break;
    }

    case Ellipse:
        path.addEllipse(rect);
        break;

    case Triangle:
        path.addPolygon(trianglePolygon());
        path.closeSubpath();
        break;

    case Line:
    case Arrow:
    default:
    {
        const QLineF l = line();
        path.moveTo(l.p1());
        path.lineTo(l.p2());
        break;
    }
    }

    return path;
}

qreal UBGraphicsShapeItem::margin() const
{
    qreal m = strokeWidth() / 2.;

    if (isLinear() && (mStartArrow || mEndArrow))
    {
        m += arrowHeadLength();
    }

    return m;
}

QRectF UBGraphicsShapeItem::boundingRect() const
{
    const qreal m = margin();
    return box().adjusted(-m, -m, m, m);
}

QPainterPath UBGraphicsShapeItem::shape() const
{
    QPainterPath outline = outlinePath();

    QPainterPathStroker stroker;
    stroker.setWidth(qMax(strokeWidth(), minHitWidth));
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);

    QPainterPath path = stroker.createStroke(outline);

    if (isLinear())
    {
        const QLineF l = line();

        if (mStartArrow)
            path.addPolygon(arrowHead(l.p1(), l.p2()));

        if (mEndArrow)
            path.addPolygon(arrowHead(l.p2(), l.p1()));
    }
    else
    {
        // closed shapes can be grabbed at their inside
        path.addPath(outline);
    }

    path.setFillRule(Qt::WindingFill);
    return path;
}

bool UBGraphicsShapeItem::isOnDarkBackground() const
{
    const UBGraphicsScene* ubScene = dynamic_cast<const UBGraphicsScene*>(QGraphicsItem::scene());
    return ubScene ? ubScene->isDarkBackground() : false;
}

void UBGraphicsShapeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    const bool dark = isOnDarkBackground();

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    QPen strokePen = pen();
    strokePen.setColor(strokeColor(dark));
    strokePen.setCapStyle(Qt::RoundCap);
    strokePen.setJoinStyle(Qt::RoundJoin);
    painter->setPen(strokePen);

    if (isLinear())
    {
        painter->setBrush(Qt::NoBrush);
        painter->drawLine(visibleLine());

        if (mStartArrow || mEndArrow)
        {
            const QLineF l = line();
            painter->setBrush(strokeColor(dark));

            if (mStartArrow)
                painter->drawPolygon(arrowHead(l.p1(), l.p2()));

            if (mEndArrow)
                painter->drawPolygon(arrowHead(l.p2(), l.p1()));
        }
    }
    else
    {
        painter->setBrush(mFilled ? QBrush(fillColor(dark)) : QBrush(Qt::NoBrush));
        painter->drawPath(outlinePath());
    }

    painter->restore();

    // never draw the rubber band, the selection is drawn with the delegate frame
    QStyleOptionGraphicsItem styleOption(*option);
    styleOption.state &= ~QStyle::State_Selected;
    Delegate()->postpaint(painter, &styleOption, widget);
}

UBItem* UBGraphicsShapeItem::deepCopy() const
{
    UBGraphicsShapeItem* copy = new UBGraphicsShapeItem(mKind);

    copy->setUuid(this->uuid());
    copyItemParameters(copy);

    return copy;
}

void UBGraphicsShapeItem::copyItemParameters(UBItem* copy) const
{
    UBGraphicsShapeItem* cp = dynamic_cast<UBGraphicsShapeItem*>(copy);

    if (cp)
    {
        cp->setKind(mKind);
        cp->setPen(pen());
        cp->setBrush(brush());
        cp->setBoxSize(mBoxSize);
        cp->setLineDirection(mLineStartsLeft, mLineStartsTop);
        cp->setArrowHeads(mStartArrow, mEndArrow);
        cp->setColorOnDarkBackground(mColorOnDarkBackground);
        cp->setColorOnLightBackground(mColorOnLightBackground);
        cp->setFilled(mFilled);
        cp->setFillOnDarkBackground(mFillOnDarkBackground);
        cp->setFillOnLightBackground(mFillOnLightBackground);

        cp->setPos(this->pos());
        cp->setTransform(this->transform());
        cp->setFlag(QGraphicsItem::ItemIsMovable, true);
        cp->setFlag(QGraphicsItem::ItemIsSelectable, true);
        cp->setData(UBGraphicsItemData::ItemLayerType, this->data(UBGraphicsItemData::ItemLayerType));
        cp->setData(UBGraphicsItemData::ItemLocked, this->data(UBGraphicsItemData::ItemLocked));
        cp->setData(UBGraphicsItemData::ItemIsHiddenOnDisplay, this->data(UBGraphicsItemData::ItemIsHiddenOnDisplay));
        cp->setData(UBGraphicsItemData::ItemOwnZValue, this->data(UBGraphicsItemData::ItemOwnZValue));

        cp->setZValue(this->zValue());
    }
}

std::shared_ptr<UBGraphicsScene> UBGraphicsShapeItem::scene()
{
    auto scenePtr = dynamic_cast<UBGraphicsScene*>(QGraphicsItem::scene());
    return scenePtr ? scenePtr->shared_from_this() : nullptr;
}

void UBGraphicsShapeItem::resize(qreal w, qreal h)
{
    const qreal m = margin();
    setBoxSize(QSizeF(w - 2 * m, h - 2 * m));
}

QSizeF UBGraphicsShapeItem::size() const
{
    return boundingRect().size();
}

void UBGraphicsShapeItem::geometryChanged()
{
    update();

    if (Delegate() && Delegate()->frame())
    {
        Delegate()->positionHandles();
    }

    std::shared_ptr<UBGraphicsScene> ubScene = scene();

    if (ubScene)
    {
        ubScene->setModified(true);
    }
}

void UBGraphicsShapeItem::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    if (Delegate()->mousePressEvent(event))
    {
        // NOOP: the delegate has selected the item
    }
    else
    {
        QAbstractGraphicsShapeItem::mousePressEvent(event);
    }

    event->accept();
}

void UBGraphicsShapeItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    if (isLocked(this))
    {
        event->accept();
        return;
    }

    if (!Delegate()->mouseMoveEvent(event))
    {
        QAbstractGraphicsShapeItem::mouseMoveEvent(event);
    }
}

void UBGraphicsShapeItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    Delegate()->mouseReleaseEvent(event);
    QAbstractGraphicsShapeItem::mouseReleaseEvent(event);
}

QVariant UBGraphicsShapeItem::itemChange(GraphicsItemChange change, const QVariant& value)
{
    QVariant newValue = value;

    if (Delegate())
        newValue = Delegate()->itemChange(change, value);

    return QAbstractGraphicsShapeItem::itemChange(change, newValue);
}
