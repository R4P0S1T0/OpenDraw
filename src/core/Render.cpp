#include "core/Render.h"

#include "core/Document.h"
#include "core/Shape.h"

#include <QPainter>

#include <algorithm>

namespace opendraw::render {

QBrush fillBrush(const Style &style, const QRectF &bounds)
{
    if (style.pattern && !style.pattern->image.isNull()) {
        const PatternFill &p = *style.pattern;
        QBrush brush(p.image);
        QTransform t;
        t.translate(bounds.left() + p.origin.x(), bounds.top() + p.origin.y());
        t.scale(p.tileSize.width() / p.image.width(), p.tileSize.height() / p.image.height());
        brush.setTransform(t);
        return brush;
    }
    if (style.gradient) {
        const Gradient &g = *style.gradient;
        // Object-bounding mode: 0..1 spans the bounding box of the path being drawn.
        auto setUp = [&g](QGradient &qg) {
            qg.setCoordinateMode(QGradient::ObjectBoundingMode);
            for (const GradientStop &stop : g.stops)
                qg.setColorAt(std::clamp(stop.offset, 0.0, 1.0), stop.color);
        };
        if (g.type == Gradient::Type::Radial) {
            QRadialGradient radial(g.center, g.radius);
            setUp(radial);
            return QBrush(radial);
        }
        QLinearGradient linear(g.start, g.end);
        setUp(linear);
        return QBrush(linear);
    }
    return style.fill.alpha() > 0 ? QBrush(style.fill) : QBrush(Qt::NoBrush);
}

void paintShape(QPainter &painter, const Shape &shape)
{
    if (shape.kind() == ShapeKind::Group) {
        for (const auto &child : shape.children())
            paintShape(painter, *child);
        return;
    }
    const Style &style = shape.style();
    painter.save();
    painter.setOpacity(painter.opacity() * std::clamp(style.opacity, 0.0, 1.0));
    if (shape.kind() == ShapeKind::Image) {
        const QImage &image = shape.image();
        if (!image.isNull()) {
            // The placement maps the unit square to the page; the image is drawn in it.
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            painter.setTransform(QTransform::fromScale(1.0 / image.width(), 1.0 / image.height())
                                     * shape.imagePlacement(),
                                 true);
            painter.drawImage(0, 0, image);
        }
        painter.restore();
        return;
    }
    painter.setBrush(fillBrush(style, shape.bounds()));
    painter.setPen(style.hasStroke() ? QPen(style.stroke, style.strokeWidth) : QPen(Qt::NoPen));
    painter.drawPath(shape.path());
    painter.restore();
}

void paintPage(QPainter &painter, const Page &page)
{
    for (const auto &layer : page.layers) {
        if (!layer->visible)
            continue;
        for (const auto &shape : layer->shapes)
            paintShape(painter, *shape);
    }
}

void paintDocument(QPainter &painter, const Document &document)
{
    paintPage(painter, *document.currentPage());
}

} // namespace opendraw::render
