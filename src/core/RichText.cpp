#include "core/RichText.h"

#include <algorithm>

namespace opendraw::richtext {

namespace {

// Clears the fields of `format` that only repeat the base.
void dropRedundant(CharFormat &format, const TextFormat &base, const std::optional<QColor> &baseColor)
{
    if (format.family && *format.family == base.family)
        format.family.reset();
    if (format.size && std::abs(*format.size - base.size) < 1e-9)
        format.size.reset();
    if (format.bold && *format.bold == base.bold)
        format.bold.reset();
    if (format.italic && *format.italic == base.italic)
        format.italic.reset();
    if (format.color && baseColor && *format.color == *baseColor)
        format.color.reset();
}

} // namespace

std::vector<CharFormat> perCharacter(const TextData &text)
{
    std::vector<CharFormat> formats(text.text.size());
    for (const TextRun &run : text.runs) {
        const int end = std::min<int>(run.start + run.length, int(formats.size()));
        for (int i = std::max(run.start, 0); i < end; ++i)
            formats[i] = run.format;
    }
    return formats;
}

void setPerCharacter(TextData &text, std::vector<CharFormat> formats)
{
    formats.resize(text.text.size());
    text.runs.clear();
    for (int i = 0; i < int(formats.size()); ++i) {
        dropRedundant(formats[i], text.format, std::nullopt);
        if (text.text[i] == QLatin1Char('\n'))
            formats[i] = {}; // a line break shows nothing to format
        if (formats[i].isEmpty())
            continue;
        if (!text.runs.empty() && text.runs.back().start + text.runs.back().length == i
            && text.runs.back().format == formats[i])
            ++text.runs.back().length;
        else
            text.runs.push_back({i, 1, formats[i]});
    }
}

CharFormat formatAt(const TextData &text, int index)
{
    for (const TextRun &run : text.runs) {
        if (index >= run.start && index < run.start + run.length)
            return run.format;
    }
    return {};
}

TextFormat effectiveFormat(const TextData &text, int index)
{
    if (index >= int(text.text.size()))
        index = int(text.text.size()) - 1;
    TextFormat format = text.format;
    if (index < 0)
        return format;
    const CharFormat c = formatAt(text, index);
    if (c.family)
        format.family = *c.family;
    if (c.size)
        format.size = *c.size;
    if (c.bold)
        format.bold = *c.bold;
    if (c.italic)
        format.italic = *c.italic;
    return format;
}

std::optional<QColor> colorAt(const TextData &text, int index)
{
    return formatAt(text, index).color;
}

void insert(TextData &text, int position, const QString &inserted)
{
    position = std::clamp(position, 0, int(text.text.size()));
    std::vector<CharFormat> formats = perCharacter(text);
    // Continue the last letter before (line breaks carry no format), or the first one.
    int from = position - 1;
    while (from >= 0 && text.text[from] == QLatin1Char('\n'))
        --from;
    const CharFormat continued = from >= 0 ? formats[from] : (formats.empty() ? CharFormat() : formats[0]);
    text.text.insert(position, inserted);
    formats.insert(formats.begin() + position, inserted.size(), continued);
    setPerCharacter(text, std::move(formats));
}

void remove(TextData &text, int start, int end)
{
    start = std::clamp(start, 0, int(text.text.size()));
    end = std::clamp(end, start, int(text.text.size()));
    if (start == end)
        return;
    std::vector<CharFormat> formats = perCharacter(text);
    text.text.remove(start, end - start);
    formats.erase(formats.begin() + start, formats.begin() + end);
    setPerCharacter(text, std::move(formats));
}

void apply(TextData &text, int start, int end, const std::function<void(CharFormat &)> &change)
{
    start = std::clamp(start, 0, int(text.text.size()));
    end = std::clamp(end, start, int(text.text.size()));
    std::vector<CharFormat> formats = perCharacter(text);
    for (int i = start; i < end; ++i)
        change(formats[i]);
    setPerCharacter(text, std::move(formats));
}

void applyToAll(TextData &text, const CharFormat &change)
{
    if (change.family)
        text.format.family = *change.family;
    if (change.size)
        text.format.size = *change.size;
    if (change.bold)
        text.format.bold = *change.bold;
    if (change.italic)
        text.format.italic = *change.italic;
    std::vector<CharFormat> formats = perCharacter(text);
    for (CharFormat &f : formats) {
        if (change.family)
            f.family.reset();
        if (change.size)
            f.size.reset();
        if (change.bold)
            f.bold.reset();
        if (change.italic)
            f.italic.reset();
        if (change.color)
            f.color = change.color;
    }
    setPerCharacter(text, std::move(formats));
}

void normalize(TextData &text, const std::optional<QColor> &baseColor)
{
    std::vector<CharFormat> formats = perCharacter(text);
    for (CharFormat &f : formats)
        dropRedundant(f, text.format, baseColor);
    setPerCharacter(text, std::move(formats));
}

} // namespace opendraw::richtext
