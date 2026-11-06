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

#ifndef UBUISTYLE_H_
#define UBUISTYLE_H_

#include <QColor>

/**
 * The one place that decides which user interface style is used.
 *
 *  - Glass: the painted "Liquid Glass" look (UBGlassStyle, plus the native
 *    window chrome of UBGlassEffect on macOS). Default on macOS.
 *  - Modern: a calm, solid Windows 11 / Fluent like look (UBModernStyle):
 *    opaque surfaces following the application theme, 8 px corners, subtle
 *    strokes and shadows, no blur. Default on Windows and Linux.
 *
 * The environment variable OPENBOARD_UI_STYLE=glass|modern overrides the
 * platform default (to preview a style on another platform). The style is
 * decided once at start up and does not change while the application runs.
 */
class UBUiStyle
{
    public:
        enum Style
        {
            Glass,
            Modern
        };

        static Style style();
        static bool isGlass() { return style() == Glass; }
        static bool isModern() { return style() == Modern; }

        /**
         * Select the Qt widget style and the font of the application for the
         * user interface style. Call once, before the theme is applied.
         * Modern on Windows: Qt's "windows11" style when available (Qt 6.8+,
         * so that the colour scheme can follow the application theme), Fusion
         * otherwise. The environment variable OPENBOARD_QT_STYLE=<key> forces
         * a Qt style (e.g. fusion).
         */
        static void applyApplicationStyle();

        /**
         * Accent colour of the system, captured before the application palette
         * is replaced by the theme (invalid when the platform has none).
         */
        static QColor systemAccent();

        /**
         * Follow the application theme with the native window chrome: on
         * Windows a dark or light title bar (DWM immersive dark mode) and
         * rounded corners for all top level windows, also those shown later.
         * Does nothing on other platforms.
         */
        static void applyWindowChrome(bool dark);
};

#endif /* UBUISTYLE_H_ */
