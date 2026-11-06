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

// macOS native "Liquid Glass" for OpenBoard.
//
// Window chrome (all macOS versions >= 12): transparent title bar, hidden
// title, no title bar separator and a full size content view, so that the
// board extends edge to edge under the title bar like Freeform. The floating
// tool clusters stay below the traffic lights using topInset().
//
// Panels (macOS 26+, opt-in): an NSGlassEffectView is placed behind a
// floating palette. The palette becomes a native child view and paints only
// its controls; the glass view is a sibling below it in the same superview.
// Everything is optional decoration: when anything is missing or fails, the
// Qt painted glass (UBGlassStyle, UBFloatingPalette) stays on.
//
// Every macOS 26 symbol is inside "#if defined(__MAC_26_0)" (SDK 26 or newer)
// and "if (@available(macOS 26.0, *))", plus a NSClassFromString check, so
// this file builds with any SDK >= 12 and runs on macOS 12 and later.
//
// Environment switches (for testing on the Mac):
//   OPENBOARD_NO_NATIVE_GLASS=1     disable all of this (normal title bar, painted glass only)
//   OPENBOARD_NATIVE_PANEL_GLASS=1  use NSGlassEffectView behind the floating panels (macOS 26+)

#include "UBGlassEffect.h"

#include <QtGlobal>
#include <QDebug>
#include <QEvent>
#include <QMainWindow>
#include <QPointer>
#include <QRect>
#include <QTimer>
#include <QToolBar>
#include <QVariant>
#include <QWidget>

#include "core/UBThemeManager.h"

#import <AppKit/AppKit.h>

namespace
{
    bool nativeGlassDisabled()
    {
        return qEnvironmentVariableIntValue("OPENBOARD_NO_NATIVE_GLASS") != 0;
    }

