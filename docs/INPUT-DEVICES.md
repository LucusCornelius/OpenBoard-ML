# Input devices: shortcuts, mouse buttons, pen buttons and tablet hotkeys

OpenBoard commands can be triggered from

* **keyboard shortcuts** (key sequences),
* **mouse buttons** other than the left and right button (middle, back, forward, extra buttons 4–24),
* **stylus buttons** (the barrel/side buttons of a pen),
* **tablet hotkeys / ExpressKeys**, which the tablet driver translates into keystrokes or mouse buttons.

In addition, the behavior of the **right** and **middle** mouse button (and of the stylus buttons that arrive as right/middle click) can be configured, and ready-made **device profiles** can be applied.

Everything is configured in *Preferences → Shortcut*.

## The shortcut table

Every command is listed with its key sequence, mouse button and tablet (stylus) button. Commands in the group *Built-in (not editable)* are fixed.

To change an assignment:

1. Select the command in the table.
2. Click **Record** (or double-click the row).
3. Do one or more of:
   * press the key sequence (it is shown in *Key Sequence*),
   * click with the mouse button you want to use, anywhere in the OpenBoard window (shown in *Mouse Button*),
   * press the stylus button you want to use (shown in *Stylus Button*); hover with the pen over the OpenBoard window and press the side button.
4. Click **Record** again to accept, or **Abort**. **Reset** restores the default of the selected command.

A key sequence or button can only be assigned to one command; conflicts are reported below the fields.

Notes:

* The left mouse button and the pen tip are always used for drawing and can't be assigned.
* The right mouse button can't be assigned in the table; its behavior is configured with *Right click / lower pen button* (see below).
* A mouse or stylus button that is assigned to a command triggers the command once on press. Its double click and release events are consumed, so the button never draws, selects or opens a context menu.
* Assigned buttons work in all modes (board, desktop annotation, documents, web). The command must be available in that mode.
* *Active keyboard shortcuts without pressing Ctrl key* also accepts all Ctrl shortcuts without Ctrl.

Assignments are stored in the user settings as `Shortcut/<command object name>` = `[key sequences, mouse button, tablet button]`. Additional key sequences that a device profile adds are stored together with the main one, separated by `; `.

## Right click, middle click and pen buttons

On the board and in the desktop annotation view, the right and middle buttons can do the following:

| Setting | Options | Default |
|---|---|---|
| Right click / lower pen button (`Shortcut/RightClickAction`) | Context menu (default) · Temporary eraser (while pressed) · Previous tool · Select tool · Pan (hand) | Context menu |
| Middle click / upper pen button (`Shortcut/MiddleClickAction`) | Pan · Previous tool · Nothing | Nothing |

* **Temporary eraser** switches to the eraser when the button is pressed and back to the previous tool when it is released. With a mouse you erase while you drag with the right button held. With a pen you can press the side button while the tip touches the surface, or press it first and then put the tip down. While hovering, the pen doesn't erase.
* **Pan (hand)** works the same way, using the hand tool.
* **Previous tool** switches to the previously used tool, so pressing it again toggles between two tools. Before any tool change it toggles between pen and eraser.
* **Select tool** switches to the selector.
* If the right button does anything other than *Context menu*, the right-click context menu on the board is suppressed. The context-menu key on the keyboard still works.
* These settings apply to every tool. Settings for individual tools aren't supported.
* If a stylus button is assigned to a command in the shortcut table, the command takes precedence over these settings.

Stylus buttons normally arrive as right click (lower button on Windows/macOS) or middle click, depending on the driver configuration (see below). That's why the settings are labeled *lower pen button* and *upper pen button*. With the default Linux `xf86-input-wacom` configuration it's the other way round: the lower button is middle and the upper button is right.

## Device profiles

*Device profile* plus **Apply device profile…** loads a preset. A confirmation dialog shows the profile's description, including the setup needed in the tablet driver. Only the commands and settings listed in the profile change; everything else stays as it is. If a key or button in the profile is already used by another editable command, it is removed there and you get a note about it.

**Reset all shortcuts** restores the defaults of every command and of the click behavior.

