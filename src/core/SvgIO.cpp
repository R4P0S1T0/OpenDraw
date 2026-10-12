#include "core/SvgIO.h"

#include "core/ContourEffect.h"
#include "core/Dimension.h"
#include "core/Document.h"
#include "core/Extrude.h"
#include "core/PathOps.h"
#include "core/Render.h"
#include "core/RichText.h"
#include "core/TextLayout.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QHash>
#include <QPainterPath>
#include <QRegularExpression>
#include <QSet>
#include <QTransform>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>

namespace opendraw::svg {

namespace {

QString number(double value)
{
    return QString::number(value, 'g', 10);
}

// ---------------------------------------------------------------------------------------------
// Writing

void writePaint(QXmlStreamWriter &w, const QString &name, const QColor &color, bool visible)
{
    if (!visible) {
        w.writeAttribute(name, QStringLiteral("none"));
        return;
    }
    w.writeAttribute(name, color.name(QColor::HexRgb));
    if (color.alpha() < 255)
        w.writeAttribute(name + QStringLiteral("-opacity"), number(color.alphaF()));
}

// Gradients are stored in bounding-box units, which is exactly the model's representation.
void writeGradient(QXmlStreamWriter &w, const Gradient &g, const QString &id)
{
    if (g.type == Gradient::Type::Radial) {
        w.writeStartElement(QStringLiteral("radialGradient"));
        w.writeAttribute(QStringLiteral("id"), id);
        w.writeAttribute(QStringLiteral("cx"), number(g.center.x()));
        w.writeAttribute(QStringLiteral("cy"), number(g.center.y()));
        w.writeAttribute(QStringLiteral("r"), number(g.radius));
    } else {
        w.writeStartElement(QStringLiteral("linearGradient"));
        w.writeAttribute(QStringLiteral("id"), id);
        w.writeAttribute(QStringLiteral("x1"), number(g.start.x()));
        w.writeAttribute(QStringLiteral("y1"), number(g.start.y()));
        w.writeAttribute(QStringLiteral("x2"), number(g.end.x()));
        w.writeAttribute(QStringLiteral("y2"), number(g.end.y()));
    }
    w.writeAttribute(QStringLiteral("gradientUnits"), QStringLiteral("objectBoundingBox"));
    for (const GradientStop &stop : g.stops) {
        w.writeEmptyElement(QStringLiteral("stop"));
        w.writeAttribute(QStringLiteral("offset"), number(stop.offset));
        w.writeAttribute(QStringLiteral("stop-color"), stop.color.name(QColor::HexRgb));
        if (stop.color.alpha() < 255)
            w.writeAttribute(QStringLiteral("stop-opacity"), number(stop.color.alphaF()));
    }
    w.writeEndElement();
}

QString pngDataUri(const QImage &image)
{
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png.toBase64());
}

QString matrix(const QTransform &t)
{
    return QStringLiteral("matrix(%1 %2 %3 %4 %5 %6)")
        .arg(number(t.m11()), number(t.m12()), number(t.m21()), number(t.m22()), number(t.dx()), number(t.dy()));
}

const QString kXlinkNamespace = QStringLiteral("http://www.w3.org/1999/xlink");

// An <image> filling the unit square, placed by a matrix (no aspect-ratio fitting).
// The name given in the Objects docker, as Inkscape stores object labels.
void writeLabel(QXmlStreamWriter &w, const QString &name)
{
    if (!name.isEmpty())
        w.writeAttribute(QString::fromLatin1(kInkscapeNamespace), QStringLiteral("label"), name);
}

void writeImageElement(QXmlStreamWriter &w, const QImage &image, const QRectF &rect, const QTransform &placement,
                       const QString &label = QString())
{
    w.writeStartElement(QStringLiteral("image"));
    writeLabel(w, label);
    w.writeAttribute(QStringLiteral("x"), number(rect.x()));
    w.writeAttribute(QStringLiteral("y"), number(rect.y()));
    w.writeAttribute(QStringLiteral("width"), number(rect.width()));
    w.writeAttribute(QStringLiteral("height"), number(rect.height()));
    w.writeAttribute(QStringLiteral("preserveAspectRatio"), QStringLiteral("none"));
    if (!placement.isIdentity())
        w.writeAttribute(QStringLiteral("transform"), matrix(placement));
    w.writeAttribute(kXlinkNamespace, QStringLiteral("href"), pngDataUri(image));
    w.writeEndElement();
}

void writePattern(QXmlStreamWriter &w, const PatternFill &pattern, const QRectF &bounds, const QString &id)
{
    const QPointF topLeft = bounds.topLeft() + pattern.origin;
    w.writeStartElement(QStringLiteral("pattern"));
    w.writeAttribute(QStringLiteral("id"), id);
    w.writeAttribute(QStringLiteral("patternUnits"), QStringLiteral("userSpaceOnUse"));
    w.writeAttribute(QStringLiteral("x"), number(topLeft.x()));
    w.writeAttribute(QStringLiteral("y"), number(topLeft.y()));
    w.writeAttribute(QStringLiteral("width"), number(pattern.tileSize.width()));
    w.writeAttribute(QStringLiteral("height"), number(pattern.tileSize.height()));
    writeImageElement(w, pattern.image, QRectF(QPointF(0, 0), pattern.tileSize), QTransform());
    w.writeEndElement();
}

void writeStyle(QXmlStreamWriter &w, const Style &style, const QString &gradientId)
{
    if (!gradientId.isEmpty())
        w.writeAttribute(QStringLiteral("fill"), QStringLiteral("url(#%1)").arg(gradientId));
    else
        writePaint(w, QStringLiteral("fill"), style.fill, style.hasFill());
    writePaint(w, QStringLiteral("stroke"), style.stroke, style.hasStroke());
    if (style.hasStroke()) {
        w.writeAttribute(QStringLiteral("stroke-width"), number(style.strokeWidth));
        if (!style.dashes.empty()) {
            QStringList lengths;
            for (double d : style.dashes)
                lengths << number(d * style.strokeWidth);
            w.writeAttribute(QStringLiteral("stroke-dasharray"), lengths.join(QLatin1Char(' ')));
        }
        if (style.cap != LineCap::Butt)
            w.writeAttribute(QStringLiteral("stroke-linecap"),
                             style.cap == LineCap::Round ? QStringLiteral("round") : QStringLiteral("square"));
        if (style.join != LineJoin::Miter)
            w.writeAttribute(QStringLiteral("stroke-linejoin"),
                             style.join == LineJoin::Round ? QStringLiteral("round") : QStringLiteral("bevel"));
    }
    if (style.strokeBehind && style.hasStroke())
        w.writeAttribute(QStringLiteral("paint-order"), QStringLiteral("stroke"));
    if (style.scaleStroke)
        w.writeAttribute(QString::fromLatin1(kNamespace), QStringLiteral("scale-stroke"), QStringLiteral("true"));
    if (style.opacity < 1.0)
        w.writeAttribute(QStringLiteral("opacity"), number(std::clamp(style.opacity, 0.0, 1.0)));
}

// --- Arrowheads: SVG markers, plus OpenDraw attributes naming them.

const char *const kArrowNames[] = {"none", "triangle", "notched", "open", "circle", "square", "diamond", "bar"};

QString arrowName(Arrowhead type)
{
    return QString::fromLatin1(kArrowNames[int(type)]);
}

std::optional<Arrowhead> parseArrow(QStringView name)
{
    for (int i = 0; i < int(std::size(kArrowNames)); ++i) {
        if (name == QLatin1String(kArrowNames[i]))
            return Arrowhead(i);
    }
    return std::nullopt;
}

// Path data of a QPainterPath made of closed outlines.
QString pathText(const QPainterPath &path)
{
    QStringList parts;
    for (int i = 0; i < path.elementCount(); ++i) {
        const QPainterPath::Element e = path.elementAt(i);
        switch (e.type) {
        case QPainterPath::MoveToElement:
            if (i > 0)
                parts << QStringLiteral("Z");
            parts << QStringLiteral("M %1 %2").arg(number(e.x), number(e.y));
            break;
        case QPainterPath::LineToElement:
            parts << QStringLiteral("L %1 %2").arg(number(e.x), number(e.y));
            break;
        case QPainterPath::CurveToElement: {
            const QPainterPath::Element c2 = path.elementAt(i + 1), to = path.elementAt(i + 2);
            parts << QStringLiteral("C %1 %2 %3 %4 %5 %6")
                         .arg(number(e.x), number(e.y), number(c2.x), number(c2.y), number(to.x), number(to.y));
            i += 2;
            break;
        }
        case QPainterPath::CurveToDataElement:
            break;
        }
    }
    parts << QStringLiteral("Z");
    return parts.join(QLatin1Char(' '));
}

// Writes markers for the arrowheads a path shows (before the element); returns their ids.
std::pair<QString, QString> writeMarkers(QXmlStreamWriter &w, const Shape &shape, int &count)
{
    const Style &style = shape.style();
    std::pair<QString, QString> ids;
    if (shape.kind() != ShapeKind::Path || !style.hasStroke() || shape.pathData().empty())
        return ids;
    auto marker = [&](Arrowhead type, bool start) {
        const QString id = QStringLiteral("arrow%1").arg(++count);
        w.writeStartElement(QStringLiteral("marker"));
        w.writeAttribute(QStringLiteral("id"), id);
        w.writeAttribute(QStringLiteral("markerUnits"), QStringLiteral("userSpaceOnUse"));
        w.writeAttribute(QStringLiteral("orient"), QStringLiteral("auto"));
        w.writeAttribute(QStringLiteral("overflow"), QStringLiteral("visible"));
        w.writeAttribute(QStringLiteral("refX"), QStringLiteral("0"));
        w.writeAttribute(QStringLiteral("refY"), QStringLiteral("0"));
        // "auto" points the marker along the path; at the start the arrow faces the other way.
        QPainterPath outline = render::arrowheadShape(type, style.strokeWidth);
        if (start)
            outline = QTransform().rotate(180).map(outline);
        w.writeEmptyElement(QStringLiteral("path"));
        w.writeAttribute(QStringLiteral("d"), pathText(outline));
        writePaint(w, QStringLiteral("fill"), style.stroke, true);
        w.writeEndElement();
        return id;
    };
    const SubPath &first = shape.pathData().front(), &last = shape.pathData().back();
    if (style.startArrow != Arrowhead::None && !first.closed && first.nodes.size() > 1)
        ids.first = marker(style.startArrow, true);
    if (style.endArrow != Arrowhead::None && !last.closed && last.nodes.size() > 1)
        ids.second = marker(style.endArrow, false);
    return ids;
}

void writeArrows(QXmlStreamWriter &w, const Style &style, const std::pair<QString, QString> &ids)
{
    // The setting is kept even where nothing shows (a closed path), as it is in OpenDraw.
    const QString ns = QString::fromLatin1(kNamespace);
    if (!ids.first.isEmpty())
        w.writeAttribute(QStringLiteral("marker-start"), QStringLiteral("url(#%1)").arg(ids.first));
    if (!ids.second.isEmpty())
        w.writeAttribute(QStringLiteral("marker-end"), QStringLiteral("url(#%1)").arg(ids.second));
    if (style.startArrow != Arrowhead::None)
        w.writeAttribute(ns, QStringLiteral("start-arrow"), arrowName(style.startArrow));
    if (style.endArrow != Arrowhead::None)
        w.writeAttribute(ns, QStringLiteral("end-arrow"), arrowName(style.endArrow));
}

// --- Effects: drop shadow (an SVG filter) and transparency gradient (an SVG mask). Other
// programs show them through the filter and mask; OpenDraw reads its own attributes back.

QString shadowText(const DropShadow &s)
{
    return QStringLiteral("%1 %2 %3 %4 %5")
        .arg(number(s.offset.x()), number(s.offset.y()), number(s.blur), s.color.name(), number(s.color.alphaF()));
}

QString transparencyText(const Gradient &g)
{
    QStringList parts{g.type == Gradient::Type::Radial ? QStringLiteral("radial") : QStringLiteral("linear"),
                      number(g.start.x()), number(g.start.y()), number(g.end.x()), number(g.end.y()),
                      number(g.center.x()), number(g.center.y()), number(g.radius)};
    for (const GradientStop &stop : g.stops)
        parts << number(stop.offset) + QLatin1Char(':') + number(stop.color.alphaF());
    return parts.join(QLatin1Char(' '));
}

std::optional<DropShadow> parseShadow(QStringView text)
{
    const QStringList parts = text.toString().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (parts.size() != 5)
        return std::nullopt;
    DropShadow s;
    s.offset = QPointF(parts[0].toDouble(), parts[1].toDouble());
    s.blur = std::max(parts[2].toDouble(), 0.0);
    s.color = QColor::fromString(parts[3]);
    if (!s.color.isValid())
        return std::nullopt;
    s.color.setAlphaF(std::clamp(parts[4].toDouble(), 0.0, 1.0));
    return s;
}

std::optional<Gradient> parseTransparency(QStringView text)
{
    const QStringList parts = text.toString().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (parts.size() < 10)
        return std::nullopt;
    Gradient g;
    g.type = parts[0] == u"radial" ? Gradient::Type::Radial : Gradient::Type::Linear;
    g.start = QPointF(parts[1].toDouble(), parts[2].toDouble());
    g.end = QPointF(parts[3].toDouble(), parts[4].toDouble());
    g.center = QPointF(parts[5].toDouble(), parts[6].toDouble());
    g.radius = parts[7].toDouble();
    for (int i = 8; i < parts.size(); ++i) {
        const QStringList stop = parts[i].split(QLatin1Char(':'));
        if (stop.size() != 2)
            return std::nullopt;
        QColor solid(Qt::black);
        solid.setAlphaF(std::clamp(stop[1].toDouble(), 0.0, 1.0));
        g.stops.push_back({std::clamp(stop[0].toDouble(), 0.0, 1.0), solid});
    }
    return g;
}

// Writes the filter and mask an element's effects need (before the element); returns their ids.
std::pair<QString, QString> writeEffectDefinitions(QXmlStreamWriter &w, const Style &style, int &count)
{
    QString filterId, maskId;
    if (style.shadow) {
        const DropShadow &s = *style.shadow;
        filterId = QStringLiteral("shadow%1").arg(++count);
        w.writeStartElement(QStringLiteral("filter"));
        w.writeAttribute(QStringLiteral("id"), filterId);
        w.writeAttribute(QStringLiteral("x"), QStringLiteral("-100%"));
        w.writeAttribute(QStringLiteral("y"), QStringLiteral("-100%"));
        w.writeAttribute(QStringLiteral("width"), QStringLiteral("300%"));
        w.writeAttribute(QStringLiteral("height"), QStringLiteral("300%"));
        w.writeEmptyElement(QStringLiteral("feGaussianBlur"));
        w.writeAttribute(QStringLiteral("in"), QStringLiteral("SourceAlpha"));
        w.writeAttribute(QStringLiteral("stdDeviation"), number(s.blur / 2.0));
        w.writeEmptyElement(QStringLiteral("feOffset"));
        w.writeAttribute(QStringLiteral("dx"), number(s.offset.x()));
        w.writeAttribute(QStringLiteral("dy"), number(s.offset.y()));
        w.writeAttribute(QStringLiteral("result"), QStringLiteral("offset"));
        w.writeEmptyElement(QStringLiteral("feFlood"));
        w.writeAttribute(QStringLiteral("flood-color"), s.color.name());
        w.writeAttribute(QStringLiteral("flood-opacity"), number(s.color.alphaF()));
        w.writeEmptyElement(QStringLiteral("feComposite"));
        w.writeAttribute(QStringLiteral("in2"), QStringLiteral("offset"));
        w.writeAttribute(QStringLiteral("operator"), QStringLiteral("in"));
        w.writeStartElement(QStringLiteral("feMerge"));
        w.writeEmptyElement(QStringLiteral("feMergeNode"));
        w.writeEmptyElement(QStringLiteral("feMergeNode"));
        w.writeAttribute(QStringLiteral("in"), QStringLiteral("SourceGraphic"));
        w.writeEndElement();
        w.writeEndElement();
    }
    if (style.transparency) {
        // A white gradient whose opacity is how solid the object is.
        Gradient white = *style.transparency;
        for (GradientStop &stop : white.stops)
            stop.color = QColor(255, 255, 255, stop.color.alpha());
        const QString gradientId = QStringLiteral("maskgradient%1").arg(++count);
        writeGradient(w, white, gradientId);
        maskId = QStringLiteral("mask%1").arg(count);
        w.writeStartElement(QStringLiteral("mask"));
        w.writeAttribute(QStringLiteral("id"), maskId);
        w.writeAttribute(QStringLiteral("maskUnits"), QStringLiteral("objectBoundingBox"));
        w.writeAttribute(QStringLiteral("maskContentUnits"), QStringLiteral("objectBoundingBox"));
        for (const char *attribute : {"x", "y"})
            w.writeAttribute(QString::fromLatin1(attribute), QStringLiteral("-0.5"));
        for (const char *attribute : {"width", "height"})
            w.writeAttribute(QString::fromLatin1(attribute), QStringLiteral("2"));
        w.writeEmptyElement(QStringLiteral("rect"));
        w.writeAttribute(QStringLiteral("x"), QStringLiteral("-0.5"));
        w.writeAttribute(QStringLiteral("y"), QStringLiteral("-0.5"));
        w.writeAttribute(QStringLiteral("width"), QStringLiteral("2"));
        w.writeAttribute(QStringLiteral("height"), QStringLiteral("2"));
        w.writeAttribute(QStringLiteral("fill"), QStringLiteral("url(#%1)").arg(gradientId));
        w.writeEndElement();
    }
    return {filterId, maskId};
}

QString contourText(const Contour &c);

void writeEffects(QXmlStreamWriter &w, const Style &style, const std::pair<QString, QString> &ids, bool original = false)
{
    const QString ns = QString::fromLatin1(kNamespace);
    if (original) {
        w.writeAttribute(QStringLiteral("display"), QStringLiteral("none"));
        w.writeAttribute(ns, QStringLiteral("original"), QStringLiteral("true"));
    }
    if (style.contour)
        w.writeAttribute(ns, QStringLiteral("contour"), contourText(*style.contour));
    if (style.mesh && style.mesh->isValid()) {
        QStringList parts{QString::number(style.mesh->columns), QString::number(style.mesh->rows)};
        for (const QColor &color : style.mesh->colors)
            parts << color.name(QColor::HexArgb);
        w.writeAttribute(ns, QStringLiteral("mesh"), parts.join(QLatin1Char(' ')));
    }
    if (style.extrusion) {
        const Extrusion &e = *style.extrusion;
        w.writeAttribute(ns, QStringLiteral("extrude"),
                         QStringLiteral("%1 %2 %3 %4").arg(number(e.vanishingPoint.x()), number(e.vanishingPoint.y()),
                                                          number(e.depth),
                                                          e.color.isValid() ? e.color.name(QColor::HexArgb)
                                                                            : QStringLiteral("shaded")));
    }
    if (style.shadow) {
        w.writeAttribute(QStringLiteral("filter"), QStringLiteral("url(#%1)").arg(ids.first));
        w.writeAttribute(ns, QStringLiteral("shadow"), shadowText(*style.shadow));
    }
    if (style.transparency) {
        w.writeAttribute(QStringLiteral("mask"), QStringLiteral("url(#%1)").arg(ids.second));
        w.writeAttribute(ns, QStringLiteral("transparency"), transparencyText(*style.transparency));
    }
}

// Rotation or skew of a rectangle, ellipse or text. The outline width is kept as it is on
// the page, as OpenDraw draws it.
void writeShapeTransform(QXmlStreamWriter &w, const Shape &shape)
{
    if (shape.shapeTransform().isIdentity())
        return;
    w.writeAttribute(QStringLiteral("transform"), matrix(shape.shapeTransform()));
    if (shape.style().hasStroke())
        w.writeAttribute(QStringLiteral("vector-effect"), QStringLiteral("non-scaling-stroke"));
}

QString contourText(const Contour &c)
{
    static const char *const directions[] = {"outside", "inside", "center"};
    return QStringLiteral("%1 %2 %3 %4 %5 %6 %7")
        .arg(QString::fromLatin1(directions[int(c.direction)]), QString::number(c.steps), number(c.offset),
             c.fill.name(), number(c.fill.alphaF()), c.stroke.name(), number(c.stroke.alphaF()));
}

std::optional<Contour> parseContour(QStringView text)
{
    const QStringList parts = text.toString().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (parts.size() != 7)
        return std::nullopt;
    Contour c;
    c.direction = parts[0] == u"inside"   ? Contour::Direction::Inside
                  : parts[0] == u"center" ? Contour::Direction::ToCenter
                                          : Contour::Direction::Outside;
    c.steps = std::clamp(parts[1].toInt(), 1, 100);
    c.offset = std::max(parts[2].toDouble(), 0.0);
    c.fill = QColor::fromString(parts[3]);
    c.stroke = QColor::fromString(parts[5]);
    if (!c.fill.isValid() || !c.stroke.isValid())
        return std::nullopt;
    c.fill.setAlphaF(std::clamp(parts[4].toDouble(), 0.0, 1.0));
    c.stroke.setAlphaF(std::clamp(parts[6].toDouble(), 0.0, 1.0));
    return c;
}

// Contour rings as plain shapes, for other programs; OpenDraw skips them when reading.
void writeContourRings(QXmlStreamWriter &w, const std::vector<contour::Ring> &rings)
{
    if (rings.empty())
        return;
    w.writeStartElement(QStringLiteral("g"));
    w.writeAttribute(QString::fromLatin1(kNamespace), QStringLiteral("generated"), QStringLiteral("true"));
    for (const contour::Ring &ring : rings) {
        w.writeEmptyElement(QStringLiteral("path"));
        w.writeAttribute(QStringLiteral("d"), pathText(ring.path));
        if (ring.path.fillRule() == Qt::OddEvenFill)
            w.writeAttribute(QStringLiteral("fill-rule"), QStringLiteral("evenodd"));
        writeStyle(w, ring.style, QString());
    }
    w.writeEndElement();
}

// Objects that connectors in the file being written are joined to: they get an id.
thread_local QSet<quint64> tJoined;

void collectJoined(const std::vector<const Shape *> &shapes)
{
    tJoined.clear();
    for (const Shape *shape : shapes) {
        if (shape->connector()) {
            tJoined.insert(shape->connector()->from);
            tJoined.insert(shape->connector()->to);
        }
    }
}

void writeJoinId(QXmlStreamWriter &w, const Shape &shape)
{
    if (tJoined.contains(shape.id()))
        w.writeAttribute(QString::fromLatin1(kNamespace), QStringLiteral("id"), QString::number(shape.id()));
}

QString dimensionText(const Dimension &d)
{
    return QStringLiteral("%1 %2 %3 %4 %5 %6 %7 %8 %9")
        .arg(number(d.from.x()), number(d.from.y()), number(d.to.x()), number(d.to.y()), number(d.offset),
             number(d.unitFactor), QString::number(d.decimals), number(d.textSize), d.unitSuffix);
}

std::optional<Dimension> parseDimension(QStringView text)
{
    const QStringList parts = text.toString().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (parts.size() < 8)
        return std::nullopt;
    Dimension d;
    d.from = QPointF(parts[0].toDouble(), parts[1].toDouble());
    d.to = QPointF(parts[2].toDouble(), parts[3].toDouble());
    d.offset = parts[4].toDouble();
    d.unitFactor = parts[5].toDouble();
    d.decimals = parts[6].toInt();
    d.textSize = parts[7].toDouble();
    d.unitSuffix = parts.mid(8).join(QLatin1Char(' '));
    return d;
}

QString envelopeText(const Envelope &e)
{
    QStringList numbers;
    for (const QPointF &p : e.points)
        numbers << number(p.x()) << number(p.y());
    return numbers.join(QLatin1Char(' '));
}

std::optional<Envelope> parseEnvelope(QStringView text)
{
    if (text.isEmpty())
        return std::nullopt;
    const QStringList v = text.toString().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (v.size() != 24)
        return std::nullopt;
    Envelope e;
    for (int i = 0; i < 12; ++i)
        e.points[i] = QPointF(v[2 * i].toDouble(), v[2 * i + 1].toDouble());
    return e;
}

void writeShapeElement(QXmlStreamWriter &w, const Shape &shape, int &gradientCount, bool original = false);
void writeShapeItself(QXmlStreamWriter &w, const Shape &shape, int &gradientCount);

// A shape and, for a PowerClip frame, its contents: clipped to the frame's outline (SVG
// clip-path), with the frame's outline drawn again over them for other programs.
void writeShape(QXmlStreamWriter &w, const Shape &shape, int &gradientCount)
{
    if (const auto &d = shape.dimension()) {
        // A group with the dimension's settings and style; inside, its lines, arrowheads and
        // label as plain shapes for other programs (OpenDraw draws them again from the settings).
        const QString ns = QString::fromLatin1(kNamespace);
        w.writeStartElement(QStringLiteral("g"));
        w.writeAttribute(ns, QStringLiteral("dimension"), dimensionText(*d));
        writeLabel(w, shape.name());
        writeJoinId(w, shape);
        Style lineStyle = shape.style();
        lineStyle.fill = Qt::transparent;
        if (!lineStyle.hasStroke()) {
            lineStyle.stroke = Qt::black;
            lineStyle.strokeWidth = 1.0;
        }
        writeStyle(w, lineStyle, QString());
        w.writeStartElement(QStringLiteral("g"));
        w.writeAttribute(ns, QStringLiteral("generated"), QStringLiteral("true"));
        w.writeEmptyElement(QStringLiteral("path"));
        w.writeAttribute(QStringLiteral("d"), pathops::toSvg(pathops::fromPainterPath(dimension::lines(*d))));
        w.writeEmptyElement(QStringLiteral("path"));
        QPainterPath marks = dimension::arrowheads(*d, lineStyle.strokeWidth);
        marks.addPath(dimension::labelOutline(*d));
        w.writeAttribute(QStringLiteral("d"), pathText(marks));
        writePaint(w, QStringLiteral("fill"), lineStyle.stroke, true);
        w.writeAttribute(QStringLiteral("stroke"), QStringLiteral("none"));
        w.writeEndElement();
        w.writeEndElement();
        return;
    }
    if (shape.clippedContents().empty()) {
        writeShapeItself(w, shape, gradientCount);
        return;
    }
    const QString ns = QString::fromLatin1(kNamespace);
    w.writeStartElement(QStringLiteral("g"));
    w.writeAttribute(ns, QStringLiteral("powerclip"), QStringLiteral("true"));
    writeShapeItself(w, shape, gradientCount);
    const QString clipId = QStringLiteral("clip%1").arg(++gradientCount);
    const QPainterPath outline = shape.path();
    w.writeStartElement(QStringLiteral("clipPath"));
    w.writeAttribute(QStringLiteral("id"), clipId);
    w.writeEmptyElement(QStringLiteral("path"));
    w.writeAttribute(QStringLiteral("d"), pathText(outline));
    if (outline.fillRule() == Qt::OddEvenFill)
        w.writeAttribute(QStringLiteral("clip-rule"), QStringLiteral("evenodd"));
    w.writeEndElement();
    w.writeStartElement(QStringLiteral("g"));
    w.writeAttribute(QStringLiteral("clip-path"), QStringLiteral("url(#%1)").arg(clipId));
    for (const auto &content : shape.clippedContents())
        writeShape(w, *content, gradientCount);
    w.writeEndElement();
    if (shape.style().hasStroke()) {
        w.writeStartElement(QStringLiteral("g"));
        w.writeAttribute(ns, QStringLiteral("generated"), QStringLiteral("true"));
        Style outlineOnly = shape.style();
        outlineOnly.fill = Qt::transparent;
        outlineOnly.gradient.reset();
        outlineOnly.pattern.reset();
        outlineOnly.shadow.reset();
        outlineOnly.contour.reset();
        outlineOnly.extrusion.reset();
        outlineOnly.transparency.reset();
        writeShapeItself(w, Shape(pathops::fromPainterPath(outline), outlineOnly), gradientCount);
        w.writeEndElement();
    }
    w.writeEndElement();
}

// An element, with its contour rings under it (outside) or over it (inside).
void writeShapeItself(QXmlStreamWriter &w, const Shape &shape, int &gradientCount)
{
    if (shape.envelope()) {
        // Other programs get the object bent, as a curve; OpenDraw reads back the object
        // itself (hidden) and its envelope.
        const QString ns = QString::fromLatin1(kNamespace);
        w.writeStartElement(QStringLiteral("g"));
        w.writeAttribute(ns, QStringLiteral("envelope"), envelopeText(*shape.envelope()));
        w.writeStartElement(QStringLiteral("g"));
        w.writeAttribute(ns, QStringLiteral("generated"), QStringLiteral("true"));
        const Shape bent(pathops::fromPainterPath(shape.path()), shape.style());
        writeShape(w, bent, gradientCount);
        w.writeEndElement();
        writeShapeElement(w, shape, gradientCount, true);
        w.writeEndElement();
        return;
    }
    if (shape.style().extrusion) {
        // The extruded sides, as plain shapes under the object.
        w.writeStartElement(QStringLiteral("g"));
        w.writeAttribute(QString::fromLatin1(kNamespace), QStringLiteral("generated"), QStringLiteral("true"));
        Style faceStyle = shape.style();
        faceStyle.dashes.clear();
        for (const extrude::Face &face : extrude::faces(shape)) {
            QStringList points;
            for (const QPointF &p : face.polygon)
                points << number(p.x()) + QLatin1Char(',') + number(p.y());
            w.writeEmptyElement(QStringLiteral("polygon"));
            w.writeAttribute(QStringLiteral("points"), points.join(QLatin1Char(' ')));
            writePaint(w, QStringLiteral("fill"), face.color, true);
            const bool outlined = faceStyle.hasStroke() && face.lines.empty(); // the back
            writePaint(w, QStringLiteral("stroke"), outlined ? faceStyle.stroke : face.color, true);
            w.writeAttribute(QStringLiteral("stroke-width"), outlined ? number(faceStyle.strokeWidth) : QStringLiteral("0.5"));
            if (!faceStyle.hasStroke())
                continue;
            for (const QLineF &line : face.lines) {
                w.writeEmptyElement(QStringLiteral("line"));
                w.writeAttribute(QStringLiteral("x1"), number(line.x1()));
                w.writeAttribute(QStringLiteral("y1"), number(line.y1()));
                w.writeAttribute(QStringLiteral("x2"), number(line.x2()));
                w.writeAttribute(QStringLiteral("y2"), number(line.y2()));
                writePaint(w, QStringLiteral("stroke"), faceStyle.stroke, true);
                w.writeAttribute(QStringLiteral("stroke-width"), number(faceStyle.strokeWidth));
                w.writeAttribute(QStringLiteral("stroke-linecap"), QStringLiteral("round"));
            }
        }
        w.writeEndElement();
    }
    const std::vector<contour::Ring> rings = contour::rings(shape);
    const bool under = shape.style().contour && shape.style().contour->direction == Contour::Direction::Outside;
    if (under)
        writeContourRings(w, rings);
    writeShapeElement(w, shape, gradientCount);
    if (!under)
        writeContourRings(w, rings);
}

void writeShapeElement(QXmlStreamWriter &w, const Shape &shape, int &gradientCount, bool original)
{
    // A gradient is written right before the element that uses it.
    QString gradientId;
    if (shape.kind() != ShapeKind::Group && shape.kind() != ShapeKind::Image) {
        if (shape.style().mesh && shape.style().mesh->isValid()) {
            // Other programs get the mesh as a picture filling the object's box.
            const MeshFill &mesh = *shape.style().mesh;
            const QRectF box = shape.localBounds();
            PatternFill picture;
            picture.image = render::meshImage(mesh, QSize(std::clamp(mesh.columns * 48, 2, 512),
                                                          std::clamp(mesh.rows * 48, 2, 512)));
            picture.tileSize = box.size();
            gradientId = QStringLiteral("pattern%1").arg(++gradientCount);
            writePattern(w, picture, box, gradientId);
        } else if (shape.style().pattern) {
            gradientId = QStringLiteral("pattern%1").arg(++gradientCount);
            writePattern(w, *shape.style().pattern, shape.localBounds(), gradientId);
        } else if (shape.style().gradient) {
            gradientId = QStringLiteral("gradient%1").arg(++gradientCount);
            writeGradient(w, *shape.style().gradient, gradientId);
        }
    }
    std::pair<QString, QString> effectIds;
    if (shape.kind() != ShapeKind::Group && shape.kind() != ShapeKind::Image)
        effectIds = writeEffectDefinitions(w, shape.style(), gradientCount);
    const std::pair<QString, QString> markerIds = writeMarkers(w, shape, gradientCount);

    switch (shape.kind()) {
    case ShapeKind::Image:
        writeImageElement(w, shape.image(), QRectF(0, 0, 1, 1), shape.imagePlacement(), shape.name());
        return;
    case ShapeKind::Rectangle: {
        const QRectF r = shape.localBounds();
        w.writeStartElement(QStringLiteral("rect"));
        w.writeAttribute(QStringLiteral("x"), number(r.x()));
        w.writeAttribute(QStringLiteral("y"), number(r.y()));
        w.writeAttribute(QStringLiteral("width"), number(r.width()));
        w.writeAttribute(QStringLiteral("height"), number(r.height()));
        if (shape.cornerRadius() > 0.0) {
            const double radius = std::min({shape.cornerRadius(), r.width() / 2.0, r.height() / 2.0});
            w.writeAttribute(QStringLiteral("rx"), number(radius));
            w.writeAttribute(QStringLiteral("ry"), number(radius));
        }
        break;
    }
    case ShapeKind::Ellipse: {
        const QRectF r = shape.localBounds();
        w.writeStartElement(QStringLiteral("ellipse"));
        w.writeAttribute(QStringLiteral("cx"), number(r.center().x()));
        w.writeAttribute(QStringLiteral("cy"), number(r.center().y()));
        w.writeAttribute(QStringLiteral("rx"), number(r.width() / 2.0));
        w.writeAttribute(QStringLiteral("ry"), number(r.height() / 2.0));
        break;
    }
    case ShapeKind::Path:
        w.writeStartElement(QStringLiteral("path"));
        w.writeAttribute(QStringLiteral("d"), pathops::toSvg(shape.pathData()));
        break;
    case ShapeKind::Group:
        w.writeStartElement(QStringLiteral("g"));
        writeLabel(w, shape.name());
        writeJoinId(w, shape);
        if (shape.isBlend()) {
            // The steps are written too, so other programs show them; OpenDraw skips them on
            // reading and works them out again.
            const QString ns = QString::fromLatin1(kNamespace);
            w.writeAttribute(ns, QStringLiteral("blend"), QString::number(shape.blendSteps()));
            writeShape(w, *shape.children()[0], gradientCount);
            w.writeStartElement(QStringLiteral("g"));
            w.writeAttribute(ns, QStringLiteral("generated"), QStringLiteral("true"));
            for (const auto &step : shape.blendIntermediates())
                writeShape(w, *step, gradientCount);
            w.writeEndElement();
            writeShape(w, *shape.children()[1], gradientCount);
            w.writeEndElement();
            return;
        }
        for (const auto &child : shape.children())
            writeShape(w, *child, gradientCount);
        w.writeEndElement();
        return;
    case ShapeKind::Text: {
        const TextData &data = shape.textData();
        const TextFormat &f = data.format;
        const QPointF origin = shape.textOrigin();
        const QString ns = QString::fromLatin1(kNamespace);
        // Text on a curve: the curve goes first, for <textPath> to refer to.
        const bool onPath = data.path && !data.isParagraph();
        QString curveId;
        if (onPath) {
            curveId = QStringLiteral("textpath%1").arg(++gradientCount);
            w.writeStartElement(QStringLiteral("defs"));
            w.writeEmptyElement(QStringLiteral("path"));
            w.writeAttribute(QStringLiteral("id"), curveId);
            w.writeAttribute(QStringLiteral("d"), pathops::toSvg(data.path->curve));
            w.writeEndElement();
        }
        w.writeStartElement(QStringLiteral("text"));
        writeLabel(w, shape.name());
        writeJoinId(w, shape);
        w.writeAttribute(QStringLiteral("xml:space"), QStringLiteral("preserve"));
        w.writeAttribute(QStringLiteral("x"), number(origin.x()));
        w.writeAttribute(QStringLiteral("y"), number(origin.y()));
        w.writeAttribute(QStringLiteral("font-family"), f.family);
        w.writeAttribute(QStringLiteral("font-size"), number(f.size));
        if (f.bold)
            w.writeAttribute(QStringLiteral("font-weight"), QStringLiteral("bold"));
        if (f.italic)
            w.writeAttribute(QStringLiteral("font-style"), QStringLiteral("italic"));
        if (f.align == TextAlign::Center || f.align == TextAlign::Right)
            w.writeAttribute(QStringLiteral("text-anchor"),
                             f.align == TextAlign::Center ? QStringLiteral("middle") : QStringLiteral("end"));
        if (f.align == TextAlign::Justify)
            w.writeAttribute(ns, QStringLiteral("align"), QStringLiteral("justify"));
        w.writeAttribute(ns, QStringLiteral("line-spacing"), number(f.lineSpacing));
        if (f.spaceBefore != 0.0 || f.spaceAfter != 0.0)
            w.writeAttribute(ns, QStringLiteral("paragraph-spacing"),
                             QStringLiteral("%1 %2").arg(number(f.spaceBefore), number(f.spaceAfter)));
        if (f.firstIndent != 0.0 || f.leftIndent != 0.0 || f.rightIndent != 0.0)
            w.writeAttribute(ns, QStringLiteral("indents"),
                             QStringLiteral("%1 %2 %3").arg(number(f.firstIndent), number(f.leftIndent), number(f.rightIndent)));
        writeShapeTransform(w, shape);
        writeStyle(w, shape.style(), gradientId);
        writeEffects(w, shape.style(), effectIds, original);

        // Every laid-out line on its own baseline, so any viewer shows the same lines. Paragraph
        // text also records its frame and marks wrapped lines, to rebuild the paragraphs.
        if (data.isParagraph()) {
            w.writeAttribute(ns, QStringLiteral("frame"),
                             QStringLiteral("%1 %2 %3 %4").arg(number(origin.x()), number(origin.y()),
                                                               number(data.frame.width()), number(data.frame.height())));
        }
        if (onPath)
            w.writeAttribute(ns, QStringLiteral("text-path"), number(data.path->offset));
        const std::vector<CharFormat> formats = richtext::perCharacter(data);
        const bool autoFormatting = w.autoFormatting();
        // Characters [from, to), parts in their own format as nested spans.
        auto writeRuns = [&](int from, int to) {
            for (int i = from; i < to;) {
                int j = i + 1;
                while (j < to && formats[j] == formats[i])
                    ++j;
                const QString chars = data.text.mid(i, j - i);
                const CharFormat &c = formats[i];
                if (c.isEmpty()) {
                    w.writeCharacters(chars);
                } else {
                    w.writeStartElement(QStringLiteral("tspan"));
                    if (c.family)
                        w.writeAttribute(QStringLiteral("font-family"), *c.family);
                    if (c.size)
                        w.writeAttribute(QStringLiteral("font-size"), number(*c.size));
                    if (c.bold)
                        w.writeAttribute(QStringLiteral("font-weight"), *c.bold ? QStringLiteral("bold") : QStringLiteral("normal"));
                    if (c.italic)
                        w.writeAttribute(QStringLiteral("font-style"), *c.italic ? QStringLiteral("italic") : QStringLiteral("normal"));
                    if (c.color)
                        writePaint(w, QStringLiteral("fill"), *c.color, true);
                    w.writeCharacters(chars);
                    w.writeEndElement();
                }
                i = j;
            }
        };
        const textlayout::Layout layout = textlayout::layout(data, origin);
        if (onPath) {
            // Each line along the curve (SVG <textPath>), the ones after the first lowered.
            const double length = pathops::toPainterPath(data.path->curve).length();
            double anchor = data.path->offset;
            if (f.align == TextAlign::Center)
                anchor += length / 2.0;
            else if (f.align == TextAlign::Right)
                anchor += length;
            if (data.path->curve.size() == 1 && data.path->curve.front().closed && length > 0.0) {
                // SVG does not go round a closed path: start on it.
                anchor = std::fmod(anchor, length);
                if (anchor < 0.0)
                    anchor += length;
            }
            for (std::size_t i = 0; i < layout.lines.size(); ++i) {
                const textlayout::Line &line = layout.lines[i];
                w.writeStartElement(QStringLiteral("textPath"));
                w.writeAttribute(kXlinkNamespace, QStringLiteral("href"), QLatin1Char('#') + curveId);
                w.writeAttribute(QStringLiteral("startOffset"), number(anchor));
                w.setAutoFormatting(false);
                if (i > 0) {
                    w.writeStartElement(QStringLiteral("tspan"));
                    w.writeAttribute(QStringLiteral("dy"),
                                     number(line.baselineStart.y() - layout.lines.front().baselineStart.y()));
                }
                writeRuns(line.start, line.start + int(line.text.size()));
                if (i > 0)
                    w.writeEndElement();
                w.writeEndElement();
                w.setAutoFormatting(autoFormatting);
            }
            w.writeEndElement();
            return;
        }
        for (const textlayout::Line &line : layout.lines) {
            // Artistic text lines share the origin; paragraph lines are placed one by one (the
            // indents and the frame decide where each goes).
            double anchorX = origin.x();
            if (data.isParagraph()) {
                anchorX = line.baselineStart.x();
                if (f.align == TextAlign::Center)
                    anchorX += line.width / 2.0;
                else if (f.align == TextAlign::Right)
                    anchorX += line.width;
            }
            w.writeStartElement(QStringLiteral("tspan"));
            w.writeAttribute(QStringLiteral("x"), number(anchorX));
            w.writeAttribute(QStringLiteral("y"), number(line.baselineStart.y()));
            if (line.wordSpacing > 0.0)
                w.writeAttribute(QStringLiteral("word-spacing"), number(line.wordSpacing));
            if (line.softBreak)
                w.writeAttribute(ns, QStringLiteral("soft"), QStringLiteral("true"));
            // No indenting inside a line: with xml:space="preserve" it would become text.
            w.setAutoFormatting(false);
            // A wrapped line keeps its trailing spaces, so the paragraph can be put back together.
            const int end = line.softBreak ? line.next : line.start + int(line.text.size());
            writeRuns(line.start, end);
            w.writeEndElement();
            w.setAutoFormatting(autoFormatting);
        }
        w.writeEndElement();
        return;
    }
    }
    writeLabel(w, shape.name());
    writeJoinId(w, shape);
    writeShapeTransform(w, shape);
    writeStyle(w, shape.style(), gradientId);
    writeEffects(w, shape.style(), effectIds, original);
    writeArrows(w, shape.style(), markerIds);
    if (const auto &c = shape.connector())
        w.writeAttribute(QString::fromLatin1(kNamespace), QStringLiteral("connector"),
                         QStringLiteral("%1 %2 %3").arg(c->from).arg(c->to).arg(c->rightAngle ? 1 : 0));
    w.writeEndElement();
}

// ---------------------------------------------------------------------------------------------
// Reading helpers

// Converts an SVG length ("12", "12px", "10mm", ...) to document units.
std::optional<double> parseLength(QStringView text)
{
    text = text.trimmed();
    struct Unit {
        QStringView suffix;
        double factor;
    };
    static const Unit units[] = {
        {u"px", 1.0},
        {u"mm", kUnitsPerMm},
        {u"cm", kUnitsPerMm * 10.0},
        {u"in", 96.0},
        {u"pt", 96.0 / 72.0},
        {u"pc", 16.0},
    };
    double factor = 1.0;
    for (const Unit &unit : units) {
        if (text.endsWith(unit.suffix)) {
            text.chop(unit.suffix.size());
            factor = unit.factor;
            break;
        }
    }
    bool ok = false;
    const double value = text.toDouble(&ok);
    if (!ok)
        return std::nullopt;
    return value * factor;
}

std::vector<double> parseNumberList(QStringView text)
{
    static const QRegularExpression numberPattern(
        QStringLiteral(R"([-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?)"));
    std::vector<double> values;
    // globalMatch(QString) rather than globalMatchView: the latter needs Qt 6.5, and Linux
    // distributions still ship Qt 6.4.
    auto it = numberPattern.globalMatch(text.toString());
    while (it.hasNext())
        values.push_back(it.next().capturedView().toDouble());
    return values;
}

// SVG transform list, e.g. "translate(10 20) rotate(45)". The rightmost transform applies first.
QTransform parseTransform(QStringView text)
{
    static const QRegularExpression itemPattern(
        QStringLiteral(R"((matrix|translate|scale|rotate|skewX|skewY)\s*\(([^)]*)\))"));
    QTransform total;
    auto it = itemPattern.globalMatch(text.toString());
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        const QStringView kind = match.capturedView(1);
        const std::vector<double> a = parseNumberList(match.capturedView(2));
        auto arg = [&a](std::size_t i, double fallback) { return i < a.size() ? a[i] : fallback; };

        QTransform item;
        if (kind == u"matrix" && a.size() == 6) {
            item = QTransform(a[0], a[1], a[2], a[3], a[4], a[5]);
        } else if (kind == u"translate") {
            item.translate(arg(0, 0.0), arg(1, 0.0));
        } else if (kind == u"scale") {
            item.scale(arg(0, 1.0), arg(1, arg(0, 1.0)));
        } else if (kind == u"rotate") {
            const double cx = arg(1, 0.0);
            const double cy = arg(2, 0.0);
            item.translate(cx, cy);
            item.rotate(arg(0, 0.0));
            item.translate(-cx, -cy);
        } else if (kind == u"skewX") {
            item = QTransform(1, 0, std::tan(arg(0, 0.0) * std::numbers::pi / 180.0), 1, 0, 0);
        } else if (kind == u"skewY") {
            item = QTransform(1, std::tan(arg(0, 0.0) * std::numbers::pi / 180.0), 0, 1, 0, 0);
        }
        total = item * total;
    }
    return total;
}

