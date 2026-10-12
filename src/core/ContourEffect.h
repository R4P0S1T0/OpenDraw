#pragma once

#include "core/Shape.h"

#include <QPainterPath>

#include <vector>

// Contours: rings around or inside an object (Style::contour), worked out from its outline
// whenever needed, so they follow any change to it.
namespace opendraw::contour {

// The outline grown (distance > 0) or shrunk (distance < 0) by `distance` all round, with
// rounded corners. Empty when shrinking leaves nothing.
QPainterPath offsetPath(const QPainterPath &outline, double distance);

struct Ring {
    QPainterPath path;
    Style style;
};

// The rings of a shape's contour, in paint order: outside rings from the outermost in (all
// under the object), inside rings from the outermost in (all over it). Empty without a contour.
std::vector<Ring> rings(const Shape &shape);

// How far outside rings reach beyond the shape's outline.
double reach(const Style &style);

} // namespace opendraw::contour
