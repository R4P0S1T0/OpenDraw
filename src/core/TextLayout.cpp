#include "core/TextLayout.h"

#include "core/RichText.h"

#include <QFont>
#include <QFontMetricsF>
#include <QLineF>

#include <algorithm>
#include <functional>
#include <map>
#include <tuple>

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

// Measures pieces of a text, each in its own format.
class Measurer {
public:
    explicit Measurer(const TextData &text)
        : m_text(text.text)
    {
        // Stretches of characters in one format, covering the whole text.
        const std::vector<CharFormat> formats = richtext::perCharacter(text);
        int start = 0;
        for (int i = 1; i <= int(formats.size()); ++i) {
            if (i == int(formats.size()) || !(formats[i] == formats[start])) {
                Segment s;
                s.start = start;
                s.end = i;
                s.format = richtext::effectiveFormat(text, start);
                s.color = formats[start].color;
                m_segments.push_back(s);
                start = i;
            }
        }
        m_base = text.format;
    }

    const QFontMetricsF &metrics(const TextFormat &format)
    {
        const auto key = std::make_tuple(format.family, format.bold, format.italic);
        auto it = m_metrics.find(key);
        if (it == m_metrics.end())
            it = m_metrics.emplace(key, QFontMetricsF(referenceFont(format))).first;
        return it->second;
    }

    // Width of the characters [from, to) (no line breaks inside).
    double advance(int from, int to)
    {
        double width = 0.0;
        forEachPiece(from, to, [&](int a, int b, const TextFormat &f, const std::optional<QColor> &) {
            width += metrics(f).horizontalAdvance(m_text.mid(a, b - a)) * f.size / kReferencePixelSize;
        });
        return width;
    }

    template<typename F>
    void forEachPiece(int from, int to, F &&f)
    {
        for (const Segment &s : m_segments) {
            const int a = std::max(from, s.start);
            const int b = std::min(to, s.end);
            if (a < b)
                f(a, b, s.format, s.color);
        }
    }

    // Format at a character (or the one before, at the end of the text).
    TextFormat formatAt(int index) const
    {
        for (const Segment &s : m_segments) {
            if (index >= s.start && index < s.end)
                return s.format;
        }
        return m_segments.empty() ? m_base : m_segments.back().format;
    }

private:
    struct Segment {
        int start = 0;
        int end = 0;
        TextFormat format;
        std::optional<QColor> color;
    };
    QString m_text;
    TextFormat m_base;
    std::vector<Segment> m_segments;
    std::map<std::tuple<QString, bool, bool>, QFontMetricsF> m_metrics;
};

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
    Measurer measure(text);
    const bool paragraph = text.isParagraph();

    Layout result;
    const double frameWidth = text.frame.width();
    const double frameBottom = origin.y() + text.frame.height();

    const QStringList paragraphs = text.text.split(QLatin1Char('\n'));
    int offset = 0; // index of the paragraph's first character in the whole text
    double baseline = 0.0;
    for (int p = 0; p < paragraphs.size(); ++p) {
        const QString &para = paragraphs[p];
        std::vector<Span> spans;
        if (paragraph) {
            auto advance = [&](int from, int count) { return measure.advance(offset + from, offset + from + count); };
            spans = wrap(para, advance, frameWidth);
        } else {
            spans.push_back({0, int(para.size()), int(para.size())});
        }
        for (std::size_t s = 0; s < spans.size(); ++s) {
            Line line;
            line.start = offset + spans[s].start;
            const int end = line.start + spans[s].length;
            line.text = text.text.mid(line.start, spans[s].length);
            line.next = s + 1 < spans.size() ? offset + spans[s + 1].start : offset + int(para.size()) + 1;
            line.softBreak = s + 1 < spans.size();

            // Pieces, and the line's height from its largest letters.
            double size = 0.0;
            double x = 0.0;
            measure.forEachPiece(line.start, end, [&](int a, int b, const TextFormat &f, const std::optional<QColor> &color) {
                const QFontMetricsF &m = measure.metrics(f);
                const double scale = f.size / kReferencePixelSize;
                Piece piece;
                piece.start = a;
                piece.length = b - a;
                piece.x = x;
                piece.width = m.horizontalAdvance(text.text.mid(a, b - a)) * scale;
                piece.format = f;
                piece.color = color;
                x += piece.width;
                line.ascent = std::max(line.ascent, m.ascent() * scale);
                line.descent = std::max(line.descent, m.descent() * scale);
                size = std::max(size, f.size);
                line.pieces.push_back(piece);
            });
            if (line.pieces.empty()) {
                const TextFormat f = measure.formatAt(line.start > 0 ? line.start - 1 : 0);
                const QFontMetricsF &m = measure.metrics(f);
                line.ascent = m.ascent() * f.size / kReferencePixelSize;
                line.descent = m.descent() * f.size / kReferencePixelSize;
                size = f.size;
            }
            line.width = x;

            if (result.lines.empty())
                baseline = paragraph ? origin.y() + line.ascent : origin.y();
            else
                baseline += size * text.format.lineSpacing;

            double left = origin.x();
            if (text.format.align == TextAlign::Center)
                left += paragraph ? (frameWidth - line.width) / 2.0 : -line.width / 2.0;
            else if (text.format.align == TextAlign::Right)
                left += paragraph ? frameWidth - line.width : -line.width;
            line.baselineStart = QPointF(left, baseline);
            for (Piece &piece : line.pieces)
                piece.x += left;
            if (paragraph && text.frame.height() > 0.0 && baseline + line.descent > frameBottom + 1e-6) {
                line.hidden = true;
                result.overflow = true;
            }

            if (!paragraph) {
                // An empty line still gets some width so the text box stays visible and clickable.
                const double boxWidth = std::max(line.width, size * 0.3);
                double boxLeft = left;
                if (line.width < boxWidth && text.format.align == TextAlign::Center)
                    boxLeft = origin.x() - boxWidth / 2.0;
                else if (line.width < boxWidth && text.format.align == TextAlign::Right)
                    boxLeft = origin.x() - boxWidth;
                const QRectF box(boxLeft, baseline - line.ascent, boxWidth, line.ascent + line.descent);
                result.bounds = result.bounds.isNull() ? box : result.bounds.united(box);
            }
            result.lines.push_back(std::move(line));
        }
        offset += int(para.size()) + 1;
    }
    result.ascent = result.lines.front().ascent;
    result.descent = result.lines.front().descent;
    if (paragraph)
        result.bounds = QRectF(origin, QSizeF(frameWidth, std::max(text.frame.height(), 1.0)));
    return result;
}

