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

#include "UBDisplayManager.h"

#include "frameworks/UBPlatformUtils.h"

#include "core/UBApplication.h"
#include "core/UBApplicationController.h"
#include "core/UBSettings.h"

#include "board/UBBoardView.h"
#include "board/UBBoardController.h"

#include "gui/UBBlackoutWidget.h"

#include "ui_blackoutWidget.h"

#include "core/memcheck.h"

UBDisplayManager::UBDisplayManager(QObject *parent)
    : QObject(parent)
{
    mUseMultiScreen = UBSettings::settings()->appUseMultiscreen->get().toBool();
    mControlFullScreen = !UBSettings::settings()->appRunInWindow->get().toBool();

    initScreenIndexes();

    connect(qApp, &QGuiApplication::screenAdded, this, &UBDisplayManager::addOrRemoveScreen);
    connect(qApp, &QGuiApplication::screenRemoved, this, &UBDisplayManager::addOrRemoveScreen);
    connect(qApp, &QGuiApplication::primaryScreenChanged, this, &UBDisplayManager::addOrRemoveScreen);

    connect(UBSettings::settings()->appScreenList, &UBSetting::changed, this, &UBDisplayManager::adjustScreens);

    mFollowControlWindowTimer.setSingleShot(true);
    mFollowControlWindowTimer.setInterval(500);
    connect(&mFollowControlWindowTimer, &QTimer::timeout, this, &UBDisplayManager::followControlWindow);
}

UBDisplayManager::~UBDisplayManager()
{
    // NOOP
}

void UBDisplayManager::initScreenIndexes()
{
    mScreensByRole.clear();
    QScreen* primaryScreen = QGuiApplication::primaryScreen();
    QList<QScreen*> screens = primaryScreen->virtualSiblings();

    // make sure primary screen is first element and therefore never dropped
    screens.insert(0, primaryScreen);

    // drop screens which duplicate another screen, i.e. have same geometry
    for (int i = 1; i < screens.size(); )
    {
        QRect iGeomentry = screens[i]->geometry();
        bool removed = false;

        for (int j = 0; j < i; ++j)
        {
            QRect jGeometry = screens[j]->geometry();

            if (iGeomentry.contains(jGeometry) || jGeometry.contains(iGeomentry))
            {
                screens.removeAt(i);
                removed = true;
                break;
            }
        }

        if (!removed)
        {
            ++i;
        }
    }

    if (screens != mAvailableScreens)
    {
        qDebug() << "availableScreens" << screens;
        mAvailableScreens = screens;
        createScreenLabels();
        emit availableScreenCountChanged(screens.count());
    }

    /*
     * Emitting availableScreenCountChanged updated the screen configuration
     * in the UBPreferencesManager, so we now get the screen list valid for
     * the new monitor configuration.
     */
}

void UBDisplayManager::createScreenLabels()
{
    if (!mLabelParent)
    {
        // don't create screen labels before parent is known
        return;
    }

    qDeleteAll(mScreenLabels);
    mScreenLabels.clear();

    const static QSize labelSize{300, 150};
    const auto screens = availableScreens();
    int screenIndex = 1;
    QFont font;
    font.setPointSize(48);

    for (QScreen* screen : screens)
    {
        QString index = QString::number(screenIndex);

        QPushButton* button = new QPushButton{mLabelParent};
        button->setWindowFlag(Qt::FramelessWindowHint, true);
        button->setWindowFlag(Qt::WindowStaysOnTopHint, true);
        button->setWindowFlag(Qt::BypassWindowManagerHint, true);
        button->setWindowFlag(Qt::Window, true);
        button->setWindowFlag(Qt::WindowDoesNotAcceptFocus, true);
        button->setAttribute(Qt::WA_ShowWithoutActivating, true);

        button->setProperty("screenIndex", index);
#ifdef QT_DEBUG
        button->setText(index + "(" + screen->name() + ")");
#else
        button->setText(index);
#endif
        button->setFont(font);
        button->move(screen->geometry().topLeft());
        button->setMinimumSize(labelSize);
        button->setCursor(Qt::PointingHandCursor);

        connect(button, &QPushButton::pressed, this, [this](){
            QPushButton* button = dynamic_cast<QPushButton*>(sender());

            if (button)
            {
                QString screenIndex = button->property("screenIndex").toString();
                emit screenLabelPressed(screenIndex);
            }
        });

        mScreenLabels << button;
        ++screenIndex;
    }
}

