#include "core/Render.h"

#include "core/Document.h"
#include "core/Shape.h"
#include "core/TextLayout.h"

#include <QPainter>
#include <QPaintDevice>

#include <algorithm>
#include <cmath>
#include <vector>

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

QPen outlinePen(const Style &style)
{
    if (!style.hasStroke())
        return QPen(Qt::NoPen);
    QPen pen(QBrush(style.stroke), style.strokeWidth);
    static const Qt::PenCapStyle caps[] = {Qt::FlatCap, Qt::RoundCap, Qt::SquareCap};
    static const Qt::PenJoinStyle joins[] = {Qt::MiterJoin, Qt::RoundJoin, Qt::BevelJoin};
    pen.setCapStyle(caps[int(style.cap)]);
    pen.setJoinStyle(joins[int(style.join)]);
    pen.setMiterLimit(4.0); // the SVG default
    if (!style.dashes.empty()) {
        // Qt wants dash/gap pairs; an odd list repeats, as in SVG.
        QList<qreal> pattern(style.dashes.begin(), style.dashes.end());
        if (pattern.size() % 2)
            pattern += pattern;
        for (qreal &v : pattern)
            v = std::max(v, 0.001);
        pen.setDashPattern(pattern);
    }
    return pen;
}

namespace {

// The shape itself: fill and outline (no opacity, shadow or transparency).
void paintBody(QPainter &painter, const Shape &shape)
{
    const Style &style = shape.style();
    if (shape.kind() == ShapeKind::Image) {
        const QImage &image = shape.image();
        if (!image.isNull()) {
            // The placement maps the unit square to the page; the image is drawn in it.
            painter.save();
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            painter.setTransform(QTransform::fromScale(1.0 / image.width(), 1.0 / image.height())
                                     * shape.imagePlacement(),
                                 true);
            painter.drawImage(0, 0, image);
            painter.restore();
        }
        return;
    }
    painter.save();
    const QTransform own = shape.shapeTransform();
    if (shape.kind() == ShapeKind::Text) {
        const TextData &data = shape.textData();
        const bool colored = std::any_of(data.runs.begin(), data.runs.end(),
                                         [](const TextRun &run) { return run.format.color.has_value(); });
        if (colored) {
            // Letters with their own colour; the rest take the text's fill.
            painter.save();
            painter.setTransform(own, true);
            painter.setPen(Qt::NoPen);
            for (const textlayout::ColoredPath &part : textlayout::coloredOutline(data, shape.textOrigin())) {
                painter.setBrush(part.color ? QBrush(*part.color) : fillBrush(style, shape.localBounds()));
                painter.drawPath(part.path);
            }
            painter.restore();
            if (style.hasStroke()) {
                painter.setBrush(Qt::NoBrush);
                painter.setPen(outlinePen(style));
                painter.drawPath(shape.path());
            }
            painter.restore();
            return;
        }
    }
    if (own.isIdentity()) {
        painter.setBrush(fillBrush(style, shape.bounds()));
        painter.setPen(outlinePen(style));
        painter.drawPath(shape.path());
    } else {
        // Rotated or skewed: the fill (gradient, texture) turns with the shape; the outline
        // keeps its width, as for any other shape.
        painter.save();
        painter.setTransform(own, true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(fillBrush(style, shape.localBounds()));
        painter.drawPath(shape.localPath());
        painter.restore();
        if (style.hasStroke()) {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(outlinePen(style));
            painter.drawPath(shape.path());
        }
    }
    painter.restore();
}

// Box blur of an 8-bit alpha image, three passes each way (close to a Gaussian).
void blurAlpha(QImage &alpha, int radius)
{
    if (radius < 1)
        return;
    const int w = alpha.width(), h = alpha.height();
    std::vector<int> line(std::max(w, h));
    auto pass = [&](bool horizontal) {
        const int length = horizontal ? w : h;
        const int count = horizontal ? h : w;
        for (int c = 0; c < count; ++c) {
            auto at = [&](int i) -> uchar & {
                return horizontal ? alpha.scanLine(c)[i] : alpha.scanLine(i)[c];
            };
            for (int i = 0; i < length; ++i)
                line[i] = at(i);
            int sum = 0;
            const int window = 2 * radius + 1;
            for (int i = -radius; i <= radius; ++i)
                sum += (i >= 0 && i < length) ? line[i] : 0;
            for (int i = 0; i < length; ++i) {
                at(i) = uchar(sum / window);
                const int out = i - radius;
                const int in = i + radius + 1;
                sum += (in < length ? line[in] : 0) - (out >= 0 ? line[out] : 0);
            }
        }
    };
    for (int i = 0; i < 3; ++i) {
        pass(true);
        pass(false);
    }
}

// Transparency gradient as a brush over `bounds` (alpha only matters).
QBrush transparencyBrush(const Gradient &g, const QRectF &bounds)
{
    auto at = [&bounds](const QPointF &f) {
        return QPointF(bounds.left() + f.x() * bounds.width(), bounds.top() + f.y() * bounds.height());
    };
    QGradient *gradient = nullptr;
    QLinearGradient linear(at(g.start), at(g.end));
    QRadialGradient radial(at(g.center), g.radius * std::max(bounds.width(), bounds.height()));
    gradient = g.type == Gradient::Type::Radial ? static_cast<QGradient *>(&radial) : static_cast<QGradient *>(&linear);
    for (const GradientStop &stop : g.stops)
        gradient->setColorAt(std::clamp(stop.offset, 0.0, 1.0), QColor(0, 0, 0, stop.color.alpha()));
    return QBrush(*gradient);
}

// Draws a shape with a shadow and/or a transparency gradient: the shape goes into an
// off-screen layer in device pixels, masked by the transparency; the shadow is the layer's
// silhouette, blurred and coloured, drawn first.
void paintWithEffects(QPainter &painter, const Shape &shape)
{
    const Style &style = shape.style();
    // Logical → painter pixels, without the offset a widget painted into its window gets
    // (resetTransform() below keeps that offset, so it must not be counted twice).
    const QTransform device = painter.combinedTransform();
    const double unit = std::sqrt(std::abs(device.determinant())); // device pixels per document unit
    const double margin = style.hasStroke() ? style.strokeWidth : 1.0;
    const QRectF body = device.mapRect(shape.bounds().adjusted(-margin, -margin, margin, margin));
    QPointF offset;
    double blur = 0.0;
    if (style.shadow) {
        offset = device.map(style.shadow->offset) - device.map(QPointF(0, 0));
        blur = std::max(style.shadow->blur, 0.0) * unit;
    }
    QRectF needed = body.united(body.translated(offset)).adjusted(-blur * 2, -blur * 2, blur * 2, blur * 2);
    // Only what can show on the device (plus room for the blur).
    if (const QPaintDevice *target = painter.device()) {
        const QRectF visible = QRectF(0, 0, target->width(), target->height())
                                   .adjusted(-blur * 3 - std::abs(offset.x()), -blur * 3 - std::abs(offset.y()),
                                             blur * 3 + std::abs(offset.x()), blur * 3 + std::abs(offset.y()));
        needed = needed.intersected(visible);
    }
    if (needed.isEmpty())
        return;
    needed = needed.toAlignedRect();
    // Sharp on high-DPI screens; never a huge image (PDF export works at 1200 dpi).
    const double ratio = painter.device() ? painter.device()->devicePixelRatioF() : 1.0;
    const double scale = std::min(ratio, 3000.0 / std::max(needed.width(), needed.height()));
    QImage layer(std::max(1, int(std::ceil(needed.width() * scale))), std::max(1, int(std::ceil(needed.height() * scale))),
                 QImage::Format_ARGB32_Premultiplied);
    layer.fill(Qt::transparent);
    {
        QPainter lp(&layer);
        lp.setRenderHints(painter.renderHints());
        lp.scale(scale, scale);
        lp.translate(-needed.topLeft());
        lp.setTransform(device, true);
        paintBody(lp, shape);
        if (style.transparency) {
            // Keep the layer only as much as the transparency allows.
            lp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
            const QRectF frame = shape.localBounds();
            lp.setTransform(shape.shapeTransform(), true);
            lp.fillRect(frame.adjusted(-margin * 4, -margin * 4, margin * 4, margin * 4),
                        transparencyBrush(*style.transparency, frame));
        }
    }

    painter.save();
    painter.resetTransform();
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    if (style.shadow) {
        QImage alpha = layer.convertToFormat(QImage::Format_Alpha8);
        blurAlpha(alpha, int(std::round(blur * scale / 2.0)));
        QImage shadow(layer.size(), QImage::Format_ARGB32_Premultiplied);
        shadow.fill(style.shadow->color);
        {
            QPainter sp(&shadow);
            sp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
            sp.drawImage(0, 0, alpha);
        }
        painter.drawImage(QRectF(needed.topLeft() + offset, needed.size()), shadow);
    }
    painter.drawImage(needed, layer);
    painter.restore();
}

} // namespace

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
    if (style.shadow || style.transparency)
        paintWithEffects(painter, shape);
    else
        paintBody(painter, shape);
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
