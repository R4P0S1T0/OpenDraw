#pragma once

#include "core/Shape.h"

#include <functional>
#include <optional>
#include <vector>

// Editing text whose parts are formatted differently (TextData::runs).
namespace opendraw::richtext {

// The formatting of each character (empty where the text's own format applies).
std::vector<CharFormat> perCharacter(const TextData &text);
// Rebuilds the runs from per-character formatting: drops what repeats the text's format,
// merges neighbours that are alike.
void setPerCharacter(TextData &text, std::vector<CharFormat> formats);

CharFormat formatAt(const TextData &text, int index);
// The full format in effect at `index` (the character there, or before it at the end).
TextFormat effectiveFormat(const TextData &text, int index);
// The letter colour at `index`, if that part of the text has its own.
std::optional<QColor> colorAt(const TextData &text, int index);

// Inserts `inserted` at `position`; it continues the formatting of the character before.
void insert(TextData &text, int position, const QString &inserted);
// Removes the characters in [start, end).
void remove(TextData &text, int start, int end);
// Changes the formatting of [start, end).
void apply(TextData &text, int start, int end, const std::function<void(CharFormat &)> &change);
// Changes the whole text: the base format takes the change and runs stop overriding those fields.
void applyToAll(TextData &text, const CharFormat &change);

// Drops overrides equal to the base format (and to `baseColor`, when given) and tidies the runs.
void normalize(TextData &text, const std::optional<QColor> &baseColor = std::nullopt);

} // namespace opendraw::richtext
