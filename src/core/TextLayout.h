#pragma once

#include "core/Shape.h"

#include <QLineF>
#include <QPainterPath>
#include <QRectF>

#include <optional>
#include <vector>

// Lays out text objects. Fonts are measured at a fixed reference size and scaled, so the
// result does not depend on the screen's DPI. Needs a QGuiApplication (font database).
//
// Artistic text: `origin` is on the first baseline, at the left edge, centre or right edge
// depending on the alignment; lines break only at '\n'.
// Paragraph text: `origin` is the top-left corner of the frame; lines also wrap at spaces (or
// inside a word too long for the frame) to fit its width, less the indents; justified lines
// widen their spaces. Paragraphs of both kinds are apart by the space before and after them.
// Parts of a text can have their own font, size, weight, slant and colour (TextData::runs);
// a line is as tall as its largest letters.
namespace opendraw::textlayout {

// A stretch of a line in one format.
struct Piece {
    int start = 0;   // index into TextData::text
    int length = 0;
    double x = 0.0;  // left end, on the page
    double width = 0.0;
    TextFormat format;
    std::optional<QColor> color; // the letters' own colour, if any
};

struct Line {
    QString text;          // what is drawn (without the spaces at a wrap)
    QPointF baselineStart; // left end of the baseline
    double width = 0.0;
    double wordSpacing = 0.0; // added to each space of a justified line
    int start = 0;         // index of the line's first character in TextData::text
    int next = 0;          // index where the following line starts
    bool softBreak = false; // wrapped, rather than ended by '\n' or the end of the text
    bool hidden = false;   // paragraph text below the bottom of its frame
    double ascent = 0.0;
    double descent = 0.0;
    std::vector<Piece> pieces;
};

struct Layout {
    std::vector<Line> lines;
    double ascent = 0.0;   // of the first line
    double descent = 0.0;
    QRectF bounds; // never empty, so an empty text can still be clicked and edited
    bool overflow = false; // paragraph text with lines that do not fit its frame
};

Layout layout(const TextData &text, const QPointF &origin);

// Glyph outlines of the whole text (hidden lines left out).
QPainterPath outline(const TextData &text, const QPointF &origin);

// The same outlines split by letter colour (no colour: the text's own fill).
struct ColoredPath {
    QPainterPath path;
    std::optional<QColor> color;
};
std::vector<ColoredPath> coloredOutline(const TextData &text, const QPointF &origin);

// Caret position for a character index into `text.text`: top and bottom of the caret line.
QLineF caret(const TextData &text, const QPointF &origin, int index);

// The character index nearest to `point` (for placing the caret with the mouse).
int indexAt(const TextData &text, const QPointF &origin, const QPointF &point);

// Highlight boxes for the characters [start, end), one per line.
std::vector<QRectF> selectionRects(const TextData &text, const QPointF &origin, int start, int end);

// The visible line holding `index` (as caret() places it).
std::size_t lineOf(const Layout &layout, int index);

} // namespace opendraw::textlayout
