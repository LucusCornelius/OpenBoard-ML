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

#ifndef UBCLICKBEHAVIOR_H
#define UBCLICKBEHAVIOR_H

#include <QList>
#include <QObject>
#include <QString>

class QAction;

/**
 * @brief Configurable behaviour of the right and middle mouse button (and of
 * the stylus barrel buttons, which arrive as right/middle button) on the board
 * and on the desktop annotation view.
 *
 * Settings:
 *  - Shortcut/RightClickAction  (contextMenu, temporaryEraser, previousTool, selectTool, pan)
 *  - Shortcut/MiddleClickAction (pan, previousTool, nothing)
 *
 * The class also keeps a small tool history used by the "previous tool"
 * behaviour and by the "Previous tool" / "Toggle pen / eraser" actions.
 * Temporary tool switches (e.g. "temporary eraser while pressed") are not
 * recorded in this history.
 */
class UBClickBehavior : public QObject
{
    Q_OBJECT

public:
    enum Action
    {
        ContextMenu = 0,    // default behaviour of the view (context menu for right button)
        TemporaryEraser,    // eraser while the button is pressed, previous tool restored on release
        PreviousTool,       // switch to the previously used tool
        SelectTool,         // switch to the selector tool
        Pan,                // hand tool while the button is pressed
        Nothing             // ignore the button
    };
    Q_ENUM(Action)

    static UBClickBehavior* instance();

    // behaviours offered for a button (Qt::RightButton or Qt::MiddleButton)
    static QList<Action> availableActions(int button);
    static Action defaultAction(int button);
    static QString settingsKey(int button);

    static QString actionText(Action action);
    static QString actionId(Action action);
    static Action actionFromId(const QString& id, bool* ok = nullptr);

    // current behaviour for a button, ContextMenu for unsupported buttons
    Action action(int button) const;
    void setAction(int button, Action action);
    void resetActions();

    // tool history
    void connectToDrawingController();
    void activatePreviousTool();
    void togglePenEraser();

    // temporary tool switch, e.g. eraser while the right button is pressed
    void beginTemporaryTool(int tool);
    void endTemporaryTool();
    bool isTemporaryToolActive() const;

    // create additional tool actions ("Previous tool", "Toggle pen / eraser"),
    // registered with the shortcut manager in UBShortcutManager::addMainActions
    QList<QAction*> createToolActions(QObject* parent);

signals:
    void actionChanged(int button, UBClickBehavior::Action action);

private slots:
    void stylusToolChanged(int tool, int previousTool);

private:
    UBClickBehavior();
    void setTool(int tool);

    bool mConnected{false};
    bool mSuppressHistory{false};
    int mPreviousTool{-1};
    int mToolBeforeTemporarySwitch{-1};
};

#endif // UBCLICKBEHAVIOR_H
