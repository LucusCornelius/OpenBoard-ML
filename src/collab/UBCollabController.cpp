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

#include "UBCollabController.h"

#include <QAction>
#include <QApplication>
#include <QIconEngine>
#include <QPainter>
#include <QToolButton>
#include <QUrl>

#include "board/UBBoardController.h"
#include "board/UBBoardView.h"
#include "board/UBDrawingController.h"
#include "core/UBApplication.h"
#include "core/UBSettings.h"
#include "document/UBDocument.h"
#include "document/UBDocumentProxy.h"
#include "document/UBDocumentToc.h"
#include "domain/UBGraphicsScene.h"
#include "gui/UBGlassStyle.h"

#include "UBCollabBoardLink.h"
#include "UBCollabCursorOverlay.h"
#include "UBCollabItems.h"
#include "UBCollabPanel.h"
#include "UBCollabRelayChannel.h"

#include "core/memcheck.h"

namespace
{
    const QString settingRelay = "Collab/RelayAddress";
    const QString settingRelayKey = "Collab/RelayKey";
    const QString settingName = "Collab/DisplayName";
    const QString settingFollow = "Collab/FollowPage";

    /**
     * The glyph of the button with the number of people in the session: a
     * flat badge at the lower right, so that a session that is on can be
     * seen at a glance without a panel of its own.
     */
    class UBCollabBadgeIconEngine : public QIconEngine
    {
        public:
            UBCollabBadgeIconEngine(const QIcon& glyph, int count, const QColor& color)
                : mGlyph(glyph)
                , mCount(count)
                , mColor(color)
            {
            }

            void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State state) override
            {
                mGlyph.paint(painter, rect, Qt::AlignCenter, mode, state);

                const qreal diameter = qMax<qreal>(9.0, rect.height() * 0.52);
                const QRectF badge(rect.right() + 1 - diameter, rect.bottom() + 1 - diameter, diameter, diameter);

                painter->save();
                painter->setRenderHint(QPainter::Antialiasing);
                painter->setPen(Qt::NoPen);
                painter->setBrush(mColor);
                painter->drawEllipse(badge);

                QFont font = painter->font();
                font.setPixelSize(qMax(7, int(diameter * 0.68)));
                font.setBold(true);
                painter->setFont(font);
                painter->setPen(Qt::white);
                painter->drawText(badge, Qt::AlignCenter, mCount > 9 ? QString("9+") : QString::number(mCount));
                painter->restore();
            }

            QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
            {
                QPixmap pixmap(size);
                pixmap.fill(Qt::transparent);
                QPainter painter(&pixmap);
                paint(&painter, QRect(QPoint(0, 0), size), mode, state);
                return pixmap;
            }

            QIconEngine* clone() const override
            {
                return new UBCollabBadgeIconEngine(mGlyph, mCount, mColor);
            }

        private:
            QIcon mGlyph;
            int mCount;
            QColor mColor;
    };

    QToolButton* buttonFor(QAction* action)
    {
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
        const QList<QObject*> objects = action->associatedObjects();
#else
        const QList<QWidget*> objects = action->associatedWidgets();
#endif

        for (QObject* object : objects)
        {
            QToolButton* button = qobject_cast<QToolButton*>(object);

            if (button && button->parentWidget())
                return button;
        }

        return nullptr;
    }
}


UBCollabController* UBCollabController::instance()
{
    static QPointer<UBCollabController> controller;

    if (!controller)
        controller = new UBCollabController(qApp);

    return controller;
}


UBCollabController::UBCollabController(QObject* parent)
    : QObject(parent)
{
    mAction = new QAction(UBGlassStyle::icon("collab"), tr("Work together"), this);
    mAction->setObjectName("actionWorkTogether");
    mAction->setCheckable(true);
    mAction->setToolTip(tr("Work together on this board with others"));

    connect(mAction, &QAction::triggered, this, [this]() {
        showPanel(!(mPanel && mPanel->isVisible()));
    });

    // the session ends while the board is still there: the last changes are sent and saved
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this]() {
        if (!mSession)
            return;

        UBCollabSession* session = mSession;
        disconnect(session, nullptr, this, nullptr);
        mSession = nullptr;
        session->leave();
        delete session;
    });
}


QString UBCollabController::relayAddress()
{
    return UBSettings::settings()->value(settingRelay, QString()).toString();
}


