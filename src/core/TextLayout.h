#pragma once

#include "core/Shape.h"

#include <QLineF>
#include <QPainterPath>
#include <QRectF>

#include <vector>

// Lays out text objects. Fonts are measured at a fixed reference size and scaled, so the
// result does not depend on the screen's DPI. Needs a QGuiApplication (font database).
//
// Artistic text: `origin` is on the first baseline, at the left edge, centre or right edge
// depending on the alignment; lines break only at '\n'.
// Paragraph text: `origin` is the top-left corner of the frame; lines also wrap at spaces (or
// inside a word too long for the frame) to fit its width.
namespace opendraw::textlayout {

struct Line {
    QString text;          // what is drawn (without the spaces at a wrap)
    QPointF baselineStart; // left end of the baseline
    double width = 0.0;
    int start = 0;         // index of the line's first character in TextData::text
    int next = 0;          // index where the following line starts
    bool softBreak = false; // wrapped, rather than ended by '\n' or the end of the text
    bool hidden = false;   // paragraph text below the bottom of its frame
};

struct Layout {
    std::vector<Line> lines;
    double ascent = 0.0;
    double descent = 0.0;
    QRectF bounds; // never empty, so an empty text can still be clicked and edited
    bool overflow = false; // paragraph text with lines that do not fit its frame
};

Layout layout(const TextData &text, const QPointF &origin);

// Glyph outlines of the whole text (hidden lines left out).
QPainterPath outline(const TextData &text, const QPointF &origin);

// Caret position for a character index into `text.text`: top and bottom of the caret line.
QLineF caret(const TextData &text, const QPointF &origin, int index);

} // namespace opendraw::textlayout