void UBDisplayManager::initScreensByRole()
{
    // without a screen list: primary screen for control, the next one for display
    assignRoles();
}


QList<QScreen*> UBDisplayManager::orderedScreens() const
{
    QList<QScreen*> ordered;
    const QVariant appScreenList = UBSettings::settings()->appScreenList->get();
    const QStringList screenList = appScreenList.toStringList();

    if (!appScreenList.isValid())
    {
        // no entry in configuration files ("old" configuration mode):
        // primary screen for control, next screen for display, the others for previous pages
        ordered = mAvailableScreens;

        if (ordered.count() > 1 && UBSettings::settings()->swapControlAndDisplayScreens->get().toBool())
        {
            std::swap(ordered[0], ordered[1]);
        }
    }
    else if (screenList.isEmpty() || screenList.at(0).isEmpty())
    {
        // explicitly stored an empty list (converting an empty entry to a QStringList
        // creates a list with one empty QString): take all available screens
        ordered = mAvailableScreens;
    }
    else
    {
        // "new" configuration mode using a list of screen indexes (1 based)
        for (const QString& entry : screenList)
        {
            const int index = entry.toInt();

            if (index < 1 || index > mAvailableScreens.size())
            {
                qDebug() << "Warning: Screen index out of range, skipped:" << index;
                continue;
            }

            QScreen* screen = mAvailableScreens[index - 1];

            if (screen && !ordered.contains(screen))
            {
                ordered << screen;
            }
        }
    }

    if (ordered.isEmpty() && !mAvailableScreens.isEmpty())
    {
        // by default use the primary screen
        ordered << mAvailableScreens.first();
    }

    return ordered;
}


QScreen* UBDisplayManager::controlWindowScreen() const
{
    QWidget* controlWidget = widget(ScreenRole::Control);

    // the legacy kiosk window (frameless) is placed on the control screen by OpenBoard,
    // a normal window (also in full screen) is wherever the user put it
    if (!controlWidget || !mControlWindowInitialized || (controlWidget->windowFlags() & Qt::FramelessWindowHint))
    {
        return nullptr;
    }

    QScreen* screen = QGuiApplication::screenAt(controlWidget->geometry().center());

    if (!screen)
    {
        screen = controlWidget->screen();
    }

    // screens duplicating another screen are not in the list
    return mAvailableScreens.contains(screen) ? screen : nullptr;
}


void UBDisplayManager::assignRoles()
{
    QVariant appScreenList = UBSettings::settings()->appScreenList->get();
    QStringList screenList = appScreenList.toStringList();
    qDebug() << "assignRoles using screen list" << screenList;

    // disable screen labels of screens in list
    disableScreenLabels(screenList);

    QList<QScreen*> ordered = orderedScreens();

    // The control role belongs to the screen the control window is on: the projection
    // goes to another screen and must never cover the window, the desktop annotation
    // overlay and screen captures are on the screen the user works on.
    if (QScreen* windowScreen = controlWindowScreen())
    {
        ordered.removeAll(windowScreen);
        ordered.prepend(windowScreen);
    }

    // "show on second screen" was switched on: use another screen for the display
    // even when the screen list names only one
    if (mUseMultiScreen && ordered.count() < 2)
    {
        for (QScreen* screen : std::as_const(mAvailableScreens))
        {
            if (!ordered.contains(screen))
            {
                ordered << screen;
                break;
            }
        }
    }

    mScreensByRole.clear();

    if (!ordered.isEmpty())
    {
        mScreensByRole[ScreenRole::Control] = ordered[0];
    }

    // the display (projection) screen only exists in multi screen mode
    if (mUseMultiScreen && ordered.count() > 1)
    {
        mScreensByRole[ScreenRole::Display] = ordered[1];
    }

    // configure previous screens
    ScreenRole role = ScreenRole::Previous1;

    for (int i = 2; i < ordered.count() && role <= ScreenRole::Previous5; ++i)
    {
        mScreensByRole[role++] = ordered[i];
    }

    // Desktop screen is same as Control screen
    mScreensByRole[ScreenRole::Desktop] = mScreensByRole.value(ScreenRole::Control);

    emit screenRolesAssigned();
}

int UBDisplayManager::numScreens()
{
    return mAvailableScreens.count();
}


