# Roadmap

Rough order of work. Items move as contributors pick them up.

## 0.1 — Foundation (current)

- [x] CMake + Qt 6 project, CI on Linux and Windows
- [x] Document model with undo/redo
- [x] Canvas with page, zoom and pan
- [x] Rectangle and ellipse tools, selection and moving
- [x] Fill colour
- [x] Save and open SVG

## 0.2 — Familiar workspace

- [ ] Property bar: position, size and outline width of the selection
- [ ] Colour palette docked on the right: left click sets fill, right click sets outline, "no colour" swatch
- [ ] Status bar showing the selection's fill and outline
- [ ] Resize handles, multiple selection, rubber-band selection
- [ ] Rulers and guidelines

## 0.3 — Real drawings

- [ ] Paths: Bézier curves, freehand and polyline tools, node editing
- [ ] Groups, ordering (to front / to back), alignment and distribution
- [ ] Text objects
- [ ] Layers and multiple pages
- [ ] Full SVG path support on import and export

## 0.4 — No more locked files

- [ ] CorelDRAW (`.cdr`) import through libcdr
- [ ] PDF and PNG export
- [ ] Versioned native format: every file records the format version, newer files open in older OpenDraw releases with a clear notice about unsupported features instead of a refusal

## Later

- Gradients, patterns and transparency
- Effects (shadow, blend, contour)
- Colour management (CMYK, ICC profiles)
- Plugin / scripting API