std::optional<QColor> parseColor(QStringView text)
{
    text = text.trimmed();
    if (text.isEmpty())
        return std::nullopt;
    if (text == u"none" || text == u"transparent")
        return QColor(Qt::transparent);
    if (text.startsWith(u"rgb")) {
        const std::vector<double> c = parseNumberList(text);
        if (c.size() < 3)
            return std::nullopt;
        const bool percent = text.contains(u'%');
        auto channel = [percent](double v) { return std::clamp(int(std::lround(percent ? v * 2.55 : v)), 0, 255); };
        QColor color(channel(c[0]), channel(c[1]), channel(c[2]));
        if (c.size() >= 4)
            color.setAlphaF(std::clamp(c[3], 0.0, 1.0));
        return color;
    }
    const QColor color = QColor::fromString(text);
    if (!color.isValid())
        return std::nullopt;
    return color;
}

// Presentation properties that children inherit from their groups.
const QString kInheritedProperties[] = {
    QStringLiteral("fill"),        QStringLiteral("stroke"),      QStringLiteral("stroke-width"),
    QStringLiteral("fill-opacity"), QStringLiteral("stroke-opacity"), QStringLiteral("color"),
    QStringLiteral("font-family"), QStringLiteral("font-size"),   QStringLiteral("font-weight"),
    QStringLiteral("font-style"),  QStringLiteral("text-anchor"), QStringLiteral("xml:space"),
    QStringLiteral("stroke-dasharray"), QStringLiteral("stroke-linecap"), QStringLiteral("stroke-linejoin"),
    QStringLiteral("paint-order"),   QStringLiteral("marker-start"), QStringLiteral("marker-end"),
};

