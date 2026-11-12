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

#include "UBWindowDragStrip.h"

#include <QEvent>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QToolBar>
#include <QWindow>

#include "board/UBBoardController.h"
#include "core/UBApplication.h"
#include "core/UBThemeManager.h"
#include "domain/UBGraphicsScene.h"
#include "gui/UBGlassEffect.h"

#include "core/memcheck.h"

namespace
{
    /** the system window move: native snapping, Spaces and moving to another screen */
    void startWindowMove(QWidget* widget)
    {
        if (QWindow* handle = widget->window()->windowHandle())
            handle->startSystemMove();
    }

    /** nothing to click at this point of the toolbar: between, before or after the buttons */
    bool isEmptyToolBarSpace(QToolBar* toolBar, const QPoint& pos)
    {
        // childAt() skips widgets transparent for the mouse (the space for the traffic lights)
        const QWidget* child = toolBar->childAt(pos);

        return !child
                || qobject_cast<const QToolBar*>(child) // empty space of a nested toolbar
                || child->metaObject() == &QWidget::staticMetaObject // spacer
                || child->inherits("QToolBarSeparator");
    }

    /**
     * A toolbar sharing the row of the traffic lights is the title bar of the
     * window: its empty space moves the window, like a unified toolbar.
     */
    class UBToolBarWindowDrag : public QObject
    {
        public:
            using QObject::QObject;

            bool eventFilter(QObject* watched, QEvent* event) override
            {
                if (event->type() != QEvent::MouseButtonPress && event->type() != QEvent::MouseButtonDblClick)
                    return false;

                QToolBar* toolBar = qobject_cast<QToolBar*>(watched);
                QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);

                // only while the toolbar lies in the title bar of a full size content window
                if (!toolBar || mouseEvent->button() != Qt::LeftButton || UBGlassEffect::topInset(toolBar) <= 0
                        || !isEmptyToolBarSpace(toolBar, mouseEvent->position().toPoint()))
                {
                    return false;
                }

                if (event->type() == QEvent::MouseButtonDblClick)
                    UBGlassEffect::titleBarDoubleClick(toolBar);
                else
                    startWindowMove(toolBar);

                event->accept();
                return true;
            }
    };
}


UBWindowDragStrip::UBWindowDragStrip(QWidget* parent, Placement placement)
    : QWidget(parent)
    , mPlacement(placement)
    , mActive(placement == Placement::Overlay)
    , mHover(false)
    , mUpdateScheduled(false)
{
    setObjectName("ubWindowDragStrip");

    // a press in the strip never reaches the board or a tool below it, also not a tablet press
    setAttribute(Qt::WA_NoMousePropagation);
    setFocusPolicy(Qt::NoFocus);
    // the title bar has the arrow, not the cursor of the current drawing tool
    setCursor(Qt::ArrowCursor);
    hide();

    // the title bar height follows the window state (full screen, frameless, display scale)
    parent->installEventFilter(this);

    if (parent->window() != parent)
        parent->window()->installEventFilter(this);

    if (QMainWindow* mainWindow = qobject_cast<QMainWindow*>(parent))
    {
        for (QToolBar* toolBar : mainWindow->findChildren<QToolBar*>(QString(), Qt::FindDirectChildrenOnly))
            toolBar->installEventFilter(this);
    }

    connect(UBThemeManager::instance(), &UBThemeManager::themeChanged, this, [this]() { update(); });

    scheduleUpdatePlacement();
}


void UBWindowDragStrip::setActive(bool active)
{
    if (active != mActive)
    {
        mActive = active;
        updatePlacement();
    }
}


void UBWindowDragStrip::enableToolBarDrag(QToolBar* toolBar)
{
    if (toolBar)
        toolBar->installEventFilter(new UBToolBarWindowDrag(toolBar));
}


bool UBWindowDragStrip::event(QEvent* event)
{
    switch (event->type())
    {
        case QEvent::TabletPress:
        case QEvent::TabletMove:
        case QEvent::TabletRelease:
            // not accepted: Qt delivers the pen as a mouse press, which moves the window like the
            // mouse does; WA_NoMousePropagation keeps the tablet event away from the board
            event->ignore();
            return true;

        case QEvent::Enter:
            mHover = true;
            update();
            break;

        case QEvent::Leave:
            mHover = false;
            update();
            break;

        default:
            break;
    }

    return QWidget::event(event);
}


