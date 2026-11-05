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


#ifndef UBGRAPHICSSHAPEITEMDELEGATE_H_
#define UBGRAPHICSSHAPEITEMDELEGATE_H_

#include "domain/UBGraphicsItemDelegate.h"

class UBGraphicsShapeItem;

/**
 * @brief Delegate of the shape items.
 *
 * Reuses the frame of UBGraphicsItemDelegate for moving, rotating and resizing.
 * The frame operates in "Resizing" mode, so that resizing changes the geometry
 * of the shape instead of scaling it. The context menu allows to change the
 * stroke color, the fill and the arrow heads.
 */
class UBGraphicsShapeItemDelegate : public UBGraphicsItemDelegate
{
    Q_OBJECT

    public:
        UBGraphicsShapeItemDelegate(UBGraphicsShapeItem* pDelegated, QObject* parent = nullptr);
        virtual ~UBGraphicsShapeItemDelegate();

        virtual void createControls() override;

    protected:
        virtual void decorateMenu(QMenu* menu) override;
        virtual void updateMenuActionState() override;

    private slots:
        void setFilled(bool filled);
        void pickStrokeColor();
        void pickFillColor();
        void setStartArrow(bool arrow);
        void setEndArrow(bool arrow);
        void setThinStroke();
        void setMediumStroke();
        void setThickStroke();

    private:
        UBGraphicsShapeItem* shapeItem() const;
        QColor pickColor(const QColor& initial, const QString& title, bool withAlpha);
        void setStrokeWidthIndex(int index);
        void contentChanged();

        QAction* mFillAction;
        QAction* mStartArrowAction;
        QAction* mEndArrowAction;
};

#endif /* UBGRAPHICSSHAPEITEMDELEGATE_H_ */