// Elements whose children are never drawn directly.
bool isNonRendering(QStringView name)
{
    static const QStringView names[] = {u"defs",   u"clipPath", u"mask",  u"marker",   u"pattern",
                                        u"symbol", u"linearGradient", u"radialGradient", u"filter",
                                        u"title",  u"desc",     u"metadata", u"style"};
    return std::find(std::begin(names), std::end(names), name) != std::end(names);
}

// Drawing elements OpenDraw does not import yet; reported so nothing disappears silently.
bool isUnsupportedGraphic(QStringView name)
{
    static const QStringView names[] = {u"use", u"foreignObject", u"switch", u"textPath"};
    return std::find(std::begin(names), std::end(names), name) != std::end(names);
}

// What an element inherits from its ancestors.
// What an open element collects its children into.
enum class Role { Other, Group, Layer, Page };

struct Context {
    QTransform transform;
    QHash<QString, QString> inherited;
    bool rendering = true;
    Role role = Role::Other;
    Layer *layer = nullptr; // for Role::Layer
    bool inPage = false;    // inside a page group
    bool inLayer = false;   // inside a layer (nested layers become groups)
    QString label;          // for Role::Group: the name the group was given
    int blendSteps = 0;     // for Role::Group: a blend's steps (opendraw:blend)
    std::optional<Envelope> envelope; // for Role::Group: the envelope of the object inside
    bool powerClip = false;           // for Role::Group: a PowerClip frame and its contents
    std::optional<PathData> clip;     // another program's clip-path on this element, on the page
    QString fileId;                   // opendraw:id, for connectors joined to the object
    std::optional<Dimension> dimension; // for a dimension's group
    Style dimensionStyle;
};