std::vector<ColoredPath> coloredOutline(const TextData &text, const QPointF &origin)
{
    const Layout l = layout(text, origin);
    std::vector<ColoredPath> result;
    for (const Line &line : l.lines) {
        if (line.hidden)
            continue;
        for (const Piece &piece : line.pieces) {
            const QString chars = text.text.mid(piece.start, piece.length);
            if (chars.trimmed().isEmpty())
                continue;
            QPainterPath glyphs;
            glyphs.addText(0, 0, referenceFont(piece.format), chars);
            const double scale = piece.format.size / kReferencePixelSize;
            QTransform t;
            t.translate(piece.x, line.baselineStart.y());
            t.scale(scale, scale);
            auto it = std::find_if(result.begin(), result.end(),
                                   [&](const ColoredPath &c) { return c.color == piece.color; });
            if (it == result.end())
                it = result.insert(result.end(), ColoredPath{QPainterPath(), piece.color});
            it->path.addPath(t.map(glyphs));
        }
    }
    return result;
}

QPainterPath outline(const TextData &text, const QPointF &origin)
{
    QPainterPath result;
    for (const ColoredPath &part : coloredOutline(text, origin))
        result.addPath(part.path);
    return result;
}

std::size_t lineOf(const Layout &l, int index)
{
    // The last line starting at or before `index`: at a wrap, the next line.
    std::size_t lineIndex = 0;
    for (std::size_t i = 0; i < l.lines.size(); ++i) {
        if (l.lines[i].start <= index)
            lineIndex = i;
    }
    return lineIndex;
}

std::vector<QRectF> selectionRects(const TextData &text, const QPointF &origin, int start, int end)
{
    std::vector<QRectF> rects;
    if (start >= end)
        return rects;
    Measurer measure(text);
    const Layout l = layout(text, origin);
    for (const Line &line : l.lines) {
        if (line.hidden)
            continue;
        const int lineEnd = line.start + int(line.text.size());
        const int a = std::max(start, line.start);
        const int b = std::min(end, lineEnd);
        // A selected line break shows as a little extra width at the end of the line.
        const bool breakSelected = end > lineEnd && start <= lineEnd;
        if (a > b || (a == b && !breakSelected))
            continue;
        const double x1 = line.baselineStart.x() + measure.advance(line.start, a);
        double x2 = line.baselineStart.x() + measure.advance(line.start, b);
        if (breakSelected)
            x2 += (line.ascent + line.descent) * 0.25;
        rects.emplace_back(QPointF(x1, line.baselineStart.y() - line.ascent),
                           QPointF(x2, line.baselineStart.y() + line.descent));
    }
    return rects;
}

QLineF caret(const TextData &text, const QPointF &origin, int index)
{
    Measurer measure(text);
    const Layout l = layout(text, origin);
    index = std::clamp(index, 0, int(text.text.size()));
    const Line &line = l.lines[lineOf(l, index)];
    const int inLine = std::clamp(index, line.start, line.start + int(line.text.size()));
    const double x = line.baselineStart.x() + measure.advance(line.start, inLine);
    const double y = line.baselineStart.y();
    return QLineF(x, y - line.ascent, x, y + line.descent);
}

int indexAt(const TextData &text, const QPointF &origin, const QPointF &point)
{
    Measurer measure(text);
    const Layout l = layout(text, origin);
    // The line whose band holds the point, or the nearest one.
    const Line *best = &l.lines.front();
    double bestDistance = 1e300;
    for (const Line &line : l.lines) {
        if (line.hidden)
            continue;
        const double top = line.baselineStart.y() - line.ascent;
        const double bottom = line.baselineStart.y() + line.descent;
        const double d = point.y() < top ? top - point.y() : point.y() > bottom ? point.y() - bottom : 0.0;
        if (d < bestDistance) {
            bestDistance = d;
            best = &line;
        }
    }
    int index = best->start;
    double nearest = 1e300;
    for (int k = 0; k <= int(best->text.size()); ++k) {
        const double x = best->baselineStart.x() + measure.advance(best->start, best->start + k);
        if (std::abs(x - point.x()) < nearest) {
            nearest = std::abs(x - point.x());
            index = best->start + k;
        }
    }
    return index;
}

} // namespace opendraw::textlayout
