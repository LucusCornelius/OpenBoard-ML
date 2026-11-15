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

#ifndef UBCOLLABPANEL_H_
#define UBCOLLABPANEL_H_

#include <QPointer>

#include "gui/UBFloatingPalette.h"

class QCheckBox;
class QLabel;
class QLineEdit;
class QStackedWidget;
class QToolButton;
class QVBoxLayout;
class UBCollabController;

/** a thin flat bar for the progress of the board that arrives */
class UBCollabProgressBar : public QWidget
{
    Q_OBJECT

    public:
        explicit UBCollabProgressBar(QWidget* parent = nullptr);

        /** 0..1, or below 0 for "busy" without a known amount */
        void setValue(qreal value);

        QSize sizeHint() const override;

    protected:
        void paintEvent(QPaintEvent* event) override;

    private:
        qreal mValue{-1};
};


/**
 * The glass panel of "Work together", below its button at the top right of
 * the board: start a session or join one, the invitation to pass on, who is
 * in the session, "follow page", leaving; the address of the relay and the
 * name in a small section of their own. It shows the state of
 * UBCollabController and asks it to act.
 */
class UBCollabPanel : public UBFloatingPalette
{
    Q_OBJECT

    public:
        UBCollabPanel(UBCollabController* controller, QWidget* parent);

        int border() override;

        /** the button it belongs to; it is placed below it */
        void setAnchor(QWidget* anchor);

        /** shows the state of the controller */
        void refresh();

        void togglePanel();
        void reposition();

    protected:
        bool eventFilter(QObject* watched, QEvent* event) override;
        void showEvent(QShowEvent* event) override;
        void hideEvent(QHideEvent* event) override;
        void keyPressEvent(QKeyEvent* event) override;
        void mousePressEvent(QMouseEvent* event) override;
        void mouseMoveEvent(QMouseEvent* event) override;
        void mouseReleaseEvent(QMouseEvent* event) override;
        void onCloseButtonClicked() override;

    private:
        QWidget* buildIdlePage();
        QWidget* buildBusyPage();
        QWidget* buildSessionPage();
        QToolButton* makeButton(const QString& text, bool primary);
        QLabel* makeHint(const QString& text);
        void rebuildParticipants();
        void startClicked();
        void joinClicked();
        void copyClicked();
        void saveSettings();

        QPointer<UBCollabController> mController;
        QPointer<QWidget> mAnchor;
        bool mRepositionPending{false};

        QLabel* mTitle{nullptr};
        QStackedWidget* mPages{nullptr};
        QLabel* mMessage{nullptr};

        // not in a session
        QWidget* mIdlePage{nullptr};
        QToolButton* mStartButton{nullptr};
        QLineEdit* mInvitationInput{nullptr};
        QToolButton* mJoinButton{nullptr};
        QLineEdit* mNameInput{nullptr};
        QLineEdit* mRelayInput{nullptr};
        QLineEdit* mRelayKeyInput{nullptr};

        // on the way in
        QWidget* mBusyPage{nullptr};
        QLabel* mBusyLabel{nullptr};
        UBCollabProgressBar* mProgress{nullptr};
        QToolButton* mCancelButton{nullptr};

        // in a session
        QWidget* mSessionPage{nullptr};
        QLabel* mSessionLabel{nullptr};
        QWidget* mInvitationBox{nullptr};
        QLineEdit* mInvitationOutput{nullptr};
        QToolButton* mCopyButton{nullptr};
        QWidget* mParticipantList{nullptr};
        QVBoxLayout* mParticipantLayout{nullptr};
        QCheckBox* mFollowBox{nullptr};
        QToolButton* mLeaveButton{nullptr};
};

#endif /* UBCOLLABPANEL_H_ */
