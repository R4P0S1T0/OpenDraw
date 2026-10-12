#include "core/Dimension.h"

#include "core/PathOps.h"
#include "core/Render.h"
#include "core/TextLayout.h"

#include <QLineF>
#include <QLocale>
#include <QPolygonF>

#include <cmath>
#include <numbers>

namespace opendraw::dimension {

namespace {

struct Frame {
    QPointF along;  // unit vector from `from` to `to`
    QPointF side;   // unit vector towards the dimension line
    QPointF start;  // ends of the dimension line
    QPointF end;
    double length = 0.0;
};

Frame frameOf(const Dimension &d)
{
    Frame f;
    const QPointF delta = d.to - d.from;
    f.length = std::hypot(delta.x(), delta.y());
    f.along = f.length > 0 ? delta / f.length : QPointF(1, 0);
    // Left of the direction on screen (y pointing down): (1, 0) has (0, -1), above it.
    const QPointF left(f.along.y(), -f.along.x());
    f.side = d.offset >= 0 ? left : -left;
    f.start = d.from + left * d.offset;
    f.end = d.to + left * d.offset;
    return f;
}

TextData labelText(const Dimension &d)
{
    TextData text{label(d), {}};
    text.format.size = std::max(d.textSize, 1.0);
    return text;
}

// From the label's own coordinates (baseline from (0, 0) to (width, 0)) to the page.
QTransform labelTransform(const Dimension &d)
{
    const Frame f = frameOf(d);
    const TextData text = labelText(d);
    const textlayout::Layout layout = textlayout::layout(text, QPointF());
    const double width = layout.lines.front().width;
    double angle = std::atan2(f.along.y(), f.along.x());
    // Readable: never upside down.
    if (angle > std::numbers::pi / 2 + 1e-9)
        angle -= std::numbers::pi;
    else if (angle <= -std::numbers::pi / 2 + 1e-9)
        angle += std::numbers::pi;
    const QPointF down(-std::sin(angle), std::cos(angle));
    const double facing = down.x() * f.side.x() + down.y() * f.side.y();
    // The whole label clear of the line, on the dimension's outer side.
    const double gap = d.textSize * 0.3;
    const double clearance = gap + (facing > 0 ? layout.ascent : layout.descent);
    const QPointF middle = (f.start + f.end) / 2 + f.side * clearance;
    QTransform t;
    t.translate(middle.x(), middle.y());
    t.rotateRadians(angle);
    t.translate(-width / 2.0, 0.0);
    return t;
}

} // namespace

QString label(const Dimension &d)
{
    const double value = QLineF(d.from, d.to).length() * d.unitFactor;
    const QString number = QLocale().toString(value, 'f', std::max(d.decimals, 0));
    return d.unitSuffix.isEmpty() ? number : number + QLatin1Char(' ') + d.unitSuffix;
}

QPainterPath lines(const Dimension &d)
{
    const Frame f = frameOf(d);
    const double gap = d.textSize * 0.2;    // between the measured point and its extension line
    const double beyond = d.textSize * 0.4; // how far extension lines pass the dimension line
    QPainterPath path;
    if (std::abs(d.offset) > gap) {
        path.moveTo(d.from + f.side * gap);
        path.lineTo(f.start + f.side * beyond);
        path.moveTo(d.to + f.side * gap);
        path.lineTo(f.end + f.side * beyond);
    }
    path.moveTo(f.start);
    path.lineTo(f.end);
    return path;
}

QPainterPath arrowheads(const Dimension &d, double width)
{
    const Frame f = frameOf(d);
    if (f.length <= 0.0)
        return {};
    QPainterPath arrows;
    arrows.setFillRule(Qt::WindingFill);
    // Pointing outwards at both ends, the tips on the extension lines: the arrow's base sits
    // at the end, so it is pulled back by its own length.
    const double length = render::arrowheadShape(Arrowhead::Triangle, width).boundingRect().width();
    arrows.addPath(render::arrowheadPath(Arrowhead::Triangle, f.start + f.along * length, -f.along, width));
    arrows.addPath(render::arrowheadPath(Arrowhead::Triangle, f.end - f.along * length, f.along, width));
    return arrows;
}

QPainterPath labelOutline(const Dimension &d)
{
    return labelTransform(d).map(textlayout::outline(labelText(d), QPointF()));
}

QPolygonF labelBox(const Dimension &d)
{
    const TextData text = labelText(d);
    const textlayout::Layout layout = textlayout::layout(text, QPointF());
    const QRectF box(0.0, -layout.ascent, layout.lines.front().width, layout.ascent + layout.descent);
    return labelTransform(d).map(QPolygonF(box));
}

PathData geometry(const Dimension &d)
{
    QPainterPath path = lines(d);
    QPolygonF box = labelBox(d);
    if (!box.isEmpty()) {
        if (box.isClosed())
            box.removeLast();
        path.addPolygon(box);
        path.closeSubpath();
    }
    return pathops::fromPainterPath(path);
}

} // namespace opendraw::dimension