// What an element clipped by another program's clip-path becomes: a PowerClip frame with no
// fill or outline of its own, holding the element.
std::unique_ptr<Shape> clipFrame(const PathData &outline, std::vector<std::unique_ptr<Shape>> contents)
{
    Style invisible;
    invisible.fill = Qt::transparent;
    invisible.stroke = Qt::transparent;
    auto frame = std::make_unique<Shape>(outline, invisible);
    frame->setClippedContents(std::move(contents));
    return frame;
}

// The presentation properties of one element: style attribute, then attributes, then inherited values.
class Properties {
public:
    Properties(const QXmlStreamAttributes &attributes, const QHash<QString, QString> &inherited)
        : m_attributes(attributes)
        , m_inherited(inherited)
    {
        const QStringView style = m_attributes.value(QStringLiteral("style"));
        for (QStringView declaration : style.split(u';')) {
            const qsizetype colon = declaration.indexOf(u':');
            if (colon < 0)
                continue;
            m_style.insert(declaration.left(colon).trimmed().toString(),
                           declaration.mid(colon + 1).trimmed().toString());
        }
    }

    // Value set on this element only.
    QString own(const QString &name) const
    {
        const auto it = m_style.constFind(name);
        if (it != m_style.constEnd())
            return *it;
        return m_attributes.value(name).toString();
    }

    QString value(const QString &name) const
    {
        const QString v = own(name);
        if (!v.isEmpty() && v != u"inherit")
            return v;
        return m_inherited.value(name);
    }

    double length(const QString &name, double fallback = 0.0) const
    {
        return parseLength(value(name)).value_or(fallback);
    }

    // A gradient fill is not resolved here (it needs the shape's bounds); see fillReference().
    Style style(double strokeScale, ReadReport &report) const
    {
        // SVG defaults: black fill, no stroke, width 1.
        Style style;
        style.fill = fillReference().isEmpty() ? paint(QStringLiteral("fill"), QColor(Qt::black), report)
                                               : QColor(Qt::transparent);
        style.stroke = paint(QStringLiteral("stroke"), QColor(Qt::transparent), report);
        style.strokeWidth = length(QStringLiteral("stroke-width"), 1.0) * strokeScale;
        applyOpacity(style.fill, value(QStringLiteral("fill-opacity")));
        applyOpacity(style.stroke, value(QStringLiteral("stroke-opacity")));
        // Dashes are stored relative to the outline width.
        const QString dashText = value(QStringLiteral("stroke-dasharray")).trimmed();
        if (!dashText.isEmpty() && dashText != u"none" && style.strokeWidth > 0.0) {
            std::vector<double> dashes = parseNumberList(dashText);
            const bool valid = !dashes.empty() && std::all_of(dashes.begin(), dashes.end(), [](double d) { return d >= 0; })
                               && std::any_of(dashes.begin(), dashes.end(), [](double d) { return d > 0; });
            if (valid) {
                for (double &d : dashes)
                    d = d * strokeScale / style.strokeWidth;
                style.dashes = std::move(dashes);
            }
        }
        const QString cap = value(QStringLiteral("stroke-linecap")).trimmed();
        style.cap = cap == u"round" ? LineCap::Round : cap == u"square" ? LineCap::Square : LineCap::Butt;
        const QString join = value(QStringLiteral("stroke-linejoin")).trimmed();
        style.join = join == u"round" ? LineJoin::Round : join == u"bevel" ? LineJoin::Bevel : LineJoin::Miter;
        // paint-order lists what is painted first; what it leaves out follows in the normal order.
        QStringList order = value(QStringLiteral("paint-order")).simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (const QString &part : {QStringLiteral("fill"), QStringLiteral("stroke")}) {
            if (!order.contains(part))
                order << part;
        }
        style.strokeBehind = order.indexOf(QStringLiteral("stroke")) < order.indexOf(QStringLiteral("fill"));
        bool ok = false;
        const double opacity = own(QStringLiteral("opacity")).toDouble(&ok);
        if (ok)
            style.opacity = std::clamp(opacity, 0.0, 1.0);
        return style;
    }

