#include "core/ContourEffect.h"

#include "core/Blend.h"

#include <QPainterPathStroker>

#include <algorithm>
#include <unordered_map>

namespace opendraw::contour {

namespace {

constexpr int kMaxRings = 100;

// The colour an object's fill starts its rings from.
QColor fillColorOf(const Style &style)
{
    if (style.gradient && !style.gradient->stops.empty())
        return style.gradient->stops.front().color;
    if (style.pattern)
        return QColor(0x80, 0x80, 0x80);
    return style.fill;
}

Style ringStyle(const Style &object, const Contour &c, double t)
{
    Style s;
    s.fill = blend::mix(object.hasFill() ? fillColorOf(object) : QColor(Qt::transparent), c.fill, t);
    s.stroke = blend::mix(object.hasStroke() ? object.stroke : QColor(Qt::transparent), c.stroke, t);
    s.strokeWidth = object.hasStroke() ? object.strokeWidth : 1.0;
    s.join = object.join;
    s.opacity = object.opacity;
    return s;
}

std::vector<Ring> computeRings(const QPainterPath &outline, const Style &style)
{
    const Contour &c = *style.contour;
    std::vector<Ring> result;
    if (c.offset <= 0.0 || outline.isEmpty())
        return result;
    if (c.direction == Contour::Direction::Outside) {
        const int n = std::clamp(c.steps, 1, kMaxRings);
        for (int i = n; i >= 1; --i)
            result.push_back({offsetPath(outline, i * c.offset), ringStyle(style, c, double(i) / n)});
        return result;
    }
    // Inside: rings shrink until nothing is left (to the centre) or the steps run out.
    std::vector<QPainterPath> paths;
    const int limit = c.direction == Contour::Direction::ToCenter ? kMaxRings : std::clamp(c.steps, 1, kMaxRings);
    for (int i = 1; i <= limit; ++i) {
        QPainterPath ring = offsetPath(outline, -i * c.offset);
        // Each ring lies within the one before; one that does not is the band turning inside out.
        const QRectF previous = paths.empty() ? outline.boundingRect() : paths.back().boundingRect();
        if (ring.isEmpty() || !previous.adjusted(-0.01, -0.01, 0.01, 0.01).contains(ring.boundingRect()))
            break;
        paths.push_back(std::move(ring));
    }
    const int n = c.direction == Contour::Direction::ToCenter ? int(paths.size()) : limit;
    for (std::size_t i = 0; i < paths.size(); ++i)
        result.push_back({std::move(paths[i]), ringStyle(style, c, double(i + 1) / std::max(n, 1))});
    return result;
}

// Rings are costly to work out, and drawing asks for them on every repaint: they are kept
// per shape until its outline or style changes.
struct CacheEntry {
    QPainterPath outline;
    Style style;
    std::vector<Ring> rings;
};

bool sameForRings(const Style &a, const Style &b)
{
    return a.contour == b.contour && a.fill == b.fill && a.stroke == b.stroke && a.strokeWidth == b.strokeWidth
           && a.gradient == b.gradient && a.pattern.has_value() == b.pattern.has_value() && a.join == b.join
           && a.opacity == b.opacity;
}

} // namespace

QPainterPath offsetPath(const QPainterPath &outline, double distance)
{
    if (distance == 0.0)
        return outline;
    // Shrinking by half the narrower side leaves nothing (and a band that wide would turn
    // inside out).
    const QRectF box = outline.boundingRect();
    if (distance < 0.0 && -distance >= std::min(box.width(), box.height()) / 2.0)
        return {};
    QPainterPathStroker stroker;
    stroker.setWidth(2.0 * std::abs(distance));
    stroker.setJoinStyle(Qt::RoundJoin);
    stroker.setCapStyle(Qt::RoundCap);
    const QPainterPath band = stroker.createStroke(outline);
    QPainterPath area = outline;
    area.closeSubpath();
    QPainterPath result = (distance > 0 ? area.united(band) : area.subtracted(band)).simplified();
    const QRectF left = result.boundingRect();
    if (left.width() < 0.5 || left.height() < 0.5)
        return {}; // a sliver, not a ring
    return result;
}

std::vector<Ring> rings(const Shape &shape)
{
    const Style &style = shape.style();
    if (!style.contour || shape.kind() == ShapeKind::Group || shape.kind() == ShapeKind::Image)
        return {};
    thread_local std::unordered_map<quint64, CacheEntry> cache;
    const QPainterPath outline = shape.path();
    auto it = cache.find(shape.id());
    if (it != cache.end() && it->second.outline == outline && sameForRings(it->second.style, style))
        return it->second.rings;
    if (cache.size() > 256)
        cache.clear();
    CacheEntry entry{outline, style, computeRings(outline, style)};
    std::vector<Ring> result = entry.rings;
    cache[shape.id()] = std::move(entry);
    return result;
}

double reach(const Style &style)
{
    if (!style.contour || style.contour->direction != Contour::Direction::Outside)
        return 0.0;
    return std::clamp(style.contour->steps, 1, kMaxRings) * std::max(style.contour->offset, 0.0)
           + (style.hasStroke() ? style.strokeWidth : 1.0);
}

} // namespace opendraw::contour