int UBDisplayManager::numPreviousViews()
{
    int previousViews = 0;

    for (ScreenRole role = ScreenRole::Previous1; role <= ScreenRole::Previous5; ++role)
    {
        if (screen(role))
        {
            ++previousViews;
        }
    }

    return previousViews;
}


void UBDisplayManager::setControlWidget(QWidget* pControlWidget)
{
    if (pControlWidget)
    {
        mWidgetsByRole[ScreenRole::Control] = pControlWidget;
    }
}

void UBDisplayManager::setDesktopWidget(QWidget* pDesktopWidget )
{
    if (pDesktopWidget)
    {
        mWidgetsByRole[ScreenRole::Desktop] = pDesktopWidget;
    }
}

void UBDisplayManager::setDisplayWidget(QWidget* pDisplayWidget)
{
    if (pDisplayWidget && pDisplayWidget != widget((ScreenRole::Display)))
    {
        if (widget(ScreenRole::Display))
        {
            widget(ScreenRole::Display)->hide();
            pDisplayWidget->setGeometry(widget(ScreenRole::Display)->geometry());
            pDisplayWidget->setWindowFlags(widget(ScreenRole::Display)->windowFlags());
        }

        mWidgetsByRole[ScreenRole::Display] = pDisplayWidget;

        // only shown in multi screen mode, never on top of the control window
        if (hasDisplay())
        {
            pDisplayWidget->setGeometry(screen(ScreenRole::Display)->geometry());
            UBPlatformUtils::showFullScreen(pDisplayWidget);
        }
    }
}


void UBDisplayManager::setPreviousDisplaysWidgets(QList<UBBoardView*> pPreviousViews)
{
    ScreenRole role(ScreenRole::Previous1);

    for (int i = 0; i < pPreviousViews.size(); ++i)
    {
        mWidgetsByRole[role++] = pPreviousViews[i];
    }
}

QWidget* UBDisplayManager::widget(ScreenRole role) const
{
    return mWidgetsByRole.value(role);
}

QScreen* UBDisplayManager::screen(ScreenRole role) const
{
    return mScreensByRole.value(role);
}

QList<QScreen *> UBDisplayManager::availableScreens() const
{
    return mAvailableScreens;
}

void UBDisplayManager::adjustScreens()
{
    assignRoles();

    if (UBPlatformUtils::sessionType() == UBPlatformUtils::SessionType::WAYLAND)
    {
        auto controlWidget = widget(ScreenRole::Control);

        // hide windows for repositioning
        if (controlWidget && hasControl() && controlWidget->isVisible())
        {
            for (auto widget : mWidgetsByRole)
            {
                if (widget)
                {
                    widget->hide();
                }
            }

            QTimer::singleShot(100, this, [this](){
                positionScreens();
            });

            return;
        }
    }

    positionScreens();
}

void UBDisplayManager::setScreenLabelParent(QWidget* labelParent)
{
    mLabelParent = labelParent;
    createScreenLabels();
}

QStringList UBDisplayManager::availableScreenIndexes()
{
    QStringList screenIndexes;

    for (const auto label : mScreenLabels)
    {
        screenIndexes << label->property("screenIndex").toString();
    }

    return screenIndexes;
}

