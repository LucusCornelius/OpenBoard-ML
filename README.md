# OpenBoard-ML

OpenBoard-ML is a fork of [OpenBoard](https://github.com/OpenBoard-org/OpenBoard), the open source
interactive whiteboard for schools, with a modern flat user interface and a set of additions aimed at
teaching maths with a pen tablet. It is based on the upstream `dev` branch (1.8.0) and builds with the
same tools; no new dependencies.

OpenBoard-ML has its own name and version numbering (currently 1.0.2, independent of upstream) and can
be installed next to OpenBoard. It keeps OpenBoard's data location and file format: documents and
settings of an existing OpenBoard installation (for example `~/Library/Application Support/OpenBoard`
on macOS) are used as they are, and `.ubz` files stay compatible both ways. Because the two share
their documents, do not run OpenBoard and OpenBoard-ML at the same time.

| macOS (Liquid Glass) | Windows and Linux (modern style) |
|---|---|
| ![macOS](docs/screenshots/macos-liquid-glass.png) | ![Modern style](docs/screenshots/modern-style.png) |

| Dark appearance | Document mode |
|---|---|
| ![Dark](docs/screenshots/macos-dark.png) | ![Documents](docs/screenshots/documents.png) |

## Added features

| Feature | Details |
|---|---|
| **Flat, floating UI**: no toolbar strip, the controls float over the board. Native Liquid Glass on macOS, a flat Fluent-like style on Windows and Linux. Light, dark or system appearance; in dark mode the board itself is dark. | [docs/UI-STYLES.md](docs/UI-STYLES.md) |
| **Maths notation typeset like LaTeX**: √, fractions, ∫ Σ Π with bounds, lim, brackets, Greek, functions. Written with the Latin Modern Math font and TeX's layout rules, as ink in the pen colour (erase, move, undo, PDF). Type formulas such as `int_0^1 sqrt(x) dx`. | [docs/MATHS.md](docs/MATHS.md) |
| **Any colour, several palettes**: colour wheel, hex/RGB, named palettes with 5–10 swatches, a chevron next to the swatches to switch palettes quickly. | [docs/FEATURES.md](docs/FEATURES.md#colours-and-palettes) |
| **Pen tablet and mouse shortcuts**: every command can get a key, a mouse button and a stylus button; right/middle click behaviour (temporary eraser, previous tool, pan); device profiles for Wacom Intuos M and Medion P82030. | [docs/INPUT-DEVICES.md](docs/INPUT-DEVICES.md) |
| **Backgrounds in real centimetres**: blank, lined, 1 cm and 0.5 cm grid, dark page; screen calibration. | [docs/BACKGROUNDS.md](docs/BACKGROUNDS.md) |
| **Shapes and text boxes**: rectangle, rounded rectangle, ellipse, triangle, line, arrow, fill; text boxes with border and background. | [docs/FEATURES.md](docs/FEATURES.md#shapes-and-text-boxes) |
| **Line angle readout**: while drawing a line the angle is shown next to the pen; lines within ±2° of horizontal or vertical become exactly straight, without snapping to the grid. | [docs/FEATURES.md](docs/FEATURES.md#line-angle) |
| **Normal window by default**, full screen with Ctrl+Shift+F; "Show on Second Screen" for a projector. Document mode restyled; no update check; startup hints off. | [docs/FEATURES.md](docs/FEATURES.md#user-interface-liquid-glass) |

All upstream OpenBoard features (documents, library, web mode, desktop annotation, podcast, PDF import
and export) are still there.

## Installing

Ready-made builds are published on the [Releases](../../releases) page of this repository.

### macOS

1. Download `OpenBoard-ML-1.0.2-macOS-arm64.dmg` from the latest release (Apple Silicon; macOS 12 or
   newer, the glass effects need macOS 26).
2. Open the image and drag `OpenBoard-ML` to `Applications`.
3. The app is not signed with an Apple developer certificate. The first time, right-click the app and
   choose **Open**, or run in Terminal:

   ```bash
   xattr -dr com.apple.quarantine /Applications/OpenBoard-ML.app
   ```

### Windows

1. Download `OpenBoard-ML-1.0.2-Windows-x64-Setup.exe` from the latest release (64-bit Windows 10
   version 1809 or newer, or Windows 11).
2. Run it. The installer is not signed with a code-signing certificate, so Windows SmartScreen shows
   **Windows protected your PC**: click **More info**, then **Run anyway**.
3. Start OpenBoard-ML from the Start menu. Nothing else has to be installed: Qt, Qt WebEngine, the
   PDF and zip libraries and the Microsoft C++ runtime are all included.

`OpenBoard-ML-1.0.2-Windows-x64-Portable.zip` contains the same program without an installer: unpack
it anywhere and start `OpenBoard-ML.exe` (Windows may ask the same SmartScreen question). The program
installs next to an existing OpenBoard instead of replacing it; documents and settings stay where
OpenBoard keeps them in your user profile, so both see the same documents.

### Linux

No package yet; build from source (below). The upstream project maintains a Debian package of
plain OpenBoard on its [downloads page](https://github.com/OpenBoard-org/OpenBoard/wiki/Downloads).

## Building from source

The upstream wiki page [Build OpenBoard from source](https://github.com/OpenBoard-org/OpenBoard/wiki/Build-OpenBoard-from-source)
applies; both `CMakeLists.txt` and `OpenBoard.pro` list all added sources. Qt 6 is recommended
(Qt 5.15 still compiles).

### macOS

Requires Xcode (with the command line tools) and Homebrew.

```bash
brew install qtbase qtsvg qt5compat qtdeclarative qtwebengine qtmultimedia qttools \
             poppler quazip openssl@3 ffmpeg pkg-config
git clone https://github.com/<your-user>/OpenBoard-ML.git
cd OpenBoard-ML
qmake OpenBoard.pro -spec macx-clang CONFIG+=release
make -j8 release
open build/macx/release/product/OpenBoard-ML.app
```

`OpenBoard.pro` finds the libraries in `/opt/homebrew`, `/usr/local` or `/opt/local` on its own
(override with `qmake OpenBoard.pro MAC_LIB_PREFIX=/some/prefix`). Qt from the Qt Online Installer
works too: use its `qmake` and add the modules *Qt WebEngine*, *Qt Multimedia* and *Qt 5 Compatibility*.
To make a distributable disk image run `release_scripts/osx/deploy-homebrew.sh <version> [output-dir]`.
It runs `macdeployqt`, repairs the library links that `macdeployqt` gets wrong with Homebrew Qt (the
Qt WebEngine helper process), signs the bundle ad hoc and writes `OpenBoard-ML-<version>-macOS-arm64.dmg`.
(creates `OpenBoard-ML.dmg` next to the app).

### Windows

`release_scripts\windows\build.ps1` does the whole build and packaging, the same way the release
build on GitHub Actions does it. It needs:

* Visual Studio 2022 or the *Build Tools for Visual Studio 2022*, workload *Desktop development
  with C++*
* Git and Python 3 (Python only to download Qt with [aqtinstall](https://github.com/miurahr/aqtinstall))
* [vcpkg](https://github.com/microsoft/vcpkg): `git clone https://github.com/microsoft/vcpkg` and
  `.\vcpkg\bootstrap-vcpkg.bat`
* [Inno Setup](https://jrsoftware.org/isinfo.php) 6.3 or newer for the installer
  (`winget install JRSoftware.InnoSetup`); without it, pass `-NoInstaller`

```powershell
git clone https://github.com/<your-user>/OpenBoard-ML.git
cd OpenBoard-ML
pwsh -File release_scripts\windows\build.ps1 -VcpkgRoot C:\path\to\vcpkg
```

The script:

1. loads the Visual Studio x64 environment (no *Native Tools* prompt needed);
2. installs the official Qt 6.10 MSVC 2022 x64 binaries with Qt WebEngine, Qt Multimedia,
   Qt Positioning, Qt WebChannel and Qt 5 Compatibility into `build\win32-deps\Qt` (or uses
   `-QtDir C:\Qt\6.10.3\msvc2022_64` from the Qt Online Installer);
3. builds poppler (with its private headers), OpenSSL and zlib with vcpkg from
   `release_scripts\windows\vcpkg.json` (pinned baseline, release DLLs; the first run takes a while,
   later runs reuse vcpkg's binary cache);
4. downloads the QuaZip sources, which `OpenBoard.pro` compiles into the program;
5. runs `qmake` and `nmake` (or `jom` when it is on `PATH`) in `build\win32-msvc`;
6. deploys everything into `install\win32\OpenBoard-ML\`: `windeployqt` (Qt, Qt WebEngine with
   `QtWebEngineProcess.exe`, its resources and locales), the vcpkg DLLs, the Microsoft C++ runtime
   DLLs, the compiled translations and OpenBoard's data folders; then checks with
   `dumpbin /dependents` that every DLL any file in there imports is either in the folder or part
   of Windows;
7. writes `install\win32\OpenBoard-ML-<version>-Windows-x64-Setup.exe`,
   `OpenBoard-ML-<version>-Windows-x64-Portable.zip` and `SHA256SUMS.txt`.

`-Phase Dependencies` or `-Phase Application` runs one half, `-Clean` starts the compile from scratch;
see `Get-Help release_scripts\windows\build.ps1 -Detailed`.

Without the script, `OpenBoard.pro` takes the libraries from any prefix with `include\`, `lib\` and
`bin\` plus a QuaZip source folder: `qmake OpenBoard.pro WIN_LIB_PREFIX=C:/deps/x64-windows
QUAZIP_SRC=C:/deps/quazip-1.7.2/quazip`. Without `WIN_LIB_PREFIX` it uses upstream's
[OpenBoard-ThirdParty](https://github.com/OpenBoard-org/OpenBoard-ThirdParty) layout, by default in
`..\OpenBoard-ThirdParty` (override with `THIRD_PARTY_PATH=...`), as upstream's
`release.win7.vc9.bat` and `OpenBoard.iss` expect.

The modern style uses the Windows 11 Qt style when available and sets a dark title bar through
`DwmSetWindowAttribute` (`-lDwmapi`).

#### How the Windows release is made

`.github/workflows/windows.yml` runs `build.ps1` on a GitHub-hosted Windows runner:

* every push to `ml-features` (except changes that only touch `docs/` or Markdown files) and
  *Run workflow* on the Actions tab build the installer and the zip; they are attached to the run as
  artifacts (Actions → the run → *Artifacts*) for 30 days;
* pushing a tag `v<version>` (for example `git tag v1.0.2 && git push origin v1.0.2`) builds the same
  and attaches both files to the GitHub Release of that tag, creating the release if needed. The
  file names come from `version.txt`, so update it before tagging.

GitHub disables workflows in a fork until the owner enables them once: open the **Actions** tab of
the fork and confirm. If a build fails, the run's summary shows the first errors and the last lines
of the log, and the artifact *windows-build-logs* has the complete logs.

### Linux (Debian 12 / Ubuntu)

```bash
sudo apt install build-essential cmake ninja-build qt6-base-dev qt6-base-private-dev \
     qt6-svg-dev qt6-5compat-dev qt6-declarative-dev qt6-webengine-dev qt6-multimedia-dev \
     qt6-tools-dev libpoppler-dev libpoppler-private-dev libquazip1-qt6-dev libssl-dev \
     libavcodec-dev libavformat-dev libswscale-dev libpipewire-0.3-dev libxkbcommon-dev
git clone https://github.com/<your-user>/OpenBoard-ML.git
cd OpenBoard-ML
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
sudo cmake --install build      # rulings and device profiles are read from the install prefix
openboard-ml
```

The CMake build installs the program `openboard-ml`, its data in `share/openboard-ml`, its system
configuration in `etc/openboard-ml` and the desktop entry `org.openboard-ml.OpenBoard-ML.desktop`,
next to (not over) an installed OpenBoard.

Linux gets the same modern style as Windows. The Linux CMake build of this fork was last built
in a container during development and should be re-checked on a real system.

## Documentation

* [docs/FEATURES.md](docs/FEATURES.md): all additions, settings keys and the UI
* [docs/MATHS.md](docs/MATHS.md): maths typesetting rules and font
* [docs/INPUT-DEVICES.md](docs/INPUT-DEVICES.md): shortcuts, tablet buttons, device profiles
* [docs/BACKGROUNDS.md](docs/BACKGROUNDS.md): rulings in centimetres, calibration
* [docs/UI-STYLES.md](docs/UI-STYLES.md): glass and modern style, appearance

## Upstream

OpenBoard is developed by the [OpenBoard community](https://github.com/OpenBoard-org/OpenBoard);
it was originally forked from Open-Sankoré, itself based on Uniboard. This fork follows the upstream
`dev` branch. Web widgets, documentation and community packages of upstream OpenBoard apply unchanged.

## Licence

GNU General Public License v3, with the OpenSSL linking exception, like OpenBoard (see
[LICENSE](LICENSE) and [COPYRIGHT](COPYRIGHT)). The bundled Latin Modern Math font is distributed under
the GUST Font License (see `resources/math/`).
