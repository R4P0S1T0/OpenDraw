#pragma once

#include "core/Shape.h"

#include <QRectF>

#include <memory>
#include <vector>

// Shapes built from a few parameters by the object tools. Each fits the box it is drawn in.
namespace opendraw::generators {

// Regular polygon with `sides` corners, the first at the top, stretched to the box.
PathData polygon(const QRectF &box, int sides);
// Star with `points` tips. `sharpness` 0–100: how deep the notches between tips go.
PathData star(const QRectF &box, int points, double sharpness);
// Spiral winding outwards from the centre, `revolutions` turns.
PathData spiral(const QRectF &box, double revolutions);
// Straight line between two points.
PathData line(const QPointF &from, const QPointF &to);
// Grid of `columns` × `rows` rectangles, grouped.
std::unique_ptr<Shape> graphPaper(const QRectF &box, int columns, int rows, const Style &style);

} // namespace opendraw::generators
