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

#include "UBInputDevicePreferences.h"

#include <QMessageBox>

#include "core/UBClickBehavior.h"
#include "core/UBShortcutManager.h"
#include "core/UBShortcutProfile.h"

#include "ui_preferences.h"

UBInputDevicePreferences::UBInputDevicePreferences(Ui::preferencesDialog* ui, QWidget* dialog)
    : QObject(dialog)
    , mUi(ui)
    , mDialog(dialog)
{
    fillClickCombo(Qt::RightButton);
    fillClickCombo(Qt::MiddleButton);

    const QString tip = tr("Applies to the board and to the desktop annotation view. "
                           "Stylus buttons arrive as right or middle click unless they are assigned to a command in the table.");
    mUi->rightClickAction->setToolTip(tip);
    mUi->middleClickAction->setToolTip(tip);

    connect(mUi->rightClickAction, QOverload<int>::of(&QComboBox::activated), this, [this](int index){
        UBClickBehavior::instance()->setAction(Qt::RightButton,
                                               static_cast<UBClickBehavior::Action>(mUi->rightClickAction->itemData(index).toInt()));
    });

    connect(mUi->middleClickAction, QOverload<int>::of(&QComboBox::activated), this, [this](int index){
        UBClickBehavior::instance()->setAction(Qt::MiddleButton,
                                               static_cast<UBClickBehavior::Action>(mUi->middleClickAction->itemData(index).toInt()));
    });

    connect(UBClickBehavior::instance(), &UBClickBehavior::actionChanged, this, [this](int button){
        selectClickAction(button);
    });

    connect(mUi->applyShortcutProfileButton, &QPushButton::clicked, this, &UBInputDevicePreferences::applyProfile);
    connect(mUi->resetAllShortcutsButton, &QPushButton::clicked, this, &UBInputDevicePreferences::resetAllShortcuts);

    refresh();
}

void UBInputDevicePreferences::refresh()
{
    selectClickAction(Qt::RightButton);
    selectClickAction(Qt::MiddleButton);

    // reload profiles, user profiles may have been added
    const QString current = mUi->shortcutProfile->currentData().toString();
    mUi->shortcutProfile->clear();

    for (const UBShortcutProfile& profile : UBShortcutProfile::availableProfiles())
    {
        mUi->shortcutProfile->addItem(profile.name, profile.source);
        mUi->shortcutProfile->setItemData(mUi->shortcutProfile->count() - 1, profile.description, Qt::ToolTipRole);
    }

    const int index = mUi->shortcutProfile->findData(current);
    mUi->shortcutProfile->setCurrentIndex(index >= 0 ? index : 0);
    mUi->applyShortcutProfileButton->setEnabled(mUi->shortcutProfile->count() > 0);
    mUi->noCtrl->setEnabled(!UBShortcutManager::shortcutManager()->hasCtrlConflicts());
}

void UBInputDevicePreferences::applyProfile()
{
    QString error;
    const UBShortcutProfile profile = UBShortcutProfile::fromFile(mUi->shortcutProfile->currentData().toString(), &error);

    if (!profile.isValid())
    {
        QMessageBox::warning(mDialog, tr("Device profile"), tr("The device profile could not be loaded: %1").arg(error));
        return;
    }

    QString question = tr("Apply the device profile \"%1\"?").arg(profile.name);

    if (!profile.description.isEmpty())
    {
        question += "\n\n" + profile.description;
    }

    question += "\n\n" + (profile.resetAll
                          ? tr("All shortcuts will be reset to their defaults.")
                          : tr("Only the commands listed in the profile are changed, all other shortcuts are kept."));

    if (QMessageBox::question(mDialog, tr("Apply device profile"), question,
                              QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Ok) != QMessageBox::Ok)
    {
        return;
    }

    clearSelection();
    const QStringList warnings = profile.apply();
    refresh();

    if (!warnings.isEmpty())
    {
        QMessageBox::information(mDialog, tr("Device profile"),
                                 tr("The device profile \"%1\" was applied with the following notes:").arg(profile.name)
                                 + "\n\n" + warnings.join("\n"));
    }
}

void UBInputDevicePreferences::resetAllShortcuts()
{
    if (QMessageBox::question(mDialog, tr("Reset all shortcuts"),
                              tr("Reset all key sequences, mouse buttons, stylus buttons and the click behavior to their defaults?"),
                              QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Ok)
    {
        return;
    }

    clearSelection();
    UBShortcutProfile::resetAllShortcuts();
    refresh();
}

void UBInputDevicePreferences::fillClickCombo(int button)
{
    QComboBox* combo = button == Qt::RightButton ? mUi->rightClickAction : mUi->middleClickAction;
    combo->clear();

    for (UBClickBehavior::Action action : UBClickBehavior::availableActions(button))
    {
        combo->addItem(UBClickBehavior::actionText(action), static_cast<int>(action));
    }
}

void UBInputDevicePreferences::selectClickAction(int button)
{
    QComboBox* combo = button == Qt::RightButton ? mUi->rightClickAction : mUi->middleClickAction;
    const int index = combo->findData(static_cast<int>(UBClickBehavior::instance()->action(button)));

    if (index >= 0)
    {
        combo->setCurrentIndex(index);
    }
}

void UBInputDevicePreferences::clearSelection()
{
    // stop recording and clear the edit fields, they may show outdated values afterwards
    if (mUi->recordButton->isChecked())
    {
        mUi->abortButton->click();
    }

    mUi->shortcutTableView->clearSelection();
    mUi->keySequence->clear();
    mUi->mouseButton->clear();
    mUi->stylusButton->clear();
    mUi->report->clear();
    mUi->shortcutsGroupBox->setEnabled(false);
}
