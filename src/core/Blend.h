#pragma once

#include "core/Shape.h"

#include <memory>
#include <vector>

// Blends: a row of shapes that change step by step from one object into another (outline,
// colours, outline width, opacity). A blend is a group of the two objects (see
// Shape::blendSteps()); the steps between them are worked out from the two whenever needed, so
// they follow any change to either end.
namespace opendraw::blend {

// The colour a fraction `t` of the way from `a` to `b`. A fully transparent end takes the
// other's hue, so fading in or out does not pass through grey.
QColor mix(const QColor &a, const QColor &b, double t);

// Rectangles, ellipses, curves and texts can blend; groups and bitmaps cannot.
bool canBlend(const Shape &shape);

// `steps` shapes between `from` and `to` (the two themselves left out), evenly spaced, from the
// one nearest `from` to the one nearest `to`.
std::vector<std::unique_ptr<Shape>> intermediates(const Shape &from, const Shape &to, int steps);

} // namespace opendraw::blend
