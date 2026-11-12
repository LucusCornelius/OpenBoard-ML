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
#include <QHash>
#include <QWidget>

#include <vector>

#include "core/UBThemeManager.h"
#include "gui/UBMainWindow.h"
#include "gui/UBModernStyle.h"
#include "gui/UBUiStyle.h"

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

    /** material of the glass panel whose content is being painted, -1: none (theme colours) */
    int sPaintMaterial = -1;

    struct UBGlassIconTint
    {
        static QColor color(QIcon::Mode mode, QIcon::State state)
        {
            const int material = sPaintMaterial;
            const bool onDark = material < 0 ? UBGlassStyle::isDark() : material == 1;
            const QColor fg = UBGlassStyle::foreground(onDark);

            if (mode == QIcon::Disabled)
                return withAlpha(fg, 0.30);

            // checked: white on the accent capsule (glass), accent on a tinted fill (modern)
            if (state == QIcon::On)
                return UBUiStyle::isGlass() ? QColor(Qt::white) : UBModernStyle::checkedForeground(onDark);

            return withAlpha(fg, 0.90);
        }
    };

    /**
     * Icon engine tinting a monochrome image (alpha mask) like the SVG icons.
     */
    class UBTintedImageIconEngine : public QIconEngine
    {
        public:
            explicit UBTintedImageIconEngine(const QImage& mask)
                : mMask(mask.convertToFormat(QImage::Format_ARGB32_Premultiplied))
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
                if (size.isEmpty() || mMask.isNull())
                    return QPixmap();

                const QColor color = UBGlassIconTint::color(mode, state);
                const QString key = QString("ubglassimg:%1:%2x%3:%4")
                        .arg(mMask.cacheKey()).arg(size.width()).arg(size.height()).arg(color.rgba(), 0, 16);

                QPixmap cached;
                if (QPixmapCache::find(key, &cached))
                    return cached;

                QImage image(size, QImage::Format_ARGB32_Premultiplied);
                image.fill(Qt::transparent);
                {
                    QPainter p(&image);
                    p.setRenderHint(QPainter::SmoothPixmapTransform);
                    QSizeF target = QSizeF(mMask.size());
                    target.scale(QSizeF(size), Qt::KeepAspectRatio);
                    p.drawImage(QRectF((size.width() - target.width()) / 2.0, (size.height() - target.height()) / 2.0,
                                       target.width(), target.height()), mMask);
                    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
                    p.fillRect(image.rect(), color);
                }

                cached = QPixmap::fromImage(image);
                QPixmapCache::insert(key, cached);
                return cached;
            }

            QIconEngine* clone() const override
            {
                return new UBTintedImageIconEngine(mMask);
            }

            QString key() const override
            {
                return QStringLiteral("UBTintedImageIconEngine");
            }

        private:
            QImage mMask;
    };

    /**
     * Application wide event filter: before a widget paints, remember the
     * material of the glass panel it belongs to, so that the tinted icons
     * use the right colour on light and on dark glass.
     */
    class UBGlassMaterialTracker : public QObject
    {
        public:
            using QObject::QObject;

            bool eventFilter(QObject* watched, QEvent* event) override
            {
                if (event->type() == QEvent::Paint && watched->isWidgetType())
                {
                    int material = -1;

                    for (QWidget* w = static_cast<QWidget*>(watched); w; w = w->parentWidget())
                    {
                        const QVariant value = w->property("glassOnDark");

                        if (value.isValid())
                        {
                            material = value.toBool() ? 1 : 0;
                            break;
                        }

                        if (w->isWindow())
                            break;
                    }

                    sPaintMaterial = material;
                }

                return false;
            }
    };

    /** mask of the rim band of a glass body, opaque at the edge fading out inwards (cached) */
    QImage rimMask(const QSize& pixelSize, qreal radius, qreal band, qreal dpr)
    {
        const QString key = QString("%1x%2:%3:%4:%5").arg(pixelSize.width()).arg(pixelSize.height())
                .arg(radius).arg(band).arg(dpr);

        static QHash<QString, QImage> cache;

        auto it = cache.constFind(key);
        if (it != cache.constEnd())
            return it.value();

        if (cache.size() > 24)
            cache.clear();

        const QRectF body(0, 0, pixelSize.width() / dpr, pixelSize.height() / dpr);

        QImage mask(pixelSize, QImage::Format_ARGB32_Premultiplied);
        mask.fill(Qt::transparent);

        QImage inner(pixelSize, QImage::Format_ARGB32_Premultiplied);
        inner.fill(Qt::transparent);
        {
            QPainter p(&inner);
            p.setRenderHint(QPainter::Antialiasing);
            p.scale(dpr, dpr);
            const qreal inset = qMin(band, qMin(body.width(), body.height()) / 2.0 - 1.0);
            p.fillPath(UBGlassStyle::roundedPath(body.adjusted(inset, inset, -inset, -inset), qMax(0.0, radius - inset)), Qt::black);
        }
        UBGlassStyle::blur(inner, qRound(band * 0.9 * dpr));

        {
            QPainter p(&mask);
            p.setRenderHint(QPainter::Antialiasing);
            p.scale(dpr, dpr);
            p.fillPath(UBGlassStyle::roundedPath(body, radius), Qt::black);
            p.resetTransform();
            p.setCompositionMode(QPainter::CompositionMode_DestinationOut);
            p.drawImage(0, 0, inner);
        }

        cache.insert(key, mask);
        return mask;
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
                return UBGlassIconTint::color(mode, state);
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
    return foreground(isDark());
}


