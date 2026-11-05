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


#ifndef UBGRAPHICSSHAPEITEM_H_
#define UBGRAPHICSSHAPEITEM_H_

#include <QtGui>
#include <QAbstractGraphicsShapeItem>

#include "core/UB.h"
#include "domain/UBItem.h"
#include "domain/UBResizableGraphicsItem.h"

class UBGraphicsScene;

/**
 * @brief A vector shape (rectangle, rounded rectangle, ellipse, triangle, line, arrow).
 *
 * The geometry of the shape is described by a "box" which always starts at the item
 * origin: QRectF(0, 0, width, height). The stroke is centered on the outline of the box.
 * Lines and arrows are drawn as a diagonal of the box, the direction being stored in
 * two flags. Moving and rotating is done with the item position / transform, resizing
 * changes the box so that the stroke width stays constant.
 *
 * Colors are stored for light and dark backgrounds; the matching variant is selected
 * at paint time depending on the background of the scene.
 */
class UBGraphicsShapeItem : public QAbstractGraphicsShapeItem, public UBItem, public UBResizableGraphicsItem, public UBGraphicsItem
{
    public:
        enum Kind
        {
            Rectangle = 0,
            RoundedRectangle,
            Ellipse,
            Triangle,
            Line,
            Arrow,
            KindCount
        };

        UBGraphicsShapeItem(Kind kind = Rectangle, QGraphicsItem* parent = nullptr);
        virtual ~UBGraphicsShapeItem();

        enum { Type = UBGraphicsItemType::ShapeItemType };

        virtual int type() const override
        {
            return Type;
        }

        static QString kindToString(Kind kind);
        static Kind kindFromString(const QString& name, bool* ok = nullptr);

        Kind kind() const { return mKind; }
        void setKind(Kind kind);

        bool isLinear() const { return mKind == Line || mKind == Arrow; }
        bool isFillable() const { return !isLinear(); }

        // geometry
        QRectF box() const { return QRectF(QPointF(0, 0), mBoxSize); }
        QSizeF boxSize() const { return mBoxSize; }
        void setBoxSize(const QSizeF& size);

        /**
         * Set the geometry from two points given in parent coordinates. Sets the position
         * of the item to the top left of the normalized rectangle and the box size to its size.
         * For lines and arrows, p1 is the start and p2 the end point.
         */
        void setGeometryFromPoints(const QPointF& p1, const QPointF& p2);

        // line direction, only used for Line and Arrow
        QLineF line() const;
        void setLineDirection(bool startsLeft, bool startsTop);
        bool lineStartsLeft() const { return mLineStartsLeft; }
        bool lineStartsTop() const { return mLineStartsTop; }

        bool hasStartArrow() const { return mStartArrow; }
        bool hasEndArrow() const { return mEndArrow; }
        void setArrowHeads(bool start, bool end);

        // style
        qreal strokeWidth() const { return pen().widthF(); }
        void setStrokeWidth(qreal width);

        QColor colorOnDarkBackground() const { return mColorOnDarkBackground; }
        QColor colorOnLightBackground() const { return mColorOnLightBackground; }
        void setColorOnDarkBackground(const QColor& color);
        void setColorOnLightBackground(const QColor& color);

        bool isFilled() const { return mFilled; }
        void setFilled(bool filled);
        QColor fillOnDarkBackground() const { return mFillOnDarkBackground; }
        QColor fillOnLightBackground() const { return mFillOnLightBackground; }
        void setFillOnDarkBackground(const QColor& color);
        void setFillOnLightBackground(const QColor& color);

        // colors for a given background
        QColor strokeColor(bool darkBackground) const;
        QColor fillColor(bool darkBackground) const;

        qreal cornerRadius() const;
        QPolygonF trianglePolygon() const;
        qreal arrowHeadLength() const;
        QPolygonF arrowHead(const QPointF& tip, const QPointF& from) const;

        /// the path of the outline (lines and arrows: the line only, without heads)
        QPainterPath outlinePath() const;
        /// the path of the visible line (shortened at the arrow heads)
        QLineF visibleLine() const;

        // QGraphicsItem
        virtual QRectF boundingRect() const override;
        virtual QPainterPath shape() const override;
        virtual void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

        // UBItem
        virtual UBItem* deepCopy() const override;
        virtual void copyItemParameters(UBItem* copy) const override;
        virtual std::shared_ptr<UBGraphicsScene> scene() override;

        // UBResizableGraphicsItem: the size is the size of the bounding rect
        virtual void resize(qreal w, qreal h) override;
        virtual QSizeF size() const override;

    protected:
        virtual void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
        virtual void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
        virtual void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
        virtual QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;

    private:
        qreal margin() const;
        bool isOnDarkBackground() const;
        void geometryChanged();

        Kind mKind;
        QSizeF mBoxSize;
        bool mLineStartsLeft;
        bool mLineStartsTop;
        bool mStartArrow;
        bool mEndArrow;

        QColor mColorOnDarkBackground;
        QColor mColorOnLightBackground;
        bool mFilled;
        QColor mFillOnDarkBackground;
        QColor mFillOnLightBackground;
};

#endif /* UBGRAPHICSSHAPEITEM_H_ */
