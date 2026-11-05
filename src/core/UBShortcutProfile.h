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

#ifndef UBSHORTCUTPROFILE_H
#define UBSHORTCUTPROFILE_H

#include <QCoreApplication>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>

/**
 * @brief A shortcut profile for an input device (keyboard, tablet, mouse).
 *
 * Profiles are JSON files. Built-in profiles are compiled into the resources
 * (resources/etc/shortcutProfiles, resource path ":/shortcutProfiles"), user
 * profiles can be placed in "<user data directory>/shortcutProfiles". Format:
 *
 * {
 *   "name": "Display name",
 *   "description": "Text shown before the profile is applied",
 *   "order": 10,                          // sort order in the list (optional)
 *   "resetAll": false,                    // reset all shortcuts before applying (optional)
 *   "rightClickAction": "temporaryEraser",// see UBClickBehavior::actionId (optional)
 *   "middleClickAction": "pan",           // (optional)
 *   "shortcuts": [
 *     { "action": "actionUndo",           // objectName of the action
 *       "keys": "Ctrl+Z",                 // primary key sequence, "" removes it (optional)
 *       "extraKeys": [ "Ctrl+Shift+F9" ], // additional key sequences (optional)
 *       "mouseButton": "Back",            // Middle, Back, Forward, Task, Extra4..Extra24, "" removes it (optional)
 *       "tabletButton": "Middle" }        // Right, Middle, Back, ... (optional)
 *   ]
 * }
 *
 * Only the listed actions and properties are changed, everything else is left untouched.
 */
class UBShortcutProfile
{
    Q_DECLARE_TR_FUNCTIONS(UBShortcutProfile)

public:
    struct Entry
    {
        QString action;
        QVariant keys;          // invalid: unchanged
        QVariant extraKeys;     // invalid: unchanged, otherwise QStringList
        QVariant mouseButton;   // invalid: unchanged, otherwise button id
        QVariant tabletButton;  // invalid: unchanged, otherwise button id
    };

    QString name;
    QString description;
    QString source;
    int order{100};
    bool resetAll{false};
    QString rightClickAction;
    QString middleClickAction;
    QList<Entry> entries;

    bool isValid() const;

    // apply the profile, returns a list of warnings (unknown actions, conflicts)
    QStringList apply() const;

    static UBShortcutProfile fromJson(const QByteArray& json, QString* error = nullptr);
    static UBShortcutProfile fromFile(const QString& path, QString* error = nullptr);

    static QStringList searchPaths();
    static QList<UBShortcutProfile> availableProfiles();

    // reset all shortcuts and the right/middle click behavior to the defaults
    static void resetAllShortcuts();
};

#endif // UBSHORTCUTPROFILE_H