void UBDisplayManager::positionScreens()
{
    // static event filter object
    static class : public QObject {
    public:
        virtual bool eventFilter(QObject* watched, QEvent* event) override
        {
            return event->type() == QEvent::Resize || QObject::eventFilter(watched, event);
        }
    } resizeEventFilter;

    qDebug() << "positionScreens";
    const bool desktopMode = isDesktopModeActive();

    if (widget(ScreenRole::Control) && hasControl())
    {
        QWidget* controlWidget = widget(ScreenRole::Control);

        QRect geometry = screenGeometry(ScreenRole::Control);

        if (!mControlFullScreen)
        {
            // window mode: a normal, resizable top level window
            positionControlWindow(controlWidget);
        }
        else if (!(controlWidget->windowFlags() & Qt::FramelessWindowHint) && controlWidget->isFullScreen())
        {
            // full screen entered at runtime from window mode: keep it as it is
            // (hidden while the desktop annotation overlay is shown)
            if (controlWidget->isHidden() && !desktopMode)
            {
                controlWidget->show();
            }
        }
        else if (!desktopMode)
        {
            // Sometimes moving control screen to the left won't operate...
            // found out that resetting the geometry solves theses cases better than "showNormal() workaround"
            // however there are cases when "showNormal()" is required to update control screen position, so doing both
            controlWidget->showNormal();

            // suppress resize events while resetting geometry
            UBApplication::boardController->controlView()->installEventFilter(&resizeEventFilter);
            controlWidget->setGeometry(QRect());
            UBApplication::boardController->controlView()->removeEventFilter(&resizeEventFilter);

            qDebug() << "control geometry" << geometry;
            controlWidget->setGeometry(geometry);
            // with Qt6, setGeometry has not the desired effect so we additionally use move and resize
            controlWidget->move(geometry.topLeft());
            controlWidget->resize(geometry.size());
            UBPlatformUtils::showFullScreen(controlWidget);
        }
    }

    // a normal window may have been restored on another screen than the configured
    // control screen: the roles follow the window (see assignRoles)
    QScreen* windowScreen = controlWindowScreen();

    if (windowScreen && windowScreen != screen(ScreenRole::Control))
    {
        assignRoles();
    }

    watchControlWindow();

    // the desktop annotation overlay covers the screen of the control window
    if (widget(ScreenRole::Desktop) && hasControl())
    {
        QWidget* desktopWidget = widget(ScreenRole::Desktop);
        const QRect desktopGeometry = screenGeometry(ScreenRole::Desktop);

        if (desktopMode && desktopWidget->isVisible())
        {
            if (desktopWidget->geometry() != desktopGeometry)
            {
                desktopWidget->setGeometry(desktopGeometry);
            }
        }
        else
        {
            desktopWidget->hide();
            desktopWidget->setGeometry(desktopGeometry);
        }
    }

    if (widget(ScreenRole::Display) && hasDisplay())
    {
        QWidget* displayWidget = widget(ScreenRole::Display);
        const QRect displayGeometry = screenGeometry(ScreenRole::Display);
        qDebug() << "display geometry" << displayGeometry;

        // (re)place the projection only when needed: it is never shown on another screen first
        if (!isShownOn(displayWidget, screen(ScreenRole::Display)))
        {
            displayWidget->hide();
            displayWidget->setGeometry(displayGeometry);
            UBPlatformUtils::showFullScreen(displayWidget);
        }
    }
    else if (widget(ScreenRole::Display))
    {
        widget(ScreenRole::Display)->hide();
    }

    for (ScreenRole role = ScreenRole::Previous1; role <= ScreenRole::Previous5; ++role)
    {
        if (widget(role))
        {
            if (screen(role) && mUseMultiScreen)
            {
                QWidget* previous = widget(role);
                qDebug() << "previous display geometry" << screenGeometry(role);

                if (!isShownOn(previous, screen(role)))
                {
                    previous->hide();
                    previous->setGeometry(screenGeometry(role));
                    UBPlatformUtils::showFullScreen(previous);
                }
            }
            else
            {
                widget(role)->hide();
            }
        }
    }

    // keep the keyboard focus on the control window, but never take it from another application
    if (widget(ScreenRole::Control) && hasControl() && !desktopMode
            && QGuiApplication::applicationState() == Qt::ApplicationActive)
    {
        widget(ScreenRole::Control)->activateWindow();
    }

    emit screenLayoutChanged();
}


bool UBDisplayManager::isShownOn(QWidget* widget, QScreen* screen)
{
    return widget && screen && widget->isVisible()
            && QGuiApplication::screenAt(widget->geometry().center()) == screen;
}


bool UBDisplayManager::isDesktopModeActive() const
{
    return UBApplication::applicationController && UBApplication::applicationController->isShowingDesktop();
}


void UBDisplayManager::watchControlWindow()
{
    QWidget* controlWidget = widget(ScreenRole::Control);
    QWindow* handle = controlWidget ? controlWidget->windowHandle() : nullptr;

    if (!handle || handle == mWatchedControlWindow)
    {
        return;
    }

    if (mWatchedControlWindow)
    {
        disconnect(mWatchedControlWindow, nullptr, this, nullptr);
    }

    mWatchedControlWindow = handle;

    // the user moved the window to another screen: let the roles follow once it settled
    connect(handle, &QWindow::screenChanged, this, [this]() {
        mFollowControlWindowTimer.start();
    });
}


