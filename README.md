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

> **Status: early development (0.3).** You can draw rectangles, ellipses, Bézier and freehand curves, edit curve nodes, write text, select several objects, group, order, align and distribute them, recolour them, undo and redo, organise work in layers and multiple pages, open SVG files from other programs (Inkscape layers included), fill with gradients and set transparency, open CorelDRAW (`.cdr`) files, and export PDF and PNG.

## Goals

- **Works with files from other programs.** SVG import understands paths, transforms, groups and inherited styles, so drawings exported from other editors open as editable objects.
- **Files you can always open.** Drawings are stored in open formats (SVG today). Every file records its format version, and an older OpenDraw never refuses a newer file: it opens everything it understands and tells you exactly what it had to leave out.
- **Import from proprietary formats.** CorelDRAW (`.cdr`, `.cdt`) files open through [libcdr](https://wiki.documentfoundation.org/DLP/Libraries/libcdr), so existing work is not trapped: every page, with curves, groups, colours, gradients, bitmaps, texture fills and text, as editable objects. The original is never overwritten; *Save* asks for a new `.svg` file. Not imported yet (limits of libcdr): PowerClip clipping, transparency effects other than uniform opacity, lenses, and spacing between paragraphs.
- **A familiar workflow.** Toolbox, property bar, colour palette and keyboard shortcuts that experienced designers already know.
- **Same app on Linux and Windows**, built with C++20 and Qt 6.

See [docs/ROADMAP.md](docs/ROADMAP.md) for what is planned next.

## Download

- **Releases:** tested builds for Windows (`.zip`, unzip and run `opendraw.exe`) and Linux (`.AppImage`, make it executable and run) are published on the [Releases page](https://github.com/R4P0S1T0/OpenDraw/releases) when a version is tagged. Versions before 1.0 are alpha quality.
- **Development builds:** every commit is built by [GitHub Actions](https://github.com/R4P0S1T0/OpenDraw/actions/workflows/build.yml); open a run and download the `OpenDraw-windows` or `OpenDraw-linux` artifact (requires a GitHub login).

## Using it

The workspace follows the layout desktop designers know: menu bar and standard toolbar at the top, a context-sensitive property bar under it (page size, orientation and units with nothing selected; position, size, mirroring, outline and opacity with a selection), the toolbox on the left (hold a tool with a small arrow to open its flyout), rulers, the colour palette along the right edge, the page navigator and the document palette (colours used in the drawing) under the page, and the fill and outline of the selection at the right of the status bar (click them to change). Dark and light colour schemes are in View › Color Scheme; toolbars, dockers and palettes can be brought back from the Window menu. The interface is available in English and Brazilian Portuguese: it follows the system language, or pick one in View › Language.

| Action | How |
|---|---|
| Select / move | Select tool (`V`), click and drag a shape |
| Select several | `Shift`+click to add or remove, drag on empty space for a rubber band (selects objects fully inside), `Ctrl` `A` for all |
| Resize | Drag one of the eight handles around the selection (several objects scale together) |
| Group / ungroup | `Ctrl` `G` / `Ctrl` `U` |
| Order | `Shift` `PgUp` to front, `Shift` `PgDn` to back, `Ctrl` `PgUp` / `Ctrl` `PgDn` one step |
| Align | `L` `R` `T` `B` (left, right, top, bottom), `E` / `C` (centres horizontally / vertically), `P` (centre on page). One object aligns to the page, several to their combined box. Distribute is in Object › Align and Distribute |
| Exact position and size | Type values in the property bar (in the unit chosen there or from the corner of the rulers); the lock keeps the proportions |
| Nudge | Arrow keys move the selection by the nudge distance (property bar, nothing selected); `Shift` moves ten times further |
| Copy, paste, duplicate | `Ctrl` `C` / `Ctrl` `X` / `Ctrl` `V` (objects travel as SVG, so they paste into other programs too), `Ctrl` `D` duplicates with the offset set on the property bar |
| Rotate and skew | Click a selected object again: the corner arrows rotate it (`Ctrl`: 15° steps), the edge arrows skew it, and the circle in the middle is the centre of rotation, which can be dragged. Click again to go back to resizing. *Angle* on the property bar and Object › Transform rotate by an exact amount. Rectangles, ellipses and text stay editable when turned |
| Mirror | Mirror buttons on the property bar, or Object › Transform |
| Page size | With nothing selected: paper type (A3–A6, Letter, Legal, Tabloid), width, height and portrait/landscape on the property bar |
| Rectangle | `F6`, drag on the page. `Ctrl` for a square, `Shift` to draw from the centre (the same keys work for every shape tool). *Corners* on the property bar rounds the corners of new or selected rectangles |
| Ellipse | `F7`, drag on the page. `Ctrl` for a circle |
| Polygon, star, spiral, graph paper | Object flyout in the toolbox: polygon (`Y`), star, spiral (`A`), graph paper (`D`). Sides, points, sharpness, revolutions, columns and rows are on the property bar while the tool is active |
| Straight line | 2-point line in the freehand flyout; `Ctrl` keeps the angle in 15° steps |
| PowerClip | Select the objects to put inside, then (with `Shift`) the frame last — a rectangle, ellipse, curve or text — and use Object › PowerClip › Place Inside Frame. They show only within the frame, between its fill and its outline, and move, scale and turn with it. Object › PowerClip › Extract Contents takes them back out |
| Interactive fill | Interactive Fill tool (`G`): drag across an object for a gradient from its colour to white, then drag the ends and the colour squares along the line. Double-click the line to add a colour; click a square, then a palette colour to recolour it, or `Delete` to remove it. The property bar switches between uniform, linear and radial |
| Mesh fill | Mesh Fill tool (`M`, same flyout): click an object to give it a grid of colours; click a node, then a palette colour, and the colours blend smoothly between nodes. The property bar sets the columns and rows or removes the mesh |
| Smart fill | Smart Fill tool: click inside any area enclosed by objects (where two shapes overlap, the ring between two circles, ...) to make a new object of exactly that shape. The property bar sets its fill colour and whether it gets the default outline |
| Drop shadow | Shadow tool: drag from an object to set where its shadow falls (`Ctrl`: straight). The property bar sets the offset, how soft (feather), how dark and what colour, or removes it |
| Blend | Blend tool (in the flyout with Drop Shadow): drag from one object to another. The shapes in between change step by step in outline, colours and outline width, and follow along when either end is moved, resized or recoloured. The property bar sets the number of steps; Object › Break Blend Apart (`Ctrl` `K`) leaves the start, a group of the steps and the end |
| Envelope | Envelope tool (same flyout): click an object, then drag the square corners and round handles of its frame to bend it. Rectangles, ellipses and texts stay what they are (text stays editable); the envelope follows moves, resizing, turns and mirroring. Object › Remove Envelope (or the property bar) takes it off. To edit the nodes of a bent curve, convert it to curves first |
| Extrude | Extrude tool (same flyout): drag from an object to give it depth; the back follows the pointer. The property bar sets the depth, the vanishing point and the colour of the sides (or shading from the object's own fill, lit from the top left), or removes it. The sides follow the object as it changes |
| Contour | Contour tool (same flyout, `Ctrl` `F9`): drag from an object outwards for rings around it, or inwards for rings inside it. The property bar picks outside, inside or to the centre, the number of rings, the distance between them and the colours of the last ring; the rings in between run from the object's own colours. Rings follow the object as it changes |
| Transparency | Transparency tool: drag across an object, from where it should be solid to where it should be transparent; drag the white and black squares to adjust. The property bar chooses none, uniform, linear or radial and the transparency at the start and end |
| Colour eyedropper | `I`: click to pick up a colour, then click inside objects to fill them or on their outline to colour it. `Shift`+click or `Esc` picks a new colour |
| Guidelines | Drag from a ruler onto the drawing; drag a guideline with the pick tool to move it, back onto a ruler to delete it. View › Guidelines shows or hides them |
| Grid and snapping | View › Grid (spacing in View › Grid Spacing). *Snap To* on the standard toolbar: grid, guidelines, objects and page. While moving, `Ctrl` keeps the move horizontal or vertical |
| Switch to the pick tool | Tap `Space`; tap again to go back to the previous tool (hold `Space` and drag to pan) |
| Bézier curve | `Shift` `B`. Click for a corner node, click and drag for a smooth node. Click the first node to close the shape; `Enter`, `Esc` or double-click to finish. `Backspace` removes the last node while drawing |
| Freehand | `F5`, drag to draw. The stroke is smoothed into a few curve segments; end near the start to close it |
| Edit nodes | Shape tool (`F10`): drag nodes and handles, double-click a segment to add a node, double-click a node or press `Delete` to remove it. Hold `Alt` while dragging a handle to make a corner |
| Paragraph text | With the text tool, drag a frame instead of clicking: the text wraps to its width. Resizing the frame reflows the text; text that does not fit is hidden and the frame turns red. Text › Convert (`Ctrl` `F8`) switches between paragraph and artistic text. Paragraph text from CorelDRAW files keeps its frame |
| Text on a path | Select a text and a curve (or any shape), then Text › Fit Text to Path: the letters follow its outline (round a closed shape they start centred on top). The text stays editable; *Offset* on the property bar slides it along the curve. Text › Straighten Text puts it back on a straight line |
| Paragraph formatting | Text › Paragraph Formatting (`Ctrl` `T`) opens the Paragraph section of the Properties docker: left, centre, right or justified (on the text bar too), line spacing, space before and after paragraphs, first-line, left and right indents. Indents and justification apply to paragraph text |
| Text | `F8`, click on the page and type. `Enter` starts a new line, `Esc` finishes. Click a text with the tool, or double-click it with the Select tool, to edit it. Font, size (pt), bold, italic and alignment are on the text bar |
| Format part of a text | While editing, select letters with `Shift`+arrows, by dragging, a double-click (a word) or `Ctrl` `A`; the font, size, bold and italic on the text bar (or `Ctrl` `B` / `Ctrl` `I`) and a palette colour then apply to them only. Typing over a selection keeps its format. Without a selection the changes apply to the whole text |
| Convert to curves | `Ctrl` `Q` turns a rectangle, ellipse or text into editable curves |
| Fill / outline colour | Colour palette on the right or document palette at the bottom: left click = fill, right click = outline, the crossed swatch = none. With nothing selected it sets the colour for new shapes |
| Fill and gradients | `Shift` `F11`: no fill, uniform colour, or a linear (with angle) or radial gradient, with a preview. `Shift` `F12` for the outline colour |
| Transparency | *Opacity* on the property bar (0–100 %), for the whole object |
| Zoom | Mouse wheel (around the cursor), `Ctrl` `+` / `Ctrl` `-`, the zoom box on the standard toolbar, `Shift` `F4` fits the page, `Shift` `F2` the selection. Zoom tool (`Z`): click to zoom in, `Shift`+click or right-click to zoom out, drag around an area |
| Pan | Pan tool (`H`, in the zoom flyout), the scroll bars, the middle mouse button, or hold `Space` and drag |
| Delete | `Delete` |
| Properties docker | On the right, above Objects (Window › Dockers › Properties). Outline: colour, width, line style (dashes and dots), ends, corners, start and end arrowheads, *Behind fill* (the fill hides the inner half of the outline) and *Scale with object* (resizing changes the outline width too). Arrowheads are also on the property bar when a curve is selected. Fill: none, uniform, linear or radial gradient, colours and angle. Transparency: 0–100 %. Changes apply at once to the selection (each one undoable), or to new shapes when nothing is selected |
| Objects docker | On the right (Window › Dockers › Objects): every page, its layers and their objects, topmost first, with groups that open. Click objects to select them on the drawing (and the other way round), double-click to rename a page, layer or object (names are saved), drag objects to restack them or onto another layer. The eye and lock icons show/hide and lock layers; the bold layer receives new objects. Buttons: new layer, delete, move up/down, *Move here* (selection to the active layer); right-click for more |
| Several drawings | Each open drawing has a tab above the rulers, with its own undo history and view. "+" (or `Ctrl` `N`) starts a new one, File › Open adds a tab (opening a file that is already open just shows it), × or File › Close closes one (asking to save changes). `Ctrl` `Tab` and the Window menu switch between them |
| Pages | Page navigator under the drawing: ⊕ inserts a page before or after the current one, arrows go to the first, previous, next and last page, right-click or double-click a page tab to rename or delete it. `PgUp` / `PgDn` also switch pages |
| Open CorelDRAW files | `Ctrl` `O` and pick a `.cdr` file, or pass it on the command line. A report lists anything that could not be imported |
| Import | `Ctrl` `I` adds the objects of an SVG or CorelDRAW file to the current page |
| Undo / redo | `Ctrl` `Z` / `Ctrl` `Y` |
| Export | `Ctrl` `E`: PDF (vector, exact page size, one PDF page per page) or PNG of the current page (choose the dpi, transparent background, or only the selected objects) |

## Building

Requirements: a C++20 compiler, CMake 3.21+, Ninja (optional) and Qt 6.4+ (Core, Gui, Widgets; Test for the tests). Optional: libcdr 0.1 and librevenge 0.0, found with pkg-config, for CorelDRAW import (without them OpenDraw builds and runs, but cannot open `.cdr` files; `-DOPENDRAW_REQUIRE_LIBCDR=ON` makes their absence an error). Optional: Qt's LinguistTools (`lrelease`) to build the interface translations (without them the interface is English only; `-DOPENDRAW_REQUIRE_TRANSLATIONS=ON` makes their absence an error).

### Linux (Debian / Ubuntu)

```sh
sudo apt install build-essential cmake ninja-build pkg-config qt6-base-dev qt6-tools-dev qt6-l10n-tools libgl-dev \
    libcdr-dev librevenge-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
./build/opendraw
```

### Windows (MSYS2)

Install [MSYS2](https://www.msys2.org/), open the **UCRT64** shell and run:

```sh
pacman -S --needed mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,pkgconf,qt6-base,qt6-tools,libcdr,librevenge}
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
./build/opendraw.exe
```

Visual Studio with the official Qt installer also works: open the folder in Visual Studio and point `CMAKE_PREFIX_PATH` at your Qt install.

## Project layout

```
src/core          document model, shapes, undo commands, SVG reading and writing, CorelDRAW import (no GUI code)
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

Packaged builds also include Qt (LGPL-3.0) and, for CorelDRAW import, libcdr and librevenge (MPL-2.0 / LGPL-2.1+) with their dependencies. CorelDRAW is a trademark of its owner; OpenDraw is not affiliated with it.