### Built-in profiles

#### Keyboard default

Restores all default key sequences, removes all mouse and stylus button assignments and resets the click behavior: right = context menu, middle = nothing.

#### Wacom Intuos M (ExpressKeys)

The Intuos M (CTL-6100/CTL-6100WL) has 4 ExpressKeys. The Wacom driver sends a keystroke for each one. This profile adds a dedicated, unambiguous key sequence as an *additional* shortcut, so the usual keyboard shortcuts (Ctrl+Z, PgUp, PgDown, …) keep working:

| ExpressKey | Keystroke to configure (Windows/Linux) | Keystroke on macOS | OpenBoard command |
|---|---|---|---|
| 1 | Ctrl+Shift+F9 | Cmd+Shift+F9 | Undo |
| 2 | Ctrl+Shift+F10 | Cmd+Shift+F10 | Toggle pen / eraser |
| 3 | Ctrl+Shift+F11 | Cmd+Shift+F11 | Next page |
| 4 | Ctrl+Shift+F12 | Cmd+Shift+F12 | Previous page |

Pen buttons:

| Pen button | Driver setting | OpenBoard behavior (set by the profile) |
|---|---|---|
| lower side button | Right Click | Temporary eraser (while pressed) |
| upper side button | Middle Click | Pan (while pressed) |

These keystrokes were chosen because they're layout independent (function keys instead of digits or letters), available on every OS, easy to enter in Wacom Center, and not used by OpenBoard or common system shortcuts. Ctrl+Alt+digit was avoided because Ctrl+Alt is AltGr on many European layouts.

#### Medion P82030 / Waltop hotkey tablet

