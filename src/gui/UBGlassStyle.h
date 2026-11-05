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

#ifndef UBGLASSSTYLE_H_
#define UBGLASSSTYLE_H_

#include <QColor>
#include <QIcon>
#include <QImage>
#include <QRectF>
#include <QString>

class QPainter;
class QPainterPath;
class UBMainWindow;

/**
 * Shared look of the "Liquid Glass" user interface: colours, glass panel
 * painting (tint, refraction highlight, shadow), a fast blur for the
 * backdrop of floating panels, theme-tinted monochrome SVG icons and the
 * application stylesheet layer for the glass controls.
 */
class UBGlassStyle
{
    public:
        static bool isDark();

        /** accent colour used for checked controls (system highlight) */
        static QColor accent();

        /** colour of monochrome icons and text on glass */
        static QColor foreground();

        /** standard metrics */
        static constexpr int panelRadius = 20;
        static constexpr int buttonSize = 38;
        static constexpr int spacing = 8;

        /**
         * Paint a glass panel body. backdrop is the blurred content behind
         * the body (may be null, then a more opaque tint is used).
         */
        static void paintGlass(QPainter* painter, const QRectF& body, qreal radius, const QImage& backdrop);

        /** soft drop shadow image for a body of the given size placed with margin in an image of outer size */
        static QImage shadowImage(const QSize& outerSize, const QRectF& body, qreal radius, qreal dpr);

        /** blur an image in place (stack blur approximation, three box passes) */
        static void blur(QImage& image, int radius);

        /** reduced resolution blurred copy of an image (used for the backdrop) */
        static QImage blurredBackdrop(const QImage& source, int blurRadius);

        /** stylesheet layer for the glass controls */
        static QString styleSheet(bool dark);

        /** monochrome SVG icon from :/images/glass/<name>.svg, tinted with the theme colours */
        static QIcon icon(const QString& name);

        /** tinted icon from any monochrome SVG resource path */
        static QIcon tintedSvgIcon(const QString& path);

        /** round colour swatch icon */
        static QIcon swatchIcon(const QColor& color);

        /** replace the dated toolbar and tool icons by the glass icon set */
        static void applyIcons(UBMainWindow* mainWindow);

        static QPainterPath roundedPath(const QRectF& rect, qreal radius);
};

#endif /* UBGLASSSTYLE_H_ */
