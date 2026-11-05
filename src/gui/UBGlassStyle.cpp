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

#include "UBGlassStyle.h"

#include <QApplication>
#include <QAction>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmapCache>
#include <QStyle>
#include <QSvgRenderer>
#include <QToolBar>

#include <vector>

#include "core/UBThemeManager.h"
#include "gui/UBMainWindow.h"

#include "core/memcheck.h"

namespace
{
    QString rgba(const QColor& c)
    {
        return QString("rgba(%1, %2, %3, %4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
    }

    QColor withAlpha(QColor c, qreal alpha)
    {
        c.setAlphaF(alpha);
        return c;
    }

    /**
     * Icon engine rendering a monochrome SVG tinted with the current theme colours:
     * foreground for normal icons, white for checked icons (drawn on the accent
     * capsule) and a faded foreground for disabled icons.
     */
    class UBTintedSvgIconEngine : public QIconEngine
    {
        public:
            explicit UBTintedSvgIconEngine(const QString& path)
                : mPath(path)
            {
            }

            void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State state) override
            {
                const qreal dpr = painter->device() ? painter->device()->devicePixelRatio() : 1.0;
                QPixmap pm = pixmap(rect.size() * dpr, mode, state);
                pm.setDevicePixelRatio(dpr);
                painter->drawPixmap(rect, pm);
            }

            QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
            {
                if (size.isEmpty())
                    return QPixmap();

                const QColor color = tint(mode, state);
                const QString key = QString("ubglass:%1:%2x%3:%4")
                        .arg(mPath).arg(size.width()).arg(size.height()).arg(color.rgba(), 0, 16);

                QPixmap cached;
                if (QPixmapCache::find(key, &cached))
                    return cached;

                QImage image(size, QImage::Format_ARGB32_Premultiplied);
                image.fill(Qt::transparent);

                QSvgRenderer renderer(mPath);
                if (renderer.isValid())
                {
                    QPainter p(&image);
                    p.setRenderHint(QPainter::Antialiasing);
                    // keep the aspect ratio of the svg and center it
                    QSizeF svgSize = renderer.defaultSize();
                    if (svgSize.isEmpty())
                        svgSize = QSizeF(size);
                    svgSize.scale(QSizeF(size), Qt::KeepAspectRatio);
                    const QRectF target((size.width() - svgSize.width()) / 2.0, (size.height() - svgSize.height()) / 2.0,
                                        svgSize.width(), svgSize.height());
                    renderer.render(&p, target);
                    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
                    p.fillRect(image.rect(), color);
                }

                cached = QPixmap::fromImage(image);
                QPixmapCache::insert(key, cached);
                return cached;
            }

            QIconEngine* clone() const override
            {
                return new UBTintedSvgIconEngine(mPath);
            }

            QString key() const override
            {
                return QStringLiteral("UBTintedSvgIconEngine");
            }

        private:
            static QColor tint(QIcon::Mode mode, QIcon::State state)
            {
                if (mode == QIcon::Disabled)
                    return withAlpha(UBGlassStyle::foreground(), 0.28);

                if (state == QIcon::On)
                    return QColor(Qt::white);

                return withAlpha(UBGlassStyle::foreground(), 0.86);
            }

            QString mPath;
    };

    void boxBlurHorizontal(QImage& image, int radius)
    {
        const int w = image.width();
        const int h = image.height();
        const int window = 2 * radius + 1;
        std::vector<QRgb> line(w);

        for (int y = 0; y < h; ++y)
        {
            QRgb* row = reinterpret_cast<QRgb*>(image.scanLine(y));
            std::copy(row, row + w, line.begin());

            int sa = 0, sr = 0, sg = 0, sb = 0;

            for (int i = -radius; i <= radius; ++i)
            {
                const QRgb p = line[qBound(0, i, w - 1)];
                sa += qAlpha(p); sr += qRed(p); sg += qGreen(p); sb += qBlue(p);
            }

            for (int x = 0; x < w; ++x)
            {
                row[x] = qRgba(sr / window, sg / window, sb / window, sa / window);

                const QRgb add = line[qMin(x + radius + 1, w - 1)];
                const QRgb sub = line[qMax(x - radius, 0)];
                sa += qAlpha(add) - qAlpha(sub);
                sr += qRed(add) - qRed(sub);
                sg += qGreen(add) - qGreen(sub);
                sb += qBlue(add) - qBlue(sub);
            }
        }
    }