    // Id of the paint server in fill="url(#id)", or empty.
    QString fillReference() const
    {
        static const QRegularExpression url(QStringLiteral(R"(^\s*url\(\s*['"]?#([^)'"\s]+))"));
        const QRegularExpressionMatch match = url.match(value(QStringLiteral("fill")));
        return match.hasMatch() ? match.captured(1) : QString();
    }

private:
    QColor paint(const QString &name, const QColor &fallback, ReadReport &report) const
    {
        const QString v = value(name).trimmed();
        if (v.isEmpty())
            return fallback;
        if (v.startsWith(u"url(")) {
            ++report.skippedElements[QCoreApplication::translate("SvgIO", "gradient or pattern outline (replaced with gray)")];
            return QColor(0x80, 0x80, 0x80);
        }
        if (v == u"currentColor")
            return parseColor(value(QStringLiteral("color"))).value_or(QColor(Qt::black));
        return parseColor(v).value_or(fallback);
    }

    static void applyOpacity(QColor &color, const QString &text)
    {
        bool ok = false;
        const double opacity = text.toDouble(&ok);
        if (ok)
            color.setAlphaF(color.alphaF() * std::clamp(opacity, 0.0, 1.0));
    }

    // Copies: the reader's attributes() returns a temporary.
    QXmlStreamAttributes m_attributes;
    QHash<QString, QString> m_inherited;
    QHash<QString, QString> m_style;
};

// Rectangles and ellipses stay editable as such; a rotation or skew becomes their shape transform.
std::unique_ptr<Shape> makeBoxShape(ShapeKind kind, const QRectF &rect, const QTransform &t, const Style &style,
                                    double cornerRadius = 0.0)
{
    if (t.type() <= QTransform::TxScale) {
        auto shape = std::make_unique<Shape>(kind, t.mapRect(rect), style);
        shape->setCornerRadius(cornerRadius * std::min(std::abs(t.m11()), std::abs(t.m22())));
        return shape;
    }
    auto shape = std::make_unique<Shape>(kind, rect, style);
    shape->setCornerRadius(cornerRadius);
    shape->setShapeTransform(t);
    return shape;
}

// How much the transform scales outline widths: not at all with vector-effect="non-scaling-stroke".
double strokeScaleOf(const QTransform &t, const Properties &props)
{
    if (props.value(QStringLiteral("vector-effect")).trimmed() == u"non-scaling-stroke")
        return 1.0;
    return std::sqrt(std::abs(t.determinant()));
}

std::unique_ptr<Shape> makePathShape(PathData path, const QTransform &t, const Style &style)
{
    pathops::transform(path, t);
    if (path.empty())
        return nullptr;
    return std::make_unique<Shape>(std::move(path), style);
}

std::optional<double> firstNumber(QStringView text)
{
    const std::vector<double> values = parseNumberList(text);
    if (values.empty())
        return std::nullopt;
    return values.front();
}

TextFormat textFormat(const Properties &props)
{
    TextFormat format;
    QString family = props.value(QStringLiteral("font-family")).section(QLatin1Char(','), 0, 0).trimmed();
    family.remove(QLatin1Char('"')).remove(QLatin1Char('\''));
    if (!family.isEmpty())
        format.family = family;
    format.size = props.length(QStringLiteral("font-size"), 16.0);
    const QString weight = props.value(QStringLiteral("font-weight"));
    format.bold = weight == u"bold" || weight == u"bolder" || weight.toInt() >= 600;
    const QString style = props.value(QStringLiteral("font-style"));
    format.italic = style == u"italic" || style == u"oblique";
    const QString anchor = props.value(QStringLiteral("text-anchor"));
    if (anchor == u"middle")
        format.align = TextAlign::Center;
    else if (anchor == u"end")
        format.align = TextAlign::Right;
    return format;
}

// ---------------------------------------------------------------------------------------------
// Gradients

// A <linearGradient> or <radialGradient> as written in the file; unset attributes may come
// from the gradient it references with xlink:href.
struct GradientDef {
    Gradient::Type type = Gradient::Type::Linear;
    QString href;
    QHash<QString, QString> attributes; // x1, y1, x2, y2, cx, cy, r, gradientUnits, gradientTransform
    std::vector<GradientStop> stops;
};
using GradientDefs = QHash<QString, GradientDef>;

// A <pattern> holding a single image, as produced by bitmap and texture fills.
struct PatternDef {
    QHash<QString, QString> attributes; // x, y, width, height, patternUnits, patternTransform
    QImage image;
};

struct PaintServers {
    GradientDefs gradients;
    QHash<QString, PatternDef> patterns;
    QHash<QString, QString> paths; // path data of <path> elements with an id (for <textPath>)
    QHash<QString, PathData> clips; // outlines of <clipPath> elements
};

// Uncompressed 32-bit BMPs often carry alpha in the fourth byte (libcdr writes bitmaps that
// way), which Qt's reader ignores. Returns a null image for anything else, and for 32-bit
// BMPs whose fourth byte is unused (all zero).
QImage bmpWithAlpha(const QByteArray &bytes)
{
    const auto u8 = [&](qsizetype i) { return quint32(quint8(bytes[i])); };
    const auto u16 = [&](qsizetype i) { return u8(i) | u8(i + 1) << 8; };
    const auto u32 = [&](qsizetype i) { return u16(i) | u16(i + 2) << 16; };
    if (bytes.size() < 54 || !bytes.startsWith("BM") || u32(14) < 40 || u16(28) != 32 || u32(30) != 0)
        return {};
    const qsizetype offset = u32(10);
    const int width = int(u32(18));
    const int rawHeight = int(u32(22));
    const int height = std::abs(rawHeight);
    if (width <= 0 || height <= 0 || width > 1 << 15 || height > 1 << 15
        || offset + qsizetype(width) * height * 4 > bytes.size())
        return {};
    QImage image(width, height, QImage::Format_ARGB32);
    bool hasAlpha = false;
    for (int y = 0; y < height; ++y) {
        const int row = rawHeight > 0 ? height - 1 - y : y; // positive height = bottom-up
        const auto *src = reinterpret_cast<const uchar *>(bytes.constData() + offset + qsizetype(row) * width * 4);
        auto *dst = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < width; ++x, src += 4) {
            dst[x] = qRgba(src[2], src[1], src[0], src[3]);
            hasAlpha |= src[3] != 0;
        }
    }
    return hasAlpha ? image : QImage();
}

// Decodes "data:image/png;base64,...." (any format Qt reads). Linked files are not supported.
QImage decodeDataUri(QStringView href)
{
    if (!href.startsWith(u"data:"))
        return {};
    const qsizetype comma = href.indexOf(u',');
    if (comma < 0)
        return {};
    const QStringView header = href.mid(5, comma - 5);
    const QByteArray payload = href.mid(comma + 1).toLatin1();
    const QByteArray bytes = header.contains(u";base64") ? QByteArray::fromBase64(payload)
                                                         : QByteArray::fromPercentEncoding(payload);
    if (QImage image = bmpWithAlpha(bytes); !image.isNull())
        return image;
    return QImage::fromData(bytes);
}

QString imageHref(const QXmlStreamAttributes &a)
{
    QString href = a.value(QStringLiteral("http://www.w3.org/1999/xlink"), QStringLiteral("href")).toString();
    if (href.isEmpty())
        href = a.value(QStringLiteral("href")).toString();
    return href.trimmed();
}

double parseOffset(QStringView text)
{
    text = text.trimmed();
    const bool percent = text.endsWith(u'%');
    if (percent)
        text.chop(1);
    return std::clamp(text.toDouble() / (percent ? 100.0 : 1.0), 0.0, 1.0);
}

// Gradients can be defined anywhere, also after the shapes using them, so they are
// collected in a separate pass over the file.
PaintServers collectPaintServers(const QByteArray &data)
{
    PaintServers servers;
    GradientDefs &defs = servers.gradients;
    QXmlStreamReader r(data);
    QString current;
    QString currentPattern;
    QString currentClip;
    while (!r.atEnd()) {
        r.readNext();
        if (r.isEndElement() && (r.name() == u"linearGradient" || r.name() == u"radialGradient")) {
            current.clear();
            continue;
        }
        if (r.isEndElement() && r.name() == u"clipPath") {
            currentClip.clear();
            continue;
        }
        if (r.isStartElement() && r.name() == u"clipPath") {
            // Clipping in the user space of the clipped element (the default) is read; clipping
            // relative to its bounding box is not.
            const bool userSpace = r.attributes().value(QStringLiteral("clipPathUnits")) != u"objectBoundingBox";
            currentClip = userSpace ? r.attributes().value(QStringLiteral("id")).toString() : QString();
            continue;
        }
        if (r.isStartElement() && !currentClip.isEmpty() && !servers.clips.contains(currentClip)) {
            // The first shape of a clip path is its outline.
            const QXmlStreamAttributes a = r.attributes();
            auto length = [&a](const char *name) { return parseLength(a.value(QString::fromLatin1(name))).value_or(0.0); };
            QPainterPath outline;
            if (r.name() == u"path") {
                if (auto path = pathops::fromSvg(a.value(QStringLiteral("d"))))
                    outline = pathops::toPainterPath(*path);
            } else if (r.name() == u"rect") {
                outline.addRect(QRectF(length("x"), length("y"), length("width"), length("height")));
            } else if (r.name() == u"circle") {
                const double radius = length("r");
                outline.addEllipse(QPointF(length("cx"), length("cy")), radius, radius);
            } else if (r.name() == u"ellipse") {
                outline.addEllipse(QPointF(length("cx"), length("cy")), length("rx"), length("ry"));
            }
            if (!outline.isEmpty()) {
                const QStringView transform = a.value(QStringLiteral("transform"));
                if (!transform.isEmpty())
                    outline = parseTransform(transform).map(outline);
                servers.clips.insert(currentClip, pathops::fromPainterPath(outline));
            }
        }
        if (r.isEndElement() && r.name() == u"pattern") {
            currentPattern.clear();
            continue;
        }
        if (!r.isStartElement())
            continue;
        const QXmlStreamAttributes a = r.attributes();
        if (r.name() == u"path" && a.hasAttribute(QStringLiteral("id"))) {
            servers.paths.insert(a.value(QStringLiteral("id")).toString(), a.value(QStringLiteral("d")).toString());
            continue;
        }
        if (r.name() == u"pattern") {
            currentPattern = a.value(QStringLiteral("id")).toString();
            PatternDef def;
            for (const QString &name : {QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("width"),
                                        QStringLiteral("height"), QStringLiteral("patternUnits"),
                                        QStringLiteral("patternTransform")}) {
                if (a.hasAttribute(name))
                    def.attributes.insert(name, a.value(name).toString());
            }
            if (!currentPattern.isEmpty())
                servers.patterns.insert(currentPattern, def);
            continue;
        }
        if (r.name() == u"image" && !currentPattern.isEmpty() && servers.patterns.contains(currentPattern)) {
            PatternDef &def = servers.patterns[currentPattern];
            if (def.image.isNull())
                def.image = decodeDataUri(imageHref(a));
            continue;
        }
        if (r.name() == u"linearGradient" || r.name() == u"radialGradient") {
            current = a.value(QStringLiteral("id")).toString();
            GradientDef def;
            def.type = r.name() == u"radialGradient" ? Gradient::Type::Radial : Gradient::Type::Linear;
            QString href = a.value(QStringLiteral("http://www.w3.org/1999/xlink"), QStringLiteral("href")).toString();
            if (href.isEmpty())
                href = a.value(QStringLiteral("href")).toString();
            def.href = href.startsWith(u'#') ? href.mid(1) : QString();
            for (const QString &name : {QStringLiteral("x1"), QStringLiteral("y1"), QStringLiteral("x2"),
                                        QStringLiteral("y2"), QStringLiteral("cx"), QStringLiteral("cy"),
                                        QStringLiteral("r"), QStringLiteral("gradientUnits"),
                                        QStringLiteral("gradientTransform")}) {
                if (a.hasAttribute(name))
                    def.attributes.insert(name, a.value(name).toString());
            }
            if (!current.isEmpty())
                defs.insert(current, def);
        } else if (r.name() == u"stop" && !current.isEmpty() && defs.contains(current)) {
            const Properties props(a, {});
            QColor color = parseColor(props.value(QStringLiteral("stop-color"))).value_or(QColor(Qt::black));
            bool ok = false;
            const double opacity = props.value(QStringLiteral("stop-opacity")).toDouble(&ok);
            if (ok)
                color.setAlphaF(color.alphaF() * std::clamp(opacity, 0.0, 1.0));
            defs[current].stops.push_back({parseOffset(a.value(QStringLiteral("offset"))), color});
        }
    }
    return servers;
}

// Follows xlink:href so inherited stops and attributes are filled in.
GradientDef resolveGradient(const GradientDefs &defs, const QString &id, int depth = 0)
{
    GradientDef def = defs.value(id);
    if (def.href.isEmpty() || depth > 8 || !defs.contains(def.href))
        return def;
    const GradientDef base = resolveGradient(defs, def.href, depth + 1);
    if (def.stops.empty())
        def.stops = base.stops;
    for (auto it = base.attributes.cbegin(); it != base.attributes.cend(); ++it) {
        if (!def.attributes.contains(it.key()))
            def.attributes.insert(it.key(), it.value());
    }
    return def;
}

// Converts a file gradient to bounding-box fractions of the shape it fills.
std::optional<Gradient> toGradient(const GradientDef &def, const QRectF &bounds, const QTransform &context)
{
    if (def.stops.empty())
        return std::nullopt;
    const bool userSpace = def.attributes.value(QStringLiteral("gradientUnits")) == u"userSpaceOnUse";
    const QTransform gradientTransform = parseTransform(def.attributes.value(QStringLiteral("gradientTransform")));

    // Percentages are fractions of the box; plain numbers are fractions (bounding-box units)
    // or user units (userSpaceOnUse).
    auto coordinate = [&def](const QString &name, double fallback) {
        const QString text = def.attributes.value(name).trimmed();
        if (text.isEmpty())
            return fallback;
        if (text.endsWith(u'%'))
            return QStringView(text).chopped(1).toDouble() / 100.0;
        return parseLength(text).value_or(fallback);
    };
    const double width = std::max(bounds.width(), 1e-9);
    const double height = std::max(bounds.height(), 1e-9);
    auto toFraction = [&](const QPointF &p) {
        if (!userSpace)
            return gradientTransform.map(p);
        const QPointF mapped = (gradientTransform * context).map(p);
        return QPointF((mapped.x() - bounds.left()) / width, (mapped.y() - bounds.top()) / height);
    };

    Gradient g;
    g.type = def.type;
    g.stops = def.stops;
    if (g.stops.size() == 1)
        g.stops.push_back({1.0, g.stops.front().color});
    if (def.type == Gradient::Type::Linear) {
        g.start = toFraction({coordinate(QStringLiteral("x1"), 0.0), coordinate(QStringLiteral("y1"), 0.0)});
        g.end = toFraction({coordinate(QStringLiteral("x2"), 1.0), coordinate(QStringLiteral("y2"), 0.0)});
    } else {
        g.center = toFraction({coordinate(QStringLiteral("cx"), 0.5), coordinate(QStringLiteral("cy"), 0.5)});
        double radius = coordinate(QStringLiteral("r"), 0.5);
        if (userSpace) // a circle in user units, approximated against the box's average side
            radius = radius * std::sqrt(std::abs((gradientTransform * context).determinant())) / ((width + height) / 2.0);
        g.radius = radius;
    }
    return g;
}

