#include "core/Blend.h"

#include "core/PathOps.h"

#include <algorithm>
#include <cmath>

namespace opendraw::blend {

namespace {

QPointF lerp(const QPointF &a, const QPointF &b, double t)
{
    return a + (b - a) * t;
}

double lerp(double a, double b, double t)
{
    return a + (b - a) * t;
}

QColor lerp(const QColor &a, const QColor &b, double t)
{
    return mix(a, b, t);
}

double distance(const QPointF &a, const QPointF &b)
{
    return std::hypot(a.x() - b.x(), a.y() - b.y());
}

// Rough length of a cubic segment (between its chord and its control polygon).
double segmentLength(const PathNode &a, const PathNode &b)
{
    return (distance(a.point, b.point) + distance(a.point, a.out) + distance(a.out, b.in) + distance(b.in, b.point)) / 2.0;
}

void reverse(SubPath &s)
{
    std::reverse(s.nodes.begin(), s.nodes.end());
    for (PathNode &n : s.nodes)
        std::swap(n.in, n.out);
}

double signedArea(const SubPath &s)
{
    double area = 0.0;
    for (std::size_t i = 0; i < s.nodes.size(); ++i) {
        const QPointF &p = s.nodes[i].point;
        const QPointF &q = s.nodes[(i + 1) % s.nodes.size()].point;
        area += p.x() * q.y() - q.x() * p.y();
    }
    return area / 2.0;
}

QRectF nodeBounds(const SubPath &s)
{
    QRectF box;
    for (const PathNode &n : s.nodes)
        box = box.isNull() ? QRectF(n.point, QSizeF(0, 0)) : box.united(QRectF(n.point, QSizeF(0, 0)));
    return box;
}

// Where a point sits in a box: (0, 0) top-left, (1, 1) bottom-right.
QPointF relative(const QPointF &p, const QRectF &box)
{
    return QPointF(box.width() > 0 ? (p.x() - box.left()) / box.width() : 0.5,
                   box.height() > 0 ? (p.y() - box.top()) / box.height() : 0.5);
}

// A closed subpath as an open one that ends where it started, the closing segment included.
void open(SubPath &s)
{
    if (!s.closed || s.nodes.empty())
        return;
    PathNode end = s.nodes.front();
    end.out = end.point;
    s.nodes.front().in = s.nodes.front().point;
    s.nodes.push_back(end);
    s.closed = false;
}

// Adds `count` nodes without changing the shape, halving the longest segments.
void addNodes(SubPath &s, int count)
{
    for (int k = 0; k < count; ++k) {
        const int segments = s.segmentCount();
        if (segments == 0) {
            s.nodes.push_back(s.nodes.back());
            continue;
        }
        int longest = 0;
        double length = -1.0;
        for (int i = 0; i < segments; ++i) {
            const double l = segmentLength(s.nodes[i], s.nodes[(i + 1) % s.nodes.size()]);
            if (l > length) {
                length = l;
                longest = i;
            }
        }
        PathData one{s};
        pathops::insertNode(one, 0, longest, 0.5);
        s = std::move(one.front());
    }
}

// Two subpaths made to correspond node for node.
void match(SubPath &a, SubPath &b)
{
    if (a.closed != b.closed) {
        open(a);
        open(b);
    }
    if (a.closed && (signedArea(a) < 0) != (signedArea(b) < 0))
        reverse(b); // both run the same way round
    const int na = int(a.nodes.size()), nb = int(b.nodes.size());
    if (na < nb)
        addNodes(a, nb - na);
    else if (nb < na)
        addNodes(b, na - nb);
    if (!a.closed)
        return;
    // Start b at the node that lines the two up best, judged by where nodes sit in their own
    // boxes (so the blend does not twist).
    const QRectF boxA = nodeBounds(a), boxB = nodeBounds(b);
    const int n = int(a.nodes.size());
    std::vector<QPointF> ra(n), rb(n);
    for (int i = 0; i < n; ++i) {
        ra[i] = relative(a.nodes[i].point, boxA);
        rb[i] = relative(b.nodes[i].point, boxB);
    }
    int best = 0;
    double bestCost = -1.0;
    for (int shift = 0; shift < n; ++shift) {
        double cost = 0.0;
        for (int i = 0; i < n && (bestCost < 0 || cost < bestCost); ++i) {
            const QPointF d = ra[i] - rb[(i + shift) % n];
            cost += d.x() * d.x() + d.y() * d.y();
        }
        if (bestCost < 0 || cost < bestCost) {
            bestCost = cost;
            best = shift;
        }
    }
    std::rotate(b.nodes.begin(), b.nodes.begin() + best, b.nodes.end());
}

// A subpath shrunk to a point: the partner of a subpath the other shape does not have.
SubPath pointLike(const SubPath &s)
{
    SubPath point = s;
    const QPointF centre = nodeBounds(s).center();
    for (PathNode &n : point.nodes)
        n = PathNode::corner(centre);
    return point;
}

PathData geometry(const Shape &shape)
{
    PathData path = shape.kind() == ShapeKind::Path ? shape.pathData() : pathops::fromPainterPath(shape.path());
    path.erase(std::remove_if(path.begin(), path.end(), [](const SubPath &s) { return s.nodes.empty(); }), path.end());
    return path;
}

Gradient asGradient(const Style &style, const Gradient &like)
{
    if (style.gradient)
        return *style.gradient;
    Gradient g = like;
    for (GradientStop &stop : g.stops)
        stop.color = style.fill;
    return g;
}

Style blendStyles(const Style &a, const Style &b, double t)
{
    // What cannot change gradually switches halfway.
    Style s = t < 0.5 ? a : b;
    if (!a.pattern && !b.pattern) {
        const bool sameKind = !a.gradient || !b.gradient
                              || (a.gradient->type == b.gradient->type && a.gradient->stops.size() == b.gradient->stops.size());
        if (!a.gradient && !b.gradient) {
            s.fill = lerp(a.fill, b.fill, t);
        } else if (sameKind) {
            const Gradient ga = asGradient(a, a.gradient ? *a.gradient : *b.gradient);
            const Gradient gb = asGradient(b, ga);
            Gradient g = ga;
            g.start = lerp(ga.start, gb.start, t);
            g.end = lerp(ga.end, gb.end, t);
            g.center = lerp(ga.center, gb.center, t);
            g.radius = lerp(ga.radius, gb.radius, t);
            for (std::size_t i = 0; i < g.stops.size(); ++i) {
                g.stops[i].offset = lerp(ga.stops[i].offset, gb.stops[i].offset, t);
                g.stops[i].color = lerp(ga.stops[i].color, gb.stops[i].color, t);
            }
            s.gradient = g;
        }
    }
    s.stroke = lerp(a.hasStroke() ? a.stroke : QColor(Qt::transparent), b.hasStroke() ? b.stroke : QColor(Qt::transparent), t);
    s.strokeWidth = lerp(a.hasStroke() ? a.strokeWidth : b.strokeWidth, b.hasStroke() ? b.strokeWidth : a.strokeWidth, t);
    s.opacity = lerp(a.opacity, b.opacity, t);
    return s;
}

} // namespace

QColor mix(const QColor &from, const QColor &to, double t)
{
    QColor a = from, b = to;
    if (a.alpha() == 0 && b.alpha() > 0)
        a = QColor(b.red(), b.green(), b.blue(), 0);
    if (b.alpha() == 0 && a.alpha() > 0)
        b = QColor(a.red(), a.green(), a.blue(), 0);
    return QColor::fromRgbF(float(lerp(a.redF(), b.redF(), t)), float(lerp(a.greenF(), b.greenF(), t)),
                            float(lerp(a.blueF(), b.blueF(), t)), float(lerp(a.alphaF(), b.alphaF(), t)));
}

bool canBlend(const Shape &shape)
{
    return shape.kind() != ShapeKind::Group && shape.kind() != ShapeKind::Image && !shape.path().isEmpty();
}

std::vector<std::unique_ptr<Shape>> intermediates(const Shape &from, const Shape &to, int steps)
{
    std::vector<std::unique_ptr<Shape>> result;
    if (steps < 1 || !canBlend(from) || !canBlend(to))
        return result;
    PathData a = geometry(from), b = geometry(to);
    if (a.empty() || b.empty())
        return result;
    // Subpaths pair up in order; one with no partner grows from (or shrinks to) a point.
    while (a.size() < b.size())
        a.push_back(pointLike(b[a.size()]));
    while (b.size() < a.size())
        b.push_back(pointLike(a[b.size()]));
    for (std::size_t i = 0; i < a.size(); ++i)
        match(a[i], b[i]);

    for (int step = 1; step <= steps; ++step) {
        const double t = double(step) / (steps + 1);
        PathData path = a;
        for (std::size_t i = 0; i < path.size(); ++i) {
            for (std::size_t j = 0; j < path[i].nodes.size(); ++j) {
                PathNode &n = path[i].nodes[j];
                const PathNode &m = b[i].nodes[j];
                n.point = lerp(n.point, m.point, t);
                n.in = lerp(n.in, m.in, t);
                n.out = lerp(n.out, m.out, t);
                n.type = NodeType::Corner;
            }
        }
        result.push_back(std::make_unique<Shape>(std::move(path), blendStyles(from.style(), to.style(), t)));
    }
    return result;
}

} // namespace opendraw::blend