    void boxBlurVertical(QImage& image, int radius)
    {
        const int w = image.width();
        const int h = image.height();
        const int window = 2 * radius + 1;
        std::vector<QRgb> column(h);

        for (int x = 0; x < w; ++x)
        {
            for (int y = 0; y < h; ++y)
                column[y] = reinterpret_cast<const QRgb*>(image.constScanLine(y))[x];

            int sa = 0, sr = 0, sg = 0, sb = 0;

            for (int i = -radius; i <= radius; ++i)
            {
                const QRgb p = column[qBound(0, i, h - 1)];
                sa += qAlpha(p); sr += qRed(p); sg += qGreen(p); sb += qBlue(p);
            }

            for (int y = 0; y < h; ++y)
            {
                reinterpret_cast<QRgb*>(image.scanLine(y))[x] = qRgba(sr / window, sg / window, sb / window, sa / window);

                const QRgb add = column[qMin(y + radius + 1, h - 1)];
                const QRgb sub = column[qMax(y - radius, 0)];
                sa += qAlpha(add) - qAlpha(sub);
                sr += qRed(add) - qRed(sub);
                sg += qGreen(add) - qGreen(sub);
                sb += qBlue(add) - qBlue(sub);
            }
        }
    }
}


bool UBGlassStyle::isDark()
{
    return UBThemeManager::instance()->isDarkMode();
}


QColor UBGlassStyle::accent()
{
    QColor color = QApplication::palette().color(QPalette::Highlight);

    if (!color.isValid())
        color = QColor(10, 132, 255);

    return color;
}


QColor UBGlassStyle::foreground()
{
    return isDark() ? QColor(242, 242, 247) : QColor(28, 28, 30);
}


QPainterPath UBGlassStyle::roundedPath(const QRectF& rect, qreal radius)
{
    const qreal r = qMax<qreal>(0.0, qMin(radius, qMin(rect.width(), rect.height()) / 2.0));
    QPainterPath path;
    path.addRoundedRect(rect, r, r);
    return path;
}


