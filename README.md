<p align="center">
  <img src="assets/branding/opendraw-logo.png" alt="OpenDraw" width="320">
</p>

<p align="center">
  <a href="https://github.com/R4P0S1T0/OpenDraw/actions/workflows/build.yml"><img src="https://github.com/R4P0S1T0/OpenDraw/actions/workflows/build.yml/badge.svg" alt="Build status"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-GPL--3.0--or--later-blue" alt="License: GPL-3.0-or-later"></a>
  <img src="https://img.shields.io/badge/platforms-Linux%20%7C%20Windows-informational" alt="Platforms: Linux, Windows">
</p>

# OpenDraw

OpenDraw is a free, open-source, cross-platform vector graphics editor for Linux and Windows. It aims to offer professional tools for illustration and graphic design, with a workflow that feels familiar to people coming from other desktop vector editors, and without the lock-in that comes with closed file formats.

> **Status: early development (0.3).** You can draw rectangles, ellipses, Bézier and freehand curves, edit curve nodes, select several objects, group, order, align and distribute them, recolour them, undo and redo, and open SVG files from other programs. Text and layers are still missing, so it is not ready for real work yet.

## Goals

- **Works with files from other programs.** SVG import understands paths, transforms, groups and inherited styles, so drawings exported from other editors open as editable objects.
- **Files you can always open.** Drawings are stored in open formats (SVG today). Every file records its format version, and an older OpenDraw never refuses a newer file: it opens everything it understands and tells you exactly what it had to leave out.
- **Import from proprietary formats.** Planned: CorelDRAW (`.cdr`) import through [libcdr](https://wiki.documentfoundation.org/DLP/Libraries/libcdr), so existing work is not trapped.
- **A familiar workflow.** Toolbox, property bar, colour palette and keyboard shortcuts that experienced designers already know.
- **Same app on Linux and Windows**, built with C++20 and Qt 6.

See [docs/ROADMAP.md](docs/ROADMAP.md) for what is planned next.

## Download

- **Releases:** tested builds for Windows (`.zip`, unzip and run `opendraw.exe`) and Linux (`.AppImage`, make it executable and run) are published on the [Releases page](https://github.com/R4P0S1T0/OpenDraw/releases) when a version is tagged. Versions before 1.0 are alpha quality.
- **Development builds:** every commit is built by [GitHub Actions](https://github.com/R4P0S1T0/OpenDraw/actions/workflows/build.yml); open a run and download the `OpenDraw-windows` or `OpenDraw-linux` artifact (requires a GitHub login).

## Using it

| Action | How |
|---|---|
| Select / move | Select tool (`V`), click and drag a shape |
| Select several | `Shift`+click to add or remove, drag on empty space for a rubber band (selects objects fully inside), `Ctrl` `A` for all |
| Resize | Drag one of the eight handles around the selection (several objects scale together) |
| Group / ungroup | `Ctrl` `G` / `Ctrl` `U` |
| Order | `Shift` `PgUp` to front, `Shift` `PgDn` to back, `Ctrl` `PgUp` / `Ctrl` `PgDn` one step |
| Align | `L` `R` `T` `B` (left, right, top, bottom), `E` / `C` (centres horizontally / vertically), `P` (centre on page). One object aligns to the page, several to their combined box. Distribute is in Object › Align and Distribute |
| Exact position and size | Type values in the property bar at the top (millimetres) |
| Rectangle | `F6`, drag on the page. Hold `Shift` for a square |
| Ellipse | `F7`, drag on the page. Hold `Shift` for a circle |
| Bézier curve | `Shift` `B`. Click for a corner node, click and drag for a smooth node. Click the first node to close the shape; `Enter`, `Esc` or double-click to finish. `Backspace` removes the last node while drawing |
| Freehand | `F5`, drag to draw. The stroke is smoothed into a few curve segments; end near the start to close it |
| Edit nodes | Shape tool (`F10`): drag nodes and handles, double-click a segment to add a node, double-click a node or press `Delete` to remove it. Hold `Alt` while dragging a handle to make a corner |
| Convert to curves | `Ctrl` `Q` turns a rectangle or ellipse into an editable curve |
| Fill / outline colour | Colour palette on the right: left click = fill, right click = outline, ✕ = none. With nothing selected it sets the colour for new shapes |
| Custom colours | `Shift` `F11` (fill), `Shift` `F12` (outline) |
| Zoom | Mouse wheel (around the cursor), `Ctrl` `+` / `Ctrl` `-`, `Shift` `F4` fits the page |
| Pan | Middle mouse button, or hold `Space` and drag |
| Delete | `Delete` |
| Undo / redo | `Ctrl` `Z` / `Ctrl` `Y` |

## Building

Requirements: a C++20 compiler, CMake 3.21+, Ninja (optional) and Qt 6.4+ (Core, Gui, Widgets; Test for the tests).

### Linux (Debian / Ubuntu)

```sh
sudo apt install build-essential cmake ninja-build qt6-base-dev libgl-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
./build/opendraw
```

### Windows (MSYS2)

Install [MSYS2](https://www.msys2.org/), open the **UCRT64** shell and run:

```sh
pacman -S --needed mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,qt6-base}
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
./build/opendraw.exe
```

Visual Studio with the official Qt installer also works: open the folder in Visual Studio and point `CMAKE_PREFIX_PATH` at your Qt install.

## Project layout

```
src/core          document model, shapes, undo commands, SVG reading and writing (no GUI code)
src/app           the Qt Widgets application: main window and drawing canvas
tests             unit tests for src/core and UI tests that drive the canvas with simulated input
assets/branding   OpenDraw logo and application icons
packaging         desktop integration files (Linux .desktop entry)
docs              roadmap and design notes
```

## Contributing

Contributions are welcome. Please read [CONTRIBUTING.md](CONTRIBUTING.md) first, in particular the clean-room rule: OpenDraw must not contain code or assets taken from proprietary software.

## Licence

OpenDraw is licensed under the [GNU General Public License v3.0 or later](LICENSE).
