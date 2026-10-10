#include "core/Shape.h"

#include <QPainterPathStroker>

#include <algorithm>
#include <atomic>

namespace opendraw {

namespace {
std::atomic<quint64> nextShapeId{1};
}

Shape::Shape(ShapeKind kind, const QRectF &bounds, const Style &style)
    : m_id(nextShapeId++)
    , m_kind(kind)
    , m_bounds(bounds.normalized())
    , m_style(style)
{
}

QPainterPath Shape::path() const
{
    QPainterPath path;
    switch (m_kind) {
    case ShapeKind::Rectangle:
        path.addRect(m_bounds);
        break;
    case ShapeKind::Ellipse:
        path.addEllipse(m_bounds);
        break;
    }
    return path;
}

bool Shape::contains(const QPointF &point, double tolerance) const
{
    const QPainterPath outline = path();
    if (m_style.hasFill() && outline.contains(point))
        return true;

    QPainterPathStroker stroker;
    stroker.setWidth(std::max(m_style.strokeWidth, 0.0) + 2.0 * tolerance);
    return stroker.createStroke(outline).contains(point);
}

} // namespace opendraw
