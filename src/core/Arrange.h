#pragma once

#include <QPointF>
#include <QRectF>

#include <vector>

// Alignment and distribution as pure geometry: given bounding boxes, how far to move each one.
namespace opendraw::arrange {

enum class Alignment {
    Left,
    Right,
    Top,
    Bottom,
    CentersHorizontally, // same horizontal centre (objects line up on a vertical axis)
    CentersVertically,   // same vertical centre (objects line up on a horizontal axis)
};

// Offsets that align every box to `reference` (usually the selection's bounds, or the page).
std::vector<QPointF> alignOffsets(const std::vector<QRectF> &boxes, Alignment alignment, const QRectF &reference);

// Offsets that leave equal gaps between the boxes along one axis, keeping the outermost
// boxes in place. Needs at least three boxes; otherwise every offset is zero.
std::vector<QPointF> distributeOffsets(const std::vector<QRectF> &boxes, Qt::Orientation orientation);

// Union of all boxes.
QRectF unite(const std::vector<QRectF> &boxes);

} // namespace opendraw::arrange