QColor UBGlassStyle::foreground(bool onDarkMaterial)
{
    return onDarkMaterial ? QColor(255, 255, 255) : QColor(22, 22, 26);
}


void UBGlassStyle::setPanelMaterial(QWidget* panel, bool onDarkMaterial)
{
    if (!panel)
        return;

    const QVariant current = panel->property("glassOnDark");

    if (current.isValid() && current.toBool() == onDarkMaterial)
        return;

    panel->setProperty("glassOnDark", onDarkMaterial);

    // descendant selectors of the stylesheet depend on the property: polish again
    QList<QWidget*> widgets = panel->findChildren<QWidget*>();
    widgets.prepend(panel);

    for (QWidget* widget : std::as_const(widgets))
    {
        widget->style()->unpolish(widget);
        widget->style()->polish(widget);
        widget->update();
    }
}


void UBGlassStyle::installMaterialTracker()
{
    static bool installed = false;

    if (installed || !qApp)
        return;

    installed = true;
    qApp->installEventFilter(new UBGlassMaterialTracker(qApp));
}


int UBGlassStyle::currentPaintMaterial()
{
    return sPaintMaterial;
}


QPainterPath UBGlassStyle::roundedPath(const QRectF& rect, qreal radius)
{
    const qreal r = qMax<qreal>(0.0, qMin(radius, qMin(rect.width(), rect.height()) / 2.0));
    QPainterPath path;
    path.addRoundedRect(rect, r, r);
    return path;
}


void UBGlassStyle::paintGlass(QPainter* painter, const QRectF& body, qreal radius)
{
    paintGlass(painter, body, radius, UBGlassBackdrop(), isDark());
}


void UBGlassStyle::paintGlass(QPainter* painter, const QRectF& body, qreal radius,
                              const UBGlassBackdrop& backdrop, bool onDark)
{
    if (body.isEmpty())
        return;

    const qreal r = qMax<qreal>(0.0, qMin(radius, qMin(body.width(), body.height()) / 2.0));
    const QPainterPath path = roundedPath(body, r);
    const bool hasBackdrop = backdrop.isValid();
    const qreal dpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    painter->setPen(Qt::NoPen);
    painter->setClipPath(path, Qt::IntersectClip);

    if (hasBackdrop)
    {
        // 1. the blurred, slightly saturated content behind the panel
        painter->drawImage(backdrop.rect, backdrop.blurred);

        // 2. lensing: along the rim the glass bends the content, the rim shows
        //    content from a little further outside, less blurred, like thick glass
        if (!backdrop.lens.isNull())
        {
            const qreal band = qMin<qreal>(14.0, qMin(body.width(), body.height()) / 2.0);
            const qreal displacement = qMin<qreal>(10.0, band * 0.75);
            const QSize pixelSize = (body.size() * dpr).toSize();

            if (!pixelSize.isEmpty())
            {
                QImage lens(pixelSize, QImage::Format_ARGB32_Premultiplied);
                lens.fill(Qt::transparent);
                {
                    QPainter lp(&lens);
                    lp.setRenderHint(QPainter::SmoothPixmapTransform);
                    lp.scale(dpr, dpr);
                    lp.translate(-body.topLeft());

                    // shrink the backdrop around the centre of the body: a point on the
                    // edge of the body shows the content 'displacement' pixels outside
                    const QPointF c = body.center();
                    const qreal sx = body.width() / (body.width() + 2.0 * displacement);
                    const qreal sy = body.height() / (body.height() + 2.0 * displacement);
                    const QRectF source = backdrop.rect;
                    const QRectF target(c.x() + (source.left() - c.x()) * sx, c.y() + (source.top() - c.y()) * sy,
                                        source.width() * sx, source.height() * sy);
                    lp.drawImage(target, backdrop.lens);

                    lp.resetTransform();
                    lp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
                    lp.drawImage(0, 0, rimMask(pixelSize, r, band, dpr));
                }

                painter->drawImage(body, lens);
            }
        }
    }

    // 3. luminosity: one flat, even tint; the light material brightens, the dark material darkens the content
    QColor tint;

    if (onDark)
    {
        // a dark grey rather than black: over black content the glass still reads as a surface
        tint = withAlpha(hasBackdrop ? QColor(46, 46, 52) : QColor(30, 30, 34), hasBackdrop ? 0.36 : 0.66);
    }
    else
    {
        tint = withAlpha(hasBackdrop ? QColor(255, 255, 255) : QColor(250, 250, 252), hasBackdrop ? 0.34 : 0.68);
    }

    painter->fillPath(path, tint);

    painter->restore();

    // 4. edge: one thin, even, light hairline (Liquid Glass is flat: no bevel, no specular)
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setBrush(Qt::NoBrush);
    painter->setPen(QPen(QColor(255, 255, 255, onDark ? 56 : 210), 1.0));
    painter->drawPath(roundedPath(body.adjusted(0.5, 0.5, -0.5, -0.5), qMax(0.0, r - 0.5)));
    painter->restore();
}