void UBDisplayManager::followControlWindow()
{
    QScreen* windowScreen = controlWindowScreen();

    if (!windowScreen || windowScreen == screen(ScreenRole::Control) || isDesktopModeActive())
    {
        return;
    }

    qDebug() << "control window moved to" << windowScreen->name();

    if (hasDisplay())
    {
        // the projection moves to a screen the control window is not on
        adjustScreens();
    }
    else
    {
        // calibrated grid size, captures and the desktop overlay follow the window;
        // the board itself is not touched
        assignRoles();
    }
}


void UBDisplayManager::blackout()
{
    for (auto& screen : mScreensByRole)
    {
        UBBlackoutWidget* blackoutWidget = new UBBlackoutWidget(); //deleted in UBDisplayManager::unBlackout
        Ui::BlackoutWidget* blackoutUi = new Ui::BlackoutWidget();
        blackoutUi->setupUi(blackoutWidget);

        connect(blackoutUi->iconButton, SIGNAL(pressed()), blackoutWidget, SLOT(doActivity()));
        connect(blackoutWidget, SIGNAL(activity()), this, SLOT(unBlackout()));

        // display Uniboard logo on main screen
        bool isControlScreen = screen == mScreensByRole[ScreenRole::Control];
        blackoutUi->iconButton->setVisible(isControlScreen);
        blackoutUi->labelClickToReturn->setVisible(isControlScreen);

        blackoutWidget->setGeometry(screen->geometry());

        mBlackoutWidgets << blackoutWidget;
        mBlackoutUiList << blackoutUi;
    }

    UBPlatformUtils::fadeDisplayOut();

    for (UBBlackoutWidget* blackoutWidget : std::as_const(mBlackoutWidgets))
    {
        UBPlatformUtils::showFullScreen(blackoutWidget);
    }
}

void UBDisplayManager::unBlackout()
{
    while (!mBlackoutWidgets.isEmpty())
    {
        // the widget is also destroyed thanks to its Qt::WA_DeleteOnClose attribute
        mBlackoutWidgets.takeFirst()->close();
    }

    qDeleteAll(mBlackoutUiList);
    mBlackoutUiList.clear();

    UBPlatformUtils::fadeDisplayIn();

    UBApplication::boardController->freezeW3CWidgets(false);
}

void UBDisplayManager::showScreenLabels(bool show)
{
    if (mScreenLabels.empty() || mScreenLabels.first().isNull() || mScreenLabels.first()->isVisible() == show)
    {
        return;
    }

    if (show)
    {
        for (auto label : mScreenLabels)
        {
            if (label)
            {
                label->showNormal();
            }
        }
    }
    else
    {
        for (const auto &label : mScreenLabels)
        {
            if (label && label->rect().contains(QCursor::pos()))
            {
                // do not hide labels when triggered by a click on one of them
                return;
            }
        }

        for (auto label : mScreenLabels)
        {
            if (label)
            {
                label->hide();
            }
        }
    }
}

void UBDisplayManager::disableScreenLabels(QStringList screenList)
{
    for (auto label : mScreenLabels)
    {
        if (label)
        {
            const auto screenIndex = label->property("screenIndex").toString();
            label->setDisabled(screenList.contains(screenIndex));
        }
    }
}

void UBDisplayManager::addOrRemoveScreen(QScreen *screen)
{
    Q_UNUSED(screen);

    initScreenIndexes();
    assignRoles();

    // positioning must be delayed, because OS also tries to position the widgets
    QTimer::singleShot(3000, this, [this](){
        QWidget* controlWidget = widget(ScreenRole::Control);
        const bool normalWindow = controlWidget && !(controlWidget->windowFlags() & Qt::FramelessWindowHint);

        if (normalWindow && !mUseMultiScreen)
        {
            // a normal window without projection: nothing to rearrange, the window stays
            // where it is (only brought back when its screen is gone) and the board is not
            // re-centred
            assignRoles();
            positionControlWindow(controlWidget);

            QWidget* desktopWidget = widget(ScreenRole::Desktop);

            if (desktopWidget && desktopWidget->isVisible() && hasControl())
            {
                desktopWidget->setGeometry(screenGeometry(ScreenRole::Desktop));
            }

            return;
        }

        positionScreens();
    });
}

void UBDisplayManager::setUseMultiScreen(bool pUse)
{
    mUseMultiScreen = pUse;
}

QSize UBDisplayManager::screenSize(ScreenRole role) const
{
    QScreen* scr = screen(role);
    return scr ? scr->size() : QSize();
}

