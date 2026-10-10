# OpenDraw

OpenDraw is a free, open-source, cross-platform vector graphics editor for Linux and Windows. It aims to offer professional tools for illustration and graphic design, with a workflow that feels familiar to people coming from other desktop vector editors, and without the lock-in that comes with closed file formats.

> **Status: early development (0.2).** You can draw, select, move, resize and recolour rectangles and ellipses, edit position and size in millimetres, undo and redo, and save and open SVG files. It is not ready for real work yet.

## Goals

- **Files you can always open.** Drawings are stored in open formats (SVG today). Every file records its format version, and an older OpenDraw never refuses a newer file: it opens everything it understands and tells you exactly what it had to leave out.
- **Import from proprietary formats.** Planned: CorelDRAW (`.cdr`) import through [libcdr](https://wiki.documentfoundation.org/DLP/Libraries/libcdr), so existing work is not trapped.
- **A familiar workflow.** Toolbox, property bar, colour palette and keyboard shortcuts that experienced designers already know.
- **Same app on Linux and Windows**, built with C++20 and Qt 6.

See [docs/ROADMAP.md](docs/ROADMAP.md) for what is planned next.

## Using it

| Action | How |
|---|---|
| Select / move | Select tool (`V`), click and drag a shape |
| Resize | Drag one of the eight handles around the selection |
| Exact position and size | Type values in the property bar at the top (millimetres) |
| Rectangle | `F6`, drag on the page. Hold `Shift` for a square |
| Ellipse | `F7`, drag on the page. Hold `Shift` for a circle |
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
src/core   document model, shapes, undo commands, SVG reading and writing (no GUI code)
src/app    the Qt Widgets application: main window and drawing canvas
tests      unit tests for src/core
docs       roadmap and design notes
```

## Contributing

Contributions are welcome. Please read [CONTRIBUTING.md](CONTRIBUTING.md) first, in particular the clean-room rule: OpenDraw must not contain code or assets taken from proprietary software.

## Licence

OpenDraw is licensed under the [GNU General Public License v3.0 or later](LICENSE).
