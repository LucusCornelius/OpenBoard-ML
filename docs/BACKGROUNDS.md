# Page backgrounds and real-size grids

This document describes how OpenBoard page backgrounds (rulings) are sized, what was
found in the 1.8.0 `dev` code base and what was changed on the `ml/backgrounds` branch.

## Default backgrounds

The background palette (toolbar button *Backgrounds*) now offers by default only:

| Palette entry | Ruling file                          | UUID                                   |
|---------------|--------------------------------------|----------------------------------------|
| Blank         | `resources/etc/background/01_blank.xml`   | `84836d5a-1846-4c91-9ff9-f2c4d118364d` |
| Lined         | `resources/etc/background/02_ruled.xml`   | `0de33238-c30e-4396-8ec4-2bfc53304389` |
| Grid 1 cm     | `resources/etc/background/03_crossed.xml` | `7493c8bb-6d98-4be6-966c-fbddf9eff529` |
| Grid 0.5 cm   | `resources/etc/background/03_grid5mm.xml` (new) | `1655ed23-9290-444a-86b8-4774a1b8f0be` |

Each of them is available in a light (white) and dark (black) variant via the existing
*Switch background color* toggle (`actionLightDarkMode`).

* The existing rulings *Empty*, *Ruled* and *Crossed* were renamed to *Blank*, *Lined* and
  *Grid 1 cm* (descriptions only, UUIDs unchanged, so existing documents keep working).
  Dutch (`xml:lang="nl"`) descriptions were added.
* *Grid 0.5 cm* is a new plain 5 mm grid. The 1 cm grid with intermediate 5 mm lines
  (*Crossed 5mm*, `04_crossed5.xml`) is still available as an optional ruling.
* All other rulings (Seyes, stave lines, German primary school rulings, isometric, border
  variants, 1 mm grid, ruled 5 mm) remain installed and can be enabled in
  *Preferences → Grid*, but are not shown in the palette by default.

### Which backgrounds appear in the palette

The setting `Board/BackgroundRulingList` (list of UUIDs) defines the backgrounds shown in
the palette and their order. Its default is set in `resources/etc/OpenBoard.config`; if the
setting is missing or empty, a hard-coded fallback with the four defaults is used
(`UBBackgroundManager`).

Before this change the palette showed *all* rulings, the list only defined the order of
the first ones. Now:

* the palette shows only the backgrounds of the list (`UBBackgroundManager::preferredBackgrounds()`);
* *Preferences → Grid* shows all rulings, each with a *Show in palette* check box. Drag and
  drop still changes the order. At least one background stays checked.

### Background of new pages

New pages use the background (ruling + light/dark) that was chosen last. This was already
implemented via the settings `Board/PageBackground` and `Board/DarkBackground`
(`UBBoardController::setBackground`, `UBPersistenceManager::createDocumentSceneAt`).
Changes:

* `Board/PageBackground` defaults to *Blank* in `OpenBoard.config`, and an unknown or empty
  UUID falls back to *Blank* (previously the page had no ruling at all and no palette button
  was checked).
* The grid size of new pages is the grid size of the current page (`UBSettings::crossSize`).

## Grid size, pixels and millimetres

### What the code does

* A ruling XML defines line spacings in **millimetres** (`<bg:spacing>10</bg:spacing>` = 10 mm).
* `UBBackgroundRuling::draw(painter, rect, gridSize, ...)` uses `dotsPerMm = gridSize / 10`,
  i.e. the **grid size is the number of scene pixels per centimetre**. The rulers, triangle,
  compass and axes tools use the same value as `pixelsPerCentimeter`.
* The grid size is stored per page (`ub:grid-size` attribute in the page SVG) and is
  changed with the slider of the background palette (`UBSettings::crossSize`,
  default `UBSettings::defaultCrossSize`).
* The board view shows the scene scaled by `mSystemScaleFactor` (page fitted into the view,
  `UBBoardController::updateSystemScaleFactor`) times the user zoom. So at zoom 100 % one
  centimetre on screen is `gridSize * mSystemScaleFactor` logical pixels.

### What was wrong

`UBBoardController::initBackgroundGridSize()` estimated the default grid size as

    gridSize = (pageHeight / screenHeight) * logicalDpi / 2.54

* It used `QScreen::logicalDotsPerInch()`, which is a font scaling value (96 on Windows and
  most Linux desktops, 72 on macOS), not the physical pixel density of the screen.
* It approximated the view scale by the screen height instead of the real board view height
  (toolbars take ~80 px).

Example: a 1920×1080 screen at 96 DPI with a 4:3 page gave a grid of 33 scene px which is
shown as ~34 px, i.e. about 0.9 cm instead of 1 cm; on macOS (72 DPI) only ~0.7 cm. On a
typical 24" full-HD monitor (~92 DPI physical) or a 13" laptop (~130 DPI) the error was even
bigger.

### What was changed

