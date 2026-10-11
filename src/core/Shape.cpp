#include "core/Shape.h"

#include "core/PathOps.h"
#include "core/TextLayout.h"

#include <QPainterPathStroker>

#include <algorithm>
#include <atomic>
#include <cmath>

namespace opendraw {

namespace {
std::atomic<quint64> nextShapeId{1};
constexpr double kPi = 3.14159265358979323846;
}

Gradient Gradient::linear(const QColor &from, const QColor &to, double angleDegrees)
{
    Gradient g;
    g.type = Type::Linear;
    g.stops = {{0.0, from}, {1.0, to}};
    const double a = angleDegrees * kPi / 180.0;
    const QPointF half(0.5 * std::cos(a), 0.5 * std::sin(a));
    g.start = QPointF(0.5, 0.5) - half;
    g.end = QPointF(0.5, 0.5) + half;
    return g;
}

Gradient Gradient::radial(const QColor &inner, const QColor &outer)
{
    Gradient g;
    g.type = Type::Radial;
    g.stops = {{0.0, inner}, {1.0, outer}};
    return g;
}

double Gradient::angle() const
{
    const QPointF d = end - start;
    double degrees = std::atan2(d.y(), d.x()) * 180.0 / kPi;
    if (degrees < 0)
        degrees += 360.0;
    return degrees;
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

Shape::Shape(std::vector<std::unique_ptr<Shape>> children)
    : m_id(nextShapeId++)
    , m_kind(ShapeKind::Group)
    , m_children(std::move(children))
{
}

Shape::Shape(TextData text, const QPointF &origin, const Style &style)
    : m_id(nextShapeId++)
    , m_kind(ShapeKind::Text)
    , m_text(std::move(text))
    , m_textOrigin(origin)
    , m_style(style)
{
}

Shape::~Shape() = default;

QRectF Shape::bounds() const
{
    switch (m_kind) {
    case ShapeKind::Rectangle:
    case ShapeKind::Ellipse:
        return m_bounds;
    case ShapeKind::Path:
        return path().boundingRect();
    case ShapeKind::Group: {
        QRectF united;
        for (const auto &child : m_children)
            united = united.isNull() ? child->bounds() : united.united(child->bounds());
        return united;
    }
    case ShapeKind::Text:
        return textlayout::layout(m_text, m_textOrigin).bounds;
    }
    return {};
}

void Shape::setBounds(const QRectF &bounds)
{
    const QRectF target = bounds.normalized();
    if (m_kind == ShapeKind::Rectangle || m_kind == ShapeKind::Ellipse) {
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
    transform(t);
}

void Shape::moveBy(const QPointF &delta)
{
    transform(QTransform::fromTranslate(delta.x(), delta.y()));
}

void Shape::transform(const QTransform &t)
{
    switch (m_kind) {
    case ShapeKind::Rectangle:
    case ShapeKind::Ellipse:
        m_bounds = t.mapRect(m_bounds);
        break;
    case ShapeKind::Path:
        pathops::transform(m_path, t);
        break;
    case ShapeKind::Group:
        for (auto &child : m_children)
            child->transform(t);
        break;
    case ShapeKind::Text: {
        // Text keeps its proportions: the font size follows the overall scale.
        m_textOrigin = t.map(m_textOrigin);
        const double scale = std::sqrt(std::abs(t.determinant()));
        if (scale > 0.0)
            m_text.format.size *= scale;
        break;
    }
    }
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
    case ShapeKind::Group:
        for (const auto &child : m_children)
            path.addPath(child->path());
        break;
    case ShapeKind::Text:
        path = textlayout::outline(m_text, m_textOrigin);
        break;
    }
    return path;
}

bool Shape::contains(const QPointF &point, double tolerance) const
{
    if (m_kind == ShapeKind::Group) {
        return std::any_of(m_children.begin(), m_children.end(),
                           [&](const auto &child) { return child->contains(point, tolerance); });
    }
    if (m_kind == ShapeKind::Text) // letters are thin: the whole text box is clickable
        return bounds().adjusted(-tolerance, -tolerance, tolerance, tolerance).contains(point);

    const QPainterPath outline = path();
    if (m_style.hasFill() && outline.contains(point))
        return true;

    QPainterPathStroker stroker;
    stroker.setWidth(std::max(m_style.strokeWidth, 0.0) + 2.0 * tolerance);
    return stroker.createStroke(outline).contains(point);
}

} // namespace opendraw
