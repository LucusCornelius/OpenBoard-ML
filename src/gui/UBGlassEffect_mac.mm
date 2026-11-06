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
// Window chrome: transparent title bar without title and separator and a
// full size content view (Qt::ExpandedClientAreaHint and
// Qt::NoTitleBarBackgroundHint), so that the board runs edge to edge under
// the title bar like Freeform. The tool clusters keep clear of the traffic
// lights using topInset(). The window appearance follows the theme of the
// application (UBThemeManager), not only the system appearance.
//
// Panels: Qt composes all widgets of a window, native child widgets
// included, from one shared backing store, so a native view behind a Qt
// widget can never be seen. The floating panels therefore stay ordinary
// widgets (layout, mouse and keyboard handling unchanged) whose painting is
// diverted by a graphics effect: nothing is painted into the window, the
// rendered controls (transparent background) are handed to a native overlay
// above the board instead. The overlay holds the system material
// (NSGlassEffectView on macOS 26 and later, NSVisualEffectView before) with
// the controls on top of it and is transparent for the mouse, so all events
// still reach the Qt widgets below it.
//
// Every macOS 26 symbol is inside "#if defined(__MAC_26_0)" (SDK 26 or newer)
// and "if (@available(macOS 26.0, *))", plus a NSClassFromString check, so
// this file builds with any SDK >= 12 and runs on macOS 12 and later.
//
// Environment switches (for testing):
//   OPENBOARD_NO_NATIVE_GLASS=1          no native chrome and no native panels (Qt painted glass only)
//   OPENBOARD_PANEL_GLASS=glass          NSGlassEffectView behind the panels (default on macOS 26+)
//   OPENBOARD_PANEL_GLASS=vibrancy       NSVisualEffectView behind the panels (default before macOS 26)
//   OPENBOARD_PANEL_GLASS=painted        Qt painted glass panels (also OPENBOARD_NATIVE_PANEL_GLASS=0)

#include "UBGlassEffect.h"

#include <QtGlobal>
#include <QEvent>
#include <QBackingStore>
#include <QGraphicsEffect>
#include <QImage>
#include <QAction>
#include <QMainWindow>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QRect>
#include <QTimer>
#include <QToolBar>
#include <QVariant>
#include <QWidget>
#include <QWindow>

#include "core/UBThemeManager.h"

#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>

/**
 * Overlay of one panel: transparent for the mouse, holds the material view
 * and the rendered controls above it.
 */
@interface OBGlassPanelHostView : NSView
@end

@implementation OBGlassPanelHostView

- (BOOL)isFlipped
{
    return YES;
}

- (NSView*)hitTest:(NSPoint)point
{
    Q_UNUSED(point);
    return nil;
}

@end

/**
 * The controls of the panel, rendered by Qt with a transparent background.
 */
@interface OBGlassPanelContentView : NSView
{
    CGImageRef mImage;
    CGFloat mScale;
}
- (void)setImage:(CGImageRef)image scale:(CGFloat)scale;
@end

@implementation OBGlassPanelContentView

- (instancetype)initWithFrame:(NSRect)frame
{
    self = [super initWithFrame:frame];

    if (self)
    {
        mImage = nil;
        mScale = 1.0;
        [self setWantsLayer:YES];
        [self setLayerContentsRedrawPolicy:NSViewLayerContentsRedrawOnSetNeedsDisplay];
    }

    return self;
}

- (void)dealloc
{
    if (mImage)
        CGImageRelease(mImage);

    mImage = nil;
#if !__has_feature(objc_arc)
    [super dealloc];
#endif
}

- (BOOL)isFlipped
{
    return YES;
}

- (BOOL)wantsUpdateLayer
{
    return YES;
}

- (NSView*)hitTest:(NSPoint)point
{
    Q_UNUSED(point);
    return nil;
}

- (void)updateLayer
{
    CALayer* layer = [self layer];
    [layer setContentsGravity:@"resize"];
    [layer setContentsScale:mScale];
    [layer setContents:(__bridge id)mImage];
}

- (void)setImage:(CGImageRef)image scale:(CGFloat)scale
{
    if (image)
        CGImageRetain(image);

    if (mImage)
        CGImageRelease(mImage);

    mImage = image;
    mScale = scale > 0 ? scale : 1.0;

    // directly, the next display cycle may come late while the mouse is captured
    [self updateLayer];
    [self setNeedsDisplay:YES];
}

@end

/**
 * Edge and shadow of one glass panel (colours with alpha).
 */
struct OBGlassLook
{
    CGFloat shadow;      // opacity of the soft shadow below the panel
    NSColor* hairline;   // thin, even edge of the glass
};

