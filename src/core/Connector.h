#pragma once

#include "core/Shape.h"

// Connectors (the Connector struct in Shape.h): lines that stay joined to two objects.
namespace opendraw::connector {

// The nodes of a connector between two objects: straight, from where the line between their
// centres leaves one outline to where it enters the other; or right-angled, from the middle of
// the facing sides of their boxes, turning halfway.
PathData route(const Shape &from, const Shape &to, bool rightAngle);

} // namespace opendraw::connector
