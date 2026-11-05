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


#ifndef UBSHORTCUTMANAGER_H
#define UBSHORTCUTMANAGER_H

#include <QAbstractTableModel>
#include <QList>
#include <QMap>
#include <QMouseEvent>
#include <QPair>
#include <QSet>
#include <QTabletEvent>

class QAction;
class QActionGroup;
class UBMainWindow;
class UBActionGroupHistory;

class UBShortcutManager : public QAbstractTableModel
{
    Q_OBJECT

private:
    UBShortcutManager();

public:
    static UBShortcutManager* shortcutManager();

    enum {
        ActionRole = Qt::UserRole,      // bool, true if row contains editable action
        GroupHeaderRole,                // bool, true if row is group header
        PrimaryShortcutRole             // QString, primary shortcut, only valid on column 2
    };

    /**
     * @brief Register actions with the shortcut manager.
     *
     * Public API for other modules: actions can be registered at any time, also
     * after startup, e.g.
     *
     *     UBShortcutManager::shortcutManager()->addActions(tr("My module"), { action1, action2 }, widget);
     *
     * - Every action needs a unique objectName(), it is used as settings key
     *   "Shortcut/<objectName>" to persist user defined key sequences and
     *   mouse/tablet buttons. Actions without objectName are listed but their
     *   settings are not persisted.
     * - The shortcut set on the action when it is registered is used as default.
     * - If a widget is given, the action is added to the widget so that its key
     *   sequence works while the widget (or its window) is active.
     * - Actions are removed automatically from the manager when they are destroyed.
     * - An action already registered in another group is moved to the "Common" group.
     */
    void addActions(const QString& group, const QList<QAction*> actions, QWidget* widget = nullptr);
    void addMainActions(UBMainWindow* mainWindow);

    void addActionGroup(QActionGroup* actionGroup);
    void removeActionGroup(QActionGroup* actionGroup);

    // Handle press, double click and release of mouse and tablet buttons. A button
    // mapped to an action triggers the action on press, the matching double click
    // and release events are consumed so that the views never see them.
    bool handleMouseEvent(QMouseEvent* event);
    bool handleTabletEvent(QTabletEvent* event);
    bool handleKeyReleaseEvent(QKeyEvent* event);

    // access to registered actions, e.g. for shortcut profiles
    QAction* actionByName(const QString& objectName) const;
    QModelIndex indexOfAction(const QString& objectName, int column = 0) const;
    void resetAll();

    // language independent button identifiers ("Middle", "Back", "Forward", "Extra4", ...)
    static QString buttonId(Qt::MouseButton button);
    static Qt::MouseButton buttonFromId(const QString& id);

    // QAbstractTableModel overrides
    virtual int rowCount(const QModelIndex &parent = QModelIndex()) const Q_DECL_OVERRIDE;
    virtual int columnCount(const QModelIndex &parent = QModelIndex()) const Q_DECL_OVERRIDE;
    virtual QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const Q_DECL_OVERRIDE;
    virtual QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const Q_DECL_OVERRIDE;
    virtual bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) Q_DECL_OVERRIDE;

    bool resetData(const QModelIndex &index);
    // additional key sequences of an action, e.g. keystrokes sent by tablet ExpressKeys
    bool setExtraShortcuts(const QModelIndex &index, const QList<QKeySequence>& shortcuts);
    bool checkData(const QModelIndex &index, const QVariant &value) const;
    bool hasCtrlConflicts(const QKeySequence& additionalShortcut = QKeySequence()) const;

    static QString buttonName(Qt::MouseButton button);
    static Qt::MouseButton buttonIndex(QString button);

public slots:
    void ignoreCtrl(bool ignore);

private:
    QString groupOfAction(const QAction* action) const;
    QList<QAction*>& actionsOfGroup(const QString& group);
    QAction* getAction(const QModelIndex& index, QString* group = nullptr) const;
    void updateSettings(const QAction* action) const;
    void applyShortcut(QAction* action, const QKeySequence& shortcut) const;
    void removeAction(QObject* action);

private:
    QList<QPair<QString,QList<QAction*>>> mActionGroups;
    QMap<Qt::MouseButton, QAction*> mMouseActions;
    QMap<Qt::MouseButton, QAction*> mTabletActions;
    QMap<QActionGroup*, UBActionGroupHistory*> mActionGroupHistoryMap;
    bool mIgnoreCtrl;
    QSet<int> mPressedMouseButtons;          // mapped mouse buttons currently pressed
    QSet<int> mPressedTabletButtons;         // mapped tablet buttons currently pressed
    QSet<int> mPendingMouseReleaseButtons;   // mouse release events the platform may still send for a tablet button

    static UBShortcutManager* sShortcutManager;
};

class UBActionGroupHistory : public QObject
{
    Q_OBJECT

public:
    UBActionGroupHistory(QActionGroup* parent);

public slots:
    void triggered(QAction* action);
    bool keyReleased(QKeyEvent* event);

private:
    QActionGroup* mActionGroup;
    QAction* mCurrentAction;
    QAction* mPreviousAction;
    QAction* mRevertingAction;
};

#endif // UBSHORTCUTMANAGER_H
