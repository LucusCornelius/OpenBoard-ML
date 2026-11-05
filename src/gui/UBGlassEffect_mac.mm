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

// macOS native glass for the main window chrome.
//
// Everything here is optional decoration: the Qt painted glass (UBGlassStyle,
// UBFloatingPalette) is always drawn, so the application looks right even if
// nothing below has an effect. The deployment target stays macOS 12; newer API
// is guarded with @available.
//
// Environment switches (for testing on the Mac):
//   OPENBOARD_NO_NATIVE_GLASS=1   disable all of this (plain title bar)
//   OPENBOARD_NO_VIBRANCY=1       keep the transparent title bar, no vibrancy view

#include "UBGlassEffect.h"

#include <QtGlobal>
#include <QEvent>
#include <QMainWindow>
#include <QStyle>
#include <QToolBar>
#include <QWidget>

#include "core/UBThemeManager.h"

#import <AppKit/AppKit.h>

namespace
{
    NSString* const vibrancyIdentifier = @"ch.openboard.glass.toolbarVibrancy";

    bool nativeGlassDisabled()
    {
        return qEnvironmentVariableIntValue("OPENBOARD_NO_NATIVE_GLASS") != 0;
    }

    bool vibrancyDisabled()
    {
        return nativeGlassDisabled() || qEnvironmentVariableIntValue("OPENBOARD_NO_VIBRANCY") != 0;
    }

    NSWindow* nativeWindow(QWidget* widget)
    {
        if (!widget)
            return nil;

        QWidget* top = widget->window();

        if (!top || !top->testAttribute(Qt::WA_WState_Created))
            return nil;

        NSView* view = reinterpret_cast<NSView*>(top->winId());

        return view ? [view window] : nil;
    }

    NSVisualEffectView* findVibrancyView(NSView* frameView)
    {
        for (NSView* subview in [frameView subviews])
        {
            if ([subview isKindOfClass:[NSVisualEffectView class]]
                    && [[subview identifier] isEqualToString:vibrancyIdentifier])
            {
                return (NSVisualEffectView*)subview;
            }
        }

        return nil;
    }

    void repolish(QWidget* top)
    {
        QList<QWidget*> widgets;
        widgets << top;

        for (QToolBar* toolBar : top->findChildren<QToolBar*>())
            widgets << toolBar;

        for (QWidget* w : widgets)
        {
            w->style()->unpolish(w);
            w->style()->polish(w);
            w->update();
        }
    }

