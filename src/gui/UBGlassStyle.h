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
class QPixmap;
class QPainterPath;
class QToolBar;
class QWidget;
class UBMainWindow;

/**
 * Images of the content behind a glass panel, in the coordinates of the
 * panel (rect). blurred is the heavily blurred and slightly saturated
 * backdrop (low resolution), lens a lightly blurred copy used for the
 * refraction along the rim. luminance is the mean luminance (0..1) of the
 * backdrop under the body, coverage the share of opaque content (0..1).
 */
struct UBGlassBackdrop
{
    QImage blurred;
    QImage lens;
    QRectF rect;
    qreal luminance = 0.5;
    qreal coverage = 0.0;

    bool isValid() const { return !blurred.isNull() && !rect.isEmpty(); }
};

/**
 * Shared look of the "Liquid Glass" user interface: colours, painted glass
 * (blurred and saturated backdrop, adaptive light / dark material, lensing
 * rim, specular highlight, contours and depth shadow), a fast blur, tinted
 * monochrome SVG icons and the application stylesheet layer.
 *
 * Glass adapts to the content below it: over light content the light
 * material (white tint, dark glyphs) is used, over dark content the dark
 * material (black tint, white glyphs), independently of the application
 * theme. A glass panel publishes the material it uses in the dynamic
 * property "glassOnDark" (true: dark material, white glyphs); the stylesheet
 * and the icon engines follow that property for everything inside the panel.
 */
class UBGlassStyle
{
    public:
        static bool isDark();

        /** accent colour used for checked controls (system highlight) */
        static QColor accent();

        /** colour of monochrome icons and text on glass of the application theme */
        static QColor foreground();

        /** colour of monochrome icons and text on the given material */
        static QColor foreground(bool onDarkMaterial);

        /** standard metrics */
        static constexpr int panelRadius = 26;
        static constexpr int buttonSize = 40;
        static constexpr int iconSize = 22;
        static constexpr int spacing = 8;
        /** room around a panel body for the drop shadow */
        static constexpr int shadowMargin = 14;

        /**
         * Paint a glass panel body. When backdrop is valid the blurred content
         * is drawn with lensing along the rim, otherwise only a translucent tint
         * (the content behind the widget shows through). onDarkMaterial selects
         * the dark (true) or the light material.
         */
        static void paintGlass(QPainter* painter, const QRectF& body, qreal radius,
                               const UBGlassBackdrop& backdrop, bool onDarkMaterial);

        /** compatibility helper: glass without backdrop in the material of the theme */
        static void paintGlass(QPainter* painter, const QRectF& body, qreal radius);

        /** soft drop shadow image for a body placed in an image of outer size */
        static QImage shadowImage(const QSize& outerSize, const QRectF& body, qreal radius, qreal dpr, bool onDarkMaterial);

        /** blur an image in place (gaussian approximation, three box passes) */
        static void blur(QImage& image, int radius);

        /**
         * Blurred backdrop at reduced resolution: the source is scaled down by
         * factor, blurred with blurRadius (in source pixels) and its saturation
         * multiplied by saturation.
         */
        static QImage blurredBackdrop(const QImage& source, int blurRadius, int factor = 4, qreal saturation = 1.2);

        /** mean luminance (0..1) of an image weighted by alpha, coverage receives the mean alpha */
        static qreal meanLuminance(const QImage& image, qreal* coverage = nullptr);

        /** stylesheet layer for the glass controls */
        static QString styleSheet(bool dark);

        /** monochrome SVG icon from :/images/glass/<name>.svg, tinted with the colours of the glass below */
        static QIcon icon(const QString& name);

        /** tinted icon from any monochrome SVG resource path */
        static QIcon tintedSvgIcon(const QString& path);

        /** tinted icon from a monochrome image (only its alpha channel is used) */
        static QIcon tintedImageIcon(const QImage& mask);

        /** round colour swatch icon */
        static QIcon swatchIcon(const QColor& color);

        /** replace the dated toolbar and tool icons by the glass icon set */
        static void applyIcons(UBMainWindow* mainWindow);

        static QPainterPath roundedPath(const QRectF& rect, qreal radius);

        /** tell the stylesheet whether the buttons of a toolbar show their text under the icon */
        static void setToolBarTextMode(QToolBar* toolBar, bool textUnderIcon);

        /**
         * Publish the material of a glass panel (property "glassOnDark") and
         * re-polish its content when it changes.
         */
        static void setPanelMaterial(QWidget* panel, bool onDarkMaterial);

        /**
         * Install the application wide paint tracker that lets the tinted icons
         * follow the material of the glass panel they are painted on.
         */
        static void installMaterialTracker();

        /** material of the widget currently being painted (-1 when not on glass) */
        static int currentPaintMaterial();

        /**
         * Colours and metrics of document mode (documents explorer), for the glass
         * style (macOS) and the modern style (Windows, Linux): flat content area, a
         * floating sidebar card, flat rounded controls, page cards with a hairline.
         */
        struct DocumentModeColors
        {
            QColor content;           // window background behind the sidebar and the page grid
            QColor sidebar;           // sidebar card (document tree)
            QColor sidebarBorder;     // hairline around the sidebar card
            QColor foreground;        // text and glyphs
            QColor secondary;         // dates, page labels, placeholder text
            QColor accent;            // selection, folder glyphs, drop target
            QColor onAccent;          // text and glyphs on the accent
            QColor hover;             // hover fill of rows and buttons
            QColor selectionInactive; // selected rows while the tree has no focus
            QColor control;           // fill of the sort box and the search field
            QColor controlHover;
            QColor track;             // zoom slider track
            QColor cardBorder;        // hairline around the page thumbnails
            QColor cardShadow;        // soft shadow below the page thumbnails
            int sidebarRadius = 12;
            int controlRadius = 8;
            int rowRadius = 6;
            bool accentPill = true;   // selected row: solid accent pill (glass) or tinted fill and indicator (modern)
        };

        static DocumentModeColors documentModeColors(bool dark);

        /** stylesheet block of document mode, used by the glass and the modern layer */
        static QString documentModeStyleSheet(bool dark);

        /** pixmap of a glyph of :/images/glass/<name>.svg in one colour (cached) */
        static QPixmap glyphPixmap(const QString& name, const QColor& color, int size, qreal dpr);
};

#endif /* UBGLASSSTYLE_H_ */
