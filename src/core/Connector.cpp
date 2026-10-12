#include "core/Connector.h"

#include <QLineF>
#include <QPainterPath>

#include <cmath>

namespace opendraw::connector {

namespace {

// Where the line from the shape's centre towards `target` crosses its outline.
QPointF edgePoint(const Shape &shape, const QPointF &target)
{
    const QRectF box = shape.bounds();
    const QPointF centre = box.center();
    const QPainterPath outline = shape.path();
    if (outline.contains(centre) && !outline.contains(target)) {
        double inside = 0.0, outside = 1.0;
        for (int i = 0; i < 30; ++i) {
            const double t = (inside + outside) / 2.0;
            if (outline.contains(centre + (target - centre) * t))
                inside = t;
            else
                outside = t;
        }
        return centre + (target - centre) * outside;
    }
    // Hollow or odd shapes: where the line leaves the box.
    const QPointF d = target - centre;
    double t = 1.0;
    if (std::abs(d.x()) > 1e-9)
        t = std::min(t, (box.width() / 2.0) / std::abs(d.x()));
    if (std::abs(d.y()) > 1e-9)
        t = std::min(t, (box.height() / 2.0) / std::abs(d.y()));
    return centre + d * t;
}

} // namespace

PathData route(const Shape &from, const Shape &to, bool rightAngle)
{
    const QRectF a = from.bounds(), b = to.bounds();
    const QPointF ca = a.center(), cb = b.center();
    SubPath line;
    if (!rightAngle) {
        line.nodes = {PathNode::corner(edgePoint(from, cb)), PathNode::corner(edgePoint(to, ca))};
        return {line};
    }
    const double dx = cb.x() - ca.x(), dy = cb.y() - ca.y();
    if (std::abs(dx) >= std::abs(dy)) {
        // Out of the facing sides, across, and in.
        const QPointF start(dx >= 0 ? a.right() : a.left(), ca.y());
        const QPointF end(dx >= 0 ? b.left() : b.right(), cb.y());
        const double middle = (start.x() + end.x()) / 2.0;
        line.nodes = {PathNode::corner(start), PathNode::corner({middle, start.y()}), PathNode::corner({middle, end.y()}),
                      PathNode::corner(end)};
    } else {
        const QPointF start(ca.x(), dy >= 0 ? a.bottom() : a.top());
        const QPointF end(cb.x(), dy >= 0 ? b.top() : b.bottom());
        const double middle = (start.y() + end.y()) / 2.0;
        line.nodes = {PathNode::corner(start), PathNode::corner({start.x(), middle}), PathNode::corner({end.x(), middle}),
                      PathNode::corner(end)};
    }
    // Straight runs need no bend nodes.
    if (std::abs(line.nodes[1].point.x() - line.nodes[2].point.x()) < 1e-9
        && std::abs(line.nodes[1].point.y() - line.nodes[2].point.y()) < 1e-9)
        line.nodes = {line.nodes.front(), line.nodes.back()};
    return {line};
}

} // namespace opendraw::connector
