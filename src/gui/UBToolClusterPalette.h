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

#ifndef UBTOOLCLUSTERPALETTE_H_
#define UBTOOLCLUSTERPALETTE_H_

#include <QList>

#include "UBFloatingPalette.h"

class QAction;
class QFrame;
class QHBoxLayout;
class QToolButton;

/**
 * A floating piece of glass holding a group of toolbar actions, anchored to a
 * corner of the board (Freeform style top bar without a toolbar strip).
 *
 * The buttons use the QActions of the main window as default actions, so
 * texts, icons, check states, menus and shortcuts stay those of the actions.
 * Groups of actions are separated by thin vertical hairlines. The cluster
 * follows the toolbar preferences: top or bottom edge, icons only or small
 * labels under the icons. At the top it keeps clear of the window title bar
 * of a full size content window (UBGlassEffect::topInset).
 */
class UBToolClusterPalette : public UBFloatingPalette
{
    Q_OBJECT

    public:
        UBToolClusterPalette(Qt::Corner anchor, QWidget* parent);
        virtual ~UBToolClusterPalette();

        /** append a group of actions, separated from the previous group by a hairline */
        void addGroup(const QList<QAction*>& actions);

        /** show small labels under the icons (true) or icons only */
        void setTextUnderIcon(bool textUnderIcon);

        QToolButton* buttonForAction(QAction* action) const;

        /** keep this horizontal distance from the anchored board edge (an open dock palette) */
        void setEdgeInset(int inset);

        /** glass bodies of the visible clusters of a board, in the coordinates of the board */
        static QList<QRect> clusterBodies(QWidget* parent);

        /** distance between the glass body and the edges of the board */
        static constexpr int edgeGap = 10;

    signals:
        /** the cluster moved to a new place (other palettes keep clear of it) */
        void repositioned();

    public slots:
        /** move the cluster to its anchor at the top or bottom edge of the board */
        void reposition();

    protected:
        bool event(QEvent* event) override;
        bool eventFilter(QObject* watched, QEvent* event) override;

        void mousePressEvent(QMouseEvent* event) override;
        void mouseMoveEvent(QMouseEvent* event) override;
        void mouseReleaseEvent(QMouseEvent* event) override;

        int border() override;
        int radius() override;

    private:
        void updateButtonVisibility();
        void scheduleReposition();

        Qt::Corner mAnchor;
        QHBoxLayout* mLayout;
        QList<QList<QToolButton*>> mGroups;
        QList<QFrame*> mSeparators;
        bool mTextUnderIcon;
        bool mRepositionPending;
        QSize mLastSize;
        int mEdgeInset;
};

#endif /* UBTOOLCLUSTERPALETTE_H_ */