QSize UBDisplayManager::availableScreenSize(ScreenRole role) const
{
    QScreen* scr = screen(role);
    return scr ? scr->availableSize() : QSize();
}

QRect UBDisplayManager::screenGeometry(ScreenRole role) const
{
    QScreen* scr = screen(role);
    return scr ? scr->geometry() : QRect();
}

qreal UBDisplayManager::physicalDpi(ScreenRole role) const
{
    QScreen* scr = screen(role);
    return scr ? scr->physicalDotsPerInch() : 96.;
}

qreal UBDisplayManager::logicalDpi(ScreenRole role) const
{
    QScreen* scr = screen(role);
    return scr ? scr->logicalDotsPerInch() : 96.;
}

void UBDisplayManager::grab(ScreenRole role, std::function<void (QPixmap)> callback, QRect rect) const
{
    QScreen* scr = screen(role);

    if (scr)
    {
        if (!rect.isValid())
        {
            rect = QRect(QPoint(0, 0), scr->geometry().size());
        }

        UBPlatformUtils::grabScreen(scr, callback, rect);
    }
    else
    {
        callback(QPixmap());
    }
}

QPixmap UBDisplayManager::grabGlobal(QRect rect) const
{
    QScreen* screen = QGuiApplication::screenAt(rect.topLeft());

    if (screen)
    {
        rect.translate(-screen->geometry().topLeft());
        return screen->grabWindow(0, rect.x(), rect.y(), rect.width(), rect.height());
    }

    return QPixmap();
}


ScreenRole &operator++(ScreenRole &role)
{
    role = ScreenRole(int(role) + 1);
    return role;
}

ScreenRole operator++(ScreenRole &role, int)
{
    ScreenRole old = role;  // copy old value
    ++role;                 // prefix increment
    return old;             // return old value
}

Qt::WindowFlags UBDisplayManager::windowModeFlags()
{
    Qt::WindowFlags flags = Qt::Window
            | Qt::WindowTitleHint
            | Qt::WindowSystemMenuHint
            | Qt::WindowCloseButtonHint
            | Qt::WindowMinimizeButtonHint
            | Qt::WindowMaximizeButtonHint;

#ifdef Q_OS_MACOS
    // let the green zoom button switch to (native) full screen
    flags |= Qt::WindowFullscreenButtonHint;
#endif

    return flags;
}

bool UBDisplayManager::isOnScreen(const QRect& geometry)
{
    if (!geometry.isValid())
    {
        return false;
    }

    // the upper part of the window (where the title bar is) must be reachable
    const QRect top(geometry.left(), geometry.top(), geometry.width(), qMin(geometry.height(), 40));

    for (QScreen* screen : QGuiApplication::screens())
    {
        const QRect visible = screen->availableGeometry().intersected(top);

        if (visible.width() >= qMin(100, geometry.width()) && visible.height() >= qMin(20, geometry.height()))
        {
            return true;
        }
    }

    return false;
}

QRect UBDisplayManager::defaultControlWindowGeometry() const
{
    QScreen* controlScreen = screen(ScreenRole::Control);

    if (!controlScreen)
    {
        controlScreen = QGuiApplication::primaryScreen();
    }

    // about 85% of the available screen area, centred
    const QRect available = controlScreen->availableGeometry();
    QRect geometry(QPoint(0, 0), available.size() * 0.85);
    geometry.moveCenter(available.center());
    return geometry;
}

void UBDisplayManager::positionControlWindow(QWidget* controlWidget)
{
    if (!mControlWindowInitialized)
    {
        // first positioning: restore the last geometry if it is still on a screen
        mControlWindowInitialized = true;

        UBSettings* settings = UBSettings::settings();
        QRect geometry = settings->appMainWindowGeometry->get().toRect();
        const bool maximized = settings->appMainWindowState->get().toString() == "maximized";

        if (!isOnScreen(geometry))
        {
            geometry = defaultControlWindowGeometry();
        }

        qDebug() << "control window geometry" << geometry << (maximized ? "maximized" : "");

        controlWidget->setGeometry(geometry);
        controlWidget->move(geometry.topLeft());
        controlWidget->resize(geometry.size());

        if (maximized)
        {
            controlWidget->showMaximized();
        }
        else
        {
            controlWidget->showNormal();
        }

        return;
    }

    // keep size, position and maximised state chosen by the user,
    // only bring the window back if it is no longer on any screen
    if (!controlWidget->isMaximized() && !controlWidget->isFullScreen() && !isOnScreen(controlWidget->geometry()))
    {
        const QRect geometry = defaultControlWindowGeometry();
        controlWidget->setGeometry(geometry);
        controlWidget->move(geometry.topLeft());
        controlWidget->resize(geometry.size());
    }

    // hidden while the desktop annotation overlay is shown
    if (controlWidget->isHidden() && !isDesktopModeActive())
    {
        controlWidget->show();
    }
}

