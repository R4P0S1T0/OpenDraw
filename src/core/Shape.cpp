#include "core/Shape.h"

#include "core/Blend.h"
#include "core/Envelope.h"
#include "core/PathOps.h"
#include "core/TextLayout.h"

#include <QPainterPathStroker>
#include <QPolygonF>

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

Shape::Shape(QImage image, const QTransform &placement)
    : m_id(nextShapeId++)
    , m_kind(ShapeKind::Image)
    , m_image(std::move(image))
    , m_imagePlacement(placement)
{
    m_style.fill = Qt::transparent;
    m_style.stroke = Qt::transparent;
}

Shape::~Shape() = default;

QRectF Shape::localBounds() const
{
    switch (m_kind) {
    case ShapeKind::Rectangle:
    case ShapeKind::Ellipse:
        return m_bounds;
    case ShapeKind::Text:
        return textlayout::layout(m_text, m_textOrigin).bounds;
    case ShapeKind::Path:
        return pathops::toPainterPath(m_path).boundingRect();
    default:
        return bounds();
    }
}

void Shape::setEnvelope(const std::optional<Envelope> &envelope)
{
    if (canHaveEnvelope(m_kind))
        m_envelope = envelope;
}

QPainterPath Shape::warp(const QPainterPath &local) const
{
    return m_envelope ? envelope::warp(*m_envelope, local, localBounds()) : local;
}

QPointF Shape::mapToPage(const QPointF &local) const
{
    QPointF p = local;
    if (m_envelope) {
        const QRectF box = localBounds();
        const QPointF unit(box.width() > 0 ? (p.x() - box.left()) / box.width() : 0.5,
                           box.height() > 0 ? (p.y() - box.top()) / box.height() : 0.5);
        const QPointF bent = envelope::map(*m_envelope, unit);
        p = QPointF(box.left() + bent.x() * box.width(), box.top() + bent.y() * box.height());
    }
    return m_shapeTransform.map(p);
}

double Shape::rotation() const
{
    QTransform t;
    if (m_kind == ShapeKind::Image)
        t = m_imagePlacement;
    else if (m_kind == ShapeKind::Rectangle || m_kind == ShapeKind::Ellipse || m_kind == ShapeKind::Text
             || m_kind == ShapeKind::Path)
        t = m_shapeTransform;
    else
        return 0.0;
    double degrees = std::atan2(t.m12(), t.m11()) * 180.0 / kPi;
    if (std::abs(degrees) < 1e-9)
        degrees = 0.0;
    return degrees;
}

QRectF Shape::bounds() const
{
    switch (m_kind) {
    case ShapeKind::Rectangle:
    case ShapeKind::Ellipse:
        return m_shapeTransform.isIdentity() && !m_envelope ? m_bounds : path().boundingRect();
    case ShapeKind::Path:
        return path().boundingRect();
    case ShapeKind::Group: {
        QRectF united;
        for (const auto &child : m_children)
            united = united.isNull() ? child->bounds() : united.united(child->bounds());
        return united;
    }
    case ShapeKind::Text: {
        const QRectF box = textlayout::layout(m_text, m_textOrigin).bounds;
        if (m_envelope) {
            QPainterPath frame;
            frame.addRect(box);
            return m_shapeTransform.map(warp(frame)).boundingRect();
        }
        return m_shapeTransform.mapRect(box);
    }
    case ShapeKind::Image:
        return m_imagePlacement.mapRect(QRectF(0, 0, 1, 1));
    }
    return {};
}

