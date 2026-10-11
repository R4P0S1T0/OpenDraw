# Contributing to OpenDraw

Thanks for helping. Bug reports, feature ideas and pull requests are all welcome.

## Building

See the [README](README.md#building). Run the tests with `ctest --test-dir build` before opening a pull request; CI runs the same build and tests on Linux and Windows.

## Code style

- C++20, Qt 6. Follow the style of the surrounding code: 4-space indentation, `m_` prefix for members, everything in the `opendraw` namespace.
- Keep `src/core` free of widget code. It holds the document model and file formats and must stay testable without a GUI.
- Every change to a document goes through a `QUndoCommand` so it can be undone.
- Add a test in `tests/` for new behaviour in `src/core`.

## Translations

Interface translations live in `translations/opendraw_<language>.ts` (Qt Linguist format) and are compiled into the program. User-visible strings go through `tr()` in classes with `Q_OBJECT`, or `QCoreApplication::translate("Context", "text")` elsewhere, so `lupdate` can find them.

- After adding or changing strings, refresh the files from the source folder (needs Qt's Linguist tools):
  `lupdate -locations none -no-obsolete -extensions cpp,h -recursive src -ts translations/opendraw_pt_BR.ts`
- Translate with Qt Linguist (`linguist translations/opendraw_pt_BR.ts`) or any editor that understands `.ts` files. Keep `%1`, `%n`, `&` mnemonics and keyboard shortcuts in the text.
- For a new language, run the same `lupdate` command with a new file name, e.g. `translations/opendraw_es.ts`; the build picks it up automatically and it appears in View › Language.

## Clean-room rule (important)

OpenDraw aims to be familiar to people coming from other vector editors, including proprietary ones. To keep the project legally safe:

- **Do not** copy, translate or adapt code from proprietary software, including decompiled or disassembled code, leaked source, or SDK samples with restrictive licences.
- **Do not** add icons, images, fonts, palettes, templates or other assets from proprietary products. Draw or source assets under a licence compatible with GPL-3.0.
- **Do not** use product names or logos of other companies as OpenDraw branding.
- File-format support must be written from public documentation, from observing files the contributor created themselves, or by using libraries with compatible licences (for example [libcdr](https://wiki.documentfoundation.org/DLP/Libraries/libcdr), MPL-2.0).
- Matching a familiar *workflow* is fine: common layouts, tool behaviour and keyboard shortcuts are not copyrightable.

If you have seen proprietary source code for the feature you are working on, say so in the pull request so maintainers can decide how to handle it.

## Licence

By contributing you agree that your contribution is licensed under the GNU General Public License v3.0 or later.