// Texture from a single-image pattern, positioned relative to the shape it fills.
std::optional<PatternFill> toPattern(const PatternDef &def, const QRectF &bounds, const QTransform &context)
{
    if (def.image.isNull())
        return std::nullopt;
    auto value = [&def](const QString &name, double fallback) {
        return parseLength(def.attributes.value(name)).value_or(fallback);
    };
    QRectF tile(value(QStringLiteral("x"), 0.0), value(QStringLiteral("y"), 0.0), value(QStringLiteral("width"), 0.0),
                value(QStringLiteral("height"), 0.0));
    if (def.attributes.value(QStringLiteral("patternUnits")) == u"objectBoundingBox") {
        tile = QRectF(bounds.left() + tile.x() * bounds.width(), bounds.top() + tile.y() * bounds.height(),
                      tile.width() * bounds.width(), tile.height() * bounds.height());
    } else {
        const QTransform t = parseTransform(def.attributes.value(QStringLiteral("patternTransform"))) * context;
        tile = t.mapRect(tile);
    }
    if (tile.width() <= 0.0 || tile.height() <= 0.0)
        return std::nullopt;
    return PatternFill{def.image, tile.size(), tile.topLeft() - bounds.topLeft()};
}

void applyFillPaint(Shape &shape, const Properties &props, const QTransform &context, const PaintServers &servers,
                    ReadReport &report)
{
    const QString id = props.fillReference();
    if (id.isEmpty())
        return;
    Style style = shape.style();
    if (servers.patterns.contains(id)) {
        if (const auto pattern = toPattern(servers.patterns.value(id), shape.bounds(), context)) {
            style.pattern = *pattern;
        } else {
            style.fill = QColor(0x80, 0x80, 0x80);
            ++report.skippedElements[QCoreApplication::translate("SvgIO", "pattern fill (replaced with gray)")];
        }
    } else if (const auto gradient = toGradient(resolveGradient(servers.gradients, id), shape.bounds(), context)) {
        style.gradient = *gradient;
        // fill-opacity applies to every stop.
        bool ok = false;
        const double opacity = props.value(QStringLiteral("fill-opacity")).toDouble(&ok);
        if (ok) {
            for (GradientStop &stop : style.gradient->stops)
                stop.color.setAlphaF(stop.color.alphaF() * std::clamp(opacity, 0.0, 1.0));
        }
    } else {
        style.fill = QColor(0x80, 0x80, 0x80);
        ++report.skippedElements[QCoreApplication::translate("SvgIO", "pattern fill (replaced with gray)")];
    }
    shape.setStyle(style);
}

// OpenDraw's shadow and transparency attributes. Filters and masks from other programs are not
// read (the object shows without them), which the report mentions.
void applyEffects(Shape &shape, const QXmlStreamAttributes &attributes, ReadReport &report)
{
    const QString ns = QString::fromLatin1(kNamespace);
    Style style = shape.style();
    const auto shadow = parseShadow(attributes.value(ns, QStringLiteral("shadow")));
    const auto transparency = parseTransparency(attributes.value(ns, QStringLiteral("transparency")));
    if (!shadow && attributes.hasAttribute(QStringLiteral("filter")))
        ++report.skippedElements[QCoreApplication::translate("SvgIO", "filter effect (shown without it)")];
    if (!transparency && attributes.hasAttribute(QStringLiteral("mask")))
        ++report.skippedElements[QCoreApplication::translate("SvgIO", "mask (shown without it)")];
    const auto contour = parseContour(attributes.value(ns, QStringLiteral("contour")));
    std::optional<Extrusion> extrusion;
    if (const QStringList e = attributes.value(ns, QStringLiteral("extrude")).toString().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        e.size() == 4) {
        extrusion = Extrusion{QPointF(e[0].toDouble(), e[1].toDouble()), std::clamp(e[2].toDouble(), 0.0, 1.0),
                              e[3] == u"shaded" ? QColor() : QColor::fromString(e[3])};
    }
    std::optional<MeshFill> mesh;
    if (const QStringList m = attributes.value(ns, QStringLiteral("mesh")).toString().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        m.size() > 2) {
        MeshFill read;
        read.columns = m[0].toInt();
        read.rows = m[1].toInt();
        for (int i = 2; i < m.size(); ++i)
            read.colors.push_back(QColor::fromString(m[i]));
        if (read.isValid())
            mesh = read;
    }
    if (mesh) {
        // The picture written for other programs gives way to the mesh itself.
        style.mesh = mesh;
        style.pattern.reset();
        shape.setStyle(style);
    }
    if (!shadow && !transparency && !contour && !extrusion)
        return;
    style.shadow = shadow;
    style.transparency = transparency;
    style.contour = contour;
    style.extrusion = extrusion;
    shape.setStyle(style);
}

// Arrowheads (OpenDraw's names, or a triangle for another program's marker) and the
// scale-with-object setting.
void applyOutlineExtras(Shape &shape, const Properties &props, const QXmlStreamAttributes &attributes,
                        ReadReport &report)
{
    const QString ns = QString::fromLatin1(kNamespace);
    if (const QStringList c = attributes.value(ns, QStringLiteral("connector")).toString().split(QLatin1Char(' '));
        c.size() == 3 && shape.kind() == ShapeKind::Path) {
        // Ids in the file, matched to the objects once the whole file is read.
        shape.setConnector(Connector{c[0].toULongLong(), c[1].toULongLong(), c[2] == u"1"});
    }
    Style style = shape.style();
    style.scaleStroke = attributes.value(ns, QStringLiteral("scale-stroke")) == u"true";
    auto arrow = [&](const QString &ownName, const QString &marker) {
        if (const auto named = parseArrow(attributes.value(ns, ownName)))
            return *named;
        const QString value = props.value(marker).trimmed();
        if (shape.kind() != ShapeKind::Path || value.isEmpty() || value == u"none")
            return Arrowhead::None;
        ++report.skippedElements[QCoreApplication::translate("SvgIO", "marker (drawn as a triangle arrowhead)")];
        return Arrowhead::Triangle;
    };
    style.startArrow = arrow(QStringLiteral("start-arrow"), QStringLiteral("marker-start"));
    style.endArrow = arrow(QStringLiteral("end-arrow"), QStringLiteral("marker-end"));
    shape.setStyle(style);
}

// Text with the formatting of each character, while a <text> element is being read.
struct RichString {
    QString text;
    std::vector<CharFormat> formats;

    void append(const QString &s, const CharFormat &f)
    {
        text += s;
        formats.insert(formats.end(), s.size(), f);
    }
    void append(const RichString &other)
    {
        text += other.text;
        formats.insert(formats.end(), other.formats.begin(), other.formats.end());
    }
    void appendChar(QChar c, const CharFormat &f)
    {
        text += c;
        formats.push_back(f);
    }
    bool isBlank() const { return text.trimmed().isEmpty(); }

    // Runs of whitespace become one space; none at the ends.
    RichString collapsed() const
    {
        RichString result;
        for (int i = 0; i < text.size(); ++i) {
            if (text[i].isSpace()) {
                if (!result.text.isEmpty() && !result.text.endsWith(QLatin1Char(' ')))
                    result.appendChar(QLatin1Char(' '), formats[i]);
            } else {
                result.appendChar(text[i], formats[i]);
            }
        }
        return result.trimmed();
    }
    RichString trimmed() const
    {
        int from = 0, to = int(text.size());
        while (from < to && text[from].isSpace())
            ++from;
        while (to > from && text[to - 1].isSpace())
            --to;
        return mid(from, to - from);
    }
    RichString mid(int from, int count) const
    {
        RichString result;
        result.text = text.mid(from, count);
        result.formats.assign(formats.begin() + from, formats.begin() + from + count);
        return result;
    }
    std::vector<RichString> split(QChar separator) const
    {
        std::vector<RichString> parts;
        int start = 0;
        for (int i = 0; i <= int(text.size()); ++i) {
            if (i == int(text.size()) || text[i] == separator) {
                parts.push_back(mid(start, i - start));
                start = i + 1;
            }
        }
        return parts;
    }
};

// The absolute formatting given by a <text> or <tspan> (font, size, weight, slant, colour).
struct SpanValues {
    QHash<QString, QString> values;

    // `attributes` override what the parent gave.
    SpanValues with(const QXmlStreamAttributes &attributes) const
    {
        SpanValues result = *this;
        const Properties own(attributes, {});
        for (const QString &key : {QStringLiteral("font-family"), QStringLiteral("font-size"), QStringLiteral("font-weight"),
                                   QStringLiteral("font-style"), QStringLiteral("fill")}) {
            const QString v = own.own(key);
            if (!v.isEmpty() && v != u"inherit")
                result.values.insert(key, v);
        }
        return result;
    }

    CharFormat format() const
    {
        const Properties props(QXmlStreamAttributes(), values);
        const TextFormat f = textFormat(props);
        CharFormat c;
        c.family = f.family;
        c.size = f.size;
        c.bold = f.bold;
        c.italic = f.italic;
        if (const auto color = parseColor(values.value(QStringLiteral("fill"))))
            c.color = *color;
        return c;
    }
};

// Reads a whole <text> element (the reader sits on its start tag) into a text object.
// Each <tspan> with its own y or dy starts a new line (unless OpenDraw marked the line before
// as wrapped); nested <tspan>s give parts of the text their own font, size and colour.
std::unique_ptr<Shape> readText(QXmlStreamReader &r, const Properties &props, const Context &context,
                                const QHash<QString, QString> &curves, ReadReport &report)
{
    const QXmlStreamAttributes attributes = r.attributes();
    std::optional<double> x = firstNumber(attributes.value(QStringLiteral("x")));
    std::optional<double> y = firstNumber(attributes.value(QStringLiteral("y")));
    const bool preserveSpace = props.value(QStringLiteral("xml:space")) == u"preserve";
    TextFormat format = textFormat(props);

    // Paragraph text written by OpenDraw (or marked up by the CorelDRAW import): its frame.
    const QString ns = QString::fromLatin1(kNamespace);
    const std::vector<double> frame = parseNumberList(attributes.value(ns, QStringLiteral("frame")));
    const bool paragraph = frame.size() == 4 && frame[2] > 0.0;

    SpanValues textValues;
    for (const QString &key : {QStringLiteral("font-family"), QStringLiteral("font-size"), QStringLiteral("font-weight"),
                               QStringLiteral("font-style"), QStringLiteral("fill")}) {
        const QString v = props.value(key);
        if (!v.isEmpty())
            textValues.values.insert(key, v);
    }
    std::vector<SpanValues> spans{textValues}; // one per open element inside <text>

    std::vector<RichString> lines;
    RichString current;
    std::vector<double> lineBaselines;
    bool previousSoft = false; // the previous line was wrapped: the next one continues it
    std::optional<TextPath> textPath; // from the first <textPath>
    std::optional<double> startOffset;
    while (!spans.empty() && !r.atEnd()) {
        r.readNext();
        if (r.isStartElement()) {
            const QXmlStreamAttributes a = r.attributes();
            spans.push_back(spans.back().with(a));
            if (r.name() == u"textPath" && !textPath) {
                QString href = a.value(kXlinkNamespace, QStringLiteral("href")).toString();
                if (href.isEmpty())
                    href = a.value(QStringLiteral("href")).toString();
                if (href.startsWith(QLatin1Char('#'))) {
                    if (auto curve = pathops::fromSvg(curves.value(href.mid(1))); curve && !curve->empty()) {
                        textPath = TextPath{*curve, 0.0};
                        const QString offset = a.value(QStringLiteral("startOffset")).toString().trimmed();
                        const double length = pathops::toPainterPath(*curve).length();
                        if (offset.endsWith(QLatin1Char('%')))
                            startOffset = offset.chopped(1).toDouble() / 100.0 * length;
                        else if (!offset.isEmpty())
                            startOffset = parseLength(offset);
                    }
                }
                if (!textPath)
                    ++report.skippedElements[QCoreApplication::translate("SvgIO", "text on a missing path (shown straight)")];
                continue;
            }
            if (r.name() != u"tspan")
                continue;
            const auto tx = firstNumber(a.value(QStringLiteral("x")));
            const auto ty = firstNumber(a.value(QStringLiteral("y")));
            if (!x && tx)
                x = tx;
            if (!y && ty)
                y = ty;
            const bool newLine = ty || a.hasAttribute(QStringLiteral("dy"));
            if (newLine && (!current.text.isEmpty() || !lines.empty()) && !previousSoft) {
                lines.push_back(current);
                current = {};
            }
            if (newLine)
                previousSoft = a.value(ns, QStringLiteral("soft")) == u"true";
            if (ty)
                lineBaselines.push_back(*ty);
        } else if (r.isEndElement()) {
            spans.pop_back();
        } else if (r.isCharacters()) {
            // Whitespace directly inside <text> is layout of the XML file, not content.
            if (spans.size() == 1 && r.isWhitespace())
                continue;
            current.append(r.text().toString(), spans.back().format());
        }
    }
    lines.push_back(current);

    if (!preserveSpace) {
        for (RichString &line : lines)
            line = line.collapsed();
    }
    while (lines.size() > 1 && lines.back().text.isEmpty())
        lines.pop_back();
    RichString content;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i > 0)
            content.appendChar(QLatin1Char('\n'), content.formats.empty() ? CharFormat() : content.formats.back());
        content.append(lines[i]);
    }
    if (content.isBlank())
        return nullptr;
    // The CorelDRAW import marks paragraph ends with U+E000 (a private-use character).
    if (content.text.contains(kParagraphMark)) {
        std::vector<RichString> paragraphs = content.split(kParagraphMark);
        for (RichString &p : paragraphs)
            p = p.trimmed();
        while (paragraphs.size() > 1 && paragraphs.back().text.isEmpty())
            paragraphs.pop_back();
        content = {};
        for (std::size_t i = 0; i < paragraphs.size(); ++i) {
            if (i > 0)
                content.appendChar(QLatin1Char('\n'), paragraphs[i - 1].formats.empty() ? CharFormat()
                                                                                          : paragraphs[i - 1].formats.back());
            content.append(paragraphs[i]);
        }
    }

    // The text's own format is that of its first letter (files often put the font on the spans
    // only); the rest of the text differs from it where it must.
    const CharFormat first = content.formats.empty() ? CharFormat() : content.formats.front();
    auto unset = [&textValues](const char *key) { return !textValues.values.contains(QString::fromLatin1(key)); };
    if (first.family && unset("font-family"))
        format.family = *first.family;
    if (first.size && unset("font-size"))
        format.size = *first.size;
    if (first.bold && unset("font-weight"))
        format.bold = *first.bold;
    if (first.italic && unset("font-style"))
        format.italic = *first.italic;
    bool ok = false;
    const double spacing = attributes.value(ns, QStringLiteral("line-spacing")).toDouble(&ok);
    if (ok && spacing > 0.0)
        format.lineSpacing = spacing;
    else if (lineBaselines.size() >= 2 && format.size > 0.0)
        format.lineSpacing = (lineBaselines[1] - lineBaselines[0]) / format.size;
    if (attributes.value(ns, QStringLiteral("align")) == u"justify")
        format.align = TextAlign::Justify;
    if (const std::vector<double> gaps = parseNumberList(attributes.value(ns, QStringLiteral("paragraph-spacing")));
        gaps.size() == 2) {
        format.spaceBefore = std::max(gaps[0], 0.0);
        format.spaceAfter = std::max(gaps[1], 0.0);
    }
    if (const std::vector<double> indents = parseNumberList(attributes.value(ns, QStringLiteral("indents")));
        indents.size() == 3) {
        format.firstIndent = indents[0];
        format.leftIndent = indents[1];
        format.rightIndent = indents[2];
    }

    const QTransform &t = context.transform;
    QPointF position(x.value_or(0.0), y.value_or(0.0));
    Style style = props.style(strokeScaleOf(t, props), report);
    if (props.value(QStringLiteral("fill")).isEmpty() && first.color)
        style.fill = *first.color; // coloured on the spans only
    TextData text{content.text, format};
    richtext::setPerCharacter(text, content.formats);
    richtext::normalize(text, style.fill);
    if (paragraph) {
        position = QPointF(frame[0], frame[1]);
        text.frame = QSizeF(frame[2], frame[3]);
    } else if (textPath) {
        // OpenDraw's own offset, or SVG's startOffset (where the anchor of the line sits).
        bool hasOwn = false;
        const double own = attributes.value(ns, QStringLiteral("text-path")).toDouble(&hasOwn);
        if (hasOwn) {
            textPath->offset = own;
        } else {
            const double length = pathops::toPainterPath(textPath->curve).length();
            double offset = startOffset.value_or(0.0);
            if (format.align == TextAlign::Center)
                offset -= length / 2.0;
            else if (format.align == TextAlign::Right)
                offset -= length;
            textPath->offset = offset;
        }
        text.path = textPath;
    }
    if (t.type() > QTransform::TxScale) {
        // Rotated or skewed: the text keeps its own size and position, turned by the transform.
        auto shape = std::make_unique<Shape>(std::move(text), position, style);
        shape->setShapeTransform(t);
        return shape;
    }
    const double scale = std::sqrt(std::abs(t.determinant()));
    text.format.size *= scale;
    text.format.firstIndent *= scale;
    text.format.leftIndent *= scale;
    text.format.rightIndent *= scale;
    if (text.path) {
        pathops::transform(text.path->curve, t);
        text.path->offset *= scale;
    }
    for (TextRun &run : text.runs) {
        if (run.format.size)
            *run.format.size *= scale;
    }
    if (paragraph) {
        const QRectF box = t.mapRect(QRectF(position, text.frame));
        text.frame = box.size();
        return std::make_unique<Shape>(std::move(text), box.topLeft(), style);
    }
    return std::make_unique<Shape>(std::move(text), t.map(position), style);
}

