# OpenBoard-ML: added features

This fork adds the features below on top of upstream OpenBoard `dev` (1.8.0 development
branch). All features are implemented with Qt only and build on Windows, macOS and Linux
with the normal upstream build instructions (CMake or qmake); no new dependencies.

| Feature | Where | Details |
|---|---|---|
| Any colour + multiple palettes | Colour wheel button after the swatches; right-click / long-press a swatch | [Colours](#colours-and-palettes) |
| Keyboard, mouse-button, pen-button and tablet shortcuts, device profiles | Preferences → Shortcut | [docs/INPUT-DEVICES.md](INPUT-DEVICES.md) |
| Page backgrounds in real centimetres (blank, lined, 1 cm, 0.5 cm, black) | Backgrounds button; Preferences → Grid | [docs/BACKGROUNDS.md](BACKGROUNDS.md) |
| Maths notation palette (√, ∫, Σ, lim, brackets, Greek, …) | "Maths" toolbar button (show/hide) | [Maths](#maths-notation-palette) |
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
* Actions `Next Color Palette` / `Previous Color Palette` can get a shortcut in
  Preferences → Shortcut (group *Colors*).
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
  *Greek* and *Functions*: √x, ⁿ√x, ∫, ∫ₐᵇ f(x) dx, ∬, ∮, Σ/Π with bounds, lim, d/dx, ∂/∂x,
  f′(x), brackets ( ) [ ] { } | |, fractions (inline and stacked), powers and indices,
  ± × ÷ ≤ ≥ ≠ ≈ → ⇒ ⇔ ∈ ℝ ℕ ∞ …, Greek letters, sin/cos/tan/ln/log/ᵍlog, vectors.
* A tap inserts into the text item being edited (at the cursor; a selection is wrapped,
  e.g. select `x+1` and tap √). With no text item active a new one is created.
* **Normal** ends superscript/subscript/overline mode.
* Visibility, position and tab are remembered (`Board/ShowMathPalette`, …).

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

## Building

Follow the upstream wiki (Build OpenBoard from source). On Debian/Ubuntu the extra dev
packages needed by the `dev` base are `libpipewire-0.3-dev`, `libxkbcommon-dev` and
`libpoppler-private-dev`. Both `CMakeLists.txt` and `OpenBoard.pro` list all new sources.