Conservative preset for the Aldi/Medion tablets. It sets Undo = Ctrl+Z and Redo = Ctrl+Y (the standard keys the hotkey software sends), right click / lower pen button = temporary eraser, and middle click (the pen rocker on Linux) = previous tool. See [Medion notes](#medion-p82030--waltop-tablets).

### Own profiles

Profiles are JSON files. OpenBoard reads them from

1. the built-in resources (`resources/etc/shortcutProfiles`, compiled in),
2. `<OpenBoard etc directory>/shortcutProfiles` (for administrators),
3. `<user data directory>/shortcutProfiles`, for example `~/.local/share/OpenBoard/shortcutProfiles` on Linux.

A file with the same name as a built-in profile replaces it. Format:

```json
{
    "name": "My tablet",
    "description": "Shown before the profile is applied",
    "order": 50,
    "resetAll": false,
    "rightClickAction": "temporaryEraser",
    "middleClickAction": "pan",
    "shortcuts": [
        { "action": "actionUndo", "extraKeys": [ "Ctrl+Shift+F9" ] },
        { "action": "actionRedo", "keys": "Ctrl+Y" },
        { "action": "actionBack", "mouseButton": "Back" },
        { "action": "actionForward", "mouseButton": "Forward", "tabletButton": "" }
    ]
}
```

* `action` is the object name of the command. Examples: `actionUndo`, `actionRedo`, `actionBack` (previous page), `actionForward` (next page), `actionNewPage`, `actionPen`, `actionEraser`, `actionMarker`, `actionSelector`, `actionHand`, `actionPreviousTool`, `actionTogglePenEraser`, `actionClearPage`, `actionColor0`…`actionColor9`, `actionLineSmall`/`Medium`/`Large`, `actionEraserSmall`/`Medium`/`Large`, `actionPodcastRecord`, `actionDesktop`, `actionBoard`. The full list is in `resources/forms/mainWindow.ui`.
* `keys` replaces the main key sequence. An empty string removes it.
* `extraKeys` sets additional key sequences, which is useful for keystrokes sent by tablet hotkeys.
* Button names for `mouseButton` and `tabletButton`: `Middle`, `Right` (stylus only), `Back`, `Forward`, `Task`, `Extra4` … `Extra24`. An empty string removes the assignment.
* `rightClickAction`: `contextMenu`, `temporaryEraser`, `previousTool`, `selectTool`, `pan`. `middleClickAction`: `pan`, `previousTool`, `nothing`.
* Key sequences use Qt's portable text format (`Ctrl+Shift+F9`, `PgDown`, `Ctrl++`).

## Wacom setup

### Windows and macOS (Wacom Center / Wacom Tablet Properties)

1. In OpenBoard, apply the profile *Wacom Intuos M (ExpressKeys)*.
2. Open **Wacom Center**. On older drivers, open **Wacom Tablet Properties** (Windows) or **System Settings → Wacom Tablet** (macOS).
3. Optional but recommended: add OpenBoard as an application in the application list, so the settings below only apply to OpenBoard.
4. Select **ExpressKeys**. For each key, choose **Keystroke…** from the drop-down. In the keystroke dialog, press the key combination from the table above (for example Ctrl+Shift+F9; on macOS ⌘⇧F9). Give it a name such as "OpenBoard Undo" and confirm with OK.
5. Select **Pen**. Set the *lower* side button to **Right Click** and the *upper* side button to **Middle Click**.
6. On Windows, keep **Use Windows Ink** enabled in the pen's mapping settings. OpenBoard (Qt 6) needs Windows Ink for pressure.

On macOS, Qt maps the Command key to "Ctrl". OpenBoard therefore expects ⌘ (Command), not the Control key, in these keystrokes.

### Linux (X11, xf86-input-wacom)

List the devices:

```sh
xsetwacom --list devices
# Wacom Intuos M Pen stylus    id: 10  type: STYLUS
# Wacom Intuos M Pad pad       id: 11  type: PAD
```

The 4 ExpressKeys of the Intuos M are pad buttons 1, 2, 3 and 8 (X buttons 4–7 are reserved for scrolling). Map them to the keystrokes of the profile:

```sh
PAD="Wacom Intuos M Pad pad"
xsetwacom set "$PAD" Button 1 "key +ctrl +shift f9 -shift -ctrl"
xsetwacom set "$PAD" Button 2 "key +ctrl +shift f10 -shift -ctrl"
xsetwacom set "$PAD" Button 3 "key +ctrl +shift f11 -shift -ctrl"
xsetwacom set "$PAD" Button 8 "key +ctrl +shift f12 -shift -ctrl"
```

Pad buttons can also be mapped to extra mouse buttons and assigned in the OpenBoard shortcut table (column *Mouse Button*). X button 8 = Back, 9 = Forward, 10 = Task, 11 … = Extra4 …. Never use X buttons 1–7 for this: 1 = left, 2 = middle, 3 = right, 4–7 = wheel.

```sh
xsetwacom set "$PAD" Button 1 "button 8"   # -> "Back"   (e.g. previous page)
xsetwacom set "$PAD" Button 2 "button 9"   # -> "Forward" (e.g. next page)
xsetwacom set "$PAD" Button 3 "button 10"  # -> "Task"
xsetwacom set "$PAD" Button 8 "button 11"  # -> "Extra4"
```

Pen side buttons: by default the lower button is X button 2 (middle) and the upper button is X button 3 (right). To get the same layout as on Windows/macOS (lower = right click):

```sh
STYLUS="Wacom Intuos M Pen stylus"
xsetwacom set "$STYLUS" Button 2 3
xsetwacom set "$STYLUS" Button 3 2
```

`xsetwacom` settings are lost after a restart or replug. Put them in a login script, or in your desktop's tablet settings.

### Linux (Wayland)

On Wayland, `xsetwacom` doesn't work. Configure the pad buttons as keystrokes in the desktop settings: GNOME *Settings → Wacom Tablet → Map Buttons*, KDE *System Settings → Drawing Tablet → Pad*. Use the keystrokes from the table above.

## Medion P82030 / Waltop tablets

What we could find out (see the sources below):

* The Medion P82030 (MD 86336) is a 12.1" USB graphics tablet. Aldi sold it from 2012. It's made by **Waltop**, not by UC-Logic. The older Medion P82012 (MD 85637) is the Waltop Slim Tablet 12.1" (USB ID `172f:0034`), also sold as Genius G-Pen F610 and Trust Slimline among others. On Linux it's supported by the kernel since 2.6.37 (`hid-waltop`).
* The hotkeys are **printed fields on the tablet surface**, not physical buttons. On Windows, the Medion/Waltop **MacroKeyManager** software turns a pen tap on a field into an action. According to the manual these include Undo, Cut, Copy, Paste, Open, Save, Close, and launching PowerPoint or Internet Explorer. The overlay sheet can be exchanged. The exact keystrokes the MacroKeyManager sends are not documented publicly. We assume the standard shortcuts (Ctrl+Z, Ctrl+X, Ctrl+C, Ctrl+V, Ctrl+O, Ctrl+S, …). Cut, Copy, Paste and Undo then work in OpenBoard without any change.
* On Windows 8/8.1/10 the MacroKeyManager doesn't detect the tablet without a patched driver package.
* On Linux the hotkey fields don't work: the pen just moves the cursor there. Users reported (`xev`) that the pen tip is button 1 and that the side rocker sends button 2 (middle) in both directions.

That's why the profile is conservative:

* It keeps the standard keys for Undo (Ctrl+Z) and Redo (Ctrl+Y).
* Right click / lower pen button = temporary eraser.
* Middle click = previous tool. On Linux the pen rocker toggles between pen and eraser.

Caveat: Ctrl+S is the *Snap* shortcut in OpenBoard, and Ctrl+W switches to the web browser. If the "Save" or "Close" hotkey sends these keys, it triggers those commands. Remove or change these shortcuts in the table if this bothers you.

If your tablet's hotkeys send other key combinations, find out what they send (for example with the key-sequence field while recording in the shortcut table, or with `xev` on Linux) and assign them to the commands you want.

## Limitations

* The left mouse button and the pen tip can't be assigned. Linux pad buttons that send X button 1–3 must be remapped (see above).
* The right mouse button can't be assigned to a command in the table. It is controlled by the right-click setting. Stylus "Right" can be assigned.
* The right/middle click settings apply to every tool. They affect the board and the desktop annotation view, but not the document or web views. In desktop mode with the selector tool, clicks pass through to the desktop, so the settings have no effect there.
* Switching the tool (temporary eraser, previous tool, select tool) deselects the selected items, the same as switching tools in the stylus palette.
* Pressing a stylus button while hovering only works if the platform reports it. Windows Ink and X11 do; some drivers send nothing until the tip touches the surface.
* The keystrokes an ExpressKey sends depend on the driver configuration. OpenBoard can't read or change the Wacom driver settings.

## For developers: registering commands

Other modules can register their actions so that users can assign shortcuts and buttons to them, also after startup:

```cpp
QAction* action = new QAction(tr("My command"), parent);
action->setObjectName("actionMyCommand");     // required, used as settings key
action->setShortcut(QKeySequence("Ctrl+Shift+M")); // becomes the default
UBShortcutManager::shortcutManager()->addActions(tr("My module"), { action }, UBApplication::mainWindow);
```

The manager loads the user's settings for the action, keeps it in the shortcut table and removes it automatically when the action is destroyed.

## Sources (Medion research)

* ubuntuusers.de: [Medion P82030 Grafiktablett](https://forum.ubuntuusers.de/topic/medion-p82030-grafiktablett/), [USB-Grafiktablett MEDION P82030 (MD 86336)](https://forum.ubuntuusers.de/post/6285842/), [MEDION MD 85637 USB Grafiktablett P82012](https://forum.ubuntuusers.de/topic/medion-md-85637-usb-grafiktablett-p82012/)
* Manual: [manualslib.de – Medion P82030](https://www.manualslib.de/manual/53235/Medion-P82030.html)
* [Medion / AIPTEK tablet MacroKeyManager fix for Windows 8/8.1/10](https://misc.daniel-marschall.de/patches/medion_tablet/)
* DIGImend: [Waltop Slim Tablet 12.1"](https://digimend.github.io/tablets/Waltop_Slim_Tablet_12.1_inch/)
* Ubuntu Forums: [HOW TO set up a Waltop tablet](https://ubuntuforums.org/showthread.php?t=1595648)
