#pragma once

#include <QPainterPath>
#include <QPointF>

#include <vector>

namespace opendraw {
struct Page;
}

// Smart fill: the area enclosed around a point by the outlines of overlapping objects.
namespace opendraw::smartfill {

// The piece of the drawing that holds `point`: inside every outline that contains it and
// outside every other one. Empty when no outline encloses the point.
QPainterPath regionAt(const std::vector<QPainterPath> &outlines, const QPointF &point);

// The outlines on a page that can enclose areas: every object on its visible layers (the
// members of groups one by one), as filled areas.
std::vector<QPainterPath> outlinesOf(const Page &page);

} // namespace opendraw::smartfill
