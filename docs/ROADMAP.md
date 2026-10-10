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
- [ ] Multiple selection, rubber-band selection
- [ ] Rulers and guidelines
- [ ] Keep unsupported SVG elements when saving, so round-tripping a newer file loses nothing

## 0.3 — Real drawings (current)

- [x] Paths: Bézier and freehand tools, node editing (move, add, delete, smooth/corner handles)
- [x] Convert rectangles and ellipses to curves
- [x] Full SVG path support on import and export, plus transforms, groups and inherited styles
- [ ] Node type commands (make smooth / make corner / make line), break and join nodes
- [ ] Groups, ordering (to front / to back), alignment and distribution
- [ ] Text objects
- [ ] Layers and multiple pages

## 0.4 — No more locked files

- [ ] CorelDRAW (`.cdr`) import through libcdr
- [ ] PDF and PNG export
- [ ] "Save for older version" is never needed: document the compatibility promise and test it in CI with files from every past release

## Later

- Gradients, patterns and transparency
- Effects (shadow, blend, contour)
- Colour management (CMYK, ICC profiles)
- Plugin / scripting API