    bool nativePanelGlassRequested()
    {
        return !nativeGlassDisabled() && qEnvironmentVariableIntValue("OPENBOARD_NATIVE_PANEL_GLASS") != 0;
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

    void updateWindowChrome(QWidget* top)
    {
        NSWindow* window = nativeWindow(top);
        QMainWindow* mainWindow = qobject_cast<QMainWindow*>(top);

        if (!window || !mainWindow)
            return;

        @try
        {
            const bool fullScreen = ([window styleMask] & NSWindowStyleMaskFullScreen) != 0;
            const bool unified = !fullScreen;

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

            if (@available(macOS 11.0, *))
            {
                if ([window titlebarSeparatorStyle] != NSTitlebarSeparatorStyleNone)
                    [window setTitlebarSeparatorStyle:NSTitlebarSeparatorStyleNone];
            }

            // the content extends below the transparent title bar: controls at the top keep clear of it
            int titleBarHeight = 0;

            if (unified)
                titleBarHeight = qMax(0, qRound(NSHeight([window frame]) - NSHeight([window contentLayoutRect])));

            top->setProperty("ubGlassTitleBarHeight", titleBarHeight);

            for (QToolBar* toolBar : mainWindow->findChildren<QToolBar*>(QString(), Qt::FindDirectChildrenOnly))
            {
                const int topMargin = (mainWindow->toolBarArea(toolBar) == Qt::TopToolBarArea) ? titleBarHeight : 0;
                const QMargins margins(0, topMargin, 0, 0);

                if (toolBar->contentsMargins() != margins)
                    toolBar->setContentsMargins(margins);
            }
        }
        @catch (NSException* exception)
        {
            qWarning("UBGlassEffect: native window chrome failed: %s", [[exception reason] UTF8String]);
        }
    }

    /**
     * Keeps the native chrome in sync with the window state (full screen, theme).
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
                    case QEvent::StyleChange:
                        if (watched->isWidgetType())
                            updateWindowChrome(static_cast<QWidget*>(watched));
                        break;

                    default:
                        break;
                }

                return false;
            }
    };

#if defined(__MAC_26_0)
    /**
     * Owns the NSGlassEffectView behind one floating panel and keeps its frame,
     * corner radius, visibility and z-order in sync with the panel.
     */
    class UBMacPanelGlass : public QObject
    {
        public:
            /** glass is a NSGlassEffectView, only created on macOS 26 and later */
            UBMacPanelGlass(QWidget* panel, NSView* glass)
                : QObject(panel)
                , mPanel(panel)
                , mGlass(glass)
            {
#if !__has_feature(objc_arc)
                [mGlass retain];
#endif
                panel->installEventFilter(this);
                sync();
            }

            ~UBMacPanelGlass() override
            {
                @try
                {
                    [mGlass removeFromSuperview];
                }
                @catch (NSException* exception)
                {
                    qWarning("UBGlassEffect: removing the panel glass failed: %s", [[exception reason] UTF8String]);
                }
#if !__has_feature(objc_arc)
                [mGlass release];
#endif
                mGlass = nil;
            }

            bool eventFilter(QObject* watched, QEvent* event) override
            {
                if (watched == mPanel)
                {
                    switch (event->type())
                    {
                        case QEvent::Move:
                        case QEvent::Resize:
                        case QEvent::Show:
                        case QEvent::Hide:
                        case QEvent::ZOrderChange:
                        case QEvent::ParentChange:
                        case QEvent::EnabledChange:
                            sync();
                            // the native view may only follow after the event: sync again
                            QTimer::singleShot(0, this, [this]() { sync(); });
                            break;

                        default:
                            break;
                    }
                }

                return false;
            }

            void sync()
            {
                if (!mPanel || !mGlass)
                    return;

                @try
                {
                    NSView* view = reinterpret_cast<NSView*>(mPanel->internalWinId());
                    NSView* superview = view ? [view superview] : nil;

                    if (!superview)
                    {
                        [mGlass setHidden:YES];
                        return;
                    }

                    // always directly below the view of the panel, in the same superview
                    NSArray<NSView*>* subviews = [superview subviews];
                    const NSUInteger viewIndex = [subviews indexOfObject:view];
                    const NSUInteger glassIndex = [subviews indexOfObject:mGlass];

                    if ([mGlass superview] != superview || glassIndex == NSNotFound
                            || viewIndex == NSNotFound || glassIndex + 1 != viewIndex)
                    {
                        [mGlass removeFromSuperview];
                        [superview addSubview:mGlass positioned:NSWindowBelow relativeTo:view];
                    }

                    // the glass covers the body of the panel, the margin around it holds no glass
                    QRect body = mPanel->property("ubGlassBody").toRect();

                    if (body.isEmpty())
                        body = mPanel->rect();

                    const NSRect bodyInView = NSMakeRect(body.x(), [view isFlipped] ? body.y() : mPanel->height() - body.bottom() - 1,
                                                         body.width(), body.height());
                    const NSRect frame = [view convertRect:bodyInView toView:superview];

                    if (!NSEqualRects([mGlass frame], frame))
                        [mGlass setFrame:frame];

                    const CGFloat radius = qMax(0.0, mPanel->property("ubGlassRadius").toReal());

                    if (@available(macOS 26.0, *))
                    {
                        NSGlassEffectView* glass = (NSGlassEffectView*)mGlass;

                        if ([glass cornerRadius] != radius)
                            [glass setCornerRadius:radius];
                    }

                    [mGlass setHidden:(mPanel->isVisible() ? NO : YES)];
                }
                @catch (NSException* exception)
                {
                    qWarning("UBGlassEffect: panel glass sync failed: %s", [[exception reason] UTF8String]);
                }
            }

        private:
            QPointer<QWidget> mPanel;
            NSView* mGlass;
    };
#endif
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

    // the window stays opaque: the board is drawn edge to edge below the transparent title bar
    top->installEventFilter(new UBMacWindowGlassFilter(top));
    updateWindowChrome(top);
}


int UBGlassEffect::topInset(QWidget* widget)
{
    if (!widget || nativeGlassDisabled())
        return 0;

    QWidget* top = widget->window();
    const int titleBarHeight = top->property("ubGlassTitleBarHeight").toInt();

    if (titleBarHeight <= 0 || top == widget)
        return qMax(0, titleBarHeight);

    const int y = widget->mapTo(top, QPoint(0, 0)).y();

    return qMax(0, titleBarHeight - y);
}


void UBGlassEffect::applyToPanel(QWidget* panel)
{
    // Native panel glass is opt-in: Qt paints all widgets of a window, native
    // child views included, into one shared backing store. The layer of the
    // native panel view may therefore show the board pixels behind the panel,
    // hiding the glass view below it. Enable it with OPENBOARD_NATIVE_PANEL_GLASS=1
    // to try; the painted glass is the default.
    if (!panel || panel->isWindow() || !nativePanelGlassRequested())
        return;

    if (panel->property("ubNativeGlass").toBool() || panel->property("ubNativeGlassFailed").toBool())
        return;

#if defined(__MAC_26_0)
    if (@available(macOS 26.0, *))
    {
        if (!NSClassFromString(@"NSGlassEffectView"))
        {
            panel->setProperty("ubNativeGlassFailed", true);
            return;
        }

        @try
        {
            // a native child view (without making the ancestors native), with a transparent background
            panel->setAttribute(Qt::WA_DontCreateNativeAncestors);
            panel->setAttribute(Qt::WA_TranslucentBackground);
            panel->setAttribute(Qt::WA_NoSystemBackground);
            panel->setAttribute(Qt::WA_NativeWindow);

            NSView* view = reinterpret_cast<NSView*>(panel->winId());

            if (!view || ![view superview])
            {
                qWarning("UBGlassEffect: no native view for panel %s, keeping the painted glass",
                         qPrintable(panel->objectName()));
                panel->setProperty("ubNativeGlassFailed", true);
                return;
            }

            NSGlassEffectView* glass = [[NSGlassEffectView alloc] initWithFrame:[view frame]];

            if (!glass)
            {
                panel->setProperty("ubNativeGlassFailed", true);
                return;
            }

            [glass setStyle:NSGlassEffectViewStyleRegular];
            [glass setTintColor:nil];

            [[view superview] addSubview:glass positioned:NSWindowBelow relativeTo:view];

            // the panel paints its controls only, the glass view below draws the material
            panel->setProperty("ubNativeGlass", true);
            new UBMacPanelGlass(panel, glass);

#if !__has_feature(objc_arc)
            [glass release];
#endif
            panel->update();
        }
        @catch (NSException* exception)
        {
            qWarning("UBGlassEffect: native panel glass failed: %s", [[exception reason] UTF8String]);
            panel->setProperty("ubNativeGlass", false);
            panel->setProperty("ubNativeGlassFailed", true);
        }
    }
#endif
}
