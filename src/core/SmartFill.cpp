#include "core/SmartFill.h"

#include "core/Document.h"

#include <optional>

namespace opendraw::smartfill {

QPainterPath regionAt(const std::vector<QPainterPath> &outlines, const QPointF &point)
{
    // Each outline splits the drawing in two; the piece holding the point is on the point's
    // side of every one of them.
    std::optional<QPainterPath> region;
    for (const QPainterPath &outline : outlines) {
        if (outline.contains(point))
            region = region ? region->intersected(outline) : outline;
    }
    if (!region || region->isEmpty())
        return {};
    for (const QPainterPath &outline : outlines) {
        if (!outline.contains(point) && outline.boundingRect().intersects(region->boundingRect()))
            *region = region->subtracted(outline);
    }
    return region->simplified();
}

std::vector<QPainterPath> outlinesOf(const Page &page)
{
    std::vector<QPainterPath> outlines;
    for (const auto &layer : page.layers) {
        if (!layer->visible)
            continue;
        for (const auto &shape : layer->shapes) {
            shape->forEachLeaf([&outlines](Shape &leaf) {
                if (leaf.kind() == ShapeKind::Image)
                    return;
                QPainterPath outline = leaf.path();
                if (!outline.isEmpty())
                    outlines.push_back(std::move(outline));
            });
        }
    }
    return outlines;
}

} // namespace opendraw::smartfill