void UBGlassStyle::paintGlass(QPainter* painter, const QRectF& body, qreal radius, const QImage& backdrop)
{
    if (body.isEmpty())
        return;

    const bool dark = isDark();
    const bool hasBackdrop = !backdrop.isNull();
    const QPainterPath path = roundedPath(body, radius);
    const qreal r = qMin(radius, qMin(body.width(), body.height()) / 2.0);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    painter->setPen(Qt::NoPen);

    // 1. the blurred content behind the panel
    if (hasBackdrop)
    {
        painter->save();
        painter->setClipPath(path);
        painter->drawImage(body, backdrop);
        painter->restore();
    }

    // 2. translucent tint with a subtle vertical gradient
    QLinearGradient tint(body.topLeft(), body.bottomLeft());

    if (dark)
    {
        tint.setColorAt(0.0, QColor(60, 60, 66, hasBackdrop ? 175 : 228));
        tint.setColorAt(1.0, QColor(34, 34, 40, hasBackdrop ? 195 : 238));
    }
    else
    {
        tint.setColorAt(0.0, QColor(255, 255, 255, hasBackdrop ? 165 : 232));
        tint.setColorAt(1.0, QColor(244, 246, 250, hasBackdrop ? 125 : 222));
    }

    painter->fillPath(path, tint);

    // 3. specular sheen on the upper part, like light caught by curved glass
    const qreal sheenHeight = qMin(body.height() * 0.55, 34.0);
    QLinearGradient sheen(body.topLeft(), body.topLeft() + QPointF(0, sheenHeight));
    sheen.setColorAt(0.0, QColor(255, 255, 255, dark ? 30 : 110));
    sheen.setColorAt(1.0, QColor(255, 255, 255, 0));
    painter->fillPath(path, sheen);

    // 4. outer hairline
    painter->setBrush(Qt::NoBrush);
    painter->setPen(QPen(dark ? QColor(0, 0, 0, 120) : QColor(0, 0, 0, 30), 1.0));
    painter->drawPath(roundedPath(body.adjusted(0.5, 0.5, -0.5, -0.5), r - 0.5));

    // 5. inner refraction highlight: lighter top-left, darker bottom-right
    QLinearGradient rim(body.topLeft(), body.bottomRight());
    if (dark)
    {
        rim.setColorAt(0.0, QColor(255, 255, 255, 95));
        rim.setColorAt(0.45, QColor(255, 255, 255, 18));
        rim.setColorAt(1.0, QColor(255, 255, 255, 40));
    }
    else
    {
        rim.setColorAt(0.0, QColor(255, 255, 255, 250));
        rim.setColorAt(0.5, QColor(255, 255, 255, 120));
        rim.setColorAt(1.0, QColor(0, 0, 0, 22));
    }
    painter->setPen(QPen(QBrush(rim), 1.2));
    painter->drawPath(roundedPath(body.adjusted(1.4, 1.4, -1.4, -1.4), r - 1.4));

    painter->restore();
}


QImage UBGlassStyle::shadowImage(const QSize& outerSize, const QRectF& body, qreal radius, qreal dpr)
{
    QImage image(outerSize * dpr, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    if (body.isEmpty())
        return image;

    const bool dark = isDark();
    const qreal margin = qMin(qMin(body.left(), body.top()),
                              qMin(outerSize.width() - body.right(), outerSize.height() - body.bottom()));

    if (margin >= 2)
    {
        // soft ambient shadow, slightly offset downwards
        QImage soft(image.size(), QImage::Format_ARGB32_Premultiplied);
        soft.fill(Qt::transparent);
        {
            QPainter p(&soft);
            p.setRenderHint(QPainter::Antialiasing);
            p.scale(dpr, dpr);
            const qreal inset = margin * 0.35;
            p.fillPath(roundedPath(body.adjusted(inset, inset + margin * 0.25, -inset, -inset + margin * 0.3), radius),
                       QColor(0, 0, 0, dark ? 150 : 70));
        }
        blur(soft, qMax(1, qRound(margin * 0.45 * dpr)));

        QPainter p(&image);
        p.drawImage(0, 0, soft);
    }

    {
        // crisp contact shadow
        QPainter p(&image);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(dpr, dpr);
        p.fillPath(roundedPath(body.adjusted(-0.5, 0.0, 0.5, 1.2), radius + 0.5), QColor(0, 0, 0, dark ? 70 : 22));

        // no shadow under the translucent body itself
        p.setCompositionMode(QPainter::CompositionMode_DestinationOut);
        p.fillPath(roundedPath(body, radius), Qt::black);
    }

    image.setDevicePixelRatio(dpr);
    return image;
}


void UBGlassStyle::blur(QImage& image, int radius)
{
    if (radius < 1 || image.isNull())
        return;

    if (image.format() != QImage::Format_ARGB32_Premultiplied)
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    // three box blur passes approximate a gaussian blur
    const int passRadius = qMax(1, radius / 2);

    for (int pass = 0; pass < 3; ++pass)
    {
        boxBlurHorizontal(image, passRadius);
        boxBlurVertical(image, passRadius);
    }
}


QImage UBGlassStyle::blurredBackdrop(const QImage& source, int blurRadius)
{
    if (source.isNull())
        return QImage();

    // blur at a quarter of the resolution, the upscaling when painting smooths further
    const int factor = 4;
    const QSize smallSize(qMax(1, source.width() / factor), qMax(1, source.height() / factor));
    QImage small = source.scaled(smallSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
            .convertToFormat(QImage::Format_ARGB32_Premultiplied);
    small.setDevicePixelRatio(1.0);

    blur(small, qMax(1, blurRadius / factor * 2));

    return small;
}


QIcon UBGlassStyle::icon(const QString& name)
{
    return tintedSvgIcon(QString(":/images/glass/%1.svg").arg(name));
}


QIcon UBGlassStyle::tintedSvgIcon(const QString& path)
{
    return QIcon(new UBTintedSvgIconEngine(path));
}


QIcon UBGlassStyle::swatchIcon(const QColor& color)
{
    QIcon icon;

    for (int size : {16, 24, 32, 48, 64})
    {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);

        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);

        const qreal border = qMax(1.0, size / 24.0);
        const QRectF circle = QRectF(0, 0, size, size).adjusted(border, border, -border, -border);

        painter.setBrush(color);
        painter.setPen(QPen(QColor(0, 0, 0, isDark() ? 0 : 45), border));
        painter.drawEllipse(circle);

        if (isDark())
        {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(QColor(255, 255, 255, 60), border));
            painter.drawEllipse(circle);
        }

        // a soft highlight gives the swatch a little depth
        QRadialGradient gloss(circle.center() - QPointF(size * 0.15, size * 0.2), size * 0.6);
        gloss.setColorAt(0.0, QColor(255, 255, 255, 60));
        gloss.setColorAt(1.0, QColor(255, 255, 255, 0));
        painter.setPen(Qt::NoPen);
        painter.setBrush(gloss);
        painter.drawEllipse(circle);

        icon.addPixmap(pixmap, QIcon::Normal, QIcon::Off);
        icon.addPixmap(pixmap, QIcon::Normal, QIcon::On);
    }

    return icon;
}


