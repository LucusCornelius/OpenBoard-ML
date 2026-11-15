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

#ifndef UBCOLLABCONTROLLER_H_
#define UBCOLLABCONTROLLER_H_

#include <QObject>
#include <QPointer>
#include <QString>
#include <QUuid>

#include <memory>

#include "UBCollabCursorOverlay.h"
#include "UBCollabPanel.h"
#include "UBCollabSession.h"

class QAction;
class UBDocumentProxy;

/**
 * "Work together" on the board: the toolbar action and its panel, the
 * session of this computer (at most one), its settings, and what follows on
 * the board: the page of the group (follow page), the cursors of the others,
 * the notices. The board sync itself is in UBCollabSession and
 * UBCollabDocumentSync; this class only connects them to the board.
 */
class UBCollabController : public QObject
{
    Q_OBJECT

    public:
        static UBCollabController* instance();

        /** the action of the toolbar; it opens the panel */
        QAction* action() const { return mAction; }

        UBCollabSession* session() const { return mSession; }

        /** the line to pass on, for the host of a session */
        QString invitation() const { return mInvitation; }

        /** why the last session ended, for the panel */
        QString lastMessage() const { return mLastMessage; }

        // settings, with the other settings (group Collab)
        static QString relayAddress();
        static void setRelayAddress(const QString& address);
        static QString relayKey();
        static void setRelayKey(const QString& key);
        static QString displayName();
        static void setDisplayName(const QString& name);
        static bool followsPage();
        void setFollowsPage(bool follow);

        /** starts a session that shares the document on the board; false with a message when it cannot */
        bool startSession();

        /** joins the session of an invitation; false with a message when the text is no invitation */
        bool joinSession(const QString& invitation);

        /** leaves the session (the host ends it for everybody) */
        void leaveSession();

        /**
         * Shows and follows a session made elsewhere (the self-test makes one
         * over a channel inside the process). The controller takes it.
         */
        void adoptSession(UBCollabSession* session, const QString& invitation, bool showJoinedBoard = true);

        UBCollabPanel* panel() const { return mPanel; }

        /** the panel below the button of the toolbar */
        void showPanel(bool show);

        static QString endMessage(UBCollabSession::EndReason reason);

    signals:
        void sessionChanged();

    private:
        explicit UBCollabController(QObject* parent);

        void ensureWidgets();
        void updateIndicator();
        void sessionStateChanged();
        void sessionEnded(UBCollabSession::EndReason reason);
        void boardSceneChanged();
        void remoteCurrentPage(const QUuid& page);
        void followNow();
        void pointerMoved(const QPointF& scenePos, bool pressed);
        void pointerLeft();
        void localOnlyAdded(int kind);

        QAction* mAction{nullptr};
        QPointer<UBCollabPanel> mPanel;
        QPointer<UBCollabCursorOverlay> mOverlay;
        QPointer<UBCollabSession> mSession;
        QString mInvitation;
        QString mLastMessage;
        bool mShowJoinedBoard{true};
        bool mFollowing{false};
        QUuid mPendingPage;
};

#endif /* UBCOLLABCONTROLLER_H_ */
