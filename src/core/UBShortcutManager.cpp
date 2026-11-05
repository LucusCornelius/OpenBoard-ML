/*
 * Copyright (C) 2015-2018 Département de l'Instruction Publique (DIP-SEM)
 *
 * Copyright (C) 2013 Open Education Foundation
 *
 * Copyright (C) 2010-2013 Groupement d'Intérêt Public pour
 * l'Education Numérique en Afrique (GIP ENA)
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

#include "UBShortcutManager.h"
#include "core/UBClickBehavior.h"
#include "core/UBSettings.h"
#include "frameworks/UBPlatformUtils.h"
#include "gui/UBMainWindow.h"

#include <QAction>
#include <QDebug>
#include <QWidget>

// property names
static const char* defaultShortcutProperty("defaultShortcut");
static const char* descriptionProperty("description");
static const char* mouseButtonProperty("mouseButton");
static const char* tabletButtonProperty("tabletButton");
static const char* primaryShortcutProperty("primaryShortcut");
static const char* extraShortcutsProperty("extraShortcuts");

// separator for multiple key sequences in settings, same as QKeySequence::listToString
static const QString shortcutListSeparator("; ");

// primary (user editable) shortcut of an action
static QKeySequence primaryShortcut(const QAction* action)
{
    QVariant primary = action->property(primaryShortcutProperty);
    return primary.isValid() ? QKeySequence(primary.toString()) : action->shortcut();
}

// helper function to flip keyboard modifier depending on Qt version
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    QKeyCombination flipModifier(const QKeyCombination& keyCombination, Qt::KeyboardModifier modifier)
    {
        Qt::KeyboardModifiers mod = keyCombination.keyboardModifiers();
        mod.setFlag(modifier, !mod.testFlag(modifier));
        return QKeyCombination(mod, keyCombination.key());
    }
#else
    int flipModifier(int keyCombination, Qt::KeyboardModifier modifier)
    {
        return keyCombination ^ modifier;
    }
#endif

UBShortcutManager* UBShortcutManager::sShortcutManager = nullptr;

UBShortcutManager::UBShortcutManager() : mIgnoreCtrl(false)
{
    actionsOfGroup(tr("Common"));
}

UBShortcutManager *UBShortcutManager::shortcutManager()
{
    if (!sShortcutManager)
    {
        sShortcutManager = new UBShortcutManager;
    }

    return sShortcutManager;
}

void UBShortcutManager::addActions(const QString& group, const QList<QAction*> actions, QWidget *widget)
{
    // actions may be added at any time, so inform attached views
    beginResetModel();

    // save default shortcuts for later
    for (QAction* action : actions)
    {
        if (action && !action->isSeparator())
        {
            if (widget && !widget->actions().contains(action))
            {
                // associate actions with widget to make sure they are triggered when this widget is visible
                widget->addAction(action);
            }

            QKeySequence shortcut = action->shortcut();

            if (!shortcut.isEmpty() && !action->property(defaultShortcutProperty).isValid())
            {
                action->setProperty(defaultShortcutProperty, shortcut.toString());
            }

            action->setProperty(descriptionProperty, action->toolTip());

            if (action->objectName().isEmpty())
            {
                if (!action->property("builtIn").toBool())
                {
                    qWarning() << "UBShortcutManager: action" << action->text() << "has no objectName, shortcut settings are not persisted";
                }
            }
            else
            {
                QStringList settings = UBSettings::settings()->value("Shortcut/" + action->objectName()).toStringList();

                if (settings.size() == 3)
                {
                    // an empty key sequence means that the user removed the shortcut,
                    // additional key sequences (e.g. from device profiles) are separated by "; "
                    QStringList keys = settings[0].split(shortcutListSeparator);
                    QKeySequence primary(keys.takeFirst());

                    if (!keys.isEmpty())
                    {
                        action->setProperty(extraShortcutsProperty, keys);
                    }

                    applyShortcut(action, primary);

                    if (int button = settings[1].toInt())
                    {
                        action->setProperty(mouseButtonProperty, button);
                        mMouseActions[static_cast<Qt::MouseButton>(button)] = action;
                    }

                    if (int button = settings[2].toInt())
                    {
                        action->setProperty(tabletButtonProperty, button);
                        mTabletActions[static_cast<Qt::MouseButton>(button)] = action;
                    }
                }
                else if (mIgnoreCtrl && !action->property("builtIn").toBool())
                {
                    // add the variant without Ctrl for actions registered after startup
                    applyShortcut(action, primaryShortcut(action));
                }
            }

            QString oldGroup = groupOfAction(action);

            if (oldGroup.isEmpty()) {
                actionsOfGroup(group) << action;
                connect(action, &QObject::destroyed, this, &UBShortcutManager::removeAction, Qt::UniqueConnection);
            }
            else if (oldGroup != group)
            {
                // remove from oldGroup, add to Common, which is always first group
                actionsOfGroup(oldGroup).removeAll(action);

                if (!mActionGroups[0].second.contains(action))
                {
                    mActionGroups[0].second << action;
                }
            }
        }
    }

    endResetModel();
}

void UBShortcutManager::addMainActions(UBMainWindow *mainWindow)
{
    addActions(tr("Common"), {
                   mainWindow->actionStylus,
                   mainWindow->actionBoard,
                   mainWindow->actionWeb,
                   mainWindow->actionDocument,
                   mainWindow->actionDesktop,
                   mainWindow->actionLibrary,
                   mainWindow->actionVirtualKeyboard,
                   mainWindow->actionOpenTutorial,
                   mainWindow->actionHideApplication,
                   mainWindow->actionCut,
                   mainWindow->actionCopy,
                   mainWindow->actionPaste,
                   mainWindow->actionQuit
               }, mainWindow);

    // Don't allow to modify these shortcuts
    mainWindow->actionCut->setProperty("builtIn", true);
    mainWindow->actionCopy->setProperty("builtIn", true);
    mainWindow->actionPaste->setProperty("builtIn", true);

    addActions(tr("Board"), {
                   mainWindow->actionUndo,
                   mainWindow->actionRedo,
                   mainWindow->actionNewPage,
                   mainWindow->actionDuplicatePage,
                   mainWindow->actionImportPage,
                   mainWindow->actionBack,
                   mainWindow->actionForward,
                   mainWindow->actionAdd,
                   mainWindow->actionClearPage,
                   mainWindow->actionEraseItems,
                   mainWindow->actionEraseAnnotations,
                   mainWindow->actionEraseBackground
               }, mainWindow);

    addActions(tr("Stylus Palette"),{
                   mainWindow->actionPen,
                   mainWindow->actionEraser,
                   mainWindow->actionMarker,
                   mainWindow->actionSelector,
                   mainWindow->actionPlay,

                   mainWindow->actionHand,
                   mainWindow->actionZoomIn,
                   mainWindow->actionZoomOut,

                   mainWindow->actionPointer,
                   mainWindow->actionLine,
                   mainWindow->actionText,
                   mainWindow->actionCapture
               }, mainWindow);

    if(UBPlatformUtils::hasVirtualKeyboard())
    {
        addActions(tr("Stylus Palette"),{ mainWindow->actionVirtualKeyboard }, mainWindow);
    }

    addActions(tr("Stylus Palette"),{ mainWindow->actionSnap }, mainWindow);

    addActions(tr("Stylus Palette"),{ mainWindow->actionShapes }, mainWindow);
    // additional tool actions for input devices (tablet ExpressKeys, pen buttons)
    addActions(tr("Stylus Palette"), UBClickBehavior::instance()->createToolActions(mainWindow), mainWindow);

    addActions(tr("Lines and colours"), {
                   mainWindow->actionLineSmall,
                   mainWindow->actionLineMedium,
                   mainWindow->actionLineLarge,
                   mainWindow->actionEraserSmall,
                   mainWindow->actionEraserMedium,
                   mainWindow->actionEraserLarge,
                   mainWindow->actionColor0,
                   mainWindow->actionColor1,
                   mainWindow->actionColor2,
                   mainWindow->actionColor3,
                   mainWindow->actionColor4,
                   mainWindow->actionColor5,
                   mainWindow->actionColor6,
                   mainWindow->actionColor7,
                   mainWindow->actionColor8,
                   mainWindow->actionColor9
               }, mainWindow);

    addActions(tr("Background"), {
                   mainWindow->actionBackgrounds,
                   mainWindow->actionDefaultGridSize,
                   mainWindow->actionLightDarkMode,
                   mainWindow->actionBackgroundBlank,
                   mainWindow->actionBackgroundLined,
                   mainWindow->actionBackgroundGrid1cm,
                   mainWindow->actionBackgroundGrid5mm
               }, mainWindow);

    addActions(tr("Podcast"), {
                   mainWindow->actionPodcastRecord //,
                   // mainWindow->actionPodcastPause currently not activated in UBPodcastRecordingPalette
               }, mainWindow);

    addActions(tr("Maths"), {
                   mainWindow->actionMathPalette
               }, mainWindow);

    // add builtIn actions
    QList<QAction*> actions;

    QAction* action = new QAction(this);
    action->setText(mainWindow->actionBack->text());
    action->setToolTip(mainWindow->actionBack->toolTip());
    action->setShortcuts( { QKeySequence(Qt::Key_Up), QKeySequence(Qt::Key_PageUp), QKeySequence(Qt::Key_Left) } );
    action->setProperty("builtIn", true);
    actions << action;

    action = new QAction(this);
    action->setText(mainWindow->actionForward->text());
    action->setToolTip(mainWindow->actionForward->toolTip());
    action->setShortcuts( { QKeySequence(Qt::Key_Down), QKeySequence(Qt::Key_PageDown), QKeySequence(Qt::Key_Right), QKeySequence(Qt::Key_Space) } );
    action->setProperty("builtIn", true);
    actions << action;

    action = new QAction(this);
    action->setText(tr("First scene"));
    action->setToolTip(tr("Show first scene"));
    action->setShortcuts( { QKeySequence(Qt::Key_Home) } );
    action->setProperty("builtIn", true);
    actions << action;

    action = new QAction(this);
    action->setText(tr("Last scene"));
    action->setToolTip(tr("Show last scene"));
    action->setShortcuts( { QKeySequence(Qt::Key_End) } );
    action->setProperty("builtIn", true);
    actions << action;

    action = new QAction(this);
    action->setText(mainWindow->actionNewPage->text());
    action->setToolTip(mainWindow->actionNewPage->toolTip());
    action->setShortcuts( { QKeySequence(Qt::Key_Insert) } );
    action->setProperty("builtIn", true);
    actions << action;

    action = new QAction(this);
    action->setText(mainWindow->actionZoomIn->text());
    action->setToolTip(mainWindow->actionZoomIn->toolTip());
    action->setShortcuts( { QKeySequence(Qt::CTRL | Qt::Key_Plus) } );
    action->setProperty("builtIn", true);
    actions << action;

    action = new QAction(this);
    action->setText(mainWindow->actionZoomOut->text());
    action->setToolTip(mainWindow->actionZoomOut->toolTip());
    action->setShortcuts( { QKeySequence(Qt::CTRL | Qt::Key_Minus) } );
    action->setProperty("builtIn", true);
    actions << action;

    action = new QAction(this);
    action->setText(tr("Zoom reset"));
    action->setToolTip(tr("Reset zoom factor"));
    action->setShortcuts( { QKeySequence(Qt::CTRL | Qt::Key_0) } );
    action->setProperty("builtIn", true);
    actions << action;

    action = new QAction(this);
    action->setText(tr("Scroll left"));
    action->setToolTip(tr("Scroll page left"));
    action->setShortcuts( { QKeySequence(Qt::CTRL | Qt::Key_Left) } );
    action->setProperty("builtIn", true);
    actions << action;

    action = new QAction(this);
    action->setText(tr("Scroll right"));
    action->setToolTip(tr("Scroll page right"));
    action->setShortcuts( { QKeySequence(Qt::CTRL | Qt::Key_Right) } );
    action->setProperty("builtIn", true);
    actions << action;

    action = new QAction(this);
    action->setText(tr("Scroll up"));
    action->setToolTip(tr("Scroll page up"));
    action->setShortcuts( { QKeySequence(Qt::CTRL | Qt::Key_Up) } );
    action->setProperty("builtIn", true);
    actions << action;

    action = new QAction(this);
    action->setText(tr("Scroll down"));
    action->setToolTip(tr("Scroll page down"));
    action->setShortcuts( { QKeySequence(Qt::CTRL | Qt::Key_Down) } );
    action->setProperty("builtIn", true);
    actions << action;

    addActions(tr("Built-in (not editable)"), actions);

    // load ignoreCtrl setting
    ignoreCtrl(UBSettings::settings()->value("Shortcut/IgnoreCtrl").toBool());
}

void UBShortcutManager::addActionGroup(QActionGroup *actionGroup)
{
    mActionGroupHistoryMap[actionGroup] = new UBActionGroupHistory(actionGroup);
}

void UBShortcutManager::removeActionGroup(QActionGroup *actionGroup)
{
    if (mActionGroupHistoryMap.contains(actionGroup))
    {
        delete mActionGroupHistoryMap[actionGroup];
        mActionGroupHistoryMap.remove(actionGroup);
    }
}

static void triggerMappedAction(QAction* action)
{
    if (action->isEnabled() && (!action->isCheckable() || !action->actionGroup() || !action->isChecked()))
    {
        action->trigger();
    }
}

bool UBShortcutManager::handleMouseEvent(QMouseEvent *event)
{
    // Note: this function is called from the application event filter, i.e. before
    // the event reaches any widget. As the filter sees the event first for the
    // QWindow and then again for the target widget, a consumed press never reaches
    // the widget and is therefore handled only once.
    const int button = event->button();

    if (button == Qt::NoButton)
    {
        return false;
    }

    switch (event->type())
    {
    case QEvent::MouseButtonPress:
        if (mPressedTabletButtons.contains(button))
        {
            // mouse event generated by the platform for a mapped tablet button
            event->accept();
            return true;
        }

        mPendingMouseReleaseButtons.clear();

        if (QAction* action = mMouseActions.value(static_cast<Qt::MouseButton>(button)))
        {
            mPressedMouseButtons.insert(button);
            triggerMappedAction(action);
            event->accept();
            return true;
        }

        break;

    case QEvent::MouseButtonDblClick:
        // the press of the second click was already handled, don't trigger twice
        if (mMouseActions.contains(static_cast<Qt::MouseButton>(button)) || mPressedTabletButtons.contains(button))
        {
            mPressedMouseButtons.insert(button);
            event->accept();
            return true;
        }

        break;

    case QEvent::MouseButtonRelease:
        if (mPressedMouseButtons.remove(button) || mPressedTabletButtons.contains(button)
                || mPendingMouseReleaseButtons.remove(button))
        {
            event->accept();
            return true;
        }

        break;

    default:
        break;
    }

    return false;
}

bool UBShortcutManager::handleTabletEvent(QTabletEvent *event)
{
    const int button = event->button();

    if (button == Qt::NoButton)
    {
        return false;
    }

    switch (event->type())
    {
    case QEvent::TabletPress:
        mPendingMouseReleaseButtons.clear();

        if (QAction* action = mTabletActions.value(static_cast<Qt::MouseButton>(button)))
        {
            mPressedTabletButtons.insert(button);
            triggerMappedAction(action);
            // accept, so that Qt does not synthesize a mouse event
            event->accept();
            return true;
        }

        break;

    case QEvent::TabletRelease:
        if (mPressedTabletButtons.remove(button))
        {
            // some platforms (e.g. macOS) send an additional mouse release
            mPendingMouseReleaseButtons.insert(button);
            event->accept();
            return true;
        }

        break;

    default:
        break;
    }

    return false;
}

bool UBShortcutManager::handleKeyReleaseEvent(QKeyEvent *event)
{
    for (UBActionGroupHistory* actionGroupHistory : mActionGroupHistoryMap.values())
    {
        if (actionGroupHistory->keyReleased(event)) {
            return true;
        }
    }

    return false;
}

int UBShortcutManager::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);

    int rows = 0;

    for (auto& actionGroup : mActionGroups)
    {
        ++rows;
        rows += actionGroup.second.size();
    }

    return rows;
}

int UBShortcutManager::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return 5;
}

QVariant UBShortcutManager::data(const QModelIndex &index, int role) const
{
    QString group;
    QAction* action = getAction(index, &group);

    switch (role)
    {
    case Qt::DisplayRole:
    case Qt::ToolTipRole:
        switch (index.column())
        {
        case 0:
            return action ? action->text() : group;

        case 1:
            return action ? action->property(descriptionProperty) : "";

        case 2:
        {
            if (!action)
            {
                return "";
            }

            QStringList result;

            for (const QKeySequence& shortcut : action->shortcuts())
            {
                result << shortcut.toString();
            }

            return result.join(", ");
        }

        case 3:
            return action ? buttonName(static_cast<Qt::MouseButton>(action->property(mouseButtonProperty).toInt())) : QVariant();

        case 4:
            return action ? buttonName(static_cast<Qt::MouseButton>(action->property(tabletButtonProperty).toInt())) : QVariant();
        }
        break;

    case Qt::FontRole:
    {
        QFont groupFont;
        groupFont.setBold(true);
        groupFont.setItalic(true);

        QFont disabledFont;
        disabledFont.setItalic(true);

        return action ? (action->property("builtIn").toBool() ? disabledFont : QVariant()) : groupFont;
    }

    case UBShortcutManager::ActionRole:
        return action && !action->property("builtIn").toBool();

    case UBShortcutManager::GroupHeaderRole:
        return !action;

    case UBShortcutManager::PrimaryShortcutRole:
        return (index.column() == 2 && action) ? primaryShortcut(action).toString() : QVariant();

    case Qt::DecorationRole:
    case Qt::EditRole:
    case Qt::StatusTipRole:
    case Qt::WhatsThisRole:
    case Qt::SizeHintRole:
    case Qt::TextAlignmentRole:
    case Qt::BackgroundRole:
    case Qt::ForegroundRole:
    case Qt::CheckStateRole:
    case Qt::InitialSortOrderRole:
        return QVariant();
    }

    return QVariant();
}

QVariant UBShortcutManager::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Orientation::Horizontal && role == Qt::DisplayRole)
    {
        switch (section)
        {
        case 0:
            return tr("Command");

        case 1:
            return tr("Description");

        case 2:
            return tr("Key Sequence");

        case 3:
            return tr("Mouse Button");

        case 4:
            return tr("Tablet Button");
        }
    }

    return QVariant();
}

bool UBShortcutManager::setData(const QModelIndex &index, const QVariant &value, int role)
{
    Q_UNUSED(role)

    if (!index.isValid())
    {
        return false;
    }

    QAction* action = getAction(index);

    switch (index.column())
    {
    case 2:
    {
        QKeySequence keySequence(value.toString());

        applyShortcut(action, keySequence);
        updateSettings(action);
        emit dataChanged(index, index);
        return true;
    }

    case 3:
        action->setProperty(mouseButtonProperty, value);

        for (Qt::MouseButton key : mMouseActions.keys())
        {
            if (mMouseActions[key] == action)
            {
                mMouseActions.remove(key);
                break;
            }
        }

        if (value.toInt() != Qt::NoButton)
        {
            mMouseActions[static_cast<Qt::MouseButton>(value.toInt())] = action;
        }

        updateSettings(action);
        emit dataChanged(index, index);
        return true;

    case 4:
        action->setProperty(tabletButtonProperty, value);

        for (Qt::MouseButton key : mTabletActions.keys())
        {
            if (mTabletActions[key] == action)
            {
                mTabletActions.remove(key);
                break;
            }
        }

        if (value.toInt() != Qt::NoButton)
        {
            mTabletActions[static_cast<Qt::MouseButton>(value.toInt())] = action;
        }

        updateSettings(action);
        emit dataChanged(index, index);
        return true;
    }

    return false;
}

bool UBShortcutManager::resetData(const QModelIndex &index)
{
    QAction* action = getAction(index);

    if (action && !action->property("builtIn").toBool())
    {
        QKeySequence shortcut(action->property(defaultShortcutProperty).toString());
        action->setProperty(extraShortcutsProperty, QVariant());
        applyShortcut(action, shortcut);
        mMouseActions.remove(static_cast<Qt::MouseButton>(action->property(mouseButtonProperty).toInt()));
        mTabletActions.remove(static_cast<Qt::MouseButton>(action->property(tabletButtonProperty).toInt()));
        action->setProperty(mouseButtonProperty, QVariant());
        action->setProperty(tabletButtonProperty, QVariant());
        updateSettings(action);
        emit dataChanged(index.siblingAtColumn(2), index.siblingAtColumn(4));
        return true;
    }

    return false;
}

bool UBShortcutManager::checkData(const QModelIndex &index, const QVariant &value) const
{
    int col = index.column();

    if (col < 2)
    {
        return true;
    }

    for (int row = 0; row < rowCount(); ++row)
    {
        if (row != index.row() &&
                !value.toString().isEmpty() &&
                data(index.siblingAtRow(row)).toString().split(", ").contains(value.toString()))
        {
            // duplicate value
            return false;
        }
    }

    return true;
}

bool UBShortcutManager::hasCtrlConflicts(const QKeySequence &additionalShortcut) const
{
    QSet<QKeySequence> shortcuts;
    QSet<QKeySequence> ctrlShortcuts;

    for (int row = 1; row < rowCount(); ++row)
    {
        QAction* action = getAction(index(row, 0));

        if (!action)
        {
            continue;
        }

        if (action->property("builtIn").toBool())
        {
            for (const QKeySequence& shortcut : action->shortcuts())
            {
                shortcuts << shortcut;
            }
        }
        else
        {
            const QKeySequence primary = primaryShortcut(action);
            shortcuts << primary;
            ctrlShortcuts << QKeySequence(flipModifier(primary[0], Qt::ControlModifier));
        }

    }

    if (!additionalShortcut.isEmpty())
    {
        shortcuts << additionalShortcut;
        ctrlShortcuts << QKeySequence(flipModifier(additionalShortcut[0], Qt::ControlModifier));
    }

    return shortcuts.intersects(ctrlShortcuts);
}

QString UBShortcutManager::buttonName(Qt::MouseButton button)
{
    switch (button)
    {
    case Qt::LeftButton: return tr("Left", "MouseButton");
    case Qt::RightButton: return tr("Right", "MouseButton");
    case Qt::MiddleButton: return tr("Middle", "MouseButton");
    case Qt::BackButton: return tr("Back", "MouseButton");
    case Qt::ForwardButton: return tr("Forward", "MouseButton");
    case Qt::TaskButton: return tr("Task", "MouseButton");
    case Qt::ExtraButton4: return tr("Extra", "MouseButton") + "4";
    case Qt::ExtraButton5: return tr("Extra", "MouseButton") + "5";
    case Qt::ExtraButton6: return tr("Extra", "MouseButton") + "6";
    case Qt::ExtraButton7: return tr("Extra", "MouseButton") + "7";
    case Qt::ExtraButton8: return tr("Extra", "MouseButton") + "8";
    case Qt::ExtraButton9: return tr("Extra", "MouseButton") + "9";
    case Qt::ExtraButton10: return tr("Extra", "MouseButton") + "10";
    case Qt::ExtraButton11: return tr("Extra", "MouseButton") + "11";
    case Qt::ExtraButton12: return tr("Extra", "MouseButton") + "12";
    case Qt::ExtraButton13: return tr("Extra", "MouseButton") + "13";
    case Qt::ExtraButton14: return tr("Extra", "MouseButton") + "14";
    case Qt::ExtraButton15: return tr("Extra", "MouseButton") + "15";
    case Qt::ExtraButton16: return tr("Extra", "MouseButton") + "16";
    case Qt::ExtraButton17: return tr("Extra", "MouseButton") + "17";
    case Qt::ExtraButton18: return tr("Extra", "MouseButton") + "18";
    case Qt::ExtraButton19: return tr("Extra", "MouseButton") + "19";
    case Qt::ExtraButton20: return tr("Extra", "MouseButton") + "20";
    case Qt::ExtraButton21: return tr("Extra", "MouseButton") + "21";
    case Qt::ExtraButton22: return tr("Extra", "MouseButton") + "22";
    case Qt::ExtraButton23: return tr("Extra", "MouseButton") + "23";
    case Qt::ExtraButton24: return tr("Extra", "MouseButton") + "24";
    default: return "";
    }
}

Qt::MouseButton UBShortcutManager::buttonIndex(QString button)
{
    for (unsigned int index = Qt::LeftButton; index < Qt::AllButtons; index <<= 1)
    {
        Qt::MouseButton but = static_cast<Qt::MouseButton>(index);

        if (button == buttonName(but))
        {
            return but;
        }
    }

    return Qt::NoButton;
}

void UBShortcutManager::ignoreCtrl(bool ignore)
{
    if (ignore == mIgnoreCtrl) {
        return;
    }

    mIgnoreCtrl = ignore;

    for (auto& actionGroup : mActionGroups)
    {
        for (QAction* action : actionGroup.second)
        {
            if (!action->property("builtIn").toBool())
            {
                // rebuild shortcut list with or without the variants without Ctrl
                applyShortcut(action, primaryShortcut(action));
            }
        }
    }

    UBSettings::settings()->setValue("Shortcut/IgnoreCtrl", ignore);
    emit dataChanged(index(0, 2), index(rowCount(), 2));
}

QString UBShortcutManager::groupOfAction(const QAction *action) const
{
    for (auto& actionGroup : mActionGroups)
    {
        for (QAction* actionInGroup : actionGroup.second)
        {
            if (action == actionInGroup)
            {
                return actionGroup.first;
            }
        }
    }

    return QString();
}

QList<QAction *> &UBShortcutManager::actionsOfGroup(const QString &group)
{
    for (auto& actionGroup : mActionGroups)
    {
        if (actionGroup.first == group) {
            return actionGroup.second;
        }
    }

    QPair<QString,QList<QAction*>> actionGroup;
    actionGroup.first = group;
    mActionGroups << actionGroup;

    return mActionGroups.last().second;
}

QAction *UBShortcutManager::getAction(const QModelIndex &index, QString *group) const
{
    int row = index.row();

    for (auto& actionGroup : mActionGroups)
    {
        if (row == 0)
        {
            if (group)
            {
                *group = actionGroup.first;
            }

            return nullptr;
        }

        if (row <= actionGroup.second.size())
        {
            if (group)
            {
                *group = actionGroup.first;
            }

            return actionGroup.second[row - 1];
        }

        --row;
        row -= actionGroup.second.size();
    }

    if (group)
    {
        group->clear();
    }

    return nullptr;
}

void UBShortcutManager::updateSettings(const QAction *action) const
{
    if (action->objectName().isEmpty())
    {
        return;
    }

    QString key = "Shortcut/" + action->objectName();
    QString keySequence = primaryShortcut(action).toString();
    QStringList extraShortcuts = action->property(extraShortcutsProperty).toStringList();
    QString defaultSequence = action->property(defaultShortcutProperty).toString();
    int mouseButton = action->property(mouseButtonProperty).toInt();
    int tabletButton = action->property(tabletButtonProperty).toInt();

    if (keySequence == defaultSequence && extraShortcuts.isEmpty() && mouseButton == 0 && tabletButton == 0)
    {
        // back to default, delete settings
        UBSettings::settings()->setValue(key, QVariant());
    }
    else
    {
        QStringList list;
        list << QStringList(QStringList(keySequence) + extraShortcuts).join(shortcutListSeparator);
        list << QString("%1").arg(mouseButton);
        list << QString("%1").arg(tabletButton);
        UBSettings::settings()->setValue(key, list);
    }
}

void UBShortcutManager::applyShortcut(QAction *action, const QKeySequence &shortcut) const
{
    // The shortcut list of an action consists of
    // - the primary shortcut (editable in the preferences),
    // - the variant of the primary shortcut without Ctrl, if "ignore Ctrl" is active,
    // - additional shortcuts set by device profiles.
    QList<QKeySequence> shortcuts;

    action->setProperty(primaryShortcutProperty, shortcut.toString());

    if (!shortcut.isEmpty())
    {
        shortcuts << shortcut;

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
        bool hasCtrl = shortcut[0].keyboardModifiers().testFlag(Qt::ControlModifier);
#else
        bool hasCtrl = shortcut[0] & Qt::ControlModifier;
#endif

        if (mIgnoreCtrl && hasCtrl)
        {
            // add the variant without Ctrl
            shortcuts << QKeySequence(flipModifier(shortcut[0], Qt::ControlModifier));
        }
    }

    for (const QString& extra : action->property(extraShortcutsProperty).toStringList())
    {
        QKeySequence extraShortcut(extra);

        if (!extraShortcut.isEmpty() && !shortcuts.contains(extraShortcut))
        {
            shortcuts << extraShortcut;
        }
    }

    action->setShortcuts(shortcuts);
}

bool UBShortcutManager::setExtraShortcuts(const QModelIndex &index, const QList<QKeySequence> &shortcuts)
{
    QAction* action = getAction(index);

    if (!action || action->property("builtIn").toBool())
    {
        return false;
    }

    QStringList extras;

    for (const QKeySequence& shortcut : shortcuts)
    {
        if (!shortcut.isEmpty() && shortcut != primaryShortcut(action))
        {
            extras << shortcut.toString();
        }
    }

    action->setProperty(extraShortcutsProperty, extras.isEmpty() ? QVariant() : QVariant(extras));
    applyShortcut(action, primaryShortcut(action));
    updateSettings(action);
    emit dataChanged(index.siblingAtColumn(2), index.siblingAtColumn(2));
    return true;
}

void UBShortcutManager::removeAction(QObject *action)
{
    // called from QObject::destroyed, don't access the action
    beginResetModel();

    for (auto& actionGroup : mActionGroups)
    {
        actionGroup.second.removeAll(static_cast<QAction*>(action));
    }

    for (auto map : { &mMouseActions, &mTabletActions })
    {
        for (const Qt::MouseButton& key : map->keys())
        {
            if (map->value(key) == action)
            {
                map->remove(key);
            }
        }
    }

    endResetModel();
}

QAction *UBShortcutManager::actionByName(const QString &objectName) const
{
    if (objectName.isEmpty())
    {
        return nullptr;
    }

    for (auto& actionGroup : mActionGroups)
    {
        for (QAction* action : actionGroup.second)
        {
            if (action->objectName() == objectName)
            {
                return action;
            }
        }
    }

    return nullptr;
}

QModelIndex UBShortcutManager::indexOfAction(const QString &objectName, int column) const
{
    for (int row = 0; row < rowCount(); ++row)
    {
        QAction* action = getAction(index(row, 0));

        if (action && action->objectName() == objectName)
        {
            return index(row, column);
        }
    }

    return QModelIndex();
}

void UBShortcutManager::resetAll()
{
    for (int row = 0; row < rowCount(); ++row)
    {
        resetData(index(row, 0));
    }
}

QString UBShortcutManager::buttonId(Qt::MouseButton button)
{
    switch (button)
    {
    case Qt::NoButton: return QString();
    case Qt::LeftButton: return "Left";
    case Qt::RightButton: return "Right";
    case Qt::MiddleButton: return "Middle";
    case Qt::BackButton: return "Back";
    case Qt::ForwardButton: return "Forward";
    case Qt::TaskButton: return "Task";
    default:
        break;
    }

    for (int n = 4; n <= 24; ++n)
    {
        if (button == static_cast<Qt::MouseButton>(Qt::ExtraButton4 << (n - 4)))
        {
            return QString("Extra%1").arg(n);
        }
    }

    return QString();
}

Qt::MouseButton UBShortcutManager::buttonFromId(const QString &id)
{
    const QString trimmed = id.trimmed();

    if (trimmed.isEmpty())
    {
        return Qt::NoButton;
    }

    // accept aliases used by Qt and X11 documentation
    if (trimmed.compare("Extra1", Qt::CaseInsensitive) == 0)
    {
        return Qt::BackButton;
    }

    if (trimmed.compare("Extra2", Qt::CaseInsensitive) == 0)
    {
        return Qt::ForwardButton;
    }

    if (trimmed.compare("Extra3", Qt::CaseInsensitive) == 0)
    {
        return Qt::TaskButton;
    }

    for (unsigned int index = Qt::LeftButton; index < Qt::AllButtons; index <<= 1)
    {
        Qt::MouseButton button = static_cast<Qt::MouseButton>(index);

        if (trimmed.compare(buttonId(button), Qt::CaseInsensitive) == 0)
        {
            return button;
        }
    }

    return Qt::NoButton;
}

// ---------- UBActionGroupHistory ----------

UBActionGroupHistory::UBActionGroupHistory(QActionGroup *parent)
    : QObject(parent)
    , mActionGroup(parent)
    , mCurrentAction(parent->checkedAction())
    , mPreviousAction(nullptr)
    , mRevertingAction(nullptr)
{
    connect(parent, &QActionGroup::triggered, this, &UBActionGroupHistory::triggered);
}

void UBActionGroupHistory::triggered(QAction *action)
{
    if (mCurrentAction != action)
    {
        mPreviousAction = mCurrentAction;
        mCurrentAction = action;
    }
}

bool UBActionGroupHistory::keyReleased(QKeyEvent *event)
{
    int key = event->key() & ~Qt::KeyboardModifierMask;

    for (QAction* action : mActionGroup->actions())
    {
        QKeySequence keySequence = action->shortcut();

        if (keySequence.count() > 0)
        {
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
            int actionKey = action->shortcut()[0].key();
#else
            int actionKey = action->shortcut()[0] & ~Qt::KeyboardModifierMask;
#endif

            if (key == actionKey)
            {
                if (event->isAutoRepeat())
                {
                    if (!mRevertingAction)
                    {
                        mRevertingAction = mPreviousAction;;
                    }
                }
                else if (mRevertingAction)
                {
                    mRevertingAction->trigger();
                    mRevertingAction = nullptr;
                }

                return true;
            }
        }
    }

    return false;
}
