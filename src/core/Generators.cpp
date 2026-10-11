#include "core/Generators.h"

#include "core/PathOps.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace opendraw::generators {

namespace {

// Point at `angle` (radians, 0 = right, clockwise on screen) on the ellipse filling `box`,
// scaled by `radius` (1 = on the ellipse).
QPointF onEllipse(const QRectF &box, double angle, double radius = 1.0)
{
    return box.center() + QPointF(std::cos(angle) * box.width() / 2.0, std::sin(angle) * box.height() / 2.0) * radius;
}

PathData closedCorners(const std::vector<QPointF> &points)
{
    SubPath sub;
    sub.closed = true;
    for (const QPointF &p : points)
        sub.nodes.push_back(PathNode::corner(p));
    return {sub};
}

constexpr double kTop = -std::numbers::pi / 2.0;

} // namespace

PathData polygon(const QRectF &box, int sides)
{
    sides = std::clamp(sides, 3, 500);
    std::vector<QPointF> points;
    for (int i = 0; i < sides; ++i)
        points.push_back(onEllipse(box, kTop + 2.0 * std::numbers::pi * i / sides));
    return closedCorners(points);
}

PathData star(const QRectF &box, int points, double sharpness)
{
    points = std::clamp(points, 3, 500);
    // Sharper stars have their inner corners closer to the centre.
    const double inner = std::clamp(1.0 - std::clamp(sharpness, 0.0, 100.0) / 100.0, 0.05, 1.0);
    std::vector<QPointF> corners;
    for (int i = 0; i < 2 * points; ++i)
        corners.push_back(onEllipse(box, kTop + std::numbers::pi * i / points, i % 2 ? inner : 1.0));
    return closedCorners(corners);
}

PathData spiral(const QRectF &box, double revolutions)
{
    revolutions = std::clamp(revolutions, 0.25, 100.0);
    // Archimedean spiral sampled finely, then fitted with smooth curve segments.
    const double total = 2.0 * std::numbers::pi * revolutions;
    const int samples = std::max(24, int(std::ceil(revolutions * 72)));
    std::vector<QPointF> points;
    for (int i = 0; i <= samples; ++i) {
        const double t = total * i / samples;
        points.push_back(onEllipse(box, kTop + t, t / total));
    }
    const double tolerance = std::max(std::min(box.width(), box.height()) / 1000.0, 0.01);
    SubPath curve = pathops::fitFreehand(points, tolerance, false);
    return {curve};
}

PathData line(const QPointF &from, const QPointF &to)
{
    SubPath sub;
    sub.nodes = {PathNode::corner(from), PathNode::corner(to)};
    return {sub};
}

std::unique_ptr<Shape> graphPaper(const QRectF &box, int columns, int rows, const Style &style)
{
    columns = std::clamp(columns, 1, 200);
    rows = std::clamp(rows, 1, 200);
    const double w = box.width() / columns;
    const double h = box.height() / rows;
    std::vector<std::unique_ptr<Shape>> cells;
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < columns; ++c)
            cells.push_back(std::make_unique<Shape>(ShapeKind::Rectangle,
                                                    QRectF(box.left() + c * w, box.top() + r * h, w, h), style));
    }
    return std::make_unique<Shape>(std::move(cells));
}

} // namespace opendraw::generators
