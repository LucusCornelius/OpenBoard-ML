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

#ifndef UBWINDOWDRAGSTRIP_H_
#define UBWINDOWDRAGSTRIP_H_

#include <QWidget>

class QToolBar;

/**
 * The grabbable top edge of a window whose content runs below a transparent
 * title bar (the full size content window on macOS, see UBGlassEffect).
 *
 * There the title bar zone belongs to the content: without this strip a press
 * below the traffic lights reaches the board (and draws) instead of moving the
 * window. The strip takes the presses in that zone: press and drag moves the
 * window with the system window move (QWindow::startSystemMove(), native on
 * every platform, also to another screen), a double click does what a double
 * click on the title bar does (UBGlassEffect::titleBarDoubleClick()). It shows
 * the application name and version as the title of the window.
 *
 * The strip exists only while the title bar overlaps the content
 * (UBGlassEffect::topInset() > 0): not in full screen, not for a frameless
 * window and not on platforms with a native title bar (Windows, Linux).
 *
 * Overlay: lies over the top edge of its parent (the board), directly above the
 *          board view and below all panels created after it, so the board keeps
 *          its size and the panels stay usable.
 * Reserve: for a QMainWindow page without a toolbar in the title bar row (web
 *          and documents with the toolbars at the bottom): the window content
 *          moves down below the strip. Active only while setActive(true).
 *
 * A toolbar in the title bar row (web and documents mode) is the title bar
 * itself: enableToolBarDrag() lets its empty space move the window.
 */
class UBWindowDragStrip : public QWidget
{
    Q_OBJECT

    public:
        enum class Placement
        {
            Overlay,
            Reserve
        };

        UBWindowDragStrip(QWidget* parent, Placement placement);

        /** Reserve placement: whether the current page of the window wants the strip */
        void setActive(bool active);

        /** the empty space of a toolbar in the title bar row moves the window */
        static void enableToolBarDrag(QToolBar* toolBar);

    protected:
        bool event(QEvent* event) override;
        bool eventFilter(QObject* watched, QEvent* event) override;
        void mousePressEvent(QMouseEvent* event) override;
        void mouseMoveEvent(QMouseEvent* event) override;
        void mouseReleaseEvent(QMouseEvent* event) override;
        void mouseDoubleClickEvent(QMouseEvent* event) override;
        void paintEvent(QPaintEvent* event) override;

    private:
        void scheduleUpdatePlacement();
        void updatePlacement();
        bool darkBackdrop() const;

        Placement mPlacement;
        bool mActive;
        bool mHover;
        bool mUpdateScheduled;
};

#endif /* UBWINDOWDRAGSTRIP_H_ */
