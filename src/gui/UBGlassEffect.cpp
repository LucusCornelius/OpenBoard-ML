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

#include "UBGlassEffect.h"

#include <QtGlobal>

#include "core/memcheck.h"

// Non macOS platforms: the glass look is completely painted by Qt.

void UBGlassEffect::applyToWindow(QWidget* window)
{
    Q_UNUSED(window);
}


void UBGlassEffect::applyToPanel(QWidget* panel)
{
    Q_UNUSED(panel);
}


void UBGlassEffect::applyToMenu(QWidget* menu)
{
    Q_UNUSED(menu);
}


int UBGlassEffect::topInset(QWidget* widget)
{
    Q_UNUSED(widget);
    return 0;
}
