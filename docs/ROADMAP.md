# Roadmap

Rough order of work. Items move as contributors pick them up.

## 0.1 — Foundation

- [x] CMake + Qt 6 project, CI on Linux and Windows
- [x] Document model with undo/redo
- [x] Canvas with page, zoom and pan
- [x] Rectangle and ellipse tools, selection and moving
- [x] Fill colour
- [x] Save and open SVG

## 0.2 — Familiar workspace

- [x] Property bar: position, size and outline width of the selection
- [x] Colour palette docked on the right: left click sets fill, right click sets outline, "no colour" swatch
- [x] Status bar showing the selection's fill and outline
- [x] Resize handles
- [x] Format version in every file; newer files open with a report instead of being refused
- [x] Familiar workspace layout: standard toolbar, context-sensitive property bar (page or object), toolbox with flyouts, document tab, rulers with selectable units, scroll bars, page navigator, document palette, fill/outline indicators in the status bar, dark and light schemes, Window menu
- [x] Zoom and pan tools, clipboard (copy/cut/paste as SVG), duplicate, nudge with arrow keys, mirroring, paper sizes and orientation
- [x] Guidelines dragged from the rulers (saved in Inkscape's format), grid, snapping to grid, guidelines, objects and page
- [x] Polygon, star, spiral, graph paper and 2-point line tools; rounded rectangle corners; colour eyedropper; Ctrl/Shift constraints and Space to switch to the pick tool
- [x] Rotating and skewing: second click on a selection, rotation centre, angle on the property bar, rotate 90°/180°; rectangles, ellipses and text keep their kind (saved as SVG transforms with non-scaling outlines)
- [ ] Drop shadow and transparency tools, smart fill
- [x] Objects docker: pages, layers and objects as a tree, selection in both directions, renaming (object names saved as Inkscape labels), drag and drop to restack or change layers
- [x] Properties docker: outline (colour, width, dashes, ends, corners), fill (none, uniform, linear, radial) and transparency, applied live; dashes, line ends and corners saved in SVG and read from other files
- [ ] Arrowheads, outline behind fill, scale outline with object
- [ ] Several documents open at once (one tab each)
- [x] Translatable interface; Brazilian Portuguese translation, chosen from the system language or View › Language
- [ ] More languages (see CONTRIBUTING.md › Translations)
- [ ] Keep unsupported SVG elements when saving, so round-tripping a newer file loses nothing

## 0.3 — Real drawings (current)

- [x] Paths: Bézier and freehand tools, node editing (move, add, delete, smooth/corner handles)
- [x] Convert rectangles and ellipses to curves
- [x] Full SVG path support on import and export, plus transforms, groups and inherited styles
- [x] Node commands: smooth, corner, to line, to curve, break, join
- [x] Packaged builds: Windows .zip and Linux AppImage from CI, GitHub release on version tags
- [x] Multiple selection (Shift+click, rubber band, select all), moving and resizing several objects
- [x] Groups (also imported from and saved as SVG `<g>`), ordering, alignment and distribution
- [x] Text objects: font, size, bold, italic, alignment, multiple lines; SVG `<text>` import and export; convert to curves
- [x] Paragraph text frames: wrapping, reflow on resize, hidden overflow, conversion to and from artistic text; frames and paragraph breaks recovered from CorelDRAW files
- [x] Per-character formatting: font, size, bold, italic and colour for selected parts of a text (selection with Shift+arrows, mouse, double-click, Ctrl+A), saved as nested tspans; mixed sizes and colours read from CorelDRAW files
- [ ] Paragraph spacing and indents, justification, text on a path
- [x] Layers (visible, locked, rename, reorder, move objects between layers) and multiple pages (tabs, Layout menu, PgUp/PgDn); Inkscape layers import; one PDF page per page

## 0.4 — No more locked files

- [x] CorelDRAW (`.cdr`, `.cdt`) import through libcdr: all pages, curves, groups, colours, gradients, bitmaps and texture fills, text (checked against the 134 sample files shipped with CorelDRAW 2026; none fails)
- [ ] CorelDRAW import gaps, mostly in libcdr itself: PowerClip clipping, transparency effects, lenses, paragraph text frames
- [x] Bitmaps (SVG `<image>` with embedded data) and bitmap pattern fills
- [x] PDF export (vector, exact page size) and PNG export (dpi, transparency, selection only)
- [ ] PDF with selectable text, CMYK and bleed for print shops
- [ ] "Save for older version" is never needed: document the compatibility promise and test it in CI with files from every past release

## Later

- [x] Linear and radial gradients, object opacity (SVG import/export, including Inkscape-style `xlink:href` gradients)
- Interactive gradient handles on the canvas, editing more than two colours, creating pattern fills, importing bitmaps from files
- Effects (shadow, blend, contour)
- Colour management (CMYK, ICC profiles)
- Plugin / scripting API