void UBGlassStyle::applyIcons(UBMainWindow* mainWindow)
{
    if (!mainWindow)
        return;

    const QList<QPair<QAction*, QString>> glassIcons = {
        {mainWindow->actionUndo, "undo"},
        {mainWindow->actionRedo, "redo"},
        {mainWindow->actionBackgrounds, "backgrounds"},
        {mainWindow->actionMathPalette, "maths"},
        {mainWindow->actionPages, "pages"},
        {mainWindow->actionBack, "previous"},
        {mainWindow->actionForward, "next"},
        {mainWindow->actionErase, "erase"},
        {mainWindow->actionBoard, "board"},
        {mainWindow->actionWeb, "web"},
        {mainWindow->actionDocument, "documents"},
        {mainWindow->actionDesktop, "desktop"},
        {mainWindow->actionMenu, "menu"},
        {mainWindow->actionSelector, "selector"},
        {mainWindow->actionPlay, "play"},
        {mainWindow->actionHand, "hand"},
        {mainWindow->actionZoomIn, "zoomIn"},
        {mainWindow->actionZoomOut, "zoomOut"},
        {mainWindow->actionPointer, "pointer"},
        {mainWindow->actionLine, "line"},
        {mainWindow->actionShapes, "shapes"},
        {mainWindow->actionText, "text"},
        {mainWindow->actionCapture, "capture"},
        {mainWindow->actionVirtualKeyboard, "keyboard"},
        {mainWindow->actionSnap, "snap"},
        {mainWindow->actionLineSmall, "lineSmall"},
        {mainWindow->actionLineMedium, "lineMedium"},
        {mainWindow->actionLineLarge, "lineLarge"},
        {mainWindow->actionEraserSmall, "eraserSmall"},
        {mainWindow->actionEraserMedium, "eraserMedium"},
        {mainWindow->actionEraserLarge, "eraserLarge"},
        {mainWindow->actionColorPreferences, "paletteSettings"},
        // web mode
        {mainWindow->actionWebTools, "tools"},
        {mainWindow->actionWebBack, "previous"},
        {mainWindow->actionWebForward, "next"},
        {mainWindow->actionWebReload, "reload"},
        {mainWindow->actionStopLoading, "stop"},
        {mainWindow->actionHome, "home"},
        {mainWindow->actionWebBigger, "zoomIn"},
        {mainWindow->actionWebSmaller, "zoomOut"},
    };

    for (const auto& entry : glassIcons)
    {
        if (entry.first)
            entry.first->setIcon(icon(entry.second));
    }

    // the shape icons are already monochrome svgs, only tint them
    const QList<QPair<QAction*, QString>> shapeIcons = {
        {mainWindow->actionShapeRect, ":/images/toolbar/shapeRect.svg"},
        {mainWindow->actionShapeRoundedRect, ":/images/toolbar/shapeRoundedRect.svg"},
        {mainWindow->actionShapeEllipse, ":/images/toolbar/shapeEllipse.svg"},
        {mainWindow->actionShapeTriangle, ":/images/toolbar/shapeTriangle.svg"},
        {mainWindow->actionShapeLine, ":/images/toolbar/shapeLine.svg"},
        {mainWindow->actionShapeArrow, ":/images/toolbar/shapeArrow.svg"},
        {mainWindow->actionShapeFill, ":/images/toolbar/shapeFill.svg"},
    };

    for (const auto& entry : shapeIcons)
    {
        if (entry.first)
            entry.first->setIcon(tintedSvgIcon(entry.second));
    }
}


