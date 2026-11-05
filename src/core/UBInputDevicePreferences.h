/*
 * Copyright (C) 2026 OpenBoard contributors
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

#ifndef UBINPUTDEVICEPREFERENCES_H
#define UBINPUTDEVICEPREFERENCES_H

#include <QObject>

namespace Ui
{
    class preferencesDialog;
}

class QWidget;

/**
 * @brief Input device part of the Shortcut tab of the preferences dialog:
 * right/middle click behavior, device profiles and "Reset all shortcuts".
 */
class UBInputDevicePreferences : public QObject
{
    Q_OBJECT

public:
    UBInputDevicePreferences(Ui::preferencesDialog* ui, QWidget* dialog);

public slots:
    void refresh();

private slots:
    void applyProfile();
    void resetAllShortcuts();

private:
    void fillClickCombo(int button);
    void selectClickAction(int button);
    void clearSelection();

    Ui::preferencesDialog* mUi;
    QWidget* mDialog;
};

#endif // UBINPUTDEVICEPREFERENCES_H