void UBCollabController::setRelayAddress(const QString& address)
{
    UBSettings::settings()->setValue(settingRelay, address.trimmed());
}


QString UBCollabController::relayKey()
{
    return UBSettings::settings()->value(settingRelayKey, QString()).toString();
}


void UBCollabController::setRelayKey(const QString& key)
{
    UBSettings::settings()->setValue(settingRelayKey, key);
}


QString UBCollabController::displayName()
{
    // no default from the system: a participant without a name is shown as "Participant"
    return UBSettings::settings()->value(settingName, QString()).toString();
}


void UBCollabController::setDisplayName(const QString& name)
{
    UBSettings::settings()->setValue(settingName, name.simplified().left(40));
}


bool UBCollabController::followsPage()
{
    return UBSettings::settings()->value(settingFollow, true).toBool();
}


void UBCollabController::setFollowsPage(bool follow)
{
    UBSettings::settings()->setValue(settingFollow, follow);

    // back on: to the page of the group
    if (follow && mSession && mSession->sync())
        remoteCurrentPage(mSession->sync()->currentPage());
}


void UBCollabController::ensureWidgets()
{
    UBBoardController* board = UBApplication::boardController;

    if (!board)
        return;

    static bool connected = false;

    if (!connected)
    {
        connected = true;
        connect(board, &UBBoardController::activeSceneChanged, this, &UBCollabController::boardSceneChanged);

        UBCollabBoardLink* link = UBCollabBoardLink::instance();
        connect(link, &UBCollabBoardLink::pointerMoved, this, &UBCollabController::pointerMoved);
        connect(link, &UBCollabBoardLink::pointerLeft, this, &UBCollabController::pointerLeft);
        connect(link, &UBCollabBoardLink::gestureEnded, this, [this]() {
            if (!mPendingPage.isNull())
            {
                const QUuid page = mPendingPage;
                mPendingPage = QUuid();
                remoteCurrentPage(page);
            }
        });
    }

    if (!mPanel)
    {
        QToolButton* button = buttonFor(mAction);
        QWidget* container = button && button->parentWidget() ? button->parentWidget()->parentWidget() : nullptr;

        if (!container)
            container = board->controlContainer();

        if (container)
        {
            mPanel = new UBCollabPanel(this, container);
            mPanel->setAnchor(button);
        }
    }

    if (!mOverlay && board->controlView())
        mOverlay = new UBCollabCursorOverlay(board->controlView());
}


void UBCollabController::showPanel(bool show)
{
    ensureWidgets();

    if (!mPanel)
        return;

    if (show)
    {
        mPanel->refresh();
        mPanel->show();
        mPanel->raise();
        mPanel->reposition();
    }
    else
    {
        mPanel->hide();
    }

    mAction->setChecked(show);
}


bool UBCollabController::startSession()
{
    if (mSession && mSession->state() != UBCollabSession::Ended)
        return false;

    UBBoardController* board = UBApplication::boardController;
    const QUrl relay(relayAddress().trimmed());

    if (!UBCollabRelayChannel::isRelayAddress(relay))
    {
        mLastMessage = tr("Enter the address of a relay first, in the relay section of this panel.");

        if (mPanel)
            mPanel->refresh();

        return false;
    }

    if (!board || !board->selectedDocument())
        return false;

    const QString code = UBCollabRelayChannel::newSessionCode();
    UBCollabRelayChannel* channel = new UBCollabRelayChannel();
    UBCollabSession* session = new UBCollabSession(UBCollabSession::Host, channel, board->selectedDocument());
    session->setDisplayName(displayName());

    adoptSession(session, UBCollabRelayChannel::invitation(relay, code));
    channel->start(relay, code, relayKey());
    return true;
}


bool UBCollabController::joinSession(const QString& invitation)
{
    if (mSession && mSession->state() != UBCollabSession::Ended)
        return false;

    QUrl relay;
    QString code;

    if (!UBCollabRelayChannel::parseInvitation(invitation.trimmed(), relay, code))
    {
        mLastMessage = tr("This is not an invitation to a session. Paste the whole line you received.");

        if (mPanel)
            mPanel->refresh();

        return false;
    }

    UBCollabRelayChannel* channel = new UBCollabRelayChannel();
    UBCollabSession* session = new UBCollabSession(UBCollabSession::Guest, channel, nullptr);
    session->setDisplayName(displayName());

    adoptSession(session, QString());
    channel->join(relay, code);
    return true;
}


