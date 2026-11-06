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

#ifndef UBGLASSEFFECT_H_
#define UBGLASSEFFECT_H_

class QWidget;

/**
 * Hook for native "Liquid Glass" materials.
 *
 * On macOS (UBGlassEffect_mac.mm) the main window gets a transparent title bar
 * with a full size content view, so that the board extends under the title
 * bar. Floating panels get the native system material (NSGlassEffectView on
 * macOS 26 and later, NSVisualEffectView before), see the .mm file.
 * On all other platforms these functions do nothing: the Qt painted glass
 * (UBFloatingPalette / UBGlassStyle) is the complete look.
 */
class UBGlassEffect
{
    public:
        /** apply the native window chrome (transparent title bar, full size content) */
        static void applyToWindow(QWidget* window);

        /**
         * Apply a native glass material behind a floating panel, when available.
         * The panel then has the property "ubNativeGlass" and must paint only its
         * controls (no glass body); it may publish "ubGlassBody" (QRect) and
         * "ubGlassRadius" (qreal) for the shape of the material.
         */
        static void applyToPanel(QWidget* panel);

        /**
         * Height of the window title bar overlapping the top of the given
         * widget (full size content windows on macOS), 0 elsewhere. Controls
         * anchored at the top edge stay below it (traffic lights).
         */
        static int topInset(QWidget* widget);
};

#endif /* UBGLASSEFFECT_H_ */
