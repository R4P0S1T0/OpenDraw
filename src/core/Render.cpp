#include "core/Render.h"

#include "core/Document.h"
#include "core/Shape.h"

#include <QPainter>

namespace opendraw::render {

void paintShape(QPainter &painter, const Shape &shape)
{
    if (shape.kind() == ShapeKind::Group) {
        for (const auto &child : shape.children())
            paintShape(painter, *child);
        return;
    }
    const Style &style = shape.style();
    painter.setBrush(style.hasFill() ? QBrush(style.fill) : QBrush(Qt::NoBrush));
    painter.setPen(style.hasStroke() ? QPen(style.stroke, style.strokeWidth) : QPen(Qt::NoPen));
    painter.drawPath(shape.path());
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
