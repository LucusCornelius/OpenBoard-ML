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

#include "UBCollabBoardLink.h"

#include <QApplication>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QTabletEvent>
#include <QTimer>
#include <QTouchEvent>

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
#include <QInputDevice>
#endif

#include "board/UBBoardController.h"
#include "board/UBBoardView.h"
#include "core/UBApplication.h"
#include "core/UBSettings.h"
#include "document/UBDocument.h"
#include "document/UBDocumentProxy.h"
#include "document/UBDocumentToc.h"
#include "domain/UBGraphicsScene.h"

#include "core/memcheck.h"

namespace
{
    QPointF eventPosition(QEvent* event)
    {
        switch (event->type())
        {
            case QEvent::MouseButtonPress:
            case QEvent::MouseButtonRelease:
            case QEvent::MouseMove:
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
                return static_cast<QMouseEvent*>(event)->position();
#else
                return static_cast<QMouseEvent*>(event)->localPos();
#endif

            case QEvent::TabletPress:
            case QEvent::TabletRelease:
            case QEvent::TabletMove:
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
                return static_cast<QTabletEvent*>(event)->position();
#else
                return static_cast<QTabletEvent*>(event)->posF();
#endif

            case QEvent::HoverMove:
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
                return static_cast<QHoverEvent*>(event)->position();
#else
                return static_cast<QHoverEvent*>(event)->posF();
#endif

            default:
                return QPointF();
        }
    }
}


UBCollabBoardLink* UBCollabBoardLink::instance()
{
    static QPointer<UBCollabBoardLink> link;

    if (!link)
        link = new UBCollabBoardLink(qApp);

    link->watchBoard();
    return link;
}


UBCollabBoardLink::UBCollabBoardLink(QObject* parent)
    : QObject(parent)
    , mStaleTimer(new QTimer(this))
{
    mStaleTimer->setInterval(500);
    connect(mStaleTimer, &QTimer::timeout, this, &UBCollabBoardLink::checkStaleGesture);
}


void UBCollabBoardLink::watchBoard()
{
    if (mViewport || !UBApplication::boardController || !UBApplication::boardController->controlView())
        return;

    mViewport = UBApplication::boardController->controlView()->viewport();

    if (mViewport)
    {
        mViewport->setMouseTracking(true);
        mViewport->installEventFilter(this);
    }
}


std::shared_ptr<UBGraphicsScene> UBCollabBoardLink::editedScene(std::shared_ptr<UBDocumentProxy> document) const
{
    if (!showsDocument(document))
        return nullptr;

    return UBApplication::boardController->activeScene();
}


QUuid UBCollabBoardLink::shownPage(std::shared_ptr<UBDocumentProxy> document) const
{
    const std::shared_ptr<UBGraphicsScene> scene = editedScene(document);
    return scene ? scene->uuid() : QUuid();
}


bool UBCollabBoardLink::showsDocument(std::shared_ptr<UBDocumentProxy> document) const
{
    UBBoardController* board = UBApplication::boardController;
    return document && board && board->selectedDocument() == document && board->activeScene();
}


void UBCollabBoardLink::showPage(std::shared_ptr<UBDocumentProxy> document, int index)
{
    UBBoardController* board = UBApplication::boardController;

    if (!board || !document || index < 0)
        return;

    if (board->selectedDocument() == document && board->activeSceneIndex() == index)
        return;

    board->setActiveDocumentScene(document, index);
}


void UBCollabBoardLink::pagesChanged(std::shared_ptr<UBDocumentProxy> document, const QUuid& shownBefore)
{
    UBBoardController* board = UBApplication::boardController;

    if (!board || !showsDocument(document) || shownBefore.isNull())
        return;

    const int index = UBDocument::getDocument(document)->toc()->findUuid(shownBefore);

    // a removed page was already replaced by the board itself
    if (index >= 0 && index != board->activeSceneIndex())
        board->setActiveDocumentScene(document, index);
}


void UBCollabBoardLink::backgroundChanged(UBGraphicsScene* scene, bool dark, const UBBackgroundRuling* ruling, int gridSize)
{
    UBBoardController* board = UBApplication::boardController;

    if (board && board->activeScene().get() == scene)
    {
        // as a change by the user: the palette and the colours of the tools follow
        if (gridSize > 0 && gridSize != scene->backgroundGridSize())
        {
            UBSettings::settings()->crossSize = gridSize;
            scene->setBackgroundGridSize(gridSize);
        }

        board->setBackground(dark, ruling);
        return;
    }

    if (scene)
    {
        scene->setSceneBackground(dark, ruling);

        if (gridSize > 0)
            scene->setBackgroundGridSize(gridSize);
    }
}


