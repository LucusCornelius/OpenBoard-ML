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

#include "UBShortcutProfile.h"

#include <algorithm>

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QMap>

#include "core/UBClickBehavior.h"
#include "core/UBSettings.h"
#include "core/UBShortcutManager.h"
#include "frameworks/UBPlatformUtils.h"

namespace
{
    // Free the value (key sequence or button name) in the given column if another
    // action uses it. Returns false if the value cannot be freed.
    bool releaseConflict(const QModelIndex& index, int column, const QString& value, QStringList& warnings)
    {
        UBShortcutManager* sm = UBShortcutManager::shortcutManager();

        for (int row = 0; row < sm->rowCount(); ++row)
        {
            if (row == index.row())
            {
                continue;
            }

            const QModelIndex other = sm->index(row, column);

            if (!sm->data(other).toString().split(", ").contains(value))
            {
                continue;
            }

            const QString otherName = sm->data(other.siblingAtColumn(0)).toString();

            if (!sm->data(other, UBShortcutManager::ActionRole).toBool())
            {
                warnings << UBShortcutProfile::tr("\"%1\" is reserved for the built-in command \"%2\" and was not assigned.")
                            .arg(value, otherName);
                return false;
            }

            if (column == 2)
            {
                if (sm->data(other, UBShortcutManager::PrimaryShortcutRole).toString() != value)
                {
                    warnings << UBShortcutProfile::tr("\"%1\" is already used by \"%2\" and was not assigned.")
                                .arg(value, otherName);
                    return false;
                }

                sm->setData(other, QString());
            }
            else
            {
                sm->setData(other, static_cast<int>(Qt::NoButton));
            }

            warnings << UBShortcutProfile::tr("\"%1\" was removed from \"%2\".").arg(value, otherName);
        }

        return true;
    }

    void applyButton(const QModelIndex& index, int column, const QVariant& value, bool tablet, QStringList& warnings)
    {
        if (!value.isValid())
        {
            return;
        }

        UBShortcutManager* sm = UBShortcutManager::shortcutManager();
        const QString id = value.toString();
        const Qt::MouseButton button = UBShortcutManager::buttonFromId(id);

        if (button == Qt::NoButton && !id.trimmed().isEmpty())
        {
            warnings << UBShortcutProfile::tr("Unknown button \"%1\".").arg(id);
            return;
        }

        if (button == Qt::LeftButton || (!tablet && button == Qt::RightButton))
        {
            // left button (pen tip) and right mouse button are reserved for drawing and the right click behavior
            warnings << UBShortcutProfile::tr("Button \"%1\" cannot be assigned to a command.").arg(id);
            return;
        }

        if (button != Qt::NoButton && !releaseConflict(index, column, UBShortcutManager::buttonName(button), warnings))
        {
            return;
        }

        sm->setData(index.siblingAtColumn(column), static_cast<int>(button));
    }
}

bool UBShortcutProfile::isValid() const
{
    return !name.isEmpty();
}

QStringList UBShortcutProfile::apply() const
{
    QStringList warnings;
    UBShortcutManager* sm = UBShortcutManager::shortcutManager();

    if (resetAll)
    {
        resetAllShortcuts();
    }

    for (const Entry& entry : entries)
    {
        const QModelIndex index = sm->indexOfAction(entry.action);

        if (!index.isValid())
        {
            warnings << tr("Unknown command \"%1\".").arg(entry.action);
            continue;
        }

        if (!sm->data(index, UBShortcutManager::ActionRole).toBool())
        {
            warnings << tr("Command \"%1\" cannot be changed.").arg(entry.action);
            continue;
        }

        if (entry.keys.isValid())
        {
            const QKeySequence keys(entry.keys.toString(), QKeySequence::PortableText);

            if (keys.isEmpty() || releaseConflict(index, 2, keys.toString(), warnings))
            {
                sm->setData(index.siblingAtColumn(2), keys.toString());
            }
        }

        if (entry.extraKeys.isValid())
        {
            QList<QKeySequence> extras;

            for (const QString& extra : entry.extraKeys.toStringList())
            {
                const QKeySequence keys(extra, QKeySequence::PortableText);

                if (keys.isEmpty())
                {
                    warnings << tr("Invalid key sequence \"%1\".").arg(extra);
                }
                else if (releaseConflict(index, 2, keys.toString(), warnings))
                {
                    extras << keys;
                }
            }

            sm->setExtraShortcuts(index, extras);
        }

        applyButton(index, 3, entry.mouseButton, false, warnings);
        applyButton(index, 4, entry.tabletButton, true, warnings);
    }

    UBClickBehavior* clickBehavior = UBClickBehavior::instance();

    for (const auto& click : { qMakePair(int(Qt::RightButton), rightClickAction), qMakePair(int(Qt::MiddleButton), middleClickAction) })
    {
        if (click.second.isEmpty())
        {
            continue;
        }

        bool ok = false;
        UBClickBehavior::Action action = UBClickBehavior::actionFromId(click.second, &ok);

        if (ok && UBClickBehavior::availableActions(click.first).contains(action))
        {
            clickBehavior->setAction(click.first, action);
        }
        else
        {
            warnings << tr("Unknown click behavior \"%1\".").arg(click.second);
        }
    }

    return warnings;
}