void UBCollabController::leaveSession()
{
    if (mSession)
        mSession->leave();
}


void UBCollabController::adoptSession(UBCollabSession* session, const QString& invitation, bool showJoinedBoard)
{
    if (mSession && mSession != session)
    {
        UBCollabSession* previous = mSession;
        disconnect(previous, nullptr, this, nullptr);
        previous->leave();
        previous->deleteLater();
    }

    mSession = session;
    mInvitation = invitation;
    mLastMessage.clear();
    mShowJoinedBoard = showJoinedBoard;

    if (!session)
        return;

    session->setParent(this);

    connect(session, &UBCollabSession::stateChanged, this, &UBCollabController::sessionStateChanged);
    connect(session, &UBCollabSession::participantsChanged, this, &UBCollabController::sessionStateChanged);
    connect(session, &UBCollabSession::snapshotProgress, this, &UBCollabController::sessionStateChanged);
    connect(session, &UBCollabSession::ended, this, &UBCollabController::sessionEnded);
    connect(session, &UBCollabSession::remoteCurrentPage, this, &UBCollabController::remoteCurrentPage);
    connect(session, &UBCollabSession::localOnlyAdded, this, &UBCollabController::localOnlyAdded);
    connect(session, &UBCollabSession::notSent, this, [](const QString& what) {
        Q_UNUSED(what);
        UBApplication::showMessage(tr("Something is too large to be shared and stays on your board only."));
    });

    connect(session, &UBCollabSession::joined, this, [this](std::shared_ptr<UBDocumentProxy> document) {
        if (!mShowJoinedBoard || !mSession || !mSession->sync())
            return;

        // the board of the host, at the page of the group
        const int index = qMax(0, UBDocument::getDocument(document)->toc()->findUuid(mSession->sync()->currentPage()));
        UBCollabBoardLink::instance()->showPage(document, index);
    });

    ensureWidgets();

    if (mOverlay)
        mOverlay->setSession(session);

    sessionStateChanged();
    emit sessionChanged();
}


void UBCollabController::sessionStateChanged()
{
    updateIndicator();

    if (mPanel)
        mPanel->refresh();

    if (mOverlay)
        mOverlay->update();
}


void UBCollabController::sessionEnded(UBCollabSession::EndReason reason)
{
    mLastMessage = endMessage(reason);

    if (reason != UBCollabSession::Left && !mLastMessage.isEmpty())
        UBApplication::showMessage(mLastMessage);

    if (mOverlay)
        mOverlay->setSession(nullptr);

    if (mSession)
    {
        disconnect(mSession.data(), nullptr, this, nullptr);
        mSession->deleteLater();
    }

    mSession = nullptr;
    mInvitation.clear();
    mPendingPage = QUuid();

    updateIndicator();

    if (mPanel)
        mPanel->refresh();

    emit sessionChanged();
}


QString UBCollabController::endMessage(UBCollabSession::EndReason reason)
{
    switch (reason)
    {
        case UBCollabSession::NotEnded:
        case UBCollabSession::Left:
            return QString();
        case UBCollabSession::HostEnded:
            return tr("The host ended the session. You keep your copy of the board.");
        case UBCollabSession::HostGone:
            return tr("The host left the session. You keep your copy of the board.");
        case UBCollabSession::ConnectionLost:
            return tr("The connection was lost. You keep your copy of the board.");
        case UBCollabSession::RelayUnreachable:
            return tr("The relay cannot be reached. Check the address of the relay and the internet connection.");
        case UBCollabSession::RelayRefused:
            return tr("The relay does not let you start a session. Check the relay key.");
        case UBCollabSession::SessionNotFound:
            return tr("There is no session for this invitation. It may have ended.");
        case UBCollabSession::SessionFull:
            return tr("The session is full.");
        case UBCollabSession::WrongCode:
            return tr("The invitation is not right: the board of the session cannot be read with it.");
        case UBCollabSession::VersionMismatch:
            return tr("The other uses a different version of OpenBoard-ML. Both need the same version.");
        case UBCollabSession::InvalidData:
            return tr("The host sent data that cannot be right. The session was ended; you keep your copy of the board.");
        case UBCollabSession::SnapshotFailed:
            return tr("The board of the host could not be received.");
        case UBCollabSession::NoAnswer:
            return tr("Nobody answers for this session.");
    }

    return QString();
}


