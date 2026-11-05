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
 * Hook for native "glass" materials.
 *
 * On macOS (UBGlassEffect_mac.mm) the main window gets a transparent title bar,
 * a full size content view and a vibrant NSVisualEffectView behind the top
 * toolbar; on macOS 26 and later floating panels get an NSGlassEffectView.
 * On all other platforms these functions do nothing: the Qt painted glass
 * (UBFloatingPalette / UBGlassStyle) is always drawn and is the complete look.
 */
class UBGlassEffect
{
    public:
        /** apply the native window chrome (title bar, vibrancy behind the toolbar) */
        static void applyToWindow(QWidget* window);

        /** apply a native glass material behind a floating panel, when available */
        static void applyToPanel(QWidget* panel);
};

#endif /* UBGLASSEFFECT_H_ */
