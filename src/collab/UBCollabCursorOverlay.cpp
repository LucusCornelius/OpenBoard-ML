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

#include "UBCollabCursorOverlay.h"

#include <QEvent>
#include <QGraphicsView>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>

#include "board/UBBoardController.h"
#include "core/UBApplication.h"
#include "domain/UBGraphicsScene.h"

#include "UBCollabBoardLink.h"
#include "UBCollabSession.h"

#include "core/memcheck.h"

UBCollabCursorOverlay::UBCollabCursorOverlay(QGraphicsView* view)
    : QWidget(view)
    , mView(view)
{
    setObjectName("collabCursors");
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::NoFocus);

    if (mView)
    {
        // a sibling of the viewport: scrolling the viewport does not move it
        mView->viewport()->installEventFilter(this);
        connect(mView->horizontalScrollBar(), &QScrollBar::valueChanged, this, qOverload<>(&QWidget::update));
        connect(mView->verticalScrollBar(), &QScrollBar::valueChanged, this, qOverload<>(&QWidget::update));
    }

    if (UBApplication::boardController)
        connect(UBApplication::boardController, &UBBoardController::zoomChanged, this, qOverload<>(&QWidget::update));

    place();
    hide();
}


void UBCollabCursorOverlay::setSession(UBCollabSession* session)
{
    if (mSession)
        disconnect(mSession.data(), nullptr, this, nullptr);

    mSession = session;

    if (mSession)
    {
        connect(mSession.data(), &UBCollabSession::cursorsChanged, this, qOverload<>(&QWidget::update));
        connect(mSession.data(), &UBCollabSession::participantsChanged, this, qOverload<>(&QWidget::update));
        place();
        show();
        raise();
    }
    else
    {
        hide();
    }

    update();
}


bool UBCollabCursorOverlay::eventFilter(QObject* watched, QEvent* event)
{
    if (mView && watched == mView->viewport() && (event->type() == QEvent::Resize || event->type() == QEvent::Move))
        place();

    return QWidget::eventFilter(watched, event);
}


void UBCollabCursorOverlay::place()
{
    if (mView)
        setGeometry(mView->viewport()->geometry());
}


void UBCollabCursorOverlay::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    if (!mSession || !mView || mSession->state() != UBCollabSession::Active)
        return;

    const QUuid shown = UBCollabBoardLink::instance()->shownPage(mSession->document());

    if (shown.isNull())
        return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const qreal scale = mView->transform().m11();
    const std::shared_ptr<UBGraphicsScene> scene = UBCollabBoardLink::instance()->editedScene(mSession->document());
    const bool dark = scene && scene->isDarkBackground();
    QFont font = painter.font();
    font.setPixelSize(11);
    font.setWeight(QFont::DemiBold);
    painter.setFont(font);

    QHash<UBCollabChannel::PeerId, QString> names;

    for (const UBCollabSession::Participant& participant : mSession->participants())
        names.insert(participant.peer, UBCollabSession::displayName(participant));

    const QHash<UBCollabChannel::PeerId, UBCollabSession::Cursor>& cursors = mSession->cursors();

    for (auto it = cursors.constBegin(); it != cursors.constEnd(); ++it)
    {
        const UBCollabSession::Cursor& cursor = it.value();

        if (cursor.page != shown)
            continue;

        const QColor color = UBCollabSession::colorFor(it.key());

        // the stroke being drawn, until the real one arrives
        if (cursor.stroke.size() >= 2)
        {
            QPainterPath path;
            path.moveTo(mView->mapFromScene(cursor.stroke.first()));

            for (int i = 1; i < cursor.stroke.size(); ++i)
                path.lineTo(mView->mapFromScene(cursor.stroke.at(i)));

            QColor ink = cursor.color.isValid() ? cursor.color : color;

            // the colour is sent for a light page; black ink is white on a dark page, as strokes are
            if (dark && ink.lightness() < 40)
            {
                const int alpha = ink.alpha();
                ink = Qt::white;
                ink.setAlpha(alpha);
            }

            painter.setPen(QPen(ink, qMax(1.0, cursor.width * scale), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(path);
        }

        if (!cursor.visible)
            continue;

        const QPointF at = mView->mapFromScene(cursor.position);

        // a flat dot with a light ring, readable on light and dark pages
        painter.setPen(QPen(QColor(255, 255, 255, 230), 1.5));
        painter.setBrush(color);
        painter.drawEllipse(at, 5.0, 5.0);

        const QString name = names.value(it.key(), tr("Participant"));
        const QFontMetrics metrics(font);
        const QRectF label(at + QPointF(9, 7), QSizeF(metrics.horizontalAdvance(name) + 14, metrics.height() + 6));

        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawRoundedRect(label, label.height() / 2, label.height() / 2);
        painter.setPen(Qt::white);
        painter.drawText(label, Qt::AlignCenter, name);
    }
}
