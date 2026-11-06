# User interface styles and appearance

OpenBoard-ML has two looks for its floating controls (tool clusters, pen panel,
stylus palette, maths palette, other floating palettes, dock tabs, menus):

| Style    | Default on      | Look |
|----------|-----------------|------|
| `glass`  | macOS           | Painted "Liquid Glass": translucent, blurred backdrop, lensing rim, highlights, material adapts to the content below (see `FEATURES.md`). On macOS also the transparent title bar (`UBGlassEffect_mac.mm`). |
| `modern` | Windows, Linux  | Calm Windows 11 / Fluent like look: solid cards (light `#F9F9F9`, dark `#2C2C2C`), 8 px corners, 1 px stroke, soft shadow, no blur. Buttons with subtle 4 px hover/pressed fills; a checked button has an accent tinted fill and an accent glyph. Follows the application theme, not the board. |

Linux uses the modern style as well: painted glass imitates Apple, so it stays
the macOS look. This was a design choice, not a technical limit.

## Choosing the style

The style is decided once at start up, in one place: `UBUiStyle::style()`
(`src/gui/UBUiStyle.*`). Code asks `UBUiStyle::isGlass()` / `isModern()`.

Environment variables (for testing and previews; nothing is saved):

* `OPENBOARD_UI_STYLE=glass|modern` overrides the platform default, e.g. to
  preview the Windows look on a Mac:
  `OPENBOARD_UI_STYLE=modern OPENBOARD_THEME=dark open OpenBoard.app`
* `OPENBOARD_QT_STYLE=<key>` forces the Qt widget style (`fusion`, `windows11`, ...).
* `OPENBOARD_THEME=light|dark|auto` overrides the saved appearance until an
  appearance is chosen in the menu.

## Appearance (dark mode)

The board menu (the `...` button of the right tool cluster) has an
**Appearance** submenu: *Follow System*, *Light*, *Dark*. The choice is saved
(`App/ThemeMode`: 0 = follow system, 1 = light, 2 = dark), applied at once to
the whole application and mirrored in Preferences > Display. In *Follow
System* the application follows live changes of the system colour scheme
(Qt 6.5+).

For native code: `UBThemeManager::themeChanged(bool isDark)` is emitted after
the palette and the stylesheets were applied, `themeModeChanged(int mode)`
when the chosen mode changes. `UBThemeManager::instance()->isDarkMode()` gives
the current theme.

## Modern style details

* Painting: `UBModernStyle::paintPanel` / `shadowImage`, used by
  `UBFloatingPalette::paintModernBackground` and the dock tabs.
* Stylesheet: `UBModernStyle::styleSheet` replaces the glass layer; the theme
  stylesheets are mapped to neutral greys and the system accent
  (`neutralizeThemeStyleSheet`).
* Palette: Fluent neutrals (`#F3F3F3` / `#202020` windows) with the system
  accent, darker on light and lighter on dark (`UBModernStyle::accent`).
* Windows only (`#ifdef Q_OS_WIN`, links `dwmapi`):
  * title bars follow the application theme (`DWMWA_USE_IMMERSIVE_DARK_MODE`)
    and windows ask for rounded corners (Windows 11), also dialogs opened later;
  * with Qt 6.8+ the Qt colour scheme follows the chosen appearance and the
    `windows11` widget style is used for dialogs and preferences (Fusion
    otherwise);
  * font: Segoe UI Variable when installed (Windows 11), else Segoe UI.