UBShortcutProfile UBShortcutProfile::fromJson(const QByteArray& json, QString* error)
{
    UBShortcutProfile profile;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);

    if (document.isNull() || !document.isObject())
    {
        if (error)
        {
            *error = parseError.errorString();
        }

        return profile;
    }

    const QJsonObject object = document.object();
    profile.name = object.value("name").toString();
    profile.description = object.value("description").toString();
    profile.order = object.value("order").toInt(100);
    profile.resetAll = object.value("resetAll").toBool(false);
    profile.rightClickAction = object.value("rightClickAction").toString();
    profile.middleClickAction = object.value("middleClickAction").toString();

    const QJsonArray shortcuts = object.value("shortcuts").toArray();

    for (const QJsonValue& value : shortcuts)
    {
        const QJsonObject shortcut = value.toObject();
        Entry entry;
        entry.action = shortcut.value("action").toString();

        if (entry.action.isEmpty())
        {
            continue;
        }

        if (shortcut.contains("keys"))
        {
            entry.keys = shortcut.value("keys").toString();
        }

        if (shortcut.contains("extraKeys"))
        {
            QStringList extras;

            for (const QJsonValue& extra : shortcut.value("extraKeys").toArray())
            {
                extras << extra.toString();
            }

            entry.extraKeys = extras;
        }

        if (shortcut.contains("mouseButton"))
        {
            entry.mouseButton = shortcut.value("mouseButton").toString();
        }

        if (shortcut.contains("tabletButton"))
        {
            entry.tabletButton = shortcut.value("tabletButton").toString();
        }

        profile.entries << entry;
    }

    if (profile.name.isEmpty() && error)
    {
        *error = tr("Profile has no name");
    }

    return profile;
}

UBShortcutProfile UBShortcutProfile::fromFile(const QString& path, QString* error)
{
    QFile file(path);

    if (!file.open(QIODevice::ReadOnly))
    {
        if (error)
        {
            *error = file.errorString();
        }

        return UBShortcutProfile();
    }

    UBShortcutProfile profile = fromJson(file.readAll(), error);
    profile.source = path;
    return profile;
}

QStringList UBShortcutProfile::searchPaths()
{
    return {
        ":/shortcutProfiles",                                           // built-in
        UBPlatformUtils::applicationEtcDirectory() + "/shortcutProfiles", // installed by administrators
        UBSettings::userDataDirectory() + "/shortcutProfiles"           // user defined
    };
}

QList<UBShortcutProfile> UBShortcutProfile::availableProfiles()
{
    // a profile file in a later search path replaces a built-in profile with the same file name
    QMap<QString, UBShortcutProfile> profileMap;

    for (const QString& path : searchPaths())
    {
        QDir dir(path);

        for (const QString& fileName : dir.entryList({ "*.json" }, QDir::Files, QDir::Name))
        {
            QString error;
            UBShortcutProfile profile = fromFile(dir.filePath(fileName), &error);

            if (profile.isValid())
            {
                profileMap[fileName] = profile;
            }
            else
            {
                qWarning() << "Invalid shortcut profile" << dir.filePath(fileName) << error;
            }
        }
    }

    QList<UBShortcutProfile> profiles = profileMap.values();

    std::stable_sort(profiles.begin(), profiles.end(), [](const UBShortcutProfile& a, const UBShortcutProfile& b){
        return a.order < b.order;
    });

    return profiles;
}

void UBShortcutProfile::resetAllShortcuts()
{
    UBShortcutManager::shortcutManager()->resetAll();
    UBClickBehavior::instance()->resetActions();
}
