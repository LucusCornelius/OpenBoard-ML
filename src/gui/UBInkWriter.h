/*
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

#ifndef UBINKWRITER_H
#define UBINKWRITER_H

#include <QObject>
#include <QPointer>
#include <QPolygonF>

#include "domain/UBMathInkLayout.h"

class QWidget;
class UBInkGhost;

/**
 * Writes laid out maths (see UBMathInkLayout) on the board as real pen strokes:
 * current pen colour and width, one strokes group and one undo step per formula.
 *
 * Placement: beginPlacement() waits for the next press on the board, showing a grey
 * preview of the formula that follows the pointer. The formula is centred on the
 * press position. Escape, a right click or another tool cancels the placement.
 */
class UBInkWriter : public QObject
{
    Q_OBJECT

    public:
        explicit UBInkWriter(QObject* parent = nullptr);
        virtual ~UBInkWriter();

        /** Size of one em on the board, in scene units (from Board/MathInkSizeCm and the grid size). */
        static qreal emSizeInScene();

        /** The formula as handwriting (smoothed and jittered strokes), in em units. */
        static QList<QPolygonF> inkStrokes(const UBMathInkLayout::Formula& formula);

        /** Write the formula on the active scene, centred on the scene position. */
        static bool write(const UBMathInkLayout::Formula& formula, const QPointF& scenePos);

        void beginPlacement(const UBMathInkLayout::Formula& formula);
        bool isPlacing() const { return mPlacing; }

    public slots:
        void cancelPlacement();

    signals:
        void placementStarted();
        void placementFinished(bool written);

    protected:
        bool eventFilter(QObject* watched, QEvent* event) override;

    private:
        void endPlacement(bool written);
        void updateGhost(const QPoint& viewportPos);
        QWidget* boardViewport() const;

        bool mPlacing{false};
        bool mSwallowRelease{false};
        bool mFilterInstalled{false};
        UBMathInkLayout::Formula mFormula;
        QList<QPolygonF> mInk;
        QRectF mInkBounds;
        QPointer<QWidget> mViewport;
        QPointer<UBInkGhost> mGhost;
};

#endif // UBINKWRITER_H
