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

#ifndef UBPENPANEL_H_
#define UBPENPANEL_H_

#include <QList>

#include "UBFloatingPalette.h"

class QFrame;
class QHBoxLayout;

/**
 * Floating glass panel anchored at the top centre of the board, holding the
 * pen options: colour swatches, custom colour, line width, eraser width and
 * the shape kind while the shapes tool is active.
 *
 * The panel only hosts the widgets, the actions behind them (and therefore the
 * shortcuts and the drawing controller signals) are unchanged. Sections are
 * separated by dividers which follow the visibility of the sections. When the
 * toolbar is at the bottom, the panel is anchored above the stylus palette.
 * The panel can be dragged; a double click on its background re-anchors it.
 */
class UBPenPanel : public UBFloatingPalette
{
    Q_OBJECT

    public:
        explicit UBPenPanel(QWidget* parent = nullptr);
        virtual ~UBPenPanel();

        /** append a section, a divider separates it from the previous visible section */
        void addSection(QWidget* section);

    public slots:
        /** move the panel to its anchor (unless the user dragged it) and keep it inside the board */
        void reposition();

    protected:
        bool event(QEvent* event) override;
        bool eventFilter(QObject* watched, QEvent* event) override;
        void mouseDoubleClickEvent(QMouseEvent* event) override;

        int border() override;
        int radius() override;

    private:
        void updateDividers();

        QHBoxLayout* mLayout;
        QList<QWidget*> mSections;
        QList<QFrame*> mDividers;
        bool mRepositionPending;
};

#endif /* UBPENPANEL_H_ */
