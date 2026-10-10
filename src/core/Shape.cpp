#include "core/Shape.h"

#include "core/PathOps.h"

#include <QPainterPathStroker>
#include <QTransform>

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

Shape::Shape(PathData path, const Style &style)
    : m_id(nextShapeId++)
    , m_kind(ShapeKind::Path)
    , m_path(std::move(path))
    , m_style(style)
{
}

QRectF Shape::bounds() const
{
    return m_kind == ShapeKind::Path ? path().boundingRect() : m_bounds;
}

void Shape::setBounds(const QRectF &bounds)
{
    const QRectF target = bounds.normalized();
    if (m_kind != ShapeKind::Path) {
        m_bounds = target;
        return;
    }
    const QRectF current = this->bounds();
    // A straight horizontal or vertical path has no extent on one axis; keep that axis as is.
    const double sx = current.width() > 0.0 ? target.width() / current.width() : 1.0;
    const double sy = current.height() > 0.0 ? target.height() / current.height() : 1.0;
    QTransform t;
    t.translate(target.left(), target.top());
    t.scale(sx, sy);
    t.translate(-current.left(), -current.top());
    pathops::transform(m_path, t);
}

void Shape::moveBy(const QPointF &delta)
{
    if (m_kind == ShapeKind::Path)
        pathops::transform(m_path, QTransform::fromTranslate(delta.x(), delta.y()));
    else
        m_bounds.translate(delta);
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
    case ShapeKind::Path:
        path = pathops::toPainterPath(m_path);
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