* New `src/gui/UBScreenCalibration.{h,cpp}`:
  * `UBScreenCalibration::detectedPixelsPerCm()` uses `QScreen::physicalDotsPerInch()` of the
    control screen (logical pixels per inch computed from the size reported by the display).
    Implausible values (< 50 or > 600 DPI, typical for projectors and TVs) fall back to the
    logical DPI.
  * `UBScreenCalibration::pixelsPerCm()` returns the user calibration from the setting
    `Board/ScreenPixelsPerCm` if set (> 0), otherwise the detected value.
  * `UBScreenCalibrationWidget`, shown in *Preferences → Grid → Screen calibration*: draws a
    10 cm line with centimetre ticks. The user measures it and enters the measured length
    (→ *Apply*), or types the pixels per centimetre directly. *Automatic* removes the
    override.
* `UBBoardController::calibratedGridSize()` computes `round(pixelsPerCm / mSystemScaleFactor)`
  using the real board view scale (falls back to the screen-height estimate while the view
  is not yet visible).
* `UBBoardController::updateDefaultGridSize()` recomputes the default after a view resize,
  page size change or a new calibration. The current grid size (`crossSize`) follows the
  default if it was at the default, and the active page follows only if it is **empty**
  (otherwise the grid would no longer match its content).
* *Reset grid size* (`actionDefaultGridSize`) sets exactly 1 cm.
* The grid size slider shows the spacing of the current ruling on screen at 100 % zoom, e.g.
  "1.0 cm" for *Grid 1 cm* and "0.5 cm" for *Grid 0.5 cm*
  (`UBBackgroundRuling::primarySpacing()`). The slider range is extended if necessary so
  that the calibrated default is always reachable.

Measured on Xvfb (1920×1080, 96 DPI → 37.8 px/cm): the 1 cm grid is drawn every 37.4 px; with
a calibration of 50 px/cm, every 49.9 px. The remaining error comes from the integer grid size
(at most half a scene pixel per centimetre, about 1–2 %).

### Limitations

* "Real centimetres" are exact at **100 % zoom** on the **control screen**. Zooming scales the
  grid like any other content. The display screen/projector shows the same page scaled to its
  own size.
* The grid size of a page is stored with the page. A page created on another computer or
  with another window size keeps its grid size (so that drawings stay aligned to the grid);
  use *Reset grid size* to get 1 cm on the current screen.
* The calibration is a single value for the control screen; it is not stored per screen.

## PDF export

`UBExportPDF` and `UBExportFullPDF` render the whole scene into a PDF page of
`sceneSize × 72 / logicalDpi` points (page backgrounds of imported PDFs keep their original
size). The grid is drawn by the same `UBGraphicsScene::drawBackground()` code and therefore
scaled exactly like the page content: the grid stays consistent with drawings, rulers and
text. Its absolute size on paper is `gridSize × 72 / logicalDpi` points and depends on the
page size, just like any other content of the page. No change was necessary. The existing
preferences *Export background grid* / *Export background color* still decide whether the
ruling and the dark color are exported.

## Pen colour on dark backgrounds

Strokes store a colour for light and for dark backgrounds
(`UBGraphicsScene::initPolygonItem`). The pen colour palettes in `OpenBoard.config`
(`PenDarkBackgroundColors`, first entry `#FFFFFF`) make the default pen white on the dark
background, and switching the background re-colours existing strokes
(`UBGraphicsScene::setSceneBackground → recolorAllItems`). Verified: after switching to the
dark background the colour palette shows white first and a new stroke is drawn white. No
change was necessary.

## Actions and shortcuts

New actions in `resources/forms/mainWindow.ui`, registered in the *Background* group of the
shortcut preferences (`UBShortcutManager::addMainActions`), without default key sequence:

| Action                     | Effect on the current page        |
|----------------------------|-----------------------------------|
| `actionBackgroundBlank`    | Blank background                  |
| `actionBackgroundLined`    | Lined background                  |
| `actionBackgroundGrid1cm`  | 1 cm grid                         |
| `actionBackgroundGrid5mm`  | 0.5 cm grid                       |
| `actionLightDarkMode`      | toggle white/black (already there)|
| `actionDefaultGridSize`    | reset grid size to 1 cm (already there) |

The ruling actions keep the current light/dark mode. The light/dark toggle now uses the state
of the active page, so it also works correctly from a shortcut while the palette is closed or
after changing pages.

## Settings

| Key                          | Default (`OpenBoard.config`)           | Meaning |
|------------------------------|----------------------------------------|---------|
| `Board/BackgroundRulingList` | Blank, Lined, Grid 1 cm, Grid 0.5 cm   | backgrounds shown in the palette, in order |
| `Board/PageBackground`       | Blank                                  | ruling of new pages (last chosen) |
| `Board/ScreenPixelsPerCm`    | `0`                                    | screen calibration, 0 = automatic |