QImage UBGlassStyle::shadowImage(const QSize& outerSize, const QRectF& body, qreal radius, qreal dpr, bool onDark)
{
    QImage image(outerSize * dpr, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    if (body.isEmpty())
        return image;

    const qreal margin = qMin(qMin(body.left(), body.top()),
                              qMin(outerSize.width() - body.right(), outerSize.height() - body.bottom()));

    if (margin >= 4)
    {
        // soft depth shadow below the glass, slightly offset downwards
        // the shadow must fade out inside the margin of the widget
        const qreal offset = qMin<qreal>(4.0, margin * 0.25);
        const qreal blurRadius = qMin<qreal>(18.0, (margin - offset) * 0.75);

        QPainter p(&image);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(dpr, dpr);
        p.fillPath(roundedPath(body.translated(0, offset).adjusted(2, 2, -2, -1), radius),
                   QColor(0, 0, 0, onDark ? 120 : 70));
        p.end();

        blur(image, qMax(1, qRound(blurRadius * dpr)));
    }

    {
        QPainter p(&image);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(dpr, dpr);

        // crisp contact shadow
        p.fillPath(roundedPath(body.adjusted(-0.5, 0.0, 0.5, 1.0), radius + 0.5), QColor(0, 0, 0, onDark ? 60 : 20));

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


QImage UBGlassStyle::blurredBackdrop(const QImage& source, int blurRadius, int factor, qreal saturation)
{
    if (source.isNull())
        return QImage();

    factor = qMax(1, factor);

    // blur at a reduced resolution, the smooth upscaling when painting softens further
    const QSize smallSize(qMax(1, source.width() / factor), qMax(1, source.height() / factor));
    QImage small = source.scaled(smallSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
            .convertToFormat(QImage::Format_ARGB32_Premultiplied);
    small.setDevicePixelRatio(1.0);

    blur(small, qMax(1, qRound(2.0 * blurRadius / factor)));

    if (!qFuzzyCompare(saturation, 1.0))
    {
        // boost the colours a little, like the glass material does
        for (int y = 0; y < small.height(); ++y)
        {
            QRgb* row = reinterpret_cast<QRgb*>(small.scanLine(y));

            for (int x = 0; x < small.width(); ++x)
            {
                const QRgb p = row[x];
                const int a = qAlpha(p);

                if (a == 0)
                    continue;

                const qreal grey = 0.2126 * qRed(p) + 0.7152 * qGreen(p) + 0.0722 * qBlue(p);
                auto boost = [&](int c) {
                    return qBound(0, qRound(grey + (c - grey) * saturation), a);
                };

                row[x] = qRgba(boost(qRed(p)), boost(qGreen(p)), boost(qBlue(p)), a);
            }
        }
    }

    return small;
}


qreal UBGlassStyle::meanLuminance(const QImage& source, qreal* coverage)
{
    if (source.isNull())
    {
        if (coverage)
            *coverage = 0.0;
        return 0.5;
    }

    const QImage image = source.format() == QImage::Format_ARGB32_Premultiplied
            ? source : source.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    double lum = 0.0;
    double alpha = 0.0;
    const int step = qMax(1, qMin(image.width(), image.height()) / 24);

    for (int y = 0; y < image.height(); y += step)
    {
        const QRgb* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));

        for (int x = 0; x < image.width(); x += step)
        {
            const QRgb p = row[x];
            // premultiplied: the colour channels already include the alpha
            lum += (0.2126 * qRed(p) + 0.7152 * qGreen(p) + 0.0722 * qBlue(p)) / 255.0;
            alpha += qAlpha(p) / 255.0;
        }
    }

    const int samples = ((image.height() + step - 1) / step) * ((image.width() + step - 1) / step);

    if (coverage)
        *coverage = samples > 0 ? alpha / samples : 0.0;

    return alpha > 0.0 ? lum / alpha : 0.5;
}


QIcon UBGlassStyle::icon(const QString& name)
{
    return tintedSvgIcon(QString(":/images/glass/%1.svg").arg(name));
}


QIcon UBGlassStyle::tintedSvgIcon(const QString& path)
{
    return QIcon(new UBTintedSvgIconEngine(path));
}


QIcon UBGlassStyle::tintedImageIcon(const QImage& mask)
{
    return QIcon(new UBTintedImageIconEngine(mask));
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

        // flat filled circle; a neutral hairline keeps white and black swatches visible on any glass
        painter.setBrush(color);
        painter.setPen(QPen(QColor(128, 128, 128, 110), border));
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
        {mainWindow->actionPen, "pen"},
        {mainWindow->actionEraser, "eraser"},
        {mainWindow->actionMarker, "marker"},
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
        {mainWindow->actionCaptureWebContent, "webCapture"},
        {mainWindow->actionWebCustomCapture, "captureArea"},
        {mainWindow->actionWebWindowCapture, "window"},
        {mainWindow->actionWebShowHideOnDisplay, "show"},
        // "add to" palette after a capture (web and desktop mode)
        {mainWindow->actionAddItemToCurrentPage, "add"},
        {mainWindow->actionAddItemToNewPage, "newDocument"},
        {mainWindow->actionAddItemToLibrary, "library"},
        {mainWindow->actionEraseDesktopAnnotations, "erase"},
        // document mode
        {mainWindow->actionNewDocument, "newDocument"},
        {mainWindow->actionNewFolder, "newFolder"},
        {mainWindow->actionImport, "import"},
        {mainWindow->actionExport, "export"},
        {mainWindow->actionRename, "rename"},
        {mainWindow->actionAddDocumentToFavorites, "favorite"},
        {mainWindow->actionDuplicate, "duplicate"},
        {mainWindow->actionDelete, "erase"},
        {mainWindow->actionOpen, "open"},
        {mainWindow->actionAddToWorkingDocument, "addToDocument"},
        {mainWindow->actionDocumentAdd, "add"},
        // OpenBoard menu
        {mainWindow->actionCut, "cut"},
        {mainWindow->actionCopy, "copy"},
        {mainWindow->actionPaste, "paste"},
        {mainWindow->actionHideApplication, "hide"},
        {mainWindow->actionSleep, "sleep"},
        {mainWindow->actionOpenTutorial, "help"},
        {mainWindow->actionHintsAndTips, "hints"},
        {mainWindow->actionPreferences, "preferences"},
        {mainWindow->actionMultiScreen, "multiScreen"},
        {mainWindow->actionToggleFullScreen, "fullScreen"},
        {mainWindow->actionPodcast, "record"},
        {mainWindow->actionQuit, "quit"},
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

    // the raw style sheets are not passed through a macro (QStringLiteral): the traditional
    // preprocessor of MSVC breaks on raw string literals containing quotes in macro arguments
    QString css = QString::fromUtf8(R"CSS(
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

/* glass palettes paint their own background */
#DesktopPropertyPalette,
#AddItemPalette,
#BackgroundPalette,
#MathPalette
{
    background: transparent;
    border: none;
}

UBToolbarButtonGroup QToolButton#desktop-ubButtonGroupLeft,
UBToolbarButtonGroup QToolButton#desktop-ubButtonGroupCenter,
UBToolbarButtonGroup QToolButton#desktop-ubButtonGroupRight
{
    qproperty-iconSize: 22px 22px;
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

    // glass panels: glyphs, hover pills and fields follow the material of each panel
    static const QString glassTemplate = QString::fromUtf8(R"CSS(
/* ---------- floating glass palettes (per material, see UBGlassStyle::setPanelMaterial) ---------- */

@P@ UBActionPaletteButton,
@P@ QToolButton#glassButton,
@P@ QToolButton#clusterButton
{
    color: @FG@;
    background: transparent;
    border: none;
    border-radius: 20px;
    padding: 4px;
    margin: 0px;
    font-weight: 600;
}

@P@ UBActionPaletteButton
{
    border-radius: 12px;
}

@S@ UBActionPaletteButton
{
    border-radius: 21px;
}

@P@ UBActionPaletteButton#glassToggle,
@P@ UBActionPaletteButton#glassToggle:checked,
@P@ UBActionPaletteButton#glassToggle:hover
{
    background: transparent;
}

@P@ QToolButton#clusterButton
{
    min-width: 40px;
    min-height: 40px;
    border-radius: 25px;
}

@P@[glassLabels="true"] QToolButton#clusterButton
{
    border-radius: 14px;
    padding: 4px 4px 2px 4px;
    min-width: 54px;
    font-size: 11px;
    font-weight: 500;
}

@P@ QToolButton#clusterButton::menu-indicator
{
    image: none;
    width: 0px;
}

@P@ UBActionPaletteButton:hover,
@P@ QToolButton#glassButton:hover,
@P@ QToolButton#clusterButton:hover
{
    background: @HOVER@;
}

@P@ UBActionPaletteButton:pressed,
@P@ QToolButton#glassButton:pressed,
@P@ QToolButton#clusterButton:pressed
{
    background: @PRESSED@;
}

@P@ UBActionPaletteButton:checked,
@P@ QToolButton#glassButton:checked,
@P@ QToolButton#clusterButton:checked
{
    background: @ACCENT@;
    color: #ffffff;
}

@P@ UBActionPaletteButton:checked:pressed,
@P@ QToolButton#glassButton:checked:pressed,
@P@ QToolButton#clusterButton:checked:pressed
{
    background: @ACCENTPRESSED@;
}

@P@ UBActionPaletteButton:disabled,
@P@ QToolButton#glassButton:disabled,
@P@ QToolButton#clusterButton:disabled
{
    color: @FGDISABLED@;
    background: transparent;
}

@P@ QFrame#clusterSeparator
{
    background: @HAIR@;
    border: none;
    min-width: 1px;
    max-width: 1px;
    margin-top: 10px;
    margin-bottom: 10px;
}

/* segmented capsule (maths palette tabs) */
@P@ QFrame#glassSegmented
{
    background: @SEGMENT@;
    border: none;
    border-radius: 16px;
}

@P@ QToolButton#glassSegment
{
    color: @FG@;
    background: transparent;
    border: none;
    border-radius: 13px;
    padding: 4px 12px;
    margin: 0px;
    min-height: 18px;
    font-weight: 600;
}

@P@ QToolButton#glassSegment:hover
{
    background: @HOVER@;
}

@P@ QToolButton#glassSegment:checked
{
    background: @SEGMENTON@;
    color: @FG@;
}

@P@ QStackedWidget,
@P@ QStackedWidget > QWidget,
@P@ QWidget#shapeToolWidget,
@P@ QWidget#penColorSection,
@P@ QSlider,
@P@ QAbstractScrollArea,
@P@ QAbstractScrollArea > QWidget#qt_scrollarea_viewport,
@P@ QAbstractScrollArea > QWidget#qt_scrollarea_viewport > QWidget,
@P@ #BackgroundPaletteScrollArea,
@P@ #BackgroundPaletteScrollArea QWidget
{
    background: transparent;
    border: none;
}

@P@ QSlider::groove:horizontal
{
    height: 4px;
    margin: 0px;
    border: none;
    border-radius: 2px;
    background: @TRACK@;
}

@P@ QSlider::sub-page:horizontal
{
    border: none;
    border-radius: 2px;
    background: @ACCENT@;
}

@P@ QSlider::add-page:horizontal
{
    border: none;
    border-radius: 2px;
    background: @TRACK@;
}

@P@ QSlider::handle:horizontal
{
    width: 18px;
    height: 18px;
    margin: -7px 0px;
    border-radius: 9px;
    border: 1px solid rgba(0, 0, 0, 40);
    background: #ffffff;
}

@P@ QLineEdit
{
    color: @FG@;
    background: @FIELD@;
    border: 1px solid @HAIR@;
    border-radius: 15px;
    padding: 4px 10px;
    min-height: 20px;
    selection-background-color: @ACCENT@;
    selection-color: #ffffff;
}

@P@ QLineEdit:focus
{
    border: 1px solid @ACCENT@;
}

@P@ QLabel,
@P@ #BackgroundPalette QLabel,
@P@ #BackgroundPaletteLabel,
@P@ #UBStartupHintsPalette QCheckBox
{
    color: @FG@;
    background: transparent;
}

@P@ QLabel#glassHint
{
    color: @FGSECONDARY@;
    font-size: 11px;
}

@P@ QCheckBox
{
    color: @FG@;
    background: transparent;
}

@P@ QToolButton#closeButton,
@P@#BackgroundPalette QToolButton#closeButton,
@P@#AddItemPalette QToolButton#closeButton,
@P@#UBStartupHintsPalette QToolButton#closeButton
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

@P@ QToolButton#closeButton:hover,
@P@#BackgroundPalette QToolButton#closeButton:hover,
@P@#AddItemPalette QToolButton#closeButton:hover,
@P@#UBStartupHintsPalette QToolButton#closeButton:hover
{
    background: @PRESSED@;
}

/* pen panel and desktop property palettes */

@P@ UBToolbarButtonGroup QToolButton,
@P@ QToolButton#glass-shapeFill
{
    color: @FG@;
    background: transparent;
    border: none;
    border-radius: 18px;
    padding: 0px;
    margin: 0px;
    min-width: 36px;
    max-width: 36px;
    min-height: 36px;
    max-height: 36px;
}

@P@ UBToolbarButtonGroup QToolButton:hover,
@P@ QToolButton#glass-shapeFill:hover
{
    background: @HOVER@;
}

@P@ UBToolbarButtonGroup QToolButton:pressed,
@P@ QToolButton#glass-shapeFill:pressed
{
    background: @PRESSED@;
}

@P@ UBToolbarButtonGroup QToolButton:checked,
@P@ QToolButton#glass-shapeFill:checked
{
    background: @ACCENT@;
    color: #ffffff;
}

@P@ UBToolbarButtonGroup QToolButton[colorSwatch="true"]
{
    border: 2px solid transparent;
    border-radius: 18px;
}

@P@ UBToolbarButtonGroup QToolButton[colorSwatch="true"]:checked
{
    background: transparent;
    border: 2px solid @RING@;
}

@P@ UBToolbarButtonGroup QToolButton[colorSwatch="true"]:checked:hover
{
    background: @HOVER@;
}

@P@ UBToolbarButtonGroup QToolButton[colorPaletteConfigButton="true"]
{
    background: transparent;
    border: none;
}

@P@ UBToolbarButtonGroup QToolButton[colorPaletteConfigButton="true"]:hover
{
    background: @HOVER@;
}

@P@ QToolButton#paletteSwitchButton
{
    color: @FG@;
    background: transparent;
    border: none;
    border-radius: 10px;
    padding: 0px;
    margin: 0px;
    min-width: 20px;
    max-width: 20px;
    min-height: 36px;
    max-height: 36px;
}

@P@ QToolButton#paletteSwitchButton:hover
{
    background: @HOVER@;
}

@P@ QToolButton#paletteSwitchButton:pressed
{
    background: @PRESSED@;
}

@P@ QFrame#glassDivider
{
    background: @HAIR@;
    border: none;
    min-width: 1px;
    max-width: 1px;
    margin-top: 9px;
    margin-bottom: 9px;
}

@P@ QPushButton#zoomPaletteButton
{
    color: @FG@;
    background: transparent;
    border: none;
    border-radius: 10px;
    padding: 2px 6px;
    font-size: 18px;
    font-weight: 600;
}

@P@ QPushButton#zoomPaletteButton:hover
{
    background: @HOVER@;
}

)CSS");

    for (const bool onDark : {false, true})
    {
        const QColor gfg = foreground(onDark);
        QString block = glassTemplate;
        block.replace("@S@", QString("UBStylusPalette[glassOnDark=\"%1\"]").arg(onDark ? "true" : "false"));
        block.replace("@P@", QString("UBFloatingPalette[glassOnDark=\"%1\"]").arg(onDark ? "true" : "false"));
        block.replace("@FGDISABLED@", rgba(withAlpha(gfg, 0.30)));
        block.replace("@FGSECONDARY@", rgba(withAlpha(gfg, 0.62)));
        block.replace("@FG@", rgba(gfg));
        block.replace("@HOVER@", rgba(onDark ? QColor(255, 255, 255, 36) : QColor(0, 0, 0, 20)));
        block.replace("@PRESSED@", rgba(onDark ? QColor(255, 255, 255, 62) : QColor(0, 0, 0, 40)));
        block.replace("@HAIR@", rgba(onDark ? QColor(255, 255, 255, 46) : QColor(0, 0, 0, 34)));
        block.replace("@TRACK@", rgba(onDark ? QColor(255, 255, 255, 60) : QColor(0, 0, 0, 40)));
        block.replace("@FIELD@", rgba(onDark ? QColor(0, 0, 0, 56) : QColor(255, 255, 255, 140)));
        block.replace("@SEGMENTON@", rgba(onDark ? QColor(255, 255, 255, 64) : QColor(255, 255, 255, 235)));
        block.replace("@SEGMENT@", rgba(onDark ? QColor(0, 0, 0, 50) : QColor(0, 0, 0, 22)));
        block.replace("@RING@", rgba(onDark ? QColor(255, 255, 255) : accentColor));
        block.replace("@ACCENTPRESSED@", rgba(accentPressed));
        block.replace("@ACCENT@", rgba(accentColor));
        css += block;
    }

    css += documentModeStyleSheet(dark);

    return css;
}


/* ---------- document mode (documents explorer) ---------- */

UBGlassStyle::DocumentModeColors UBGlassStyle::documentModeColors(bool dark)
{
    DocumentModeColors c;

    if (UBUiStyle::isModern())
    {
        // Fluent: window neutrals, card surface, 4 px controls, tinted selection with an indicator
        c.content = dark ? QColor(0x20, 0x20, 0x20) : QColor(0xf3, 0xf3, 0xf3);
        c.sidebar = UBModernStyle::surface(dark);
        c.sidebarBorder = UBModernStyle::stroke(dark);
        c.foreground = UBModernStyle::foreground(dark);
        c.accent = UBModernStyle::accent(dark);
        c.onAccent = dark ? QColor(0, 0, 0) : QColor(255, 255, 255);
        c.sidebarRadius = UBModernStyle::panelRadius;
        c.controlRadius = UBModernStyle::controlRadius;
        c.rowRadius = UBModernStyle::controlRadius;
        c.accentPill = false;
    }
    else
    {
        // macOS: graphite instead of grey slabs in the dark, a white sidebar card in the light
        c.content = dark ? QColor(22, 22, 24) : QColor(240, 240, 243);
        c.sidebar = dark ? QColor(36, 36, 39) : QColor(255, 255, 255);
        c.sidebarBorder = dark ? QColor(255, 255, 255, 22) : QColor(0, 0, 0, 20);
        c.foreground = dark ? QColor(242, 242, 247) : QColor(28, 28, 30);
        c.accent = accent();
        c.onAccent = QColor(255, 255, 255);
    }

    c.secondary = withAlpha(c.foreground, dark ? 0.55 : 0.52);
    c.hover = dark ? QColor(255, 255, 255, 14) : QColor(0, 0, 0, 10);
    c.selectionInactive = dark ? QColor(255, 255, 255, 28) : QColor(0, 0, 0, 18);
    c.control = dark ? QColor(118, 118, 128, 56) : QColor(118, 118, 128, 26);
    c.controlHover = dark ? QColor(118, 118, 128, 80) : QColor(118, 118, 128, 42);
    c.track = dark ? QColor(255, 255, 255, 46) : QColor(0, 0, 0, 30);
    c.cardBorder = dark ? QColor(255, 255, 255, 36) : QColor(0, 0, 0, 30);
    c.cardShadow = dark ? QColor(0, 0, 0, 120) : QColor(0, 0, 0, 30);

    return c;
}


QString UBGlassStyle::documentModeStyleSheet(bool dark)
{
    const DocumentModeColors c = documentModeColors(dark);

    QString css = QString::fromUtf8(R"CSS(
/* ---------- document mode (UBGlassStyle::documentModeStyleSheet) ---------- */

QToolBar#documentToolBar
{
    background: @CONTENT@;
    border: none;
}

QToolBar#documentToolBar QWidget#ubTrafficLightsSpacer
{
    background: transparent;
}

QWidget#documents,
QWidget#documents QWidget#topWidget,
QWidget#documents QFrame#topRightFrame,
QWidget#documents QWidget#bottomRightFrame
{
    background: @CONTENT@;
    border: none;
}

QWidget#documents QSplitter#splitter,
QWidget#documents QSplitter#splitter::handle
{
    background: transparent;
    border: none;
}

QWidget#documents QWidget#topLeftWidget
{
    background: @SIDEBAR@;
    border: 1px solid @SIDEBARBORDER@;
    border-radius: @SIDEBARRADIUS@px;
}

QWidget#documents UBDocumentTreeView
{
    background: transparent;
    color: @FG@;
    border: none;
    outline: none;
    show-decoration-selected: 0;
    selection-background-color: transparent;
    selection-color: @FG@;
}

QWidget#documents UBDocumentTreeView::item,
QWidget#documents UBDocumentTreeView::item:hover,
QWidget#documents UBDocumentTreeView::item:selected,
QWidget#documents UBDocumentTreeView::item:selected:active,
QWidget#documents UBDocumentTreeView::item:selected:!active,
QWidget#documents UBDocumentTreeView::branch,
QWidget#documents UBDocumentTreeView::branch:selected
{
    background: transparent;
    border: none;
}

QWidget#documents UBDocumentTreeView QLineEdit
{
    color: @FG@;
    background: @SIDEBAR@;
    border: 1px solid @ACCENT@;
    border-radius: 5px;
    padding: 0px 4px;
    margin: 0px;
    selection-background-color: @ACCENT@;
    selection-color: @ONACCENT@;
}

QWidget#documents UBDocumentTreeView QLineEdit[invalid="true"]
{
    border: 1px solid @ERROR@;
    background: @ERRORFILL@;
}

QWidget#documents UBDocumentTreeView QScrollBar:vertical,
QWidget#documents UBDocumentThumbnailWidget QScrollBar:vertical
{
    background: transparent;
    border: none;
    width: 10px;
    margin: 4px 2px 4px 0px;
}

QWidget#documents UBDocumentTreeView QScrollBar::handle:vertical,
QWidget#documents UBDocumentThumbnailWidget QScrollBar::handle:vertical
{
    background: @SCROLL@;
    border: none;
    border-radius: 4px;
    min-height: 32px;
}

QWidget#documents UBDocumentTreeView QScrollBar::add-line,
QWidget#documents UBDocumentTreeView QScrollBar::sub-line,
QWidget#documents UBDocumentThumbnailWidget QScrollBar::add-line,
QWidget#documents UBDocumentThumbnailWidget QScrollBar::sub-line
{
    height: 0px;
    width: 0px;
    border: none;
    background: transparent;
}

QWidget#documents UBDocumentTreeView QScrollBar::add-page,
QWidget#documents UBDocumentTreeView QScrollBar::sub-page,
QWidget#documents UBDocumentThumbnailWidget QScrollBar::add-page,
QWidget#documents UBDocumentThumbnailWidget QScrollBar::sub-page
{
    background: transparent;
}

QWidget#documents QComboBox#sortKind
{
    color: @FG@;
    background: @CONTROL@;
    border: none;
    border-radius: @CONTROLRADIUS@px;
    padding: 4px 6px 4px 10px;
    min-height: 20px;
}

QWidget#documents QComboBox#sortKind:hover,
QWidget#documents QComboBox#sortKind:on
{
    background: @CONTROLHOVER@;
}

QWidget#documents QComboBox#sortKind::drop-down
{
    subcontrol-origin: padding;
    subcontrol-position: center right;
    width: 18px;
    border: none;
    background: transparent;
}

QWidget#documents QComboBox#sortKind::down-arrow
{
    image: url(@ARROW@);
    width: 10px;
    height: 10px;
}

QWidget#documents QComboBox#sortKind QAbstractItemView
{
    color: @FG@;
    background: @SIDEBAR@;
    border: 1px solid @SIDEBARBORDER@;
    padding: 4px;
    outline: none;
    selection-background-color: @ACCENT@;
    selection-color: @ONACCENT@;
}

QWidget#documents QToolButton#sortOrder
{
    background: transparent;
    border: none;
    border-radius: @CONTROLRADIUS@px;
    padding: 3px;
    margin: 0px;
    min-width: 22px;
    min-height: 22px;
}

QWidget#documents QToolButton#sortOrder:hover,
QWidget#documents QToolButton#sortOrder:checked:hover
{
    background: @CONTROLHOVER@;
}

QWidget#documents QToolButton#sortOrder:checked
{
    background: transparent;
}

QWidget#documents QLineEdit#filterText
{
    color: @FG@;
    background: @CONTROL@;
    border: 1px solid transparent;
    border-radius: @CONTROLRADIUS@px;
    padding: 3px 6px 3px 2px;
    min-height: 20px;
    selection-background-color: @ACCENT@;
    selection-color: @ONACCENT@;
}

QWidget#documents QLineEdit#filterText:hover
{
    background: @CONTROLHOVER@;
}

QWidget#documents QLineEdit#filterText:focus
{
    background: @SIDEBAR@;
    border: 1px solid @ACCENT@;
}

QWidget#documents QSlider#documentZoomSlider
{
    background: transparent;
}

QWidget#documents QSlider#documentZoomSlider::groove:horizontal
{
    height: 4px;
    margin: 0px;
    border: none;
    border-radius: 2px;
    background: @TRACK@;
}

QWidget#documents QSlider#documentZoomSlider::sub-page:horizontal
{
    border: none;
    border-radius: 2px;
    background: @ACCENT@;
}

QWidget#documents QSlider#documentZoomSlider::add-page:horizontal
{
    border: none;
    border-radius: 2px;
    background: @TRACK@;
}

QWidget#documents QSlider#documentZoomSlider::handle:horizontal
{
    width: 16px;
    height: 16px;
    margin: -6px 0px;
    border-radius: 8px;
    border: 1px solid @HANDLEBORDER@;
    background: @HANDLE@;
}

QWidget#documents QToolButton#documentZoomOut,
QWidget#documents QToolButton#documentZoomIn
{
    background: transparent;
    border: none;
    border-radius: @CONTROLRADIUS@px;
    padding: 3px;
    margin: 0px;
}

QWidget#documents QToolButton#documentZoomOut:hover,
QWidget#documents QToolButton#documentZoomIn:hover
{
    background: @CONTROLHOVER@;
}
)CSS");

    css.replace("@CONTENT@", rgba(c.content));
    css.replace("@SIDEBARBORDER@", rgba(c.sidebarBorder));
    css.replace("@SIDEBARRADIUS@", QString::number(c.sidebarRadius));
    css.replace("@SIDEBAR@", rgba(c.sidebar));
    css.replace("@FG@", rgba(c.foreground));
    css.replace("@ONACCENT@", rgba(c.onAccent));
    css.replace("@ACCENT@", rgba(c.accent));
    // opaque: the field lies on the selection pill
    const QColor error = dark ? QColor(255, 69, 58) : QColor(255, 59, 48);
    const qreal errorShare = dark ? 0.22 : 0.10;
    const QColor errorFill(qRound(c.sidebar.red() * (1 - errorShare) + error.red() * errorShare),
                           qRound(c.sidebar.green() * (1 - errorShare) + error.green() * errorShare),
                           qRound(c.sidebar.blue() * (1 - errorShare) + error.blue() * errorShare));
    css.replace("@ERRORFILL@", rgba(errorFill));
    css.replace("@ERROR@", rgba(dark ? QColor(255, 69, 58) : QColor(255, 59, 48)));
    css.replace("@SCROLL@", rgba(dark ? QColor(255, 255, 255, 70) : QColor(0, 0, 0, 60)));
    css.replace("@CONTROLHOVER@", rgba(c.controlHover));
    css.replace("@CONTROLRADIUS@", QString::number(c.controlRadius));
    css.replace("@CONTROL@", rgba(c.control));
    css.replace("@ARROW@", dark ? QStringLiteral(":/images/glass/comboArrowOnDark.svg")
                                : QStringLiteral(":/images/glass/comboArrowOnLight.svg"));
    css.replace("@TRACK@", rgba(c.track));
    css.replace("@HANDLEBORDER@", rgba(dark ? QColor(0, 0, 0, 90) : QColor(0, 0, 0, 40)));
    css.replace("@HANDLE@", rgba(dark ? QColor(220, 220, 224) : QColor(255, 255, 255)));

    return css;
}


QPixmap UBGlassStyle::glyphPixmap(const QString& name, const QColor& color, int size, qreal dpr)
{
    const QSize pixelSize(qRound(size * dpr), qRound(size * dpr));
    const QString key = QString("ubglyph:%1:%2:%3").arg(name).arg(pixelSize.width()).arg(color.rgba(), 0, 16);

    QPixmap cached;
    if (QPixmapCache::find(key, &cached))
        return cached;

    QImage image(pixelSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QSvgRenderer renderer(QString(":/images/glass/%1.svg").arg(name));

    if (renderer.isValid())
    {
        QPainter p(&image);
        p.setRenderHint(QPainter::Antialiasing);
        renderer.render(&p, QRectF(QPointF(0, 0), QSizeF(pixelSize)));
        p.setCompositionMode(QPainter::CompositionMode_SourceIn);
        p.fillRect(image.rect(), color);
    }

    cached = QPixmap::fromImage(image);
    cached.setDevicePixelRatio(dpr);
    QPixmapCache::insert(key, cached);
    return cached;
}