/**
 * What the system glass hardly shows over a plain white board, and not at
 * all in a window that is not key: a soft, subtle drop shadow around the
 * body (never under it, where the glass would pick it up) and a thin, even
 * hairline along the edge. Flat, no gradients: these layers look the same
 * whether the window is key or not.
 */
@interface OBGlassDecorView : NSView
{
    CALayer* mShadow;
    CAShapeLayer* mShadowMask;
    CAShapeLayer* mHairline;
}
- (void)setBody:(NSRect)body radius:(CGFloat)radius look:(const OBGlassLook&)look;
@end

@implementation OBGlassDecorView

- (instancetype)initWithFrame:(NSRect)frame
{
    self = [super initWithFrame:frame];

    if (self)
    {
        [self setWantsLayer:YES];

        // QuartzCore classes by name: no extra framework to link
        mShadow = [[NSClassFromString(@"CALayer") alloc] init];
        mShadowMask = [[NSClassFromString(@"CAShapeLayer") alloc] init];
        [mShadowMask setFillRule:@"even-odd"];
        [mShadow setMask:mShadowMask];
        [mShadow setShadowColor:[[NSColor blackColor] CGColor]];

        mHairline = [[NSClassFromString(@"CAShapeLayer") alloc] init];
        [mHairline setFillColor:nil];

        [[self layer] addSublayer:mShadow];
        [[self layer] addSublayer:mHairline];
    }

    return self;
}

- (void)dealloc
{
#if !__has_feature(objc_arc)
    [mShadow release];
    [mShadowMask release];
    [mHairline release];
    [super dealloc];
#endif
}

- (BOOL)isFlipped
{
    return YES;
}

- (NSView*)hitTest:(NSPoint)point
{
    Q_UNUSED(point);
    return nil;
}

- (void)setBody:(NSRect)body radius:(CGFloat)radius look:(const OBGlassLook&)look
{
    [NSClassFromString(@"CATransaction") begin];
    [NSClassFromString(@"CATransaction") setDisableActions:YES];

    const CGFloat r = qMin<CGFloat>(radius, qMin(NSWidth(body), NSHeight(body)) / 2.0);
    // the top of the body is at y = 0 when the layer geometry is flipped
    const bool topAtZero = [[self layer] isGeometryFlipped] || [self isFlipped];

    // shadow: path of the body, masked to the outside of the body
    const NSRect bounds = [self bounds];
    CGPathRef bodyPath = CGPathCreateWithRoundedRect(body, r, r, NULL);
    [mShadow setFrame:bounds];
    [mShadow setShadowPath:bodyPath];
    [mShadow setShadowOpacity:look.shadow];
    [mShadow setShadowRadius:16.0];
    [mShadow setShadowOffset:CGSizeMake(0, topAtZero ? -6.0 : 6.0)];

    CGMutablePathRef outside = CGPathCreateMutable();
    CGPathAddRect(outside, NULL, NSInsetRect(bounds, -80, -80));
    CGPathAddPath(outside, NULL, bodyPath);
    [mShadowMask setFrame:bounds];
    [mShadowMask setPath:outside];
    CGPathRelease(outside);
    CGPathRelease(bodyPath);

    // hairline: one even stroke just inside the edge
    const CGFloat lineWidth = 1.0;
    const NSRect inner = NSInsetRect(body, lineWidth / 2.0, lineWidth / 2.0);
    const CGFloat rr = qMax<CGFloat>(0.0, r - lineWidth / 2.0);
    CGPathRef edge = CGPathCreateWithRoundedRect(inner, rr, rr, NULL);
    [mHairline setFrame:bounds];
    [mHairline setPath:edge];
    [mHairline setLineWidth:lineWidth];
    [mHairline setStrokeColor:[(look.hairline ? look.hairline : [NSColor clearColor]) CGColor]];
    CGPathRelease(edge);

    [NSClassFromString(@"CATransaction") commit];
}

@end

namespace
{
    enum class PanelMaterial
    {
        Painted,
        Vibrancy,
        Glass
    };

    bool nativeGlassDisabled()
    {
        return qEnvironmentVariableIntValue("OPENBOARD_NO_NATIVE_GLASS") != 0;
    }

    bool systemGlassAvailable()
    {
#if defined(__MAC_26_0)
        if (@available(macOS 26.0, *))
            return NSClassFromString(@"NSGlassEffectView") != nil;
#endif
        return false;
    }

