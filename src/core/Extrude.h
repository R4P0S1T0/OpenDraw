#pragma once

#include "core/Shape.h"

#include <QLineF>
#include <QPolygonF>

#include <vector>

// Extrusions (Style::extrusion): side faces worked out from an object's outline whenever
// needed, so they follow any change to it.
namespace opendraw::extrude {

struct Face {
    QPolygonF polygon;
    QColor color;
    // Edges drawn with the object's outline: the back edge, and the edges back from sharp
    // corners (not those between the many faces of a curve).
    std::vector<QLineF> lines;
};

// Where the vanishing point of a shape's extrusion is on the page.
QPointF vanishingPoint(const Shape &shape);
// A point of the front pulled back along the extrusion.
QPointF back(const Shape &shape, const QPointF &front);

// The faces to draw under the object, back to front: the back copy, then the sides from the
// one nearest the vanishing point outwards. Empty without an extrusion.
std::vector<Face> faces(const Shape &shape);

} // namespace opendraw::extrude
