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

#ifndef UBMODERNSTYLE_H_
#define UBMODERNSTYLE_H_

#include <QColor>
#include <QImage>
#include <QRectF>
#include <QString>

class QPainter;
class QPalette;

/**
 * The "Modern" user interface style (see UBUiStyle): a calm Windows 11 /
 * Fluent like look used on Windows and Linux.
 *
 * Floating panels are solid cards (light #F9F9F9, dark #2C2C2C) with 8 px
 * corners, a 1 px subtle stroke and a soft elevation shadow, without blur,
 * lensing or highlights. The material follows the application theme, not
 * the content below. Buttons get subtle 4 px rounded hover and pressed
 * fills; a checked button shows an accent tinted fill with an accent glyph.
 *
 * The floating palettes keep publishing the dynamic property "glassOnDark"
 * (here always equal to the theme), so the tinted icons and the stylesheet
 * selectors work the same way as for the glass style.
 */
class UBModernStyle
{
    public:
        /** corner radius of panels (Fluent overlay radius) */
        static constexpr int panelRadius = 8;

        /** corner radius of controls (Fluent control radius) */
        static constexpr int controlRadius = 4;

        /** surface colour of a floating panel */
        static QColor surface(bool dark);

        /** 1 px stroke around a panel */
        static QColor stroke(bool dark);

        /** primary text and glyph colour */
        static QColor foreground(bool dark);

        /**
         * accent colour for the theme: the system accent, adjusted like the
         * Fluent accent fills (darker on light, lighter on dark)
         */
        static QColor accent(bool dark);

        /** colour of the glyph of a checked control (accent text) */
        static QColor checkedForeground(bool dark);

        /** paint a panel body: solid surface and stroke */
        static void paintPanel(QPainter* painter, const QRectF& body, qreal radius, bool dark);

        /** soft elevation shadow for a body placed in an image of outer size */
        static QImage shadowImage(const QSize& outerSize, const QRectF& body, qreal radius, qreal dpr, bool dark);

        /** adjust the application palette of the theme to the Fluent neutrals and accent */
        static void adjustPalette(QPalette& palette, bool dark);

        /**
         * Theme stylesheets (lightTheme.qss / darkTheme.qss) use blue tinted greys and a
         * fixed blue: map the slate greys to neutral greys of the same luminance and the
         * blues to the accent colour, so that dialogs match the Fluent neutrals.
         */
        static QString neutralizeThemeStyleSheet(const QString& css);

        /** stylesheet layer for the controls (replaces UBGlassStyle::styleSheet) */
        static QString styleSheet(bool dark);
};

#endif /* UBMODERNSTYLE_H_ */