void UBGlassStyle::setToolBarTextMode(QToolBar* toolBar, bool textUnderIcon)
{
    if (!toolBar)
        return;

    const QVariant current = toolBar->property("glassText");

    if (current.isValid() && current.toBool() == textUnderIcon)
        return;

    toolBar->setProperty("glassText", textUnderIcon);

    // descendant selectors depend on the property of the toolbar: polish the buttons again
    QList<QWidget*> widgets = toolBar->findChildren<QWidget*>();
    widgets.prepend(toolBar);

    for (QWidget* widget : std::as_const(widgets))
    {
        widget->style()->unpolish(widget);
        widget->style()->polish(widget);
        widget->updateGeometry();
    }
}


QString UBGlassStyle::styleSheet(bool dark)
{
    const QColor fg = dark ? QColor(242, 242, 247) : QColor(28, 28, 30);
    const QColor fgDisabled = withAlpha(fg, 0.32);
    const QColor hover = dark ? QColor(255, 255, 255, 26) : QColor(0, 0, 0, 14);
    const QColor pressed = dark ? QColor(255, 255, 255, 46) : QColor(0, 0, 0, 30);
    const QColor hairline = dark ? QColor(255, 255, 255, 30) : QColor(0, 0, 0, 28);
    const QColor accentColor = accent();
    const QColor accentPressed = accentColor.darker(115);
    const QColor barTop = dark ? QColor(48, 48, 52) : QColor(250, 250, 252);
    const QColor barBottom = dark ? QColor(38, 38, 42) : QColor(240, 242, 246);
    const QColor menuBackground = dark ? QColor(44, 44, 48) : QColor(252, 252, 253);
    const QColor popupBackground = dark ? QColor(40, 40, 44) : QColor(248, 249, 251);

    const QString barBackground = QString("qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %1, stop:1 %2)")
            .arg(rgba(barTop), rgba(barBottom));

    QString css = QStringLiteral(R"CSS(
/* ---------- Liquid Glass layer (UBGlassStyle) ---------- */

QToolBar#boardToolBar,
QToolBar#webToolBar,
QToolBar#documentToolBar
{
    background: @BAR@;
    border: none;
    border-bottom: 1px solid @HAIR@;
    spacing: 4px;
    padding: 5px 10px;
}

QToolBar#boardToolBar::separator,
QToolBar#webToolBar::separator,
QToolBar#documentToolBar::separator
{
    background: @HAIR@;
    border: none;
    width: 1px;
    margin: 8px 8px;
}

QToolBar#boardToolBar QToolButton,
QToolBar#webToolBar QToolButton,
QToolBar#documentToolBar QToolButton
{
    color: @FG@;
    background: transparent;
    border: none;
    border-radius: 10px;
    padding: 4px 6px;
    margin: 0px;
    min-width: 30px;
    min-height: 30px;
    height: 28px;
}

QToolBar#boardToolBar[glassText="true"] QToolButton,
QToolBar#webToolBar[glassText="true"] QToolButton,
QToolBar#documentToolBar[glassText="true"] QToolButton
{
    height: 44px;
}

QToolBar#boardToolBar QToolButton:hover,
QToolBar#webToolBar QToolButton:hover,
QToolBar#documentToolBar QToolButton:hover
{
    background: @HOVER@;
    border: none;
}

QToolBar#boardToolBar QToolButton:pressed,
QToolBar#webToolBar QToolButton:pressed,
QToolBar#documentToolBar QToolButton:pressed
{
    background: @PRESSED@;
    color: @FG@;
}

QToolBar#boardToolBar QToolButton:checked,
QToolBar#webToolBar QToolButton:checked,
QToolBar#documentToolBar QToolButton:checked
{
    background: @ACCENT@;
    color: #ffffff;
}

QToolBar#boardToolBar QToolButton:disabled,
QToolBar#webToolBar QToolButton:disabled,
QToolBar#documentToolBar QToolButton:disabled
{
    background: transparent;
    color: @FGDISABLED@;
}

/* macOS: the native vibrancy view of UBGlassEffect shows through the top bar */
QMainWindow[ubGlassVibrancy="true"]
{
    background: transparent;
}

QMainWindow[ubGlassVibrancy="true"] QToolBar#boardToolBar,
QMainWindow[ubGlassVibrancy="true"] QToolBar#webToolBar,
QMainWindow[ubGlassVibrancy="true"] QToolBar#documentToolBar
{
    background: transparent;
}

QToolBar QToolButton#ubButtonMenu
{
    padding-right: 14px;
}

QToolBar QToolButton#ubButtonMenu::menu-indicator
{
    subcontrol-origin: padding;
    subcontrol-position: bottom right;
    right: 3px;
    bottom: 6px;
}

/* ---------- floating glass palettes ---------- */

UBActionPaletteButton,
QToolButton#glassButton
{
    color: @FG@;
    background: transparent;
    border: none;
    border-radius: 12px;
    padding: 3px;
    margin: 0px;
    font-weight: 600;
}

UBActionPaletteButton:hover,
QToolButton#glassButton:hover
{
    background: @HOVER@;
}

UBActionPaletteButton:pressed,
QToolButton#glassButton:pressed
{
    background: @PRESSED@;
}

UBActionPaletteButton:checked,
QToolButton#glassButton:checked
{
    background: @ACCENT@;
    color: #ffffff;
}

UBActionPaletteButton:checked:pressed,
QToolButton#glassButton:checked:pressed
{
    background: @ACCENTPRESSED@;
}

UBActionPaletteButton:disabled,
QToolButton#glassButton:disabled
{
    color: @FGDISABLED@;
    background: transparent;
}

UBFloatingPalette QStackedWidget,
UBFloatingPalette QStackedWidget > QWidget,
UBFloatingPalette QWidget#shapeToolWidget,
UBFloatingPalette QSlider,
UBFloatingPalette QAbstractScrollArea,
UBFloatingPalette QAbstractScrollArea > QWidget#qt_scrollarea_viewport,
UBFloatingPalette QAbstractScrollArea > QWidget#qt_scrollarea_viewport > QWidget,
#BackgroundPaletteScrollArea,
#BackgroundPaletteScrollArea QWidget
{
    background: transparent;
    border: none;
}

UBFloatingPalette QSlider::groove:horizontal
{
    height: 4px;
    margin: 0px;
    border: none;
    border-radius: 2px;
    background: @TRACK@;
}

UBFloatingPalette QSlider::sub-page:horizontal
{
    border: none;
    border-radius: 2px;
    background: @ACCENT@;
}

UBFloatingPalette QSlider::add-page:horizontal
{
    border: none;
    border-radius: 2px;
    background: @TRACK@;
}

UBFloatingPalette QSlider::handle:horizontal
{
    width: 18px;
    height: 18px;
    margin: -7px 0px;
    border-radius: 9px;
    border: 1px solid @HAIR@;
    background: #ffffff;
}

#BackgroundPaletteScrollArea UBActionPaletteButton:hover
{
    background: @HOVER@;
}

#BackgroundPaletteScrollArea UBActionPaletteButton:checked
{
    background: @ACCENT@;
}

#BackgroundPalette QLabel,
#BackgroundPaletteLabel,
UBFloatingPalette QLabel
{
    color: @FG@;
    background: transparent;
}

UBFloatingPalette QToolButton#closeButton,
#BackgroundPalette QToolButton#closeButton,
#AddItemPalette QToolButton#closeButton,
#UBStartupHintsPalette QToolButton#closeButton
{
    background: @HOVER@;
    border: none;
    border-radius: 11px;
    padding: 0px;
    margin: 0px;
    min-width: 22px;
    max-width: 22px;
    min-height: 22px;
    max-height: 22px;
}

UBFloatingPalette QToolButton#closeButton:hover,
#BackgroundPalette QToolButton#closeButton:hover,
#AddItemPalette QToolButton#closeButton:hover,
#UBStartupHintsPalette QToolButton#closeButton:hover
{
    background: @PRESSED@;
}

/* ---------- pen panel and desktop property palettes ---------- */

UBPenPanel QToolButton,
UBToolbarButtonGroup QToolButton#desktop-ubButtonGroupLeft,
UBToolbarButtonGroup QToolButton#desktop-ubButtonGroupCenter,
UBToolbarButtonGroup QToolButton#desktop-ubButtonGroupRight
{
    color: @FG@;
    background: transparent;
    border: none;
    border-radius: 17px;
    padding: 0px;
    margin: 0px;
    min-width: 34px;
    max-width: 34px;
    min-height: 34px;
    max-height: 34px;
}

UBPenPanel QToolButton:hover,
UBToolbarButtonGroup QToolButton#desktop-ubButtonGroupLeft:hover,
UBToolbarButtonGroup QToolButton#desktop-ubButtonGroupCenter:hover,
UBToolbarButtonGroup QToolButton#desktop-ubButtonGroupRight:hover
{
    background: @HOVER@;
    border: none;
}

UBPenPanel QToolButton:pressed,
UBToolbarButtonGroup QToolButton#desktop-ubButtonGroupLeft:pressed,
UBToolbarButtonGroup QToolButton#desktop-ubButtonGroupCenter:pressed,
UBToolbarButtonGroup QToolButton#desktop-ubButtonGroupRight:pressed
{
    background: @PRESSED@;
}

UBPenPanel QToolButton:checked,
UBToolbarButtonGroup QToolButton#desktop-ubButtonGroupLeft:checked,
UBToolbarButtonGroup QToolButton#desktop-ubButtonGroupCenter:checked,
UBToolbarButtonGroup QToolButton#desktop-ubButtonGroupRight:checked
{
    background: @ACCENT@;
    color: #ffffff;
    border: none;
}

UBToolbarButtonGroup QToolButton#desktop-ubButtonGroupLeft,
UBToolbarButtonGroup QToolButton#desktop-ubButtonGroupCenter,
UBToolbarButtonGroup QToolButton#desktop-ubButtonGroupRight
{
    qproperty-iconSize: 22px 22px;
}

UBPenPanel QToolButton[colorSwatch="true"]:checked,
QToolButton#desktop-ubButtonGroupLeft[colorSwatch="true"]:checked,
QToolButton#desktop-ubButtonGroupCenter[colorSwatch="true"]:checked,
QToolButton#desktop-ubButtonGroupRight[colorSwatch="true"]:checked
{
    background: transparent;
    border: 3px solid @ACCENT@;
}

UBPenPanel QToolButton[colorSwatch="true"]:checked:hover,
QToolButton#desktop-ubButtonGroupLeft[colorSwatch="true"]:checked:hover,
QToolButton#desktop-ubButtonGroupCenter[colorSwatch="true"]:checked:hover,
QToolButton#desktop-ubButtonGroupRight[colorSwatch="true"]:checked:hover
{
    background: @HOVER@;
}

QToolButton#desktop-ubButtonGroupRight[colorPaletteConfigButton="true"],
QToolButton#glass-ubButtonGroupRight[colorPaletteConfigButton="true"]
{
    background: transparent;
    border: none;
}

QToolButton#desktop-ubButtonGroupRight[colorPaletteConfigButton="true"]:hover,
QToolButton#glass-ubButtonGroupRight[colorPaletteConfigButton="true"]:hover
{
    background: @HOVER@;
}

QPushButton#zoomPaletteButton
{
    color: @FG@;
    background: transparent;
    border: none;
    border-radius: 10px;
    padding: 2px 6px;
    font-size: 18px;
    font-weight: 600;
}

QPushButton#zoomPaletteButton:hover
{
    background: @HOVER@;
}

/* glass palettes paint their own background */
#DesktopPropertyPalette,
#AddItemPalette,
#BackgroundPalette
{
    background: transparent;
    border: none;
}

UBPenPanel QFrame#glassDivider
{
    background: @HAIR@;
    border: none;
    min-width: 1px;
    max-width: 1px;
    margin-top: 7px;
    margin-bottom: 7px;
}

/* ---------- popups, menus, dialogs ---------- */

QFrame#colorPickerPopup
{
    background: @POPUP@;
    border: 1px solid @HAIR@;
    border-radius: 0px;
}

QFrame#colorPickerPopup QLabel
{
    background: transparent;
}

QMenu
{
    background-color: @MENU@;
    border: 1px solid @HAIR@;
    padding: 6px 4px;
    font-size: 13px;
}

QMenu::item
{
    background-color: transparent;
    color: @FG@;
    padding: 6px 28px 6px 12px;
    margin: 0px 2px;
    border-radius: 6px;
}

QMenu::item:selected
{
    background-color: @ACCENT@;
    color: #ffffff;
}

QMenu::item:disabled
{
    color: @FGDISABLED@;
}

QMenu::icon
{
    padding-left: 8px;
}

QMenu::separator
{
    height: 1px;
    background: @HAIR@;
    border: none;
    margin: 5px 10px;
}

QDialog#preferencesDialog QTabBar::tab
{
    background: transparent;
    color: @FG@;
    border: none;
    border-radius: 8px;
    padding: 6px 14px;
    margin: 6px 2px 4px 2px;
    min-width: 8ex;
}

QDialog#preferencesDialog QTabBar::tab:hover:!selected
{
    background: @HOVER@;
}

QDialog#preferencesDialog QTabBar::tab:selected
{
    background: @ACCENT@;
    color: #ffffff;
    border: none;
}

QDialog#preferencesDialog QTabWidget::pane
{
    border: 1px solid @HAIR@;
    border-radius: 10px;
    top: -1px;
}
)CSS");

    css.replace("@BAR@", barBackground);
    css.replace("@HAIR@", rgba(hairline));
    css.replace("@FG@", rgba(fg));
    css.replace("@FGDISABLED@", rgba(fgDisabled));
    css.replace("@HOVER@", rgba(hover));
    css.replace("@PRESSED@", rgba(pressed));
    css.replace("@ACCENTPRESSED@", rgba(accentPressed));
    css.replace("@ACCENT@", rgba(accentColor));
    css.replace("@MENU@", rgba(menuBackground));
    css.replace("@TRACK@", rgba(withAlpha(fg, 0.18)));
    css.replace("@POPUP@", rgba(popupBackground));

    return css;
}
