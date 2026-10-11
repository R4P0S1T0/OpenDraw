#include "core/TextLayout.h"

#include <QFont>
#include <QFontMetricsF>
#include <QLineF>

#include <algorithm>
#include <functional>

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

struct Span {
    int start;  // within the paragraph
    int length; // drawn characters
    int next;   // where the following line starts
};

// Greedy wrapping of one paragraph to `width`: lines break after spaces; a word wider than
// the frame is broken between characters.
std::vector<Span> wrap(const QString &paragraph, const std::function<double(int, int)> &advance, double width)
{
    std::vector<Span> spans;
    const int n = int(paragraph.size());
    int start = 0;
    while (start < n) {
        int fit = -1;
        for (int j = start; j <= n; ++j) {
            if (j < n && paragraph[j] != QLatin1Char(' '))
                continue;
            if (j > start && advance(start, j - start) > width)
                break;
            fit = j;
            if (j == n)
                break;
        }
        int end = fit;
        int next = fit;
        if (fit <= start) {
            // Not even one word fits: break inside it (at least one character per line).
            end = start + 1;
            while (end < n && paragraph[end] != QLatin1Char(' ') && advance(start, end + 1 - start) <= width)
                ++end;
            next = end;
        }
        while (next < n && paragraph[next] == QLatin1Char(' '))
            ++next;
        spans.push_back({start, end - start, next});
        start = next;
    }
    if (spans.empty())
        spans.push_back({0, 0, 0});
    return spans;
}

} // namespace

Layout layout(const TextData &text, const QPointF &origin)
{
    const QFont font = referenceFont(text.format);
    const QFontMetricsF metrics(font);
    const double scale = scaleOf(text.format);
    const bool paragraph = text.isParagraph();

    Layout result;
    result.ascent = metrics.ascent() * scale;
    result.descent = metrics.descent() * scale;
    const double lineHeight = text.format.size * text.format.lineSpacing;
    const double frameWidth = text.frame.width();
    const double frameBottom = origin.y() + text.frame.height();
    const double firstBaseline = paragraph ? origin.y() + result.ascent : origin.y();

    const QStringList paragraphs = text.text.split(QLatin1Char('\n'));
    int offset = 0; // index of the paragraph's first character in the whole text
    for (int p = 0; p < paragraphs.size(); ++p) {
        const QString &para = paragraphs[p];
        std::vector<Span> spans;
        if (paragraph) {
            auto advance = [&](int from, int count) { return metrics.horizontalAdvance(para.mid(from, count)) * scale; };
            spans = wrap(para, advance, frameWidth);
        } else {
            spans.push_back({0, int(para.size()), int(para.size())});
        }
        for (std::size_t s = 0; s < spans.size(); ++s) {
            Line line;
            line.text = para.mid(spans[s].start, spans[s].length);
            line.width = metrics.horizontalAdvance(line.text) * scale;
            line.start = offset + spans[s].start;
            line.next = s + 1 < spans.size() ? offset + spans[s + 1].start : offset + int(para.size()) + 1;
            line.softBreak = s + 1 < spans.size();
            double x = origin.x();
            const double room = paragraph ? frameWidth : 0.0;
            if (text.format.align == TextAlign::Center)
                x += paragraph ? (room - line.width) / 2.0 : -line.width / 2.0;
            else if (text.format.align == TextAlign::Right)
                x += paragraph ? room - line.width : -line.width;
            const double baseline = firstBaseline + double(result.lines.size()) * lineHeight;
            line.baselineStart = QPointF(x, baseline);
            if (paragraph && text.frame.height() > 0.0 && baseline + result.descent > frameBottom + 1e-6) {
                line.hidden = true;
                result.overflow = true;
            }
            result.lines.push_back(line);

            if (!paragraph) {
                // An empty line still gets some width so the text box stays visible and clickable.
                const double boxWidth = std::max(line.width, text.format.size * 0.3);
                double boxLeft = x;
                if (line.width < boxWidth && text.format.align == TextAlign::Center)
                    boxLeft = origin.x() - boxWidth / 2.0;
                else if (line.width < boxWidth && text.format.align == TextAlign::Right)
                    boxLeft = origin.x() - boxWidth;
                const QRectF box(boxLeft, baseline - result.ascent, boxWidth, result.ascent + result.descent);
                result.bounds = result.bounds.isNull() ? box : result.bounds.united(box);
            }
        }
        offset += int(para.size()) + 1;
    }
    if (paragraph)
        result.bounds = QRectF(origin, QSizeF(frameWidth, std::max(text.frame.height(), 1.0)));
    return result;
}

QPainterPath outline(const TextData &text, const QPointF &origin)
{
    const QFont font = referenceFont(text.format);
    const double scale = scaleOf(text.format);
    const Layout l = layout(text, origin);

    QPainterPath result;
    for (const Line &line : l.lines) {
        if (line.text.isEmpty() || line.hidden)
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

    // The line holding `index`: the last one starting at or before it. At a wrap, the caret
    // goes to the start of the next line.
    index = std::clamp(index, 0, int(text.text.size()));
    std::size_t lineIndex = 0;
    for (std::size_t i = 0; i < l.lines.size(); ++i) {
        if (l.lines[i].start <= index)
            lineIndex = i;
    }
    const Line &line = l.lines[lineIndex];
    const int inLine = std::clamp(index - line.start, 0, int(line.text.size()));
    const double x = line.baselineStart.x() + metrics.horizontalAdvance(line.text.left(inLine)) * scale;
    const double y = line.baselineStart.y();
    return QLineF(x, y - l.ascent, x, y + l.descent);
}

} // namespace opendraw::textlayout