bool UBWindowDragStrip::eventFilter(QObject* watched, QEvent* event)
{
    switch (event->type())
    {
        case QEvent::Resize:
            if (watched == parentWidget())
                updatePlacement();
            break;

        case QEvent::Show:
        case QEvent::Hide:
            scheduleUpdatePlacement();
            break;

        case QEvent::DynamicPropertyChange:
            if (static_cast<QDynamicPropertyChangeEvent*>(event)->propertyName() == "ubGlassTitleBarHeight")
                scheduleUpdatePlacement();
            break;

        default:
            break;
    }

    return QWidget::eventFilter(watched, event);
}


void UBWindowDragStrip::mousePressEvent(QMouseEvent* event)
{
    event->accept();

    if (event->button() == Qt::LeftButton)
        startWindowMove(this);
}


void UBWindowDragStrip::mouseMoveEvent(QMouseEvent* event)
{
    event->accept();
}


void UBWindowDragStrip::mouseReleaseEvent(QMouseEvent* event)
{
    event->accept();
}


void UBWindowDragStrip::mouseDoubleClickEvent(QMouseEvent* event)
{
    event->accept();

    if (event->button() == Qt::LeftButton)
        UBGlassEffect::titleBarDoubleClick(this);
}


void UBWindowDragStrip::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    // flat: an even veil and a hairline along the lower edge, nothing else
    const bool dark = darkBackdrop();
    const QColor veil = dark ? QColor(255, 255, 255, mHover ? 26 : 14) : QColor(0, 0, 0, mHover ? 18 : 9);
    const QColor hairline = dark ? QColor(255, 255, 255, 56) : QColor(0, 0, 0, 46);
    const qreal pixel = 1.0 / qMax<qreal>(1.0, devicePixelRatioF());

    QPainter painter(this);
    painter.fillRect(rect(), veil);
    painter.fillRect(QRectF(0, height() - pixel, width(), pixel), hairline);
}


void UBWindowDragStrip::scheduleUpdatePlacement()
{
    if (mUpdateScheduled)
        return;

    mUpdateScheduled = true;
    QTimer::singleShot(0, this, [this]() { updatePlacement(); });
}


void UBWindowDragStrip::updatePlacement()
{
    mUpdateScheduled = false;

    QWidget* host = parentWidget();

    if (!host)
        return;

    int height = 0;

    if (mPlacement == Placement::Overlay)
    {
        height = UBGlassEffect::topInset(host);
    }
    else if (QMainWindow* mainWindow = qobject_cast<QMainWindow*>(host))
    {
        // a toolbar in the title bar row is the title bar itself (enableToolBarDrag())
        bool titleBarToolBar = false;

        for (QToolBar* toolBar : mainWindow->findChildren<QToolBar*>(QString(), Qt::FindDirectChildrenOnly))
        {
            if (!toolBar->isHidden() && mainWindow->toolBarArea(toolBar) == Qt::TopToolBarArea)
                titleBarToolBar = true;
        }

        height = (mActive && !titleBarToolBar) ? UBGlassEffect::topInset(mainWindow) : 0;

        // the content of the window starts below the strip
        const QMargins margins(0, height, 0, 0);

        if (mainWindow->contentsMargins() != margins)
            mainWindow->setContentsMargins(margins);
    }

    if (height > 0)
    {
        setGeometry(0, 0, host->width(), height);

        if (mPlacement == Placement::Reserve)
            raise();

        show();
    }
    else
    {
        hide();
    }
}


bool UBWindowDragStrip::darkBackdrop() const
{
    // over the board: follow the page background (a dark page in the light theme and vice versa)
    if (mPlacement == Placement::Overlay && UBApplication::boardController)
    {
        if (std::shared_ptr<UBGraphicsScene> scene = UBApplication::boardController->activeScene())
            return scene->isDarkBackground();
    }

    return UBThemeManager::instance()->isDarkMode();
}
