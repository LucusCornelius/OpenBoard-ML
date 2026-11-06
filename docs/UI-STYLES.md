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
  `OPENBOARD_UI_STYLE=modern OPENBOARD_THEME=dark open OpenBoard-ML.app`
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

The board follows the appearance: in *Dark* the board is dark too (`#1C1C1E`,
white ink), so the adaptive glass panels become dark glass with white glyphs on
macOS. *Preferences > Display > Board follows dark mode* (default on) keeps a
white board in the dark appearance. The saved pages are not changed; see
`BACKGROUNDS.md`, *Dark appearance*.

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

## Document mode

The documents explorer (Documents button) uses the same flat language in both
styles. All colours and radii come from `UBGlassStyle::documentModeColors(dark)`
(glass on macOS, Fluent values for the modern style); the stylesheet block is
`UBGlassStyle::documentModeStyleSheet(dark)`, appended by both
`UBGlassStyle::styleSheet` and `UBModernStyle::styleSheet`.

* Layout: the document toolbar and the page grid share one flat content
  background (light `#F0F0F3`, dark graphite `#161618`; modern: Fluent window
  neutrals). The document tree sits in a floating sidebar card (white / `#242427`,
  12 px corners and a hairline; modern: card surface, 8 px). The splitter is an
  8 px transparent gap.
* Tree (`UBDocumentTreeView::drawRow` / `drawBranches`, `UBDocumentTreeItemDelegate`):
  28 px rows, one rounded pill per row: hover = subtle fill, selection = solid
  accent pill with white text (glass) or a tinted fill with a short accent
  indicator (modern), grey while the tree has no focus. Chevrons instead of
  branch lines, no horizontal scrolling (the name is elided, the date sits right
  aligned in local time). Monochrome glyphs from `resources/images/glass/`:
  `myDocuments`, `folder`, `erase` (Trash) in the accent colour, `document`,
  `favorite`, `documentCurrent` (the document open on the board, accent).
* Drag and drop: the folder (or, for pages, the document) receiving the drop is
  shown as an accent outlined pill; no line between rows.
* Rename: the inline field has an accent outline; a name that already exists
  gets a red outline (`QLineEdit[invalid="true"]`).
* Controls: sort box, sort direction button and search field are flat rounded
  fills (8 px, modern 4 px); the search field has a search glyph.
* Pages (`UBThumbnail`, also in the board's page list): flat cards with 6 px
  corners, a hairline and a soft shadow; the selection is a 3 px accent ring
  with a small gap; the page number uses the secondary text colour, the accent
  when selected. Drop position between pages: a flat accent bar.
* Zoom: thin capsule slider with zoom out / zoom in glyph buttons at the
  bottom right. Empty state: a glyph and "No document selected" when no
  document is selected. Messages (copying, importing) appear as a glass capsule
  at the bottom centre of the page grid.
