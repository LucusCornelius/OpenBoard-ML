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

#ifndef UBCOLLABCURSOROVERLAY_H_
#define UBCOLLABCURSOROVERLAY_H_

#include <QPointer>
#include <QWidget>

class QGraphicsView;
class UBCollabSession;

/**
 * The cursors of the others on the board: a small mark in the colour of the
 * participant with the name, and the stroke he is drawing, while he is on the
 * page that is shown. A transparent layer over the board that takes no
 * events; nothing of it is part of the page (not saved, not in thumbnails).
 */
class UBCollabCursorOverlay : public QWidget
{
    Q_OBJECT

    public:
        explicit UBCollabCursorOverlay(QGraphicsView* view);

        void setSession(UBCollabSession* session);

    protected:
        bool eventFilter(QObject* watched, QEvent* event) override;
        void paintEvent(QPaintEvent* event) override;

    private:
        void place();

        QPointer<QGraphicsView> mView;
        QPointer<UBCollabSession> mSession;
};

#endif /* UBCOLLABCURSOROVERLAY_H_ */
