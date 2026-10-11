#include "core/SvgIO.h"

#include "core/Document.h"
#include "core/PathOps.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QHash>
#include <QRegularExpression>
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
    if (style.opacity < 1.0)
        w.writeAttribute(QStringLiteral("opacity"), number(std::clamp(style.opacity, 0.0, 1.0)));
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

void writeShape(QXmlStreamWriter &w, const Shape &shape, int &gradientCount)
{
    // A gradient is written right before the element that uses it.
    QString gradientId;
    if (shape.kind() != ShapeKind::Group && shape.kind() != ShapeKind::Image) {
        if (shape.style().pattern) {
            gradientId = QStringLiteral("pattern%1").arg(++gradientCount);
            writePattern(w, *shape.style().pattern, shape.localBounds(), gradientId);
        } else if (shape.style().gradient) {
            gradientId = QStringLiteral("gradient%1").arg(++gradientCount);
            writeGradient(w, *shape.style().gradient, gradientId);
        }
    }

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
        for (const auto &child : shape.children())
            writeShape(w, *child, gradientCount);
        w.writeEndElement();
        return;
    case ShapeKind::Text: {
        const TextFormat &f = shape.textData().format;
        const QPointF origin = shape.textOrigin();
        w.writeStartElement(QStringLiteral("text"));
        writeLabel(w, shape.name());
        w.writeAttribute(QStringLiteral("xml:space"), QStringLiteral("preserve"));
        w.writeAttribute(QStringLiteral("x"), number(origin.x()));
        w.writeAttribute(QStringLiteral("y"), number(origin.y()));
        w.writeAttribute(QStringLiteral("font-family"), f.family);
        w.writeAttribute(QStringLiteral("font-size"), number(f.size));
        if (f.bold)
            w.writeAttribute(QStringLiteral("font-weight"), QStringLiteral("bold"));
        if (f.italic)
            w.writeAttribute(QStringLiteral("font-style"), QStringLiteral("italic"));
        if (f.align != TextAlign::Left)
            w.writeAttribute(QStringLiteral("text-anchor"),
                             f.align == TextAlign::Center ? QStringLiteral("middle") : QStringLiteral("end"));
        writeShapeTransform(w, shape);
        writeStyle(w, shape.style(), gradientId);
        // One tspan per line, each on its own baseline.
        const QStringList lines = shape.textData().text.split(QLatin1Char('\n'));
        for (int i = 0; i < lines.size(); ++i) {
            w.writeStartElement(QStringLiteral("tspan"));
            w.writeAttribute(QStringLiteral("x"), number(origin.x()));
            w.writeAttribute(QStringLiteral("y"), number(origin.y() + i * f.size * f.lineSpacing));
            w.writeCharacters(lines[i]);
            w.writeEndElement();
        }
        w.writeEndElement();
        return;
    }
    }
    writeLabel(w, shape.name());
    writeShapeTransform(w, shape);
    writeStyle(w, shape.style(), gradientId);
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
};

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

// Reads a whole <text> element (the reader sits on its start tag) into a text object.
// Each <tspan> with its own y or dy starts a new line.
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
    while (!r.atEnd()) {
        r.readNext();
        if (r.isEndElement() && (r.name() == u"linearGradient" || r.name() == u"radialGradient")) {
            current.clear();
            continue;
        }
        if (r.isEndElement() && r.name() == u"pattern") {
            currentPattern.clear();
            continue;
        }
        if (!r.isStartElement())
            continue;
        const QXmlStreamAttributes a = r.attributes();
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

std::unique_ptr<Shape> readText(QXmlStreamReader &r, const Properties &props, const Context &context,
                                ReadReport &report)
{
    const QXmlStreamAttributes attributes = r.attributes();
    std::optional<double> x = firstNumber(attributes.value(QStringLiteral("x")));
    std::optional<double> y = firstNumber(attributes.value(QStringLiteral("y")));
    const bool preserveSpace = props.value(QStringLiteral("xml:space")) == u"preserve";
    TextFormat format = textFormat(props);

    QStringList lines;
    QString current;
    std::vector<double> lineBaselines;
    int depth = 1;
    while (depth > 0 && !r.atEnd()) {
        r.readNext();
        if (r.isStartElement()) {
            ++depth;
            if (r.name() != u"tspan")
                continue;
            const QXmlStreamAttributes a = r.attributes();
            const auto tx = firstNumber(a.value(QStringLiteral("x")));
            const auto ty = firstNumber(a.value(QStringLiteral("y")));
            if (!x && tx)
                x = tx;
            if (!y && ty)
                y = ty;
            const bool newLine = ty || a.hasAttribute(QStringLiteral("dy"));
            if (newLine && (!current.isEmpty() || !lines.isEmpty())) {
                lines << current;
                current.clear();
            }
            if (ty)
                lineBaselines.push_back(*ty);
        } else if (r.isEndElement()) {
            --depth;
        } else if (r.isCharacters()) {
            // Whitespace directly inside <text> is layout of the XML file, not content.
            if (depth == 1 && r.isWhitespace())
                continue;
            current += r.text();
        }
    }
    lines << current;

    if (!preserveSpace) {
        static const QRegularExpression spaces(QStringLiteral("\\s+"));
        for (QString &line : lines)
            line = line.replace(spaces, QStringLiteral(" ")).trimmed();
    }
    while (lines.size() > 1 && lines.back().isEmpty())
        lines.removeLast();
    if (lines.join(QString()).trimmed().isEmpty())
        return nullptr;

    if (lineBaselines.size() >= 2 && format.size > 0.0)
        format.lineSpacing = (lineBaselines[1] - lineBaselines[0]) / format.size;

    const QTransform &t = context.transform;
    const QPointF position(x.value_or(0.0), y.value_or(0.0));
    const Style style = props.style(strokeScaleOf(t, props), report);
    TextData text{lines.join(QLatin1Char('\n')), format};
    if (t.type() > QTransform::TxScale) {
        // Rotated or skewed: the text keeps its own size and position, turned by the transform.
        auto shape = std::make_unique<Shape>(std::move(text), position, style);
        shape->setShapeTransform(t);
        return shape;
    }
    text.format.size *= std::sqrt(std::abs(t.determinant()));
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
                    if (!children.empty()) {
                        auto group = std::make_unique<Shape>(std::move(children));
                        group->setName(stack.back().label);
                        containers.back().push_back(std::move(group));
                    }
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
        // Hidden pages and layers are still read; any other hidden element is skipped.
        if (hidden && !isPage && !isLayer)
            context.rendering = false;

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
            if (auto text = readText(r, props, context, localReport)) {
                applyFillPaint(*text, props, context.transform, paintServers, localReport);
                text->setName(attributes.value(QString::fromLatin1(kInkscapeNamespace), QStringLiteral("label")).toString());
                containers.back().push_back(std::move(text));
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
                if (auto path = pathops::fromSvg(attributes.value(QStringLiteral("d"))))
                    shape = makePathShape(std::move(*path), t, props.style(strokeScale, localReport));
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
                containers.back().push_back(std::move(shape));
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

    document.setPages(std::move(pages));
    document.setPageSize(pageSize);
    document.setGuides(std::move(guides));
    if (report)
        *report = localReport;
    return true;
}

} // namespace opendraw::svg