PathData pointsToPath(QStringView points, bool closed)
{
    const std::vector<double> v = parseNumberList(points);
    SubPath sp;
    for (std::size_t i = 0; i + 1 < v.size(); i += 2)
        sp.nodes.push_back(PathNode::corner({v[i], v[i + 1]}));
    sp.closed = closed;
    if (sp.nodes.size() < 2)
        return {};
    return {sp};
}

} // namespace

int ReadReport::skippedCount() const
{
    int total = 0;
    for (int n : skippedElements)
        total += n;
    return total;
}

namespace {
void writeRootStart(QXmlStreamWriter &w, const QSizeF &page)
{
    w.writeStartElement(QStringLiteral("svg"));
    w.writeDefaultNamespace(QStringLiteral("http://www.w3.org/2000/svg"));
    w.writeNamespace(QString::fromLatin1(kNamespace), QStringLiteral("opendraw"));
    w.writeNamespace(QString::fromLatin1(kInkscapeNamespace), QStringLiteral("inkscape"));
    w.writeNamespace(kXlinkNamespace, QStringLiteral("xlink"));
    w.writeAttribute(QStringLiteral("version"), QStringLiteral("1.1"));
    w.writeAttribute(QString::fromLatin1(kNamespace), QStringLiteral("format-version"),
                     QString::number(kFormatVersion));
    w.writeAttribute(QStringLiteral("width"), number(page.width()));
    w.writeAttribute(QStringLiteral("height"), number(page.height()));
    w.writeAttribute(QStringLiteral("viewBox"),
                     QStringLiteral("0 0 %1 %2").arg(number(page.width()), number(page.height())));
}
} // namespace

QByteArray writeShapes(const std::vector<const Shape *> &shapes, const QSizeF &pageSize)
{
    QByteArray data;
    QXmlStreamWriter w(&data);
    w.writeStartDocument();
    writeRootStart(w, pageSize);
    collectJoined(shapes);
    int gradientCount = 0;
    for (const Shape *shape : shapes)
        writeShape(w, *shape, gradientCount);
    w.writeEndElement();
    w.writeEndDocument();
    return data;
}

std::vector<std::unique_ptr<Shape>> readShapes(const QByteArray &data)
{
    QByteArray copy = data;
    QBuffer buffer(&copy);
    buffer.open(QIODevice::ReadOnly);
    Document document;
    std::vector<std::unique_ptr<Shape>> shapes;
    if (!read(&buffer, document))
        return shapes;
    auto pages = document.releasePages();
    for (auto &layer : pages.front()->layers) {
        for (auto &shape : layer->shapes)
            shapes.push_back(std::move(shape));
    }
    return shapes;
}

bool write(const Document &document, QIODevice *device)
{
    QXmlStreamWriter w(device);
    w.setAutoFormatting(true);
    w.writeStartDocument();

    writeRootStart(w, document.pageSize());
    w.writeNamespace(QString::fromLatin1(kSodipodiNamespace), QStringLiteral("sodipodi"));
    w.writeAttribute(QString::fromLatin1(kNamespace), QStringLiteral("page-name"), document.pages().front()->name);

    // Guidelines the way Inkscape stores them, so they show up there too: a point on the
    // guide with y measured upwards, and the guide's normal.
    if (!document.guides().empty()) {
        const QString sodipodi = QString::fromLatin1(kSodipodiNamespace);
        const double height = document.pageSize().height();
        w.writeStartElement(sodipodi, QStringLiteral("namedview"));
        for (const Guide &guide : document.guides()) {
            w.writeEmptyElement(sodipodi, QStringLiteral("guide"));
            const bool vertical = guide.orientation == Qt::Vertical;
            const QPointF point = vertical ? QPointF(guide.position, height) : QPointF(0, height - guide.position);
            w.writeAttribute(QStringLiteral("position"), number(point.x()) + QLatin1Char(',') + number(point.y()));
            w.writeAttribute(QStringLiteral("orientation"), vertical ? QStringLiteral("1,0") : QStringLiteral("0,1"));
        }
        w.writeEndElement();
    }

    // Layers use Inkscape's layer markup too, so other editors show them as layers.
    {
        std::vector<const Shape *> all;
        for (const auto &page : document.pages()) {
            for (const auto &layer : page->layers) {
                for (const auto &shape : layer->shapes)
                    all.push_back(shape.get());
            }
        }
        collectJoined(all);
    }
    int gradientCount = 0;
    auto writeLayer = [&w, &gradientCount](const Layer &layer) {
        w.writeStartElement(QStringLiteral("g"));
        w.writeAttribute(QString::fromLatin1(kNamespace), QStringLiteral("layer"), layer.name);
        w.writeAttribute(QString::fromLatin1(kInkscapeNamespace), QStringLiteral("groupmode"), QStringLiteral("layer"));
        w.writeAttribute(QString::fromLatin1(kInkscapeNamespace), QStringLiteral("label"), layer.name);
        if (!layer.visible)
            w.writeAttribute(QStringLiteral("display"), QStringLiteral("none"));
        if (layer.locked)
            w.writeAttribute(QString::fromLatin1(kNamespace), QStringLiteral("locked"), QStringLiteral("true"));
        for (const auto &shape : layer.shapes)
            writeShape(w, *shape, gradientCount);
        w.writeEndElement();
    };

    // The first page is the drawing every SVG viewer shows; further pages are hidden groups.
    const auto &pages = document.pages();
    for (const auto &layer : pages.front()->layers)
        writeLayer(*layer);
    for (std::size_t i = 1; i < pages.size(); ++i) {
        w.writeStartElement(QStringLiteral("g"));
        w.writeAttribute(QString::fromLatin1(kNamespace), QStringLiteral("page"), pages[i]->name);
        w.writeAttribute(QStringLiteral("display"), QStringLiteral("none"));
        for (const auto &layer : pages[i]->layers)
            writeLayer(*layer);
        w.writeEndElement();
    }

    w.writeEndElement();
    w.writeEndDocument();
    return !w.hasError();
}

