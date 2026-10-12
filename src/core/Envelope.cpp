#include "core/Envelope.h"

#include <QLineF>
#include <QPolygonF>

#include <algorithm>
#include <cmath>

namespace opendraw {

Envelope Envelope::identity()
{
    return Envelope{{QPointF(0, 0), QPointF(1.0 / 3, 0), QPointF(2.0 / 3, 0), QPointF(1, 0), QPointF(1, 1.0 / 3),
                     QPointF(1, 2.0 / 3), QPointF(1, 1), QPointF(2.0 / 3, 1), QPointF(1.0 / 3, 1), QPointF(0, 1),
                     QPointF(0, 2.0 / 3), QPointF(0, 1.0 / 3)}};
}

namespace envelope {

namespace {

QPointF cubic(const QPointF &a, const QPointF &b, const QPointF &c, const QPointF &d, double t)
{
    const double s = 1.0 - t;
    return a * (s * s * s) + b * (3 * s * s * t) + c * (3 * s * t * t) + d * (t * t * t);
}

} // namespace

QPointF map(const Envelope &envelope, const QPointF &unit)
{
    const auto &p = envelope.points;
    const double u = unit.x(), v = unit.y();
    const QPointF top = cubic(p[0], p[1], p[2], p[3], u);
    const QPointF bottom = cubic(p[9], p[8], p[7], p[6], u);
    const QPointF left = cubic(p[0], p[11], p[10], p[9], v);
    const QPointF right = cubic(p[3], p[4], p[5], p[6], v);
    const QPointF corners = p[0] * ((1 - u) * (1 - v)) + p[3] * (u * (1 - v)) + p[9] * ((1 - u) * v) + p[6] * (u * v);
    return top * (1 - v) + bottom * v + left * (1 - u) + right * u - corners;
}

QPainterPath warp(const Envelope &envelope, const QPainterPath &path, const QRectF &box)
{
    QPainterPath result;
    result.setFillRule(path.fillRule());
    if (path.isEmpty())
        return result;
    auto toUnit = [&box](const QPointF &q) {
        return QPointF(box.width() > 0 ? (q.x() - box.left()) / box.width() : 0.5,
                       box.height() > 0 ? (q.y() - box.top()) / box.height() : 0.5);
    };
    auto fromUnit = [&box](const QPointF &q) {
        return QPointF(box.left() + q.x() * box.width(), box.top() + q.y() * box.height());
    };
    // Straight stretches bend too, so they are cut into short pieces first.
    const double step = std::max(std::max(box.width(), box.height()) / 100.0, 0.05);
    for (const QPolygonF &polygon : path.toSubpathPolygons()) {
        if (polygon.isEmpty())
            continue;
        QPolygonF bent;
        for (int i = 0; i < polygon.size(); ++i) {
            if (i > 0) {
                const QLineF edge(polygon[i - 1], polygon[i]);
                const int pieces = int(std::ceil(edge.length() / step));
                for (int k = 1; k < pieces; ++k)
                    bent << fromUnit(map(envelope, toUnit(edge.pointAt(double(k) / pieces))));
            }
            bent << fromUnit(map(envelope, toUnit(polygon[i])));
        }
        result.addPolygon(bent);
        if (polygon.isClosed())
            result.closeSubpath();
    }
    return result;
}

Envelope transformed(const Envelope &envelope, const QTransform &unit)
{
    std::array<QPointF, 12> mapped;
    for (int i = 0; i < 12; ++i)
        mapped[i] = unit.map(envelope.points[i]);
    if (unit.determinant() < 0) {
        // Mirrored: go round the other way, so the edges still run clockwise.
        std::array<QPointF, 12> reversed;
        for (int i = 0; i < 12; ++i)
            reversed[i] = mapped[(12 - i) % 12];
        mapped = reversed;
    }
    // Start again at the corner nearest the top-left.
    int first = 0;
    double best = 1e300;
    for (int i = 0; i < 12; i += 3) {
        const double d = mapped[i].x() + mapped[i].y();
        if (d < best) {
            best = d;
            first = i;
        }
    }
    Envelope result;
    for (int i = 0; i < 12; ++i)
        result.points[i] = mapped[(first + i) % 12];
    return result;
}

QPainterPath outline(const Envelope &envelope)
{
    const auto &p = envelope.points;
    QPainterPath path;
    path.moveTo(p[0]);
    for (int edge = 0; edge < 4; ++edge) {
        const int i = edge * 3;
        path.cubicTo(p[i + 1], p[i + 2], p[(i + 3) % 12]);
    }
    path.closeSubpath();
    return path;
}

} // namespace envelope
} // namespace opendraw