void Shape::setBounds(const QRectF &bounds)
{
    const QRectF target = bounds.normalized();
    if (m_kind == ShapeKind::Text && m_text.isParagraph() && m_shapeTransform.isIdentity() && !m_envelope) {
        // Resizing a paragraph frame reflows the text instead of scaling it.
        m_textOrigin = target.topLeft();
        m_text.frame = target.size();
        return;
    }
    if ((m_kind == ShapeKind::Rectangle || m_kind == ShapeKind::Ellipse) && !m_style.pattern
        && m_shapeTransform.isIdentity() && !m_envelope) {
        if (m_style.scaleStroke && m_bounds.width() > 0.0 && m_bounds.height() > 0.0)
            m_style.strokeWidth *= std::sqrt(target.width() * target.height() / (m_bounds.width() * m_bounds.height()));
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
    // Rectangles, ellipses and texts take a move or scale directly into their geometry while
    // they are upright; a rotation or skew (and anything after one) goes into shapeTransform().
    // So do curves with an envelope, which must turn with them.
    const bool boxed = m_kind == ShapeKind::Rectangle || m_kind == ShapeKind::Ellipse || m_kind == ShapeKind::Text
                       || (m_kind == ShapeKind::Path && m_envelope);
    const bool direct = !boxed || (m_shapeTransform.isIdentity() && t.type() <= QTransform::TxScale);

    // Mirroring the geometry mirrors the envelope too, so the object stays bent the same way.
    if (m_envelope && direct && (t.m11() < 0 || t.m22() < 0)) {
        QTransform flip;
        flip.translate(t.m11() < 0 ? 1 : 0, t.m22() < 0 ? 1 : 0);
        flip.scale(t.m11() < 0 ? -1 : 1, t.m22() < 0 ? -1 : 1);
        m_envelope = envelope::transformed(*m_envelope, flip);
    }

    // An outline set to scale with the object follows the overall change of size.
    if (m_style.scaleStroke && m_kind != ShapeKind::Group) {
        const double scale = std::sqrt(std::abs(t.determinant()));
        if (scale > 0.0)
            m_style.strokeWidth *= scale;
    }

    // A texture fill scales along with the shape it fills (under shapeTransform() the
    // painter scales it already).
    if (m_style.pattern && direct) {
        const double sx = std::hypot(t.m11(), t.m12());
        const double sy = std::hypot(t.m21(), t.m22());
        m_style.pattern->tileSize = QSizeF(m_style.pattern->tileSize.width() * sx,
                                           m_style.pattern->tileSize.height() * sy);
        m_style.pattern->origin = QPointF(m_style.pattern->origin.x() * sx, m_style.pattern->origin.y() * sy);
    }

    if (!direct) {
        m_shapeTransform = m_shapeTransform * t;
        // Back upright (rotated there and back, for example): fold it into the geometry again.
        const bool upright = m_kind == ShapeKind::Text ? m_shapeTransform.type() <= QTransform::TxTranslate
                                                       : m_shapeTransform.type() <= QTransform::TxScale;
        if (upright) {
            const QTransform fold = m_shapeTransform;
            const double width = m_style.strokeWidth; // already scaled above
            m_shapeTransform = QTransform();
            transform(fold);
            m_style.strokeWidth = width;
        }
        return;
    }

    switch (m_kind) {
    case ShapeKind::Image:
        m_imagePlacement = m_imagePlacement * t;
        break;
    case ShapeKind::Rectangle:
    case ShapeKind::Ellipse:
        m_bounds = t.mapRect(m_bounds);
        m_cornerRadius *= std::min(std::hypot(t.m11(), t.m12()), std::hypot(t.m21(), t.m22()));
        break;
    case ShapeKind::Path:
        pathops::transform(m_path, t);
        break;
    case ShapeKind::Group:
        for (auto &child : m_children)
            child->transform(t);
        break;
    case ShapeKind::Text: {
        // Text keeps its proportions: the font size follows the overall scale. A paragraph
        // frame scales with the drawing (its origin stays its top-left corner).
        const double scale = std::sqrt(std::abs(t.determinant()));
        if (scale > 0.0) {
            m_text.format.size *= scale;
            m_text.format.firstIndent *= scale;
            m_text.format.leftIndent *= scale;
            m_text.format.rightIndent *= scale;
            for (TextRun &run : m_text.runs) {
                if (run.format.size)
                    *run.format.size *= scale;
            }
        }
        if (m_text.isParagraph()) {
            const QRectF frame = t.mapRect(QRectF(m_textOrigin, m_text.frame));
            m_textOrigin = frame.topLeft();
            m_text.frame = frame.size();
        } else {
            m_textOrigin = t.map(m_textOrigin);
        }
        if (m_text.path) {
            pathops::transform(m_text.path->curve, t);
            m_text.path->offset *= scale;
        }
        break;
    }
    }
}

QPainterPath Shape::path() const
{
    if (canHaveEnvelope(m_kind)) {
        const QPainterPath local = warp(localPath());
        return m_shapeTransform.isIdentity() ? local : m_shapeTransform.map(local);
    }
    return localPath();
}

QPainterPath Shape::localPath() const
{
    QPainterPath path;
    switch (m_kind) {
    case ShapeKind::Rectangle:
        if (m_cornerRadius > 0.0) {
            const double r = std::min({m_cornerRadius, m_bounds.width() / 2.0, m_bounds.height() / 2.0});
            path.addRoundedRect(m_bounds, r, r);
        } else {
            path.addRect(m_bounds);
        }
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
        for (const auto &step : blendIntermediates())
            path.addPath(step->path());
        break;
    case ShapeKind::Text:
        path = textlayout::outline(m_text, m_textOrigin);
        break;
    case ShapeKind::Image: {
        QPainterPath square;
        square.addRect(0, 0, 1, 1);
        path = m_imagePlacement.map(square);
        break;
    }
    }
    return path;
}

std::vector<std::unique_ptr<Shape>> Shape::blendIntermediates() const
{
    if (!isBlend())
        return {};
    return blend::intermediates(*m_children[0], *m_children[1], m_blendSteps);
}

bool Shape::contains(const QPointF &point, double tolerance) const
{
    if (m_kind == ShapeKind::Group) {
        auto hit = [&](const auto &child) { return child->contains(point, tolerance); };
        if (std::any_of(m_children.begin(), m_children.end(), hit))
            return true;
        if (!isBlend())
            return false;
        const auto steps = blendIntermediates();
        return std::any_of(steps.begin(), steps.end(), hit);
    }
    if (m_kind == ShapeKind::Text) { // letters are thin: the whole text box is clickable
        const QRectF box = localBounds().adjusted(-tolerance, -tolerance, tolerance, tolerance);
        if (m_envelope) {
            QPainterPath frame;
            frame.addRect(localBounds());
            return m_shapeTransform.map(warp(frame)).contains(point);
        }
        return m_shapeTransform.isIdentity() ? box.contains(point)
                                             : m_shapeTransform.map(QPolygonF(box)).containsPoint(point, Qt::OddEvenFill);
    }
    if (m_kind == ShapeKind::Image)
        return path().contains(point);

    const QPainterPath outline = path();
    if (m_style.hasFill() && outline.contains(point))
        return true;

    QPainterPathStroker stroker;
    stroker.setWidth(std::max(m_style.strokeWidth, 0.0) + 2.0 * tolerance);
    return stroker.createStroke(outline).contains(point);
}

} // namespace opendraw