bool read(QIODevice *device, Document &document, QString *errorMessage, ReadReport *report)
{
    const QByteArray data = device->readAll();
    const PaintServers paintServers = collectPaintServers(data);
    QXmlStreamReader r(data);
    QSizeF pageSize = Document::defaultPageSize();
    bool foundSvg = false;
    ReadReport localReport;

    // One entry per open element: every start element pushes, every end element pops.
    std::vector<Context> stack{Context{}};
    // Shapes being collected: loose top-level shapes, then one list per open group, layer or page.
    std::vector<ShapeList> containers(1);
    // pages[0] is the first page, whose layers sit at the top level of the file.
    std::vector<std::unique_ptr<Page>> pages;
    pages.push_back(std::make_unique<Page>());
    std::vector<Guide> guides;
    QHash<QString, Shape *> joinable; // objects connectors may join, by their id in the file
    double userTop = 0.0;    // root viewBox, for guide positions
    double userHeight = pageSize.height();

    while (!r.atEnd()) {
        r.readNext();
        if (r.isEndElement()) {
            if (stack.size() > 1) {
                switch (stack.back().role) {
                case Role::Group: {
                    ShapeList children = std::move(containers.back());
                    containers.pop_back();
                    const std::size_t before = containers.back().size();
                    if (const auto &dimension = stack.back().dimension) {
                        // A dimension, drawn again from its settings.
                        auto shape = std::make_unique<Shape>(PathData{}, stack.back().dimensionStyle);
                        shape->setDimension(dimension);
                        shape->setName(stack.back().label);
                        containers.back().push_back(std::move(shape));
                    } else if (stack.back().clip && !children.empty()) {
                        containers.back().push_back(clipFrame(*stack.back().clip, std::move(children)));
                    } else if (stack.back().powerClip && !children.empty()
                        && Shape::canHoldContents(children.front()->kind())) {
                        // The frame, then a group of what is inside it.
                        std::unique_ptr<Shape> frame = std::move(children.front());
                        if (children.size() > 1 && children[1]->kind() == ShapeKind::Group)
                            frame->setClippedContents(children[1]->takeChildren());
                        containers.back().push_back(std::move(frame));
                    } else if (const auto &envelope = stack.back().envelope; envelope && children.size() == 1
                                                                       && Shape::canHaveEnvelope(children.front()->kind())) {
                        // An envelope's group holds just the bent object.
                        children.front()->setEnvelope(envelope);
                        containers.back().push_back(std::move(children.front()));
                    } else if (!children.empty()) {
                        auto group = std::make_unique<Shape>(std::move(children));
                        group->setName(stack.back().label);
                        if (group->children().size() == 2)
                            group->setBlendSteps(stack.back().blendSteps);
                        containers.back().push_back(std::move(group));
                    }
                    if (!stack.back().fileId.isEmpty() && containers.back().size() > before)
                        joinable.insert(stack.back().fileId, containers.back().back().get());
                    break;
                }
                case Role::Layer:
                    stack.back().layer->shapes = std::move(containers.back());
                    containers.pop_back();
                    break;
                case Role::Page: {
                    // Shapes on the page but outside any layer get a layer of their own.
                    ShapeList loose = std::move(containers.back());
                    containers.pop_back();
                    if (!loose.empty()) {
                        auto layer = Document::makeLayer(QCoreApplication::translate("SvgIO", "Layer"));
                        layer->shapes = std::move(loose);
                        pages.back()->layers.push_back(std::move(layer));
                    }
                    break;
                }
                case Role::Other:
                    break;
                }
                stack.pop_back();
            }
            continue;
        }
        if (!r.isStartElement())
            continue;

        const QStringView name = r.name();
        const QXmlStreamAttributes attributes = r.attributes();
        const Context &parent = stack.back();
        const Properties props(attributes, parent.inherited);

        Context context = parent;
        context.role = Role::Other; // not inherited; set below for groups, layers and pages
        context.layer = nullptr;
        const QStringView transformText = attributes.value(QStringLiteral("transform"));
        if (!transformText.isEmpty())
            context.transform = parseTransform(transformText) * parent.transform;
        for (const QString &property : kInheritedProperties) {
            const QString v = props.own(property);
            if (!v.isEmpty() && v != u"inherit")
                context.inherited.insert(property, v);
        }
        if (isNonRendering(name))
            context.rendering = false;

        const QString ns = QString::fromLatin1(kNamespace);
        const bool hidden = props.own(QStringLiteral("display")) == u"none";
        const bool isPage = name == u"g" && !parent.inPage && !parent.inLayer && attributes.hasAttribute(ns, QStringLiteral("page"));
        const bool isLayer = name == u"g" && !parent.inLayer
                             && (attributes.hasAttribute(ns, QStringLiteral("layer"))
                                 || attributes.value(QString::fromLatin1(kInkscapeNamespace), QStringLiteral("groupmode"))
                                        == u"layer");
        // The object under an envelope, hidden from other programs (which show it bent).
        const bool original = attributes.value(ns, QStringLiteral("original")) == u"true";
        // Hidden pages and layers are still read; any other hidden element is skipped.
        if (hidden && !isPage && !isLayer && !original)
            context.rendering = false;
        context.envelope = name == u"g" ? parseEnvelope(attributes.value(ns, QStringLiteral("envelope"))) : std::nullopt;
        context.powerClip = name == u"g" && attributes.value(ns, QStringLiteral("powerclip")) == u"true";
        context.fileId = attributes.value(ns, QStringLiteral("id")).toString();
        context.dimension = name == u"g" ? parseDimension(attributes.value(ns, QStringLiteral("dimension"))) : std::nullopt;
        if (context.dimension)
            context.dimensionStyle = props.style(1.0, localReport);
        context.clip.reset();
        if (const QString clipRef = props.own(QStringLiteral("clip-path")).trimmed();
            clipRef.startsWith(u"url(") && !parent.powerClip && context.rendering) {
            static const QRegularExpression url(QStringLiteral(R"(^url\(\s*['"]?#([^)'"\s]+))"));
            const QRegularExpressionMatch match = url.match(clipRef);
            if (match.hasMatch() && paintServers.clips.contains(match.captured(1))) {
                PathData outline = paintServers.clips.value(match.captured(1));
                pathops::transform(outline, context.transform);
                context.clip = std::move(outline);
            } else {
                ++localReport.skippedElements[QCoreApplication::translate("SvgIO", "clip path (shown without it)")];
            }
        }
        // Steps of a blend, worked out again from its ends.
        if (attributes.value(ns, QStringLiteral("generated")) == u"true")
            context.rendering = false;
        context.blendSteps = name == u"g" ? attributes.value(ns, QStringLiteral("blend")).toInt() : 0;

        if (isPage && context.rendering) {
            auto page = std::make_unique<Page>();
            page->name = attributes.value(ns, QStringLiteral("page")).toString();
            pages.push_back(std::move(page));
            containers.emplace_back();
            context.role = Role::Page;
            context.inPage = true;
        } else if (isLayer && context.rendering) {
            QString layerName = attributes.value(ns, QStringLiteral("layer")).toString();
            if (layerName.isEmpty())
                layerName = attributes.value(QString::fromLatin1(kInkscapeNamespace), QStringLiteral("label")).toString();
            auto layer = Document::makeLayer(layerName.isEmpty() ? QCoreApplication::translate("SvgIO", "Layer") : layerName);
            layer->visible = !hidden;
            layer->locked = attributes.value(ns, QStringLiteral("locked")) == u"true"
                            || attributes.value(QString::fromLatin1(kSodipodiNamespace), QStringLiteral("insensitive"))
                                   == u"true";
            Page *target = parent.inPage ? pages.back().get() : pages.front().get();
            context.layer = layer.get();
            target->layers.push_back(std::move(layer));
            containers.emplace_back();
            context.role = Role::Layer;
            context.inLayer = true;
        } else if (name == u"svg" && !foundSvg) {
            foundSvg = true;
            localReport.formatVersion = attributes.value(ns, QStringLiteral("format-version")).toInt();
            pages.front()->name = attributes.value(ns, QStringLiteral("page-name")).toString();
            const std::vector<double> viewBox = parseNumberList(attributes.value(QStringLiteral("viewBox")));
            if (viewBox.size() == 4 && viewBox[2] > 0 && viewBox[3] > 0) {
                // width/height give the real size; the viewBox maps drawing units onto it
                // (e.g. width="8.5in" with a viewBox in points).
                const auto width = parseLength(attributes.value(QStringLiteral("width")));
                const auto height = parseLength(attributes.value(QStringLiteral("height")));
                const double sx = width && *width > 0 ? *width / viewBox[2] : 1.0;
                const double sy = height && *height > 0 ? *height / viewBox[3] : sx;
                pageSize = {viewBox[2] * sx, viewBox[3] * sy};
                context.transform = context.transform * QTransform::fromTranslate(-viewBox[0], -viewBox[1])
                                    * QTransform::fromScale(sx, sy);
                userTop = viewBox[1];
                userHeight = viewBox[3];
            } else {
                pageSize = {props.length(QStringLiteral("width"), pageSize.width()),
                            props.length(QStringLiteral("height"), pageSize.height())};
                userHeight = pageSize.height();
            }
        } else if (name == u"guide" && r.namespaceUri() == QString::fromLatin1(kSodipodiNamespace)) {
            // Inkscape's guidelines: a point on the guide, measured with y going up, and the
            // direction of the guide's normal.
            const std::vector<double> position = parseNumberList(attributes.value(QStringLiteral("position")));
            const std::vector<double> normal = parseNumberList(attributes.value(QStringLiteral("orientation")));
            if (position.size() == 2 && normal.size() == 2 && (normal[0] != 0.0 || normal[1] != 0.0)
                && (normal[0] == 0.0 || normal[1] == 0.0)) {
                const QPointF point = parent.transform.map(QPointF(position[0], userTop + userHeight - position[1]));
                if (normal[0] != 0.0)
                    guides.push_back({Qt::Vertical, point.x()});
                else
                    guides.push_back({Qt::Horizontal, point.y()});
            }
        } else if (name == u"text" && context.rendering) {
            if (auto text = readText(r, props, context, paintServers.paths, localReport)) {
                applyFillPaint(*text, props, context.transform, paintServers, localReport);
                text->setName(attributes.value(QString::fromLatin1(kInkscapeNamespace), QStringLiteral("label")).toString());
                applyEffects(*text, attributes, localReport);
                applyOutlineExtras(*text, props, attributes, localReport);
                if (context.clip) {
                    std::vector<std::unique_ptr<Shape>> clipped;
                    clipped.push_back(std::move(text));
                    containers.back().push_back(clipFrame(*context.clip, std::move(clipped)));
                } else {
                    containers.back().push_back(std::move(text));
                }
                if (!context.fileId.isEmpty())
                    joinable.insert(context.fileId, containers.back().back().get());
            }
            continue; // readText consumed the whole element, end tag included
        } else if (context.rendering) {
            const QTransform &t = context.transform;
            const double strokeScale = strokeScaleOf(t, props);
            std::unique_ptr<Shape> shape;

            if (name == u"rect") {
                const QRectF rect(props.length(QStringLiteral("x")), props.length(QStringLiteral("y")),
                                  props.length(QStringLiteral("width")), props.length(QStringLiteral("height")));
                double rx = props.length(QStringLiteral("rx"), -1.0);
                double ry = props.length(QStringLiteral("ry"), -1.0);
                if (rx < 0.0)
                    rx = ry;
                if (ry < 0.0)
                    ry = rx;
                if (rect.width() > 0.0 && rect.height() > 0.0) {
                    const Style style = props.style(strokeScale, localReport);
                    // Equal radii stay an editable rounded rectangle (unless stretched unevenly).
                    const bool even = t.type() > QTransform::TxScale
                                      || std::abs(std::abs(t.m11()) - std::abs(t.m22())) < 1e-9;
                    if (rx > 0.0 && std::abs(rx - ry) < 1e-9 && even) {
                        shape = makeBoxShape(ShapeKind::Rectangle, rect, t, style, rx);
                    } else if (rx > 0.0) {
                        QPainterPath rounded;
                        rounded.addRoundedRect(rect, std::min(rx, rect.width() / 2), std::min(ry, rect.height() / 2));
                        shape = makePathShape(pathops::fromPainterPath(rounded), t, style);
                    } else {
                        shape = makeBoxShape(ShapeKind::Rectangle, rect, t, style);
                    }
                }
            } else if (name == u"ellipse" || name == u"circle") {
                const double cx = props.length(QStringLiteral("cx"));
                const double cy = props.length(QStringLiteral("cy"));
                const double rx = name == u"circle" ? props.length(QStringLiteral("r"))
                                                    : props.length(QStringLiteral("rx"));
                const double ry = name == u"circle" ? rx : props.length(QStringLiteral("ry"));
                if (rx > 0.0 && ry > 0.0)
                    shape = makeBoxShape(ShapeKind::Ellipse, QRectF(cx - rx, cy - ry, 2 * rx, 2 * ry), t,
                                         props.style(strokeScale, localReport));
            } else if (name == u"path") {
                if (auto path = pathops::fromSvg(attributes.value(QStringLiteral("d")))) {
                    if (original && t.type() > QTransform::TxScale) {
                        // A turned curve under an envelope keeps its own geometry.
                        shape = std::make_unique<Shape>(std::move(*path), props.style(strokeScale, localReport));
                        shape->setShapeTransform(t);
                    } else {
                        shape = makePathShape(std::move(*path), t, props.style(strokeScale, localReport));
                    }
                }
            } else if (name == u"line") {
                SubPath line;
                line.nodes = {PathNode::corner({props.length(QStringLiteral("x1")), props.length(QStringLiteral("y1"))}),
                              PathNode::corner({props.length(QStringLiteral("x2")), props.length(QStringLiteral("y2"))})};
                Style style = props.style(strokeScale, localReport);
                style.fill = Qt::transparent; // lines are never filled
                shape = makePathShape({line}, t, style);
            } else if (name == u"polyline" || name == u"polygon") {
                shape = makePathShape(pointsToPath(attributes.value(QStringLiteral("points")), name == u"polygon"), t,
                                      props.style(strokeScale, localReport));
            } else if (name == u"image") {
                QImage image = decodeDataUri(imageHref(attributes));
                const QRectF rect(props.length(QStringLiteral("x")), props.length(QStringLiteral("y")),
                                  props.length(QStringLiteral("width")), props.length(QStringLiteral("height")));
                if (image.isNull()) {
                    ++localReport.skippedElements[QCoreApplication::translate("SvgIO", "<image> linked to a separate file")];
                } else if (rect.width() > 0 && rect.height() > 0) {
                    // Stretched to its box; preserveAspectRatio fitting is not applied.
                    const QTransform placement =
                        QTransform(rect.width(), 0, 0, rect.height(), rect.x(), rect.y()) * t;
                    shape = std::make_unique<Shape>(std::move(image), placement);
                    const double opacity = props.own(QStringLiteral("opacity")).toDouble();
                    if (props.own(QStringLiteral("opacity")).size()) {
                        Style style = shape->style();
                        style.opacity = std::clamp(opacity, 0.0, 1.0);
                        shape->setStyle(style);
                    }
                }
            } else if (isUnsupportedGraphic(name)) {
                ++localReport.skippedElements[QStringLiteral("<%1>").arg(name)];
            }

            if (shape) {
                applyFillPaint(*shape, props, t, paintServers, localReport);
                shape->setName(attributes.value(QString::fromLatin1(kInkscapeNamespace), QStringLiteral("label")).toString());
                applyEffects(*shape, attributes, localReport);
                applyOutlineExtras(*shape, props, attributes, localReport);
                if (context.clip) {
                    std::vector<std::unique_ptr<Shape>> clipped;
                    clipped.push_back(std::move(shape));
                    containers.back().push_back(clipFrame(*context.clip, std::move(clipped)));
                } else {
                    containers.back().push_back(std::move(shape));
                }
                if (!context.fileId.isEmpty())
                    joinable.insert(context.fileId, containers.back().back().get());
            }

            if (name == u"g") {
                context.role = Role::Group;
                context.label = attributes.value(QString::fromLatin1(kInkscapeNamespace), QStringLiteral("label")).toString();
                containers.emplace_back();
            }
        }

        stack.push_back(std::move(context));
    }

    if (r.hasError()) {
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("SvgIO", "Line %1: %2").arg(r.lineNumber()).arg(r.errorString());
        return false;
    }
    if (!foundSvg) {
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("SvgIO", "The file is not an SVG document.");
        return false;
    }

    // Shapes outside any layer (a plain SVG) form the bottom layer of the first page.
    if (!containers.front().empty()) {
        auto layer = Document::makeLayer(QCoreApplication::translate("Document", "Layer 1"));
        layer->shapes = std::move(containers.front());
        pages.front()->layers.insert(pages.front()->layers.begin(), std::move(layer));
    }
    if (pages.front()->layers.empty() && pages.size() > 1)
        pages.erase(pages.begin());
    for (std::size_t i = 0; i < pages.size(); ++i) {
        if (pages[i]->name.isEmpty())
            pages[i]->name = QCoreApplication::translate("Document", "Page %1").arg(i + 1);
    }

    // Connectors refer to objects by their ids in the file; join them to the objects just read
    // (a connector whose objects are not in the file becomes a plain line).
    for (const auto &page : pages) {
        for (const auto &layer : page->layers) {
            for (const auto &shape : layer->shapes) {
                if (!shape->connector())
                    continue;
                Connector c = *shape->connector();
                const Shape *from = joinable.value(QString::number(c.from));
                const Shape *to = joinable.value(QString::number(c.to));
                if (from && to) {
                    c.from = from->id();
                    c.to = to->id();
                    shape->setConnector(c);
                } else {
                    shape->setConnector(std::nullopt);
                }
            }
        }
    }

    document.setPages(std::move(pages));
    document.setPageSize(pageSize);
    document.setGuides(std::move(guides));
    if (report)
        *report = localReport;
    return true;
}

} // namespace opendraw::svg
