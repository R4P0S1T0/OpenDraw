#pragma once

#include "core/Shape.h"

#include <QLineF>
#include <QPainterPath>
#include <QRectF>

#include <vector>

// Lays out text objects. Fonts are measured at a fixed reference size and scaled, so the
// result does not depend on the screen's DPI. Needs a QGuiApplication (font database).
namespace opendraw::textlayout {

struct Line {
    QString text;
    QPointF baselineStart; // left end of the baseline
    double width = 0.0;
};

struct Layout {
    std::vector<Line> lines;
    double ascent = 0.0;
    double descent = 0.0;
    QRectF bounds; // never empty, so an empty text can still be clicked and edited
};

Layout layout(const TextData &text, const QPointF &origin);

// Glyph outlines of the whole text.
QPainterPath outline(const TextData &text, const QPointF &origin);

// Caret position for a character index into `text.text`: top and bottom of the caret line.
QLineF caret(const TextData &text, const QPointF &origin, int index);

} // namespace opendraw::textlayout