    PanelMaterial panelMaterial()
    {
        static const PanelMaterial material = []() {
            if (nativeGlassDisabled())
                return PanelMaterial::Painted;

            const QString requested = qEnvironmentVariable("OPENBOARD_PANEL_GLASS").toLower();

            if (requested == "painted"
                    || (qEnvironmentVariableIsSet("OPENBOARD_NATIVE_PANEL_GLASS") && qEnvironmentVariableIntValue("OPENBOARD_NATIVE_PANEL_GLASS") == 0))
                return PanelMaterial::Painted;

            if (requested == "vibrancy")
                return PanelMaterial::Vibrancy;

            return systemGlassAvailable() ? PanelMaterial::Glass : PanelMaterial::Vibrancy;
        }();

        return material;
    }

    NSAppearance* themeAppearance()
    {
        return [NSAppearance appearanceNamed:UBThemeManager::instance()->isDarkMode()
                ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua];
    }

    /**
     * One flat, even tint in the glass of the dark theme. Over the (usually
     * white) board the plain dark system glass is a light grey, too light for
     * white glyphs; a moderate dark tint keeps it a clean dark glass.
     */
    NSColor* glassVeil(bool dark, PanelMaterial material)
    {
        if (!dark)
            return nil;

        return [NSColor colorWithWhite:0.0 alpha:(material == PanelMaterial::Glass ? 0.30 : 0.25)];
    }

    NSColor* white(CGFloat alpha) { return [NSColor colorWithWhite:1.0 alpha:qBound<CGFloat>(0, alpha, 1)]; }

    /** edge and shadow of the glass panels in the light and the dark theme */
    OBGlassLook glassLook(bool dark)
    {
        OBGlassLook look;
        look.shadow = dark ? 0.32 : 0.14;
        look.hairline = white(dark ? 0.22 : 0.85);
        return look;
    }

    /** the content view of the window holding the widget, nil when not created */
    NSView* nativeTopView(QWidget* widget)
    {
        if (!widget)
            return nil;

        QWidget* top = widget->window();

        if (!top || !top->testAttribute(Qt::WA_WState_Created))
            return nil;

        return reinterpret_cast<NSView*>(top->winId());
    }

