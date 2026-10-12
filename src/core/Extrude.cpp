#include "core/Extrude.h"

#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace opendraw::extrude {

namespace {

QColor shaded(const QColor &color, double factor)
{
    return QColor::fromRgbF(float(std::clamp(color.redF() * factor, 0.0, 1.0)),
                            float(std::clamp(color.greenF() * factor, 0.0, 1.0)),
                            float(std::clamp(color.blueF() * factor, 0.0, 1.0)), color.alphaF());
}

QColor baseColor(const Style &style)
{
    if (style.extrusion->color.isValid())
        return style.extrusion->color;
    if (style.gradient && !style.gradient->stops.empty())
        return style.gradient->stops.front().color;
    if (style.pattern || !style.hasFill())
        return QColor(0xc0, 0xc0, 0xc0);
    return style.fill;
}

double signedArea(const QPolygonF &polygon)
{
    double area = 0.0;
    for (int i = 0; i < polygon.size(); ++i) {
        const QPointF &p = polygon[i];
        const QPointF &q = polygon[(i + 1) % polygon.size()];
        area += p.x() * q.y() - q.x() * p.y();
    }
    return area / 2.0;
}

std::vector<Face> computeFaces(const Shape &shape)
{
    const Style &style = shape.style();
    const QPointF vp = vanishingPoint(shape);
    const double depth = std::clamp(style.extrusion->depth, 0.0, 1.0);
    const QColor base = baseColor(style);
    // Light from the top left.
    const QPointF light = QPointF(-0.55, -0.83);

    std::vector<Face> sides;
    std::vector<Face> backs;
    for (QPolygonF polygon : shape.path().toSubpathPolygons()) {
        if (polygon.size() > 1 && polygon.isClosed())
            polygon.removeLast();
        if (polygon.size() < 2)
            continue;
        const bool clockwise = signedArea(polygon) > 0; // on screen, y pointing down
        const int n = int(polygon.size());
        auto pulled = [&](const QPointF &p) { return p + (vp - p) * depth; };
        QPolygonF backPolygon;
        for (const QPointF &p : polygon)
            backPolygon << pulled(p);
        backs.push_back({backPolygon, shaded(base, 0.35), {}});
        // A corner is sharp where the outline turns by more than about 30 degrees.
        auto sharp = [&](int i) {
            const QLineF in(polygon[(i + n - 1) % n], polygon[i]), out(polygon[i], polygon[(i + 1) % n]);
            if (in.length() < 1e-6 || out.length() < 1e-6)
                return false;
            const double turn = std::abs(in.angleTo(out));
            return std::min(turn, 360.0 - turn) > 30.0;
        };
        for (int i = 0; i < n; ++i) {
            const QPointF a = polygon[i], b = polygon[(i + 1) % n];
            const QPointF d = b - a;
            const double length = std::hypot(d.x(), d.y());
            if (length < 1e-6)
                continue;
            // Outward normal, so faces turned to the light are lighter.
            const QPointF normal = clockwise ? QPointF(d.y(), -d.x()) / length : QPointF(-d.y(), d.x()) / length;
            const double lit = normal.x() * light.x() + normal.y() * light.y();
            const double factor = std::clamp(0.62 + 0.38 * lit, 0.3, 1.0);
            Face face{QPolygonF{a, b, pulled(b), pulled(a)}, shaded(base, factor), {QLineF(pulled(a), pulled(b))}};
            if (sharp(i))
                face.lines.emplace_back(a, pulled(a));
            if (sharp((i + 1) % n))
                face.lines.emplace_back(b, pulled(b));
            sides.push_back(std::move(face));
        }
    }
    // Back to front: faces nearer the vanishing point are further away.
    auto distanceToVp = [&vp](const Face &f) {
        const QPointF centre = (f.polygon[0] + f.polygon[1] + f.polygon[2] + f.polygon[3]) / 4.0;
        return QLineF(centre, vp).length();
    };
    std::stable_sort(sides.begin(), sides.end(),
                     [&](const Face &x, const Face &y) { return distanceToVp(x) < distanceToVp(y); });
    backs.insert(backs.end(), sides.begin(), sides.end());
    return backs;
}

struct CacheEntry {
    QPainterPath outline;
    Style style;
    std::vector<Face> faces;
};

bool sameForFaces(const Style &a, const Style &b)
{
    return a.extrusion == b.extrusion && a.fill == b.fill && a.gradient == b.gradient
           && a.pattern.has_value() == b.pattern.has_value();
}

} // namespace

QPointF vanishingPoint(const Shape &shape)
{
    const QPointF offset = shape.style().extrusion ? shape.style().extrusion->vanishingPoint : QPointF();
    return shape.bounds().center() + offset;
}

QPointF back(const Shape &shape, const QPointF &front)
{
    if (!shape.style().extrusion)
        return front;
    return front + (vanishingPoint(shape) - front) * std::clamp(shape.style().extrusion->depth, 0.0, 1.0);
}

std::vector<Face> faces(const Shape &shape)
{
    const Style &style = shape.style();
    if (!style.extrusion || shape.kind() == ShapeKind::Group || shape.kind() == ShapeKind::Image)
        return {};
    // Worked out once per change of the object, not on every repaint.
    thread_local std::unordered_map<quint64, CacheEntry> cache;
    const QPainterPath outline = shape.path();
    auto it = cache.find(shape.id());
    if (it != cache.end() && it->second.outline == outline && sameForFaces(it->second.style, style))
        return it->second.faces;
    if (cache.size() > 256)
        cache.clear();
    CacheEntry entry{outline, style, computeFaces(shape)};
    std::vector<Face> result = entry.faces;
    cache[shape.id()] = std::move(entry);
    return result;
}

} // namespace opendraw::extrude
