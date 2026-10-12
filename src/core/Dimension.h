#pragma once

#include "core/Shape.h"

#include <QPainterPath>
#include <QString>

// Dimension lines (the Dimension struct in Shape.h): two extension lines from the measured
// points, the dimension line between them with arrowheads, and the measurement as text.
namespace opendraw::dimension {

// The measurement as shown, e.g. "42.50 mm".
QString label(const Dimension &dimension);

// The extension lines and the dimension line, as an open path.
QPainterPath lines(const Dimension &dimension);
// The arrowheads at both ends of the dimension line, for an outline `width` wide.
QPainterPath arrowheads(const Dimension &dimension, double width);
// The label's letters, placed beside the middle of the dimension line and turned along it
// (never upside down).
QPainterPath labelOutline(const Dimension &dimension);
// The box around the label.
QPolygonF labelBox(const Dimension &dimension);

// Nodes for the dimension's shape: its lines and the box around its label (so it can be
// measured and clicked).
PathData geometry(const Dimension &dimension);

} // namespace opendraw::dimension