    NSWindow* nativeWindow(QWidget* widget)
    {
        NSView* view = nativeTopView(widget);
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

            // follow the light / dark theme of the application, not only the system appearance
            NSAppearance* appearance = themeAppearance();

            if (appearance && ![[[window appearance] name] isEqualToString:[appearance name]])
                [window setAppearance:appearance];

#if QT_VERSION < QT_VERSION_CHECK(6, 9, 0)
            // older Qt does not know the expanded client area: set the window style directly
            const NSWindowStyleMask mask = [window styleMask] | NSWindowStyleMaskFullSizeContentView;

            if (mask != [window styleMask])
                [window setStyleMask:mask];

            if (![window titlebarAppearsTransparent])
                [window setTitlebarAppearsTransparent:YES];
#endif

            if ([window titleVisibility] != NSWindowTitleHidden)
                [window setTitleVisibility:NSWindowTitleHidden];

            if (@available(macOS 11.0, *))
            {
                if ([window titlebarSeparatorStyle] != NSTitlebarSeparatorStyleNone)
                    [window setTitlebarSeparatorStyle:NSTitlebarSeparatorStyleNone];
            }

            // the content extends below the transparent title bar: controls at the top keep clear of it
            int titleBarHeight = 0;

            if (!fullScreen)
                titleBarHeight = qMax(0, qRound(NSHeight([window frame]) - NSHeight([window contentLayoutRect])));

            top->setProperty("ubGlassTitleBarHeight", titleBarHeight);

            // the toolbars of the web and document modes share the row of the traffic lights (unified
            // title bar): they start to the right of the window buttons
            int trafficLightsRight = 0;

            if (titleBarHeight > 0)
            {
                for (NSWindowButton type : {NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton})
                {
                    NSButton* button = [window standardWindowButton:type];

                    if (button && ![button isHidden] && [button superview])
                    {
                        const NSRect frame = [[button superview] convertRect:[button frame] toView:nil];
                        trafficLightsRight = qMax(trafficLightsRight, qRound(NSMaxX(frame)));
                    }
                }

                if (trafficLightsRight > 0)
                    trafficLightsRight += 12;
            }

            for (QToolBar* toolBar : mainWindow->findChildren<QToolBar*>(QString(), Qt::FindDirectChildrenOnly))
            {
                const int width = (mainWindow->toolBarArea(toolBar) == Qt::TopToolBarArea) ? trafficLightsRight : 0;
                QWidget* spacer = toolBar->findChild<QWidget*>("ubTrafficLightsSpacer", Qt::FindDirectChildrenOnly);

                if (!spacer && width > 0)
                {
                    spacer = new QWidget(toolBar);
                    spacer->setObjectName("ubTrafficLightsSpacer");
                    spacer->setAttribute(Qt::WA_TransparentForMouseEvents);
                    toolBar->insertWidget(toolBar->actions().value(0), spacer);
                }

                if (spacer && spacer->minimumWidth() != width)
                {
                    spacer->setFixedSize(width, 1);

                    for (QAction* action : toolBar->actions())
                    {
                        if (toolBar->widgetForAction(action) == spacer)
                            action->setVisible(width > 0);
                    }
                }
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
                , mWindow(window)
            {
                QObject::connect(UBThemeManager::instance(), &UBThemeManager::themeChanged, this, [this]() {
                    if (mWindow)
                        updateWindowChrome(mWindow);
                });
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

        private:
            QPointer<QWidget> mWindow;
    };

    /**
     * The native menu material (NSVisualEffectView, menu material, behind the
     * window) below the content of a translucent Qt popup menu, with rounded corners.
     */
    class UBMacMenuMaterial : public QObject
    {
        public:
            explicit UBMacMenuMaterial(QWidget* menu)
                : QObject(menu)
                , mMenu(menu)
            {
                menu->installEventFilter(this);
            }

            bool eventFilter(QObject* watched, QEvent* event) override
            {
                if (watched == mMenu && event->type() == QEvent::Show)
                    install();

                return false;
            }

        private:
            void install()
            {
                NSView* content = nativeTopView(mMenu);
                NSWindow* window = content ? [content window] : nil;

                if (!window)
                    return;

                // the material in the light or dark appearance of the application theme
                NSAppearance* appearance = themeAppearance();

                if (appearance && ![[[window appearance] name] isEqualToString:[appearance name]])
                    [window setAppearance:appearance];

                if (mInstalledIn == (__bridge void*)window)
                    return;

                @try
                {
                    NSView* frameView = [content superview];

                    if (!frameView)
                        return;

                    mInstalledIn = (__bridge void*)window;

                    [window setOpaque:NO];
                    [window setBackgroundColor:[NSColor clearColor]];
                    [window setHasShadow:YES];

                    const CGFloat radius = 10.0;
                    NSVisualEffectView* material = [[NSVisualEffectView alloc] initWithFrame:[frameView bounds]];
                    [material setMaterial:NSVisualEffectMaterialMenu];
                    [material setBlendingMode:NSVisualEffectBlendingModeBehindWindow];
                    [material setState:NSVisualEffectStateActive];
                    [material setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];

                    NSImage* mask = [NSImage imageWithSize:NSMakeSize(2 * radius + 1, 2 * radius + 1) flipped:NO
                                            drawingHandler:^BOOL(NSRect rect) {
                        [[NSColor blackColor] setFill];
                        [[NSBezierPath bezierPathWithRoundedRect:rect xRadius:radius yRadius:radius] fill];
                        return YES;
                    }];
                    [mask setCapInsets:NSEdgeInsetsMake(radius, radius, radius, radius)];
                    [mask setResizingMode:NSImageResizingModeStretch];
                    [material setMaskImage:mask];

                    [frameView addSubview:material positioned:NSWindowBelow relativeTo:content];
#if !__has_feature(objc_arc)
                    [material release];
#endif
                    [window invalidateShadow];
                }
                @catch (NSException* exception)
                {
                    qWarning("UBGlassEffect: native menu material failed: %s", [[exception reason] UTF8String]);
                }
            }

            QPointer<QWidget> mMenu;
            void* mInstalledIn = nullptr;
    };

    class UBMacPanelGlass;

    /**
     * Paints nothing into the window: the rendered panel (with a transparent
     * background) is handed to the native overlay instead.
     */
    class UBGlassMirrorEffect : public QGraphicsEffect
    {
        public:
            UBGlassMirrorEffect(UBMacPanelGlass* glass, QObject* parent)
                : QGraphicsEffect(parent)
                , mGlass(glass)
            {
            }

            void detach()
            {
                mGlass = nullptr;
            }

        protected:
            void draw(QPainter* painter) override;

        private:
            UBMacPanelGlass* mGlass;
    };

    /**
     * Owns the native overlay of one floating panel and keeps its frame,
     * corner radius, appearance, visibility and z-order in sync with the panel.
     */
    class UBMacPanelGlass : public QObject
    {
        public:
            UBMacPanelGlass(QWidget* panel, PanelMaterial material)
                : QObject(panel)
                , mPanel(panel)
                , mMaterial(material)
                , mHost(nil)
                , mMaterialView(nil)
                , mContent(nil)
                , mImageKey(0)
                , mSyncPending(false)
            {
                sInstances.append(this);

                mHost = [[OBGlassPanelHostView alloc] initWithFrame:NSZeroRect];
                [mHost setWantsLayer:YES];

#if defined(__MAC_26_0)
                if (material == PanelMaterial::Glass)
                {
                    if (@available(macOS 26.0, *))
                    {
                        NSGlassEffectView* glass = [[NSGlassEffectView alloc] initWithFrame:NSZeroRect];
                        [glass setStyle:NSGlassEffectViewStyleRegular];
                        mMaterialView = glass;

                        // a veil inside the glass (below its rim highlights) sets the light / dark material
                        mVeil = [[NSView alloc] initWithFrame:NSZeroRect];
                        [mVeil setWantsLayer:YES];
                        [glass setContentView:mVeil];
                    }
                }
#endif
                if (!mMaterialView)
                {
                    NSVisualEffectView* vibrancy = [[NSVisualEffectView alloc] initWithFrame:NSZeroRect];
                    [vibrancy setMaterial:NSVisualEffectMaterialPopover];
                    [vibrancy setBlendingMode:NSVisualEffectBlendingModeWithinWindow];
                    [vibrancy setState:NSVisualEffectStateActive];
                    [vibrancy setWantsLayer:YES];
                    [[vibrancy layer] setMasksToBounds:YES];
                    mMaterialView = vibrancy;

                    mVeil = [[NSView alloc] initWithFrame:NSZeroRect];
                    [mVeil setWantsLayer:YES];
                    [mVeil setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
                    [vibrancy addSubview:mVeil];
                }

                mContent = [[OBGlassPanelContentView alloc] initWithFrame:NSZeroRect];

                mDecor = [[OBGlassDecorView alloc] initWithFrame:NSZeroRect];

                [mHost addSubview:mMaterialView];
                [mHost addSubview:mDecor positioned:NSWindowAbove relativeTo:mMaterialView];
                [mHost addSubview:mContent positioned:NSWindowAbove relativeTo:mDecor];
                [mHost setHidden:YES];

                mEffect = new UBGlassMirrorEffect(this, panel);
                panel->setGraphicsEffect(mEffect);

                QObject::connect(UBThemeManager::instance(), &UBThemeManager::themeChanged, this, [this]() {
                    scheduleSync();
                });

                findGpuChildren();
                watchAncestors();
                sync();
            }

            ~UBMacPanelGlass() override
            {
                sInstances.removeAll(this);

                if (mEffect)
                    mEffect->detach();

                @try
                {
                    [mHost removeFromSuperview];
                }
                @catch (NSException* exception)
                {
                    qWarning("UBGlassEffect: removing the panel glass failed: %s", [[exception reason] UTF8String]);
                }
#if !__has_feature(objc_arc)
                [mDecor release];
                [mVeil release];
                [mContent release];
                [mMaterialView release];
                [mHost release];
#endif
                mContent = nil;
                mMaterialView = nil;
                mHost = nil;
            }

            bool eventFilter(QObject* watched, QEvent* event) override
            {
                Q_UNUSED(watched);

                switch (event->type())
                {
                    case QEvent::Show:
                        if (watched == mPanel)
                            findGpuChildren();
                        sync();
                        scheduleSync();
                        break;

                    case QEvent::Move:
                    case QEvent::Resize:
                    case QEvent::Hide:
                    case QEvent::ZOrderChange:
                    case QEvent::EnabledChange:
                    case QEvent::WinIdChange:
                        sync();
                        // the native views may only follow after the event: sync again
                        scheduleSync();
                        break;

                    case QEvent::ParentChange:
                        watchAncestors();
                        sync();
                        scheduleSync();
                        break;

                    default:
                        break;
                }

                return false;
            }

            /**
             * Content composed by the GPU (web views) cannot be rendered through the
             * effect: while such a child is visible the panel paints itself (Qt
             * painted glass) and the native overlay is hidden.
             */
            bool suspended()
            {
                bool gpuVisible = false;

                for (const QPointer<QWidget>& child : std::as_const(mGpuChildren))
                {
                    if (child && child->isVisible())
                        gpuVisible = true;
                }

                // a panel moved into a translucent window (the desktop annotation overlay takes the
                // dock palettes and the "add to" palette) paints its glass itself, like the panels
                // created there: no native material on top of the desktop
                if (mPanel && mPanel->window()->testAttribute(Qt::WA_TranslucentBackground))
                    gpuVisible = true;

                if (gpuVisible != mSuspended)
                {
                    mSuspended = gpuVisible;

                    // the panel paints its glass body itself while suspended
                    if (mPanel)
                        mPanel->setProperty("ubNativeGlass", !mSuspended);

                    QTimer::singleShot(0, this, [this]() {
                        sync();

                        if (mPanel)
                            mPanel->update();
                    });
                }

                return mSuspended;
            }

            void findGpuChildren()
            {
                mGpuChildren.clear();

                if (!mPanel)
                    return;

                for (QWidget* child : mPanel->findChildren<QWidget*>())
                {
                    if (child->inherits("QOpenGLWidget") || child->inherits("QQuickWidget") || child->inherits("QWebEngineView"))
                        mGpuChildren << child;
                }
            }

            QBackingStore* windowBackingStore() const
            {
                return mPanel ? mPanel->window()->backingStore() : nullptr;
            }

            /** the panel was painted: show the rendered controls on the glass */
            void setImage(const QPixmap& pixmap, const QPoint& offset)
            {
                if (pixmap.isNull() || !mContent)
                    return;

                if (pixmap.cacheKey() == mImageKey && offset == mImageOffset)
                    return;

                QImage image = pixmap.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);

                // the panel is also repainted when only the content below it changes: same controls, no update
                const size_t contentHash = qHashBits(image.constBits(), image.sizeInBytes());

                if (contentHash == mContentHash && image.size() == mContentSize && offset == mImageOffset && mImageKey != 0)
                {
                    mImageKey = pixmap.cacheKey();
                    // the glass body may have changed with the paint (dock tabs publish it there)
                    sync();
                    return;
                }

                mImageKey = pixmap.cacheKey();
                mImageOffset = offset;
                mContentHash = contentHash;
                mContentSize = image.size();
                CGImageRef cgImage = image.toCGImage();

                @try
                {
                    const qreal dpr = pixmap.devicePixelRatio();
                    [mContent setFrame:NSMakeRect(offset.x(), offset.y(), pixmap.width() / dpr, pixmap.height() / dpr)];
                    [mContent setImage:cgImage scale:dpr];
                }
                @catch (NSException* exception)
                {
                    qWarning("UBGlassEffect: panel content update failed: %s", [[exception reason] UTF8String]);
                }

                if (cgImage)
                    CGImageRelease(cgImage);

                // the first image of a shown panel, or a changed glass body
                sync();
            }

        private:
            void scheduleSync()
            {
                if (mSyncPending)
                    return;

                mSyncPending = true;

                QTimer::singleShot(0, this, [this]() {
                    mSyncPending = false;
                    sync();
                });
            }

            void watchAncestors()
            {
                for (const QPointer<QWidget>& watched : std::as_const(mWatched))
                {
                    if (watched)
                        watched->removeEventFilter(this);
                }

                mWatched.clear();

                // the panel, and its ancestors for moves and resizes of the board area
                for (QWidget* widget = mPanel; widget; widget = widget->isWindow() ? nullptr : widget->parentWidget())
                {
                    widget->installEventFilter(this);
                    mWatched << widget;
                }
            }

            /** paint order of a widget in its window, used to stack the overlays */
            static QList<int> stackingPath(QWidget* widget)
            {
                QList<int> path;

                for (QWidget* w = widget; w && !w->isWindow(); w = w->parentWidget())
                {
                    QWidget* parent = w->parentWidget();

                    if (!parent)
                        break;

                    path.prepend(parent->children().indexOf(w));
                }

                return path;
            }

            static void restack(NSView* topView)
            {
                QList<UBMacPanelGlass*> ordered;

                for (UBMacPanelGlass* glass : std::as_const(sInstances))
                {
                    if (glass->mPanel && glass->mHost && [glass->mHost superview] == topView)
                        ordered << glass;
                }

                if (ordered.size() < 2)
                    return;

                std::stable_sort(ordered.begin(), ordered.end(), [](UBMacPanelGlass* a, UBMacPanelGlass* b) {
                    return stackingPath(a->mPanel) < stackingPath(b->mPanel);
                });

                // current order of the overlays in the native view
                QList<UBMacPanelGlass*> current;

                for (NSView* subview in [topView subviews])
                {
                    for (UBMacPanelGlass* glass : std::as_const(ordered))
                    {
                        if (glass->mHost == subview)
                            current << glass;
                    }
                }

                if (current == ordered)
                    return;

                for (UBMacPanelGlass* glass : std::as_const(ordered))
                    [topView addSubview:glass->mHost positioned:NSWindowAbove relativeTo:nil];
            }

            void sync()
            {
                if (!mPanel || !mHost)
                    return;

                @try
                {
                    NSView* topView = nativeTopView(mPanel);

                    if (!topView)
                    {
                        [mHost setHidden:YES];
                        return;
                    }

                    if ([mHost superview] != topView)
                    {
                        [mHost removeFromSuperview];
                        [topView addSubview:mHost positioned:NSWindowAbove relativeTo:nil];
                    }

                    // stay above the native child views Qt may have added later
                    if ([[topView subviews] lastObject] != mHost)
                    {
                        bool onlyOverlaysAbove = true;
                        bool seen = false;

                        for (NSView* subview in [topView subviews])
                        {
                            if (subview == mHost)
                                seen = true;
                            else if (seen && ![subview isKindOfClass:[OBGlassPanelHostView class]])
                                onlyOverlaysAbove = false;
                        }

                        if (!onlyOverlaysAbove)
                            [topView addSubview:mHost positioned:NSWindowAbove relativeTo:nil];
                    }

                    restack(topView);

                    QWidget* top = mPanel->window();
                    const QPoint origin = mPanel->mapTo(top, QPoint(0, 0));
                    const QSize size = mPanel->size();

                    NSRect frame = NSMakeRect(origin.x(), origin.y(), size.width(), size.height());

                    if (![topView isFlipped])
                        frame.origin.y = NSHeight([topView bounds]) - origin.y() - size.height();

                    if (!NSEqualRects([mHost frame], frame))
                        [mHost setFrame:frame];

                    // the material covers the body of the panel, the margin around it stays clear; a
                    // body given as an inset follows the size at once (a resize is seen here before
                    // the panel itself handles it)
                    QRect body = mPanel->property("ubGlassBody").toRect();
                    const QVariant inset = mPanel->property("ubGlassInset");

                    if (inset.userType() == qMetaTypeId<QMargins>())
                        body = mPanel->rect().marginsRemoved(inset.value<QMargins>());
                    else if (inset.isValid())
                        body = mPanel->rect().adjusted(inset.toInt(), inset.toInt(), -inset.toInt(), -inset.toInt());

                    if (body.isEmpty())
                        body = mPanel->rect();

                    const NSRect bodyFrame = NSMakeRect(body.x(), body.y(), body.width(), body.height());

                    if (!NSEqualRects([mMaterialView frame], bodyFrame))
                        [mMaterialView setFrame:bodyFrame];

                    const CGFloat radius = qMax(0.0, mPanel->property("ubGlassRadius").toReal());
                    // the material of the panel: adapts to the content below (UBFloatingPalette), else the theme
                    const QVariant onDark = mPanel->property("glassOnDark");
                    const bool dark = onDark.isValid() ? onDark.toBool() : UBThemeManager::instance()->isDarkMode();

#if defined(__MAC_26_0)
                    if (@available(macOS 26.0, *))
                    {
                        if ([mMaterialView isKindOfClass:[NSGlassEffectView class]])
                        {
                            NSGlassEffectView* glass = (NSGlassEffectView*)mMaterialView;

                            if ([glass cornerRadius] != radius)
                                [glass setCornerRadius:radius];

                        }
                    }
#endif
                    if ([mMaterialView isKindOfClass:[NSVisualEffectView class]])
                    {
                        [[mMaterialView layer] setCornerRadius:radius];
                        [mVeil setFrame:[mMaterialView bounds]];
                    }

                    if (mVeil)
                    {
                        NSColor* veil = glassVeil(dark, mMaterial);
                        [[mVeil layer] setBackgroundColor:veil ? [veil CGColor] : nil];
                        [[mVeil layer] setCornerRadius:radius];
                    }

                    if (mDecor)
                    {
                        if (!NSEqualRects([mDecor frame], [mHost bounds]))
                            [mDecor setFrame:[mHost bounds]];

                        [mDecor setBody:bodyFrame radius:radius look:glassLook(dark)];
                    }

                    // light glass with dark glyphs, or dark glass with white glyphs
                    NSAppearance* appearance = [NSAppearance appearanceNamed:(dark ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua)];

                    if (appearance && ![[[mHost appearance] name] isEqualToString:[appearance name]])
                        [mHost setAppearance:appearance];

                    // wait for the first rendered image, an empty glass body would flash
                    // (never in a translucent window, see suspended()); a panel without size
                    // (a collapsed dock) is not painted any more: its last body and controls
                    // would stay on the native glass, which is not clipped to the panel
                    // (it opens again with the next rendered image, not with the old controls)
                    if (size.isEmpty())
                        mImageKey = 0;

                    const bool visible = mPanel->isVisible() && !size.isEmpty() && mImageKey != 0 && !mSuspended
                            && !mPanel->window()->testAttribute(Qt::WA_TranslucentBackground);
                    [mHost setHidden:(visible ? NO : YES)];
                }
                @catch (NSException* exception)
                {
                    qWarning("UBGlassEffect: panel glass sync failed: %s", [[exception reason] UTF8String]);
                }
            }

            QPointer<QWidget> mPanel;
            PanelMaterial mMaterial;
            NSView* mHost;
            NSView* mMaterialView;
            NSView* mVeil = nil;
            OBGlassDecorView* mDecor = nil;
            OBGlassPanelContentView* mContent;
            QPointer<UBGlassMirrorEffect> mEffect;
            qint64 mImageKey;
            QPoint mImageOffset;
            size_t mContentHash = 0;
            QSize mContentSize;
            bool mSyncPending;
            bool mSuspended = false;
            QList<QPointer<QWidget>> mWatched;
            QList<QPointer<QWidget>> mGpuChildren;

            static QList<UBMacPanelGlass*> sInstances;
    };

    QList<UBMacPanelGlass*> UBMacPanelGlass::sInstances;

    void UBGlassMirrorEffect::draw(QPainter* painter)
    {
        if (!mGlass || mGlass->suspended())
        {
            drawSource(painter);
            return;
        }

        // only a paint of the window counts: other glass panels render this one while they
        // capture the content behind them (clipped to their area); that result must not
        // replace the controls on the native glass
        QBackingStore* store = mGlass->windowBackingStore();

        if (!store || !painter || painter->device() != store->paintDevice())
        {
            return;
        }

        // never painted into the window, only on the native glass
        QPoint offset;
        const QPixmap pixmap = sourcePixmap(Qt::LogicalCoordinates, &offset, QGraphicsEffect::NoPad);
        mGlass->setImage(pixmap, offset);
    }
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

#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    // the board extends below the transparent title bar: no safe area margins for the content
    top->setAttribute(Qt::WA_ContentsMarginsRespectsSafeArea, false);

    const Qt::WindowFlags flags = top->windowFlags() | Qt::ExpandedClientAreaHint | Qt::NoTitleBarBackgroundHint;

    if (top->windowHandle())
    {
        // already created: change the hints without re-creating the window
        top->overrideWindowFlags(flags);
        top->windowHandle()->setFlags(flags);
    }
    else
    {
        top->setWindowFlags(flags);
    }
#endif

    // the window stays opaque: the board is drawn edge to edge below the transparent title bar
    top->installEventFilter(new UBMacWindowGlassFilter(top));
    updateWindowChrome(top);
}


void UBGlassEffect::applyToMenu(QWidget* menu)
{
    if (!menu || nativeGlassDisabled() || menu->property("ubNativeMenu").toBool())
        return;

    QList<QWidget*> menus = menu->findChildren<QWidget*>();
    menus.prepend(menu);

    for (QWidget* m : std::as_const(menus))
    {
        if (!m->inherits("QMenu") || m->property("ubNativeMenu").toBool())
            continue;

        // translucent before the native window exists: the material shows through the Qt content
        m->setProperty("ubNativeMenu", true);
        m->setAttribute(Qt::WA_TranslucentBackground);
        m->setStyleSheet(QStringLiteral("QMenu { background: transparent; border: none; padding: 5px 0px; }"));
        new UBMacMenuMaterial(m);
    }
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
    if (!panel || panel->isWindow() || panelMaterial() == PanelMaterial::Painted)
        return;

    if (panel->property("ubNativeGlassInstalled").toBool() || panel->property("ubNativeGlassFailed").toBool())
        return;

    // another effect (e.g. an animation) owns the painting of the panel
    if (panel->graphicsEffect())
        return;

    // translucent windows (desktop annotation) show the desktop behind them, the painted glass
    // is made for that
    // through the graphics effect
    if (panel->window()->testAttribute(Qt::WA_TranslucentBackground))
        return;

    // native child views would be stacked above the overlay
    for (QWidget* child : panel->findChildren<QWidget*>())
    {
        if (child->internalWinId())
            return;
    }

    @try
    {
        // the panel paints its controls only, the native material is drawn below them
        panel->setProperty("ubNativeGlass", true);
        panel->setProperty("ubNativeGlassInstalled", true);
        new UBMacPanelGlass(panel, panelMaterial());
        panel->update();
    }
    @catch (NSException* exception)
    {
        qWarning("UBGlassEffect: native panel glass failed: %s", [[exception reason] UTF8String]);
        panel->setProperty("ubNativeGlass", false);
        panel->setProperty("ubNativeGlassFailed", true);
    }
}