void UBDisplayManager::setControlFullScreen(bool fullScreen)
{
    QWidget* controlWidget = widget(ScreenRole::Control);

    if (fullScreen == mControlFullScreen)
    {
        emit controlFullScreenChanged(mControlFullScreen);
        return;
    }

    if (fullScreen && controlWidget && !(controlWidget->windowFlags() & Qt::FramelessWindowHint))
    {
        // remember the window mode geometry to return to it later
        mWindowedMaximized = controlWidget->isMaximized();
        mWindowedGeometry = controlWidget->normalGeometry().isValid() ? controlWidget->normalGeometry() : controlWidget->geometry();
    }

    mControlFullScreen = fullScreen;

    if (controlWidget)
    {
        if (fullScreen)
        {
            if (controlWidget->windowFlags() & Qt::FramelessWindowHint)
            {
                // legacy kiosk window: use the regular screen positioning
                adjustScreens();
            }
            else
            {
#ifndef Q_OS_MACOS
                // cover the screen the window is currently on, also without a window manager
                // supporting the full screen state; the window mode geometry is restored when leaving
                if (QScreen* currentScreen = controlWidget->screen())
                {
                    controlWidget->setGeometry(currentScreen->geometry());
                }
#endif
                UBPlatformUtils::showFullScreen(controlWidget);
            }
        }
        else
        {
            if (controlWidget->windowFlags() & Qt::FramelessWindowHint)
            {
                // started in kiosk mode: give the window a title bar and window buttons
                controlWidget->setWindowFlags(windowModeFlags());
            }

            controlWidget->showNormal();

            QRect geometry = mWindowedGeometry;

            if (!isOnScreen(geometry))
            {
                geometry = defaultControlWindowGeometry();
            }

            controlWidget->setGeometry(geometry);
            controlWidget->move(geometry.topLeft());
            controlWidget->resize(geometry.size());

            if (mWindowedMaximized)
            {
                controlWidget->showMaximized();
            }

            mControlWindowInitialized = true;
            UBPlatformUtils::showMenuBarAndDock();
        }

        controlWidget->activateWindow();
        controlWidget->raise();
    }

    emit controlFullScreenChanged(mControlFullScreen);
}

void UBDisplayManager::controlWindowStateChanged()
{
    QWidget* controlWidget = widget(ScreenRole::Control);

    // only relevant for a window with a title bar, e.g. full screen entered or left by the
    // window system (macOS zoom button) instead of by OpenBoard
    if (!controlWidget || (controlWidget->windowFlags() & Qt::FramelessWindowHint) || !controlWidget->isVisible())
    {
        return;
    }

    const bool fullScreen = controlWidget->isFullScreen();

    if (fullScreen != mControlFullScreen)
    {
        if (fullScreen)
        {
            mWindowedMaximized = false;
            mWindowedGeometry = controlWidget->normalGeometry();
        }

        mControlFullScreen = fullScreen;
        mControlWindowInitialized = true;
        emit controlFullScreenChanged(mControlFullScreen);
    }
}

void UBDisplayManager::saveControlWindowGeometry()
{
    QWidget* controlWidget = widget(ScreenRole::Control);

    if (!controlWidget || !mControlWindowInitialized)
    {
        // never shown in window mode
        return;
    }

    QRect geometry;
    bool maximized;

    if (mControlFullScreen)
    {
        geometry = mWindowedGeometry;
        maximized = mWindowedMaximized;
    }
    else
    {
        maximized = controlWidget->isMaximized();
        geometry = maximized ? controlWidget->normalGeometry() : controlWidget->geometry();
    }

    if (geometry.isValid())
    {
        UBSettings::settings()->appMainWindowGeometry->set(geometry);
    }

    UBSettings::settings()->appMainWindowState->set(maximized ? "maximized" : "normal");
}
