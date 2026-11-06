/*
 * Copyright (C) 2015-2022 Département de l'Instruction Publique (DIP-SEM)
 *
 * Copyright (C) 2013 Open Education Foundation
 *
 * Copyright (C) 2010-2013 Groupement d'Intérêt Public pour
 * l'Education Numérique en Afrique (GIP ENA)
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




#ifndef UBDISPLAYMANAGER_H_
#define UBDISPLAYMANAGER_H_

#include <QMap>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QTimer>
#include <QWindow>

class UBBlackoutWidget;
class UBBoardView;

namespace Ui
{
    class BlackoutWidget;
}

enum class ScreenRole : int
{
    None = 0, Control, Display, Desktop, Previous1, Previous2, Previous3, Previous4, Previous5
};

// prefix increment
ScreenRole& operator++(ScreenRole& role);

// postfix increment
ScreenRole operator++(ScreenRole& role, int);

class UBDisplayManager : public QObject
{
    Q_OBJECT;

    public:
        UBDisplayManager(QObject *parent = 0);
        virtual ~UBDisplayManager();

        int numScreens();

        int numPreviousViews();

        void setControlWidget(QWidget* pControlWidget);

        void setDisplayWidget(QWidget* pDisplayWidget);

        void setDesktopWidget(QWidget* pDesktopWidget);

        void setPreviousDisplaysWidgets(QList<UBBoardView*> pPreviousViews);

        QWidget* widget(ScreenRole role) const;
        QScreen* screen(ScreenRole role) const;

        QList<QScreen*> availableScreens() const;

        bool hasControl()
        {
            return mScreensByRole.value(ScreenRole::Control);
        }

        bool hasDisplay()
        {
            return mUseMultiScreen && mScreensByRole.value(ScreenRole::Display);
        }

        bool hasPrevious()
        {
            return mUseMultiScreen && mScreensByRole.value(ScreenRole::Previous1);
        }

        bool useMultiScreen()
        {
            return mUseMultiScreen;
        }

        void setUseMultiScreen(bool pUse);

        QSize screenSize(ScreenRole role) const;
        QSize availableScreenSize(ScreenRole role) const;
        QRect screenGeometry(ScreenRole role) const;
        qreal physicalDpi(ScreenRole role) const;
        qreal logicalDpi(ScreenRole role) const;

        void grab(ScreenRole role, std::function<void(QPixmap)> callback, QRect rect = {}) const;
        QPixmap grabGlobal(QRect rect) const;

        void initScreensByRole();
        void assignRoles();
        void positionScreens();
        void adjustScreens();

        // window mode: let the control role (and with it the display and desktop screens)
        // follow the screen the control window is on now
        void followControlWindow();

        void setScreenLabelParent(QWidget* labelParent);
        QStringList availableScreenIndexes();

        // Full screen vs. window mode of the control (main) window.
        // Initialised from App/RunInWindow, can be toggled at runtime.
        bool isControlFullScreen() const
        {
            return mControlFullScreen;
        }

        void setControlFullScreen(bool fullScreen);

        // to be called when the window state of the control window changed outside
        // of OpenBoard's control (e.g. the macOS green zoom button)
        void controlWindowStateChanged();

        // persist geometry and maximised state of the control window in window mode
        void saveControlWindowGeometry();

        // window flags of the main window in window mode
        static Qt::WindowFlags windowModeFlags();

    signals:
        void screenRolesAssigned();
        void screenLayoutChanged();
        void availableScreenCountChanged(int screenCount);
        void screenLabelPressed(const QString& screenIndex);
        void controlFullScreenChanged(bool fullScreen);

    public slots:
        void blackout();
        void unBlackout();
        void showScreenLabels(bool show);
        void disableScreenLabels(QStringList screenList);

    private slots:

        void addOrRemoveScreen(QScreen* screen);

    private:

        void initScreenIndexes();
        void createScreenLabels();
        QList<QScreen*> orderedScreens() const;
        QScreen* controlWindowScreen() const;
        bool isDesktopModeActive() const;
        static bool isShownOn(QWidget* widget, QScreen* screen);
        void watchControlWindow();
        void positionControlWindow(QWidget* controlWidget);
        QRect defaultControlWindowGeometry() const;
        static bool isOnScreen(const QRect& geometry);

        QList<UBBlackoutWidget*> mBlackoutWidgets;
        QList<Ui::BlackoutWidget*> mBlackoutUiList;

        QList<QScreen*> mAvailableScreens;
        QMap<ScreenRole, QPointer<QScreen>> mScreensByRole;
        QMap<ScreenRole, QPointer<QWidget>> mWidgetsByRole;

        QList<QPointer<QPushButton>> mScreenLabels;
        QWidget* mLabelParent{nullptr};

        bool mUseMultiScreen;

        bool mControlFullScreen{false};
        bool mControlWindowInitialized{false};
        QPointer<QWindow> mWatchedControlWindow;
        QTimer mFollowControlWindowTimer;     // roles follow the control window once it settled on a screen
        QRect mWindowedGeometry;            // normal geometry in window mode before switching to full screen
        bool mWindowedMaximized{false};     // maximised state in window mode before switching to full screen
};

#endif /* UBDISPLAYMANAGER_H_ */
