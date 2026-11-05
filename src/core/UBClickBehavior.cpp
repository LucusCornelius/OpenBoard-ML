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

#include "UBClickBehavior.h"

#include <QAction>

#include "core/UB.h"
#include "core/UBSettings.h"
#include "board/UBDrawingController.h"

UBClickBehavior* UBClickBehavior::instance()
{
    static UBClickBehavior* sInstance = new UBClickBehavior;
    return sInstance;
}

UBClickBehavior::UBClickBehavior()
    : QObject(nullptr)
{
}

QList<UBClickBehavior::Action> UBClickBehavior::availableActions(int button)
{
    switch (button)
    {
    case Qt::RightButton:
        return { ContextMenu, TemporaryEraser, PreviousTool, SelectTool, Pan };

    case Qt::MiddleButton:
        return { Pan, PreviousTool, Nothing };

    default:
        return {};
    }
}

UBClickBehavior::Action UBClickBehavior::defaultAction(int button)
{
    // middle button did nothing on the board before this setting was introduced
    return button == Qt::MiddleButton ? Nothing : ContextMenu;
}

QString UBClickBehavior::settingsKey(int button)
{
    switch (button)
    {
    case Qt::RightButton:
        return "Shortcut/RightClickAction";

    case Qt::MiddleButton:
        return "Shortcut/MiddleClickAction";

    default:
        return QString();
    }
}

QString UBClickBehavior::actionText(Action action)
{
    switch (action)
    {
    case ContextMenu: return tr("Context menu (default)");
    case TemporaryEraser: return tr("Temporary eraser (while pressed)");
    case PreviousTool: return tr("Previous tool");
    case SelectTool: return tr("Select tool");
    case Pan: return tr("Pan (hand)");
    case Nothing: return tr("Nothing");
    }

    return QString();
}

QString UBClickBehavior::actionId(Action action)
{
    switch (action)
    {
    case ContextMenu: return "contextMenu";
    case TemporaryEraser: return "temporaryEraser";
    case PreviousTool: return "previousTool";
    case SelectTool: return "selectTool";
    case Pan: return "pan";
    case Nothing: return "nothing";
    }

    return QString();
}

UBClickBehavior::Action UBClickBehavior::actionFromId(const QString& id, bool* ok)
{
    for (Action action : { ContextMenu, TemporaryEraser, PreviousTool, SelectTool, Pan, Nothing })
    {
        if (actionId(action).compare(id, Qt::CaseInsensitive) == 0)
        {
            if (ok)
            {
                *ok = true;
            }

            return action;
        }
    }

    if (ok)
    {
        *ok = false;
    }

    return ContextMenu;
}

UBClickBehavior::Action UBClickBehavior::action(int button) const
{
    const QString key = settingsKey(button);

    if (key.isEmpty())
    {
        return ContextMenu;
    }

    bool ok = false;
    const Action configured = actionFromId(UBSettings::settings()->value(key).toString(), &ok);

    if (ok && availableActions(button).contains(configured))
    {
        return configured;
    }

    return defaultAction(button);
}

void UBClickBehavior::setAction(int button, Action action)
{
    const QString key = settingsKey(button);

    if (key.isEmpty() || !availableActions(button).contains(action))
    {
        return;
    }

    if (this->action(button) != action)
    {
        UBSettings::settings()->setValue(key, actionId(action));
        emit actionChanged(button, action);
    }
}

void UBClickBehavior::resetActions()
{
    setAction(Qt::RightButton, defaultAction(Qt::RightButton));
    setAction(Qt::MiddleButton, defaultAction(Qt::MiddleButton));
}

void UBClickBehavior::connectToDrawingController()
{
    if (!mConnected)
    {
        connect(UBDrawingController::drawingController(), &UBDrawingController::stylusToolChanged,
                this, &UBClickBehavior::stylusToolChanged);
        mConnected = true;
    }
}

void UBClickBehavior::activatePreviousTool()
{
    connectToDrawingController();

    const int current = UBDrawingController::drawingController()->stylusTool();
    int target = mPreviousTool;

    if (target < 0 || target == current)
    {
        // no history yet: toggle between pen and eraser
        target = current == UBStylusTool::Pen ? UBStylusTool::Eraser : UBStylusTool::Pen;
    }

    UBDrawingController::drawingController()->setStylusTool(target);
}

void UBClickBehavior::togglePenEraser()
{
    connectToDrawingController();

    UBDrawingController* dc = UBDrawingController::drawingController();

    if (dc->stylusTool() == UBStylusTool::Eraser)
    {
        const int latest = dc->latestDrawingTool();
        dc->setStylusTool(latest >= 0 ? latest : UBStylusTool::Pen);
    }
    else
    {
        dc->setStylusTool(UBStylusTool::Eraser);
    }
}

void UBClickBehavior::beginTemporaryTool(int tool)
{
    connectToDrawingController();

    if (mToolBeforeTemporarySwitch < 0)
    {
        mToolBeforeTemporarySwitch = UBDrawingController::drawingController()->stylusTool();
    }

    setTool(tool);
}

void UBClickBehavior::endTemporaryTool()
{
    if (mToolBeforeTemporarySwitch >= 0)
    {
        const int tool = mToolBeforeTemporarySwitch;
        mToolBeforeTemporarySwitch = -1;
        setTool(tool);
    }
}

bool UBClickBehavior::isTemporaryToolActive() const
{
    return mToolBeforeTemporarySwitch >= 0;
}

QList<QAction*> UBClickBehavior::createToolActions(QObject* parent)
{
    QAction* previousTool = new QAction(tr("Previous tool"), parent);
    previousTool->setObjectName("actionPreviousTool");
    previousTool->setToolTip(tr("Switch back to the previously used tool"));
    connect(previousTool, &QAction::triggered, this, &UBClickBehavior::activatePreviousTool);

    QAction* togglePenEraser = new QAction(tr("Toggle pen / eraser"), parent);
    togglePenEraser->setObjectName("actionTogglePenEraser");
    togglePenEraser->setToolTip(tr("Switch between eraser and the last drawing tool"));
    connect(togglePenEraser, &QAction::triggered, this, &UBClickBehavior::togglePenEraser);

    return { previousTool, togglePenEraser };
}

void UBClickBehavior::stylusToolChanged(int tool, int previousTool)
{
    if (!mSuppressHistory && previousTool >= 0 && previousTool != tool)
    {
        mPreviousTool = previousTool;
    }
}

void UBClickBehavior::setTool(int tool)
{
    const bool suppress = mSuppressHistory;
    mSuppressHistory = true;
    UBDrawingController::drawingController()->setStylusTool(tool);
    mSuppressHistory = suppress;
}