void UBCollabBoardLink::pageSizeChanged(UBGraphicsScene* scene, const QSize& size)
{
    UBBoardController* board = UBApplication::boardController;

    if (board && board->activeScene().get() == scene)
        board->setPageSize(size);
    else if (scene)
        scene->setNominalSize(size);
}


void UBCollabBoardLink::saveShownPage(std::shared_ptr<UBDocumentProxy> document)
{
    if (showsDocument(document))
        UBApplication::boardController->persistCurrentScene(true, true);
}


QWidget* UBCollabBoardLink::boardViewport() const
{
    return mViewport;
}


bool UBCollabBoardLink::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != mViewport)
        return QObject::eventFilter(watched, event);

    switch (event->type())
    {
        case QEvent::MouseButtonPress:
            startGesture(Mouse);
            pointerEvent(eventPosition(event), true);
            break;

        case QEvent::TabletPress:
            startGesture(Tablet);
            pointerEvent(eventPosition(event), true);
            break;

        case QEvent::TouchBegin:
        case QEvent::TouchUpdate:
        {
            // the touch pad of a laptop sends touches to the widget under the pointer: they
            // are no press on the board; a touch screen is
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
            const bool touchPad = static_cast<QTouchEvent*>(event)->device()
                    && static_cast<QTouchEvent*>(event)->device()->type() == QInputDevice::DeviceType::TouchPad;
#else
            const bool touchPad = static_cast<QTouchEvent*>(event)->device()
                    && static_cast<QTouchEvent*>(event)->device()->type() == QTouchDevice::TouchPad;
#endif

            if (!touchPad)
            {
                if (event->type() == QEvent::TouchBegin)
                    startGesture(Touch);
                else if (mGestureActive)
                    mLastGestureEvent.restart();
            }
            break;
        }

        case QEvent::MouseButtonRelease:
        case QEvent::TabletRelease:
            pointerEvent(eventPosition(event), false);
            endGesture();
            break;

        case QEvent::TouchEnd:
        case QEvent::TouchCancel:
            if (mGestureSource == Touch)
                endGesture();
            break;

        case QEvent::MouseMove:
        {
            const bool buttons = static_cast<QMouseEvent*>(event)->buttons() != Qt::NoButton;

            // the release of this press went elsewhere: the press is over
            if (mGestureActive && mGestureSource == Mouse && !buttons)
                endGesture();
            else if (mGestureActive && buttons)
                mLastGestureEvent.restart();

            pointerEvent(eventPosition(event), mGestureActive);
            break;
        }

        case QEvent::TabletMove:
        {
            QTabletEvent* tablet = static_cast<QTabletEvent*>(event);
            const bool touching = tablet->pressure() > 0 || tablet->buttons() != Qt::NoButton;

            if (mGestureActive && mGestureSource == Tablet && !touching)
                endGesture();
            else if (mGestureActive && touching)
                mLastGestureEvent.restart();

            pointerEvent(eventPosition(event), mGestureActive);
            break;
        }

        case QEvent::HoverMove:
            pointerEvent(eventPosition(event), mGestureActive);
            break;

        case QEvent::Leave:
            if (!mGestureActive)
                emit pointerLeft();
            break;

        default:
            break;
    }

    return QObject::eventFilter(watched, event);
}


void UBCollabBoardLink::startGesture(Source source)
{
    mGestureActive = true;
    mGestureSource = source;
    mLastGestureEvent.restart();
    mStaleTimer->start();
}


void UBCollabBoardLink::endGesture()
{
    mStaleTimer->stop();

    if (!mGestureActive)
        return;

    mGestureActive = false;
    mGestureSource = NoSource;

    // once the board handled the release (the stroke is finished, its undo step pushed)
    QTimer::singleShot(0, this, &UBCollabBoardLink::gestureEnded);
}


void UBCollabBoardLink::checkStaleGesture()
{
    if (!mGestureActive)
    {
        mStaleTimer->stop();
        return;
    }

    // still pressed: a pen held still, a button held down
    if (mLastGestureEvent.elapsed() < 3000 || QGuiApplication::mouseButtons() != Qt::NoButton)
        return;

    UBBoardController* board = UBApplication::boardController;

    if (board && board->activeScene() && board->activeScene()->isInputDevicePressed())
        return;

    // a press whose release never reached the board must not hold the sync back
    endGesture();
}


void UBCollabBoardLink::pointerEvent(const QPointF& viewportPos, bool pressed)
{
    UBBoardController* board = UBApplication::boardController;

    if (!board || !board->controlView())
        return;

    emit pointerMoved(board->controlView()->mapToScene(viewportPos.toPoint()), pressed);
}
