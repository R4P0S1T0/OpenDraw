#include "core/TextLayout.h"

#include <QFont>
#include <QFontMetricsF>
#include <QLineF>

#include <algorithm>

namespace opendraw::textlayout {

namespace {

// Fonts are measured at this pixel size and scaled to the requested size.
constexpr int kReferencePixelSize = 100;

QFont referenceFont(const TextFormat &format)
{
    QFont font(format.family);
    font.setPixelSize(kReferencePixelSize);
    font.setBold(format.bold);
    font.setItalic(format.italic);
    font.setKerning(true);
    return font;
}

double scaleOf(const TextFormat &format)
{
    return format.size / kReferencePixelSize;
}

} // namespace

Layout layout(const TextData &text, const QPointF &origin)
{
    const QFont font = referenceFont(text.format);
    const QFontMetricsF metrics(font);
    const double scale = scaleOf(text.format);

    Layout result;
    result.ascent = metrics.ascent() * scale;
    result.descent = metrics.descent() * scale;
    const double lineHeight = text.format.size * text.format.lineSpacing;

    const QStringList lines = text.text.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        Line line;
        line.text = lines[i];
        line.width = metrics.horizontalAdvance(line.text) * scale;
        double x = origin.x();
        if (text.format.align == TextAlign::Center)
            x -= line.width / 2.0;
        else if (text.format.align == TextAlign::Right)
            x -= line.width;
        line.baselineStart = QPointF(x, origin.y() + i * lineHeight);
        result.lines.push_back(line);

        // An empty line still gets some width so the text box stays visible and clickable.
        const double boxWidth = std::max(line.width, text.format.size * 0.3);
        double boxLeft = x;
        if (line.width < boxWidth && text.format.align == TextAlign::Center)
            boxLeft = origin.x() - boxWidth / 2.0;
        else if (line.width < boxWidth && text.format.align == TextAlign::Right)
            boxLeft = origin.x() - boxWidth;
        const QRectF box(boxLeft, line.baselineStart.y() - result.ascent, boxWidth, result.ascent + result.descent);
        result.bounds = result.bounds.isNull() ? box : result.bounds.united(box);
    }
    return result;
}

QPainterPath outline(const TextData &text, const QPointF &origin)
{
    const QFont font = referenceFont(text.format);
    const double scale = scaleOf(text.format);
    const Layout l = layout(text, origin);

    QPainterPath result;
    for (const Line &line : l.lines) {
        if (line.text.isEmpty())
            continue;
        QPainterPath glyphs;
        glyphs.addText(0, 0, font, line.text);
        QTransform t;
        t.translate(line.baselineStart.x(), line.baselineStart.y());
        t.scale(scale, scale);
        result.addPath(t.map(glyphs));
    }
    return result;
}

QLineF caret(const TextData &text, const QPointF &origin, int index)
{
    const QFont font = referenceFont(text.format);
    const QFontMetricsF metrics(font);
    const double scale = scaleOf(text.format);
    const Layout l = layout(text, origin);

    // Find the line holding `index` and the offset inside it.
    int remaining = std::clamp(index, 0, int(text.text.size()));
    int lineIndex = 0;
    while (lineIndex < int(l.lines.size()) - 1 && remaining > l.lines[lineIndex].text.size()) {
        remaining -= int(l.lines[lineIndex].text.size()) + 1; // +1 for the '\n'
        ++lineIndex;
    }
    const Line &line = l.lines[lineIndex];
    const double x = line.baselineStart.x() + metrics.horizontalAdvance(line.text.left(remaining)) * scale;
    const double y = line.baselineStart.y();
    return QLineF(x, y - l.ascent, x, y + l.descent);
}

} // namespace opendraw::textlayout
