#include "core/SvgIO.h"

#include "core/Document.h"
#include "core/PathOps.h"

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

QString tr(const char *text)
{
    return QCoreApplication::translate("SvgIO", text);
}

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

void writeStyle(QXmlStreamWriter &w, const Style &style)
{
    writePaint(w, QStringLiteral("fill"), style.fill, style.hasFill());
    writePaint(w, QStringLiteral("stroke"), style.stroke, style.hasStroke());
    if (style.hasStroke())
        w.writeAttribute(QStringLiteral("stroke-width"), number(style.strokeWidth));
}

void writeShape(QXmlStreamWriter &w, const Shape &shape)
{
    switch (shape.kind()) {
    case ShapeKind::Rectangle: {
        const QRectF r = shape.bounds();
        w.writeStartElement(QStringLiteral("rect"));
        w.writeAttribute(QStringLiteral("x"), number(r.x()));
        w.writeAttribute(QStringLiteral("y"), number(r.y()));
        w.writeAttribute(QStringLiteral("width"), number(r.width()));
        w.writeAttribute(QStringLiteral("height"), number(r.height()));
        break;
    }
    case ShapeKind::Ellipse: {
        const QRectF r = shape.bounds();
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
        for (const auto &child : shape.children())
            writeShape(w, *child);
        w.writeEndElement();
        return;
    case ShapeKind::Text: {
        const TextFormat &f = shape.textData().format;
        const QPointF origin = shape.textOrigin();
        w.writeStartElement(QStringLiteral("text"));
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
        writeStyle(w, shape.style());
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
    writeStyle(w, shape.style());
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
    static const QStringView names[] = {u"image", u"use", u"foreignObject", u"switch", u"textPath"};
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

    Style style(double strokeScale, ReadReport &report) const
    {
        // SVG defaults: black fill, no stroke, width 1.
        Style style;
        style.fill = paint(QStringLiteral("fill"), QColor(Qt::black), report);
        style.stroke = paint(QStringLiteral("stroke"), QColor(Qt::transparent), report);
        style.strokeWidth = length(QStringLiteral("stroke-width"), 1.0) * strokeScale;
        applyOpacity(style.fill, value(QStringLiteral("fill-opacity")));
        applyOpacity(style.stroke, value(QStringLiteral("stroke-opacity")));
        const QString opacity = own(QStringLiteral("opacity"));
        applyOpacity(style.fill, opacity);
        applyOpacity(style.stroke, opacity);
        return style;
    }

private:
    QColor paint(const QString &name, const QColor &fallback, ReadReport &report) const
    {
        const QString v = value(name).trimmed();
        if (v.isEmpty())
            return fallback;
        if (v.startsWith(u"url(")) {
            ++report.skippedElements[tr("gradient or pattern paint (replaced with gray)")];
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

// Rectangles and ellipses stay editable as such unless rotated or skewed, which needs a path.
std::unique_ptr<Shape> makeBoxShape(ShapeKind kind, const QRectF &rect, const QTransform &t, const Style &style)
{
    if (t.type() <= QTransform::TxScale)
        return std::make_unique<Shape>(kind, t.mapRect(rect), style);
    QPainterPath path;
    if (kind == ShapeKind::Ellipse)
        path.addEllipse(rect);
    else
        path.addRect(rect);
    return std::make_unique<Shape>(pathops::fromPainterPath(t.map(path)), style);
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
    const double scale = std::sqrt(std::abs(t.determinant()));
    if (t.type() > QTransform::TxScale)
        ++report.skippedElements[QCoreApplication::translate("SvgIO", "rotated or skewed text (shown upright)")];
    format.size *= scale;
    const QPointF origin = t.map(QPointF(x.value_or(0.0), y.value_or(0.0)));
    Style style = props.style(scale, report);
    return std::make_unique<Shape>(TextData{lines.join(QLatin1Char('\n')), format}, origin, style);
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

bool write(const Document &document, QIODevice *device)
{
    QXmlStreamWriter w(device);
    w.setAutoFormatting(true);
    w.writeStartDocument();

    const QSizeF page = document.pageSize();
    w.writeStartElement(QStringLiteral("svg"));
    w.writeDefaultNamespace(QStringLiteral("http://www.w3.org/2000/svg"));
    w.writeNamespace(QString::fromLatin1(kNamespace), QStringLiteral("opendraw"));
    w.writeNamespace(QString::fromLatin1(kInkscapeNamespace), QStringLiteral("inkscape"));
    w.writeAttribute(QStringLiteral("version"), QStringLiteral("1.1"));
    w.writeAttribute(QString::fromLatin1(kNamespace), QStringLiteral("format-version"),
                     QString::number(kFormatVersion));
    w.writeAttribute(QStringLiteral("width"), number(page.width()));
    w.writeAttribute(QStringLiteral("height"), number(page.height()));
    w.writeAttribute(QStringLiteral("viewBox"),
                     QStringLiteral("0 0 %1 %2").arg(number(page.width()), number(page.height())));
    w.writeAttribute(QString::fromLatin1(kNamespace), QStringLiteral("page-name"), document.pages().front()->name);

    // Layers use Inkscape's layer markup too, so other editors show them as layers.
    auto writeLayer = [&w](const Layer &layer) {
        w.writeStartElement(QStringLiteral("g"));
        w.writeAttribute(QString::fromLatin1(kNamespace), QStringLiteral("layer"), layer.name);
        w.writeAttribute(QString::fromLatin1(kInkscapeNamespace), QStringLiteral("groupmode"), QStringLiteral("layer"));
        w.writeAttribute(QString::fromLatin1(kInkscapeNamespace), QStringLiteral("label"), layer.name);
        if (!layer.visible)
            w.writeAttribute(QStringLiteral("display"), QStringLiteral("none"));
        if (layer.locked)
            w.writeAttribute(QString::fromLatin1(kNamespace), QStringLiteral("locked"), QStringLiteral("true"));
        for (const auto &shape : layer.shapes)
            writeShape(w, *shape);
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
    QXmlStreamReader r(device);
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

    while (!r.atEnd()) {
        r.readNext();
        if (r.isEndElement()) {
            if (stack.size() > 1) {
                switch (stack.back().role) {
                case Role::Group: {
                    ShapeList children = std::move(containers.back());
                    containers.pop_back();
                    if (!children.empty())
                        containers.back().push_back(std::make_unique<Shape>(std::move(children)));
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
                        auto layer = Document::makeLayer(tr("Layer"));
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
            auto layer = Document::makeLayer(layerName.isEmpty() ? tr("Layer") : layerName);
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
                pageSize = {viewBox[2], viewBox[3]};
                context.transform = context.transform * QTransform::fromTranslate(-viewBox[0], -viewBox[1]);
            } else {
                pageSize = {props.length(QStringLiteral("width"), pageSize.width()),
                            props.length(QStringLiteral("height"), pageSize.height())};
            }
        } else if (name == u"text" && context.rendering) {
            if (auto text = readText(r, props, context, localReport))
                containers.back().push_back(std::move(text));
            continue; // readText consumed the whole element, end tag included
        } else if (context.rendering) {
            const QTransform &t = context.transform;
            const double strokeScale = std::sqrt(std::abs(t.determinant()));
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
                    if (rx > 0.0) {
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
            } else if (isUnsupportedGraphic(name)) {
                ++localReport.skippedElements[QStringLiteral("<%1>").arg(name)];
            }

            if (shape)
                containers.back().push_back(std::move(shape));

            if (name == u"g") {
                context.role = Role::Group;
                containers.emplace_back();
            }
        }

        stack.push_back(std::move(context));
    }

    if (r.hasError()) {
        if (errorMessage)
            *errorMessage = tr("Line %1: %2").arg(r.lineNumber()).arg(r.errorString());
        return false;
    }
    if (!foundSvg) {
        if (errorMessage)
            *errorMessage = tr("The file is not an SVG document.");
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
    if (report)
        *report = localReport;
    return true;
}

} // namespace opendraw::svg
