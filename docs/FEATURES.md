# OpenBoard-ML: added features

This fork adds the features below on top of upstream OpenBoard `dev` (1.8.0 development
branch). All features are implemented with Qt only and build on Windows, macOS and Linux
with the normal upstream build instructions (CMake or qmake); no new dependencies.

| Feature | Where | Details |
|---|---|---|
| Any colour + multiple palettes | Colour wheel button after the swatches; right-click / long-press a swatch; palette chevron before the swatches | [Colours](#colours-and-palettes) |
| Keyboard, mouse-button, pen-button and tablet shortcuts, device profiles | Preferences → Shortcut | [docs/INPUT-DEVICES.md](INPUT-DEVICES.md) |
| Page backgrounds in real centimetres (blank, lined, 1 cm, 0.5 cm, black) | Backgrounds button; Preferences → Grid | [docs/BACKGROUNDS.md](BACKGROUNDS.md) |
| Maths notation palette, typeset like LaTeX (√, ∫, Σ, lim, brackets, Greek, …) | "Maths" toolbar button (show/hide) | [Maths](#maths-notation-palette), [docs/MATHS.md](MATHS.md) |
| Shapes tool and text boxes (Freeform-like) | Stylus palette, after the Line tool; text item title bar | [Shapes](#shapes-and-text-boxes) |

## Colours and palettes

* The **colour wheel button** after the colour swatches opens a stylus-friendly picker:
  hue/saturation area, brightness bar, quick colours, hex and RGB fields, **More…**
  (system colour dialog), **Use** (draw with this colour now, for pen or marker) and
  **Add to palette** (appends a swatch to the active palette).
* **Right-click or long-press a swatch** to replace that swatch's colour ("Set color N").
* **Palettes**: the picker has a palette drop-down and a **Manage…** button
  (also Preferences → Pen / Marker → *Color palettes…*): create, rename, duplicate,
  delete palettes; add, edit, remove and reorder colours; palette size 5–10.
  Dark-background variants are derived automatically (near-black becomes white).
* **Palette switch**: a small chevron directly before the swatches in the pen panel.
  A click switches to the next palette (wraps around); the swatches and the palette size
  follow at once. Right-click or press and hold (500 ms, stylus friendly) opens a compact
  menu with all palettes (name, a row of their colours, a check mark on the active one) and
  *Manage palettes...*. The tooltip names the active palette; the chevron is hidden while
  there is only one palette.
* Actions `Next Color Palette` / `Previous Color Palette` can get a shortcut in
  Preferences → Shortcut (group *Colors*); the chevron uses the same action, so they stay in sync.
* Settings: `Board/ColorPalettes` (JSON), `Board/ActiveColorPalette`.

## Input devices

See [INPUT-DEVICES.md](INPUT-DEVICES.md). Summary:

* Every command can have a key sequence, an extra mouse button (middle, back, forward,
  extra 4–24) and a stylus button. Mapped buttons never draw.
* **Right click / lower pen button** can be: context menu, temporary eraser, previous tool,
  select tool or pan. **Middle click / upper pen button**: pan, previous tool or nothing.
* **Device profiles**: *Keyboard default*, *Wacom Intuos M (ExpressKeys)* and
  *Medion P82030 / Waltop hotkey tablet*. Own profiles are JSON files in the user data
  directory `shortcutProfiles/`.
* Wacom Intuos M: configure the four ExpressKeys in Wacom Center as
  Ctrl+Shift+F9 … F12 (Cmd on macOS) → Undo, Toggle pen/eraser, Next page, Previous page.

## Backgrounds

See [BACKGROUNDS.md](BACKGROUNDS.md). Summary:

* The Backgrounds palette shows **Blank, Lined, Grid 1 cm, Grid 0.5 cm** and the
  light/dark switch (dark = black page, pen turns white automatically).
* Other rulings (Seyes, stave, German primary school, isometric, …) are off by default;
  enable them with *Show in palette* in Preferences → Grid.
* Grid sizes are real centimetres on the control screen. If your screen reports a wrong
  physical size, calibrate it in Preferences → Grid → *Screen calibration* (measure the
  10 cm line).
* Actions `Blank / Lined / Grid 1 cm / Grid 0.5 cm background` and `Light/dark mode`
  can get shortcuts (group *Background*).

## Maths notation palette

* Toolbar button **Maths** shows/hides a floating palette with tabs *Calculus*, *Algebra*,
  *Greek* and *Functions*: √, fractions, ∫ ∬ ∮ with bounds, Σ/Π with bounds, lim, d/dx,
  ∂/∂x, f(x), f′(x), dx dy dt, brackets, powers/indices, relations and arrows, sets, Greek
  letters, sin/cos/tan/ln/log, eˣ, vectors.
* Symbols and formulas are **typeset like LaTeX**: glyphs of the bundled OpenType maths font
  *Latin Modern Math* (the Computer Modern look of TeX, GUST Font License) laid out with
  TeX's rules from the font's MATH table (script sizes 70 %/50 %, script, fraction and
  radical shifts, rule thickness, maths axis, larger operators and delimiters, TeX spacing
  between atoms). Variables and lower case Greek italic; digits, sin/cos/ln/lim, the d of dx
  and capital Greek upright. Research, sources and rules: [docs/MATHS.md](MATHS.md).
* They are written as **filled ink** in the current pen colour (one strokes group per
  formula): they follow the light/dark page colours, erase, select, move, undo, save and
  export to PDF like pen strokes. Tap a symbol, then tap the board where it should go; a
  grey preview follows the pointer.
* **Ink text**: type e.g. `f(x) = x^2 + 1/(x-1)` or `int_0^1 sqrt(x) dx` and press Write.
  Syntax: `^` `_` for scripts, `'` primes, `a/b` stacked fraction (`//` inline slash),
  `sqrt() int sum prod lim vec()`, Greek names (`alpha`, `pi`),
  `inf partial -> => <= >= != pm times cdot in RR NN ...`; spaces are ignored as in TeX,
  two spaces leave a wide space (`\quad`).
* **Size** slider: font size 12–96 pt, default **28 pt** (1 em ≈ 1 cm of the page grid,
  capitals 6.8 mm, x-height 4.3 mm: one ruled line, like handwriting).
* **Display** button: TeX display style (full size fractions, limits above and below sums,
  large integrals) instead of the default text style (compact, like inline maths).
* Settings: `Board/ShowMathPalette`, `Board/MathPalettePosition`, `Board/MathPaletteTab`,
  `Board/MathFontSize` (pt), `Board/MathDisplayStyle`. The old `Board/MathInkSizeCm` and
  `Board/MathInkJitter` are removed on start.
* Font: `resources/math/latinmodern-math.otf` (unmodified) with `GUST-FONT-LICENSE.txt` and
  `README-Latin-Modern-Math.txt`.

## Shapes and text boxes

* **Shapes tool** in the stylus palette (after Line). While active the toolbar shows the
  kind (rectangle, rounded rectangle, ellipse, triangle, line, arrow) and a **Fill** toggle.
  Drag to draw; Shift constrains to square / circle / 45°; snapping follows the Snap
  setting. Stroke colour and width follow the pen.
* Shapes are items: select, move, resize (stroke width stays constant), rotate, copy,
  paste, group, z-order, delete, undo/redo. The item menu offers line colour, width, fill
  and arrow heads. *Erase items* removes them, the stroke eraser does not.
* Shapes are stored as standard SVG (`rect`, `ellipse`, `polygon`, `line`, `path`) with
  `ub:` attributes in the page and reload correctly; PDF export renders them.
* **Text boxes**: the text item title bar has a box button that cycles
  none → border → border + background → background. Also in the item menu.
  Stored as `ub:boxBorder` / `ub:boxFill`.
* Settings: `Board/ShapeKind`, `Board/ShapeFill`.

## User interface (Liquid Glass)

* **Freeform-style layout**: in board mode there is no toolbar strip. The board runs edge to
  edge and the controls float above it as pieces of glass (the "control layer"):
  * top-left cluster: Undo, Redo | Backgrounds, Maths | Pages, Previous, Next, Erase;
  * top-right cluster: Web, Documents, Desktop | OpenBoard menu (Board while not on the board);
  * pen panel capsule at the top centre, tool capsule at the bottom centre; Backgrounds and
    Maths palettes float. Groups are separated by thin hairlines; the active item is a solid
    accent capsule, hover/press show a light glass pill.
  * Preference *toolbar at the bottom*: clusters and pen panel move to the bottom edge, the
    tool capsule floats above them. Preference *toolbar text*: small labels under the
    cluster icons; icon only is the default (`App/ToolBarDisplayText=false`).
  * The board toolbar QActions stay alive (shortcuts, menus, states); web and document mode
    keep their (restyled) toolbars. Dock palette tabs are kept clear of the clusters.
* **Painted Liquid Glass** (all platforms, `UBGlassStyle::paintGlass`, `UBFloatingPalette`):
  * always translucent: the board behind each panel is captured, blurred (about 20 px, at
    quarter resolution) and its saturation raised (x1.2);
  * adaptive luminosity: the mean luminance under the panel selects the light material
    (white tint, dark glyphs) over light content and the dark material (black tint, white
    glyphs) over dark content, independently of the app theme (the theme decides only when
    nothing can be captured, e.g. over the desktop in annotation mode). The panel publishes
    the material in the property `glassOnDark`; stylesheet colours and tinted icons follow it;
  * lensing rim: a 14 px band along the edge shows less blurred content from slightly
    outside the panel, as if bent by thick glass;
  * flat like Liquid Glass: one even tint, one thin light hairline edge, a soft shadow; no
    bevel, gradients or specular highlights;
  * capsules for single-row panels, 26 px radius otherwise; 40 px buttons with 22 px
    glyphs; flat round 24 px colour swatches (thin neutral hairline); capsule sliders and fields; the
    maths palette is compact (36 px glyphs, segmented tabs, size and ink text in one row).
* **macOS** (`src/gui/UBGlassEffect_mac.mm`):
  * window: `Qt::ExpandedClientAreaHint` + `Qt::NoTitleBarBackgroundHint` (Qt 6.9+), no title,
    no separator: the board runs under the transparent title bar, the clusters sit below the
    traffic lights, the web and document toolbars start to the right of them (unified title
    bar). The window `NSAppearance` follows the app theme, also when it changes at runtime.
  * panels: real system material behind every floating panel, the dock tabs and the open
    dock bodies (pages, library: a floating glass sidebar with frosted cards inside):
    `NSGlassEffectView` on macOS 26+, `NSVisualEffectView` before. Qt paints all widgets of a
    window into one backing store, so a native view can never show *behind* a Qt widget.
    The panels therefore stay normal Qt widgets (layout, mouse, keyboard unchanged) with a
    graphics effect that paints nothing into the window and hands the rendered controls
    (transparent background) to a native overlay above the board: material view, a soft
    shadow outside the body and a thin even light hairline (both flat, also visible when the
    window is not key and the system flattens its glass), controls image; transparent for the
    mouse. Frame, corner radius, visibility and stacking follow the panel.
  * adaptive like the system glass of small controls: the luminance of the content below a
    floating panel selects light glass with dark glyphs (over bright content, e.g. the white
    board, also in the dark theme) or dark glass with white glyphs (over dark content, also in
    the light theme). The dark glass gets one flat 30 % dark tint. Dock bodies and tabs keep
    the material of the theme. `OPENBOARD_GLASS_ADAPTIVE=0` makes the floating panels follow
    the theme instead (dark theme over a white board: a flat dark grey).
  * while a panel shows GPU composed content (a web view in the library) it paints itself
    (painted glass) and its overlay is hidden; panels in translucent windows (desktop
    annotation) always keep the painted glass.
  * the OpenBoard (⋯) menu and its sub menus: translucent Qt menus on the native menu
    material (`NSVisualEffectView`, menu material, rounded corners, system shadow), in the
    appearance of the app theme.
  * environment switches: `OPENBOARD_PANEL_GLASS=glass|vibrancy|painted` forces a panel
    material (`OPENBOARD_NATIVE_PANEL_GLASS=0` = painted); `OPENBOARD_NO_NATIVE_GLASS=1`
    disables all native chrome (normal title bar, painted glass); `OPENBOARD_THEME=light|dark|auto`
    overrides the theme without touching the saved setting.
* Backgrounds palette: compact rounded page swatches (60 x 46) with the ruling and an accent
  ring for the current background, also used in the preferences.
* Monochrome SF Symbols like icons (`resources/images/glass/`, tinted per material) also for
  the pen, eraser and marker tools, the dock tabs, the document toolbar, the web tools
  palette and the OpenBoard menu.
* Dark/light theme (`App/ThemeMode`: 0 system, 1 light, 2 dark) applies to menus, dialogs,
  dock palettes and the web/document toolbars; painted glass panels adapt to the content below,
  native glass panels on macOS follow the theme.
* The app runs in a normal resizable window by default (`App/RunInWindow=true`); window
  geometry is remembered. **Full screen**: Ctrl+Shift+F (OpenBoard menu), or the green
  zoom button on macOS.
* The software update check has been removed. Startup hints are off by default and are
  never shown when their files are missing.

## Building

Follow the upstream wiki (Build OpenBoard from source). Both `CMakeLists.txt` and
`OpenBoard.pro` list all new sources.

### Linux (CMake)

Debian/Ubuntu: in addition to the usual Qt 6 dev packages the `dev` base needs
`libpipewire-0.3-dev`, `libxkbcommon-dev` and `libpoppler-private-dev`.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
sudo cmake --install build      # rulings/profiles are read from the install prefix
```

### macOS (qmake)

The CMake build requires PipeWire and is Linux-only; use qmake. `OpenBoard.pro` detects
the library prefix automatically: `/opt/homebrew` (Homebrew on Apple Silicon),
`/usr/local` (Homebrew on Intel) or `/opt/local` (MacPorts), and finds the installed
QuaZip version. Override with `qmake OpenBoard.pro MAC_LIB_PREFIX=/some/prefix`.

1. Xcode + `xcode-select --install`.
2. Qt 6 from the Qt Online Installer with the modules *Qt WebEngine*, *Qt Multimedia* and
   *Qt 5 Compatibility Module*.
3. Libraries: `brew install poppler quazip openssl@3 ffmpeg pkg-config`
   (or `sudo port install poppler quazip openssl3 ffmpeg`).
4. Build:

```bash
~/Qt/6.9.3/macos/bin/qmake OpenBoard.pro -spec macx-clang CONFIG+=release
make -j8 release
~/Qt/6.9.3/macos/bin/macdeployqt build/macx/release/product/OpenBoard.app
```

Add `CONFIG+=universal` to the qmake line for an x86_64 + arm64 build (needs universal
libraries). `release_scripts/osx/package.sh` builds a DMG; set `BASE_QT_DIR` in it first.

### Windows

Use qmake with Qt 6 (MSVC) as described in the upstream wiki; the third-party layout
(`../OpenBoard-ThirdParty`) is unchanged.