    void updateWindowChrome(QWidget* top)
    {
        NSWindow* window = nativeWindow(top);
        QMainWindow* mainWindow = qobject_cast<QMainWindow*>(top);

        if (!window || !mainWindow)
            return;

        @try
        {
            QToolBar* boardToolBar = mainWindow->findChild<QToolBar*>(QStringLiteral("boardToolBar"));
            const bool fullScreen = ([window styleMask] & NSWindowStyleMaskFullScreen) != 0;
            const bool toolBarAtTop = boardToolBar && mainWindow->toolBarArea(boardToolBar) == Qt::TopToolBarArea;
            const bool unified = toolBarAtTop && !fullScreen;

            // follow the light / dark theme of the application, not only the system appearance
            NSAppearance* appearance = [NSAppearance appearanceNamed:UBThemeManager::instance()->isDarkMode()
                    ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua];

            if (appearance && ![[[window appearance] name] isEqualToString:[appearance name]])
                [window setAppearance:appearance];

            // only touch the window when something changes, this runs on every resize
            const NSWindowStyleMask mask = unified
                    ? ([window styleMask] | NSWindowStyleMaskFullSizeContentView)
                    : ([window styleMask] & ~NSWindowStyleMaskFullSizeContentView);

            if (mask != [window styleMask])
                [window setStyleMask:mask];

            if ([window titlebarAppearsTransparent] != (unified ? YES : NO))
                [window setTitlebarAppearsTransparent:unified ? YES : NO];

            const NSWindowTitleVisibility titleVisibility = unified ? NSWindowTitleHidden : NSWindowTitleVisible;

            if ([window titleVisibility] != titleVisibility)
                [window setTitleVisibility:titleVisibility];

            // the toolbars extend below the transparent title bar: keep their buttons below the traffic lights
            int titleBarHeight = 0;

            if (unified)
            {
                titleBarHeight = qMax(0, qRound(NSHeight([window frame]) - NSHeight([window contentLayoutRect])));
            }

            for (QToolBar* toolBar : mainWindow->findChildren<QToolBar*>(QString(), Qt::FindDirectChildrenOnly))
            {
                const int top = (mainWindow->toolBarArea(toolBar) == Qt::TopToolBarArea) ? titleBarHeight : 0;
                const QMargins margins(0, top, 0, 0);

                if (toolBar->contentsMargins() != margins)
                    toolBar->setContentsMargins(margins);
            }

            // vibrancy behind the title bar and the top toolbar
            NSView* contentView = [window contentView];
            NSView* frameView = [contentView superview];

            if (!frameView)
                return;

            NSVisualEffectView* vibrancy = findVibrancyView(frameView);
            const bool wantVibrancy = unified && !vibrancyDisabled() && top->property("ubGlassTranslucentWindow").toBool();

            // the glass stylesheet makes the window and toolbar backgrounds transparent
            // only while the vibrancy view is behind them
            if (top->property("ubGlassVibrancy").toBool() != wantVibrancy)
            {
                top->setProperty("ubGlassVibrancy", wantVibrancy);
                repolish(top);
            }

            if (wantVibrancy)
            {
                if (!vibrancy)
                {
                    vibrancy = [[NSVisualEffectView alloc] initWithFrame:NSZeroRect];
                    [vibrancy setIdentifier:vibrancyIdentifier];
                    [vibrancy setMaterial:NSVisualEffectMaterialSidebar];
                    [vibrancy setBlendingMode:NSVisualEffectBlendingModeBehindWindow];
                    [vibrancy setState:NSVisualEffectStateFollowsWindowActiveState];
                    [vibrancy setAutoresizingMask:NSViewWidthSizable | NSViewMinYMargin];
                    [frameView addSubview:vibrancy positioned:NSWindowBelow relativeTo:contentView];
#if !__has_feature(objc_arc)
                    [vibrancy release];
#endif
                }

                const CGFloat height = titleBarHeight + (boardToolBar && boardToolBar->isVisible() ? boardToolBar->height() : 0);
                const NSRect bounds = [frameView bounds];
                const CGFloat y = [frameView isFlipped] ? 0 : NSHeight(bounds) - height;
                [vibrancy setFrame:NSMakeRect(0, y, NSWidth(bounds), height)];
                [vibrancy setHidden:NO];
            }
            else if (vibrancy)
            {
                [vibrancy setHidden:YES];
            }
        }
        @catch (NSException* exception)
        {
            qWarning("UBGlassEffect: native window chrome failed: %s", [[exception reason] UTF8String]);
        }
    }

    /**
     * Keeps the native chrome in sync with the window state (full screen,
     * toolbar position and height).
     */
    class UBMacWindowGlassFilter : public QObject
    {
        public:
            explicit UBMacWindowGlassFilter(QWidget* window)
                : QObject(window)
            {
            }

            bool eventFilter(QObject* watched, QEvent* event) override
            {
                switch (event->type())
                {
                    case QEvent::Show:
                    case QEvent::WindowStateChange:
                    case QEvent::Resize:
                    case QEvent::LayoutRequest:
                        if (watched->isWidgetType())
                            updateWindowChrome(static_cast<QWidget*>(watched));
                        break;

                    default:
                        break;
                }

                return false;
            }
    };
}


void UBGlassEffect::applyToWindow(QWidget* widget)
{
    if (!widget || nativeGlassDisabled())
        return;

    QWidget* top = widget->window();

    if (top->property("ubGlassWindow").toBool())
    {
        updateWindowChrome(top);
        return;
    }

    top->setProperty("ubGlassWindow", true);

    // Vibrancy needs a non opaque Qt window. This can only be requested before
    // the native window is created; the central widget stays opaque.
    if (!vibrancyDisabled() && !top->testAttribute(Qt::WA_WState_Created) && qobject_cast<QMainWindow*>(top))
    {
        top->setAttribute(Qt::WA_TranslucentBackground);
        top->setProperty("ubGlassTranslucentWindow", true);

        if (QWidget* central = static_cast<QMainWindow*>(top)->centralWidget())
            central->setAutoFillBackground(true);
    }

    top->installEventFilter(new UBMacWindowGlassFilter(top));
    updateWindowChrome(top);
}


void UBGlassEffect::applyToPanel(QWidget* panel)
{
    // Floating panels are non native child widgets painted into the window's
    // single Qt layer, together with the board below them. A native
    // NSGlassEffectView (macOS 26) or NSVisualEffectView can only be placed
    // above that layer (hiding the panel's buttons) or below it (hidden by the
    // board), so panels keep the Qt painted glass with the real blurred
    // backdrop of the board (UBFloatingPalette). The native material is used
    // for the window chrome only.
    Q_UNUSED(panel);
}