void UBCollabController::updateIndicator()
{
    const bool on = mSession && mSession->state() == UBCollabSession::Active;
    const QIcon glyph = UBGlassStyle::icon("collab");

    if (on)
    {
        const int count = mSession->participantCount();
        mAction->setIcon(QIcon(new UBCollabBadgeIconEngine(glyph, count, QColor(0x30, 0xB0, 0x50))));
        mAction->setToolTip(count == 1 ? tr("Work together: a session is on, only you so far")
                                       : tr("Work together: a session is on with %1 people").arg(count));
    }
    else
    {
        mAction->setIcon(glyph);
        mAction->setToolTip(tr("Work together on this board with others"));
    }
}


void UBCollabController::boardSceneChanged()
{
    if (mOverlay)
        mOverlay->update();

    // a page turned by someone else, or by the board after a change of someone else, is not sent
    if (!mSession || !mSession->sync() || mFollowing || mSession->sync()->isApplying() || !followsPage())
        return;

    UBCollabBoardLink* link = UBCollabBoardLink::instance();
    const QUuid page = link->shownPage(mSession->document());

    // a page turned by the user takes the others along
    if (!page.isNull())
        mSession->sync()->setLocalCurrentPage(page);
}


void UBCollabController::remoteCurrentPage(const QUuid& page)
{
    if (!mSession || !followsPage() || page.isNull())
        return;

    UBCollabBoardLink* link = UBCollabBoardLink::instance();
    std::shared_ptr<UBDocumentProxy> document = mSession->document();

    if (!link->showsDocument(document))
        return;

    // never in the middle of a stroke: once the pen is lifted
    if (link->isGestureActive())
    {
        mPendingPage = page;
        return;
    }

    const int index = UBDocument::getDocument(document)->toc()->findUuid(page);

    if (index < 0 || page == link->shownPage(document))
        return;

    mFollowing = true;
    link->showPage(document, index);
    mFollowing = false;
}


void UBCollabController::followNow()
{
    if (mSession && mSession->sync())
        remoteCurrentPage(mSession->sync()->currentPage());
}


void UBCollabController::pointerMoved(const QPointF& scenePos, bool pressed)
{
    if (!mSession || mSession->state() != UBCollabSession::Active)
        return;

    UBBoardController* board = UBApplication::boardController;
    UBCollabBoardLink* link = UBCollabBoardLink::instance();

    if (!board || !link->showsDocument(mSession->document()))
        return;

    const int tool = UBDrawingController::drawingController()->stylusTool();
    const bool marker = tool == UBStylusTool::Marker;
    const bool drawing = pressed && (tool == UBStylusTool::Pen || marker);

    QColor color = marker ? board->markerColorOnLightBackground() : board->penColorOnLightBackground();

    if (marker && color.alpha() == 255)
        color.setAlphaF(UBSettings::settings()->boardMarkerAlpha->get().toDouble());

    // the width on the board in page units, so that it is right at any zoom of the others
    const qreal scale = board->systemScaleFactor() * board->currentZoom();
    const qreal screenWidth = marker ? UBSettings::settings()->currentMarkerWidth() : UBSettings::settings()->currentPenWidth();
    const qreal width = scale > 0 ? screenWidth / scale : screenWidth;

    mSession->setPointer(board->activeScene()->uuid(), scenePos, true, drawing, tool, color, width);
}


void UBCollabController::pointerLeft()
{
    if (mSession && mSession->state() == UBCollabSession::Active && UBApplication::boardController
            && UBApplication::boardController->activeScene())
    {
        mSession->setPointer(UBApplication::boardController->activeScene()->uuid(), QPointF(), false, false, 0, QColor(), 0);
    }
}


void UBCollabController::localOnlyAdded(int kind)
{
    switch (kind)
    {
        case UBCollabItems::LocalTool:
            UBApplication::showMessage(tr("The ruler, the compass and the other drawing tools are not shared: the others do not see them."));
            break;

        case UBCollabItems::LocalContent:
            UBApplication::showMessage(tr("Videos, sound, apps, web content and PDF pages added during the session are not shared: they stay on your board only."));
            break;

        default:
            UBApplication::showMessage(tr("Videos, sound, apps and web content of the shared board are not shared and were left out."));
            break;
    }
}
