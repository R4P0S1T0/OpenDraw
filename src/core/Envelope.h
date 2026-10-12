#pragma once

#include "core/Shape.h"

#include <QPainterPath>
#include <QTransform>

// Envelopes (see the Envelope struct in Shape.h): the object's box is mapped onto a frame of
// four cubic edges (a Coons patch), and its outline is bent with it.
namespace opendraw::envelope {

// The point of the frame at (u, v), each from 0 to 1 across the object's box.
QPointF map(const Envelope &envelope, const QPointF &unit);

// `path`, lying in `box`, bent so the box fills the envelope. Curves and long straight edges
// are divided finely enough to bend smoothly.
QPainterPath warp(const Envelope &envelope, const QPainterPath &path, const QRectF &box);

// The envelope of an object whose own geometry was transformed by `unit` (in box fractions,
// e.g. a mirror), so it bends the same way on the result.
Envelope transformed(const Envelope &envelope, const QTransform &unit);

// The frame's four edges, in box fractions.
QPainterPath outline(const Envelope &envelope);

} // namespace opendraw::envelope
