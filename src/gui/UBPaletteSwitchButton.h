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

#ifndef UBPALETTESWITCHBUTTON_H_
#define UBPALETTESWITCHBUTTON_H_

#include <QPointer>
#include <QToolButton>

class QAction;
class QMenu;
class QTimer;

/**
 * Small chevron button placed directly before the colour swatches of the pen
 * panel, to switch between the named colour palettes quickly.
 *
 *  - click: next palette (wraps around), through the "Next Color Palette"
 *    action so that its shortcut and the button stay in sync;
 *  - right-click or press and hold (stylus): a compact menu with all palettes
 *    (name, a row of their colours, a check mark on the active one) and
 *    "Manage palettes..." (the palette dialog).
 *
 * The button is hidden while there is only one palette.
 */
class UBPaletteSwitchButton : public QToolButton
{
    Q_OBJECT

    public:
        UBPaletteSwitchButton(QAction* nextPaletteAction, QWidget* parent = nullptr);
        virtual ~UBPaletteSwitchButton();

    public slots:
        /** show the menu with all palettes next to the button */
        void showPaletteMenu();

    protected:
        void mousePressEvent(QMouseEvent* event) override;
        void mouseMoveEvent(QMouseEvent* event) override;
        void mouseReleaseEvent(QMouseEvent* event) override;
        void contextMenuEvent(QContextMenuEvent* event) override;

    private slots:
        void refresh();
        void holdTimeout();
        void nextPalette();

    private:
        QPointer<QAction> mNextPaletteAction;
        QTimer* mHoldTimer;
        bool mHeld;
        QPointer<QMenu> mMenu;
};

#endif /* UBPALETTESWITCHBUTTON_H_ */
