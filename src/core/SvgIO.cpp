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
    QStringLiteral("fill"),           QStringLiteral("stroke"),         QStringLiteral("stroke-width"),
    QStringLiteral("fill-opacity"),   QStringLiteral("stroke-opacity"), QStringLiteral("color"),
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
    static const QStringView names[] = {u"text", u"image", u"use", u"foreignObject", u"switch"};
    return std::find(std::begin(names), std::end(names), name) != std::end(names);
}

// What an element inherits from its ancestors.
struct Context {
    QTransform transform;
    QHash<QString, QString> inherited;
    bool rendering = true;
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
    w.writeAttribute(QStringLiteral("version"), QStringLiteral("1.1"));
    w.writeAttribute(QString::fromLatin1(kNamespace), QStringLiteral("format-version"),
                     QString::number(kFormatVersion));
    w.writeAttribute(QStringLiteral("width"), number(page.width()));
    w.writeAttribute(QStringLiteral("height"), number(page.height()));
    w.writeAttribute(QStringLiteral("viewBox"),
                     QStringLiteral("0 0 %1 %2").arg(number(page.width()), number(page.height())));

    for (const auto &shape : document.shapes()) {
        switch (shape->kind()) {
        case ShapeKind::Rectangle: {
            const QRectF r = shape->bounds();
            w.writeStartElement(QStringLiteral("rect"));
            w.writeAttribute(QStringLiteral("x"), number(r.x()));
            w.writeAttribute(QStringLiteral("y"), number(r.y()));
            w.writeAttribute(QStringLiteral("width"), number(r.width()));
            w.writeAttribute(QStringLiteral("height"), number(r.height()));
            break;
        }
        case ShapeKind::Ellipse: {
            const QRectF r = shape->bounds();
            w.writeStartElement(QStringLiteral("ellipse"));
            w.writeAttribute(QStringLiteral("cx"), number(r.center().x()));
            w.writeAttribute(QStringLiteral("cy"), number(r.center().y()));
            w.writeAttribute(QStringLiteral("rx"), number(r.width() / 2.0));
            w.writeAttribute(QStringLiteral("ry"), number(r.height() / 2.0));
            break;
        }
        case ShapeKind::Path:
            w.writeStartElement(QStringLiteral("path"));
            w.writeAttribute(QStringLiteral("d"), pathops::toSvg(shape->pathData()));
            break;
        }
        writeStyle(w, shape->style());
        w.writeEndElement();
    }

    w.writeEndElement();
    w.writeEndDocument();
    return !w.hasError();
}

bool read(QIODevice *device, Document &document, QString *errorMessage, ReadReport *report)
{
    QXmlStreamReader r(device);
    std::vector<std::unique_ptr<Shape>> shapes;
    QSizeF pageSize = Document::defaultPageSize();
    bool foundSvg = false;
    ReadReport localReport;

    // One entry per open element: every start element pushes, every end element pops.
    std::vector<Context> stack{Context{}};

    while (!r.atEnd()) {
        r.readNext();
        if (r.isEndElement()) {
            if (stack.size() > 1)
                stack.pop_back();
            continue;
        }
        if (!r.isStartElement())
            continue;

        const QStringView name = r.name();
        const QXmlStreamAttributes attributes = r.attributes();
        const Context &parent = stack.back();
        const Properties props(attributes, parent.inherited);

        Context context = parent;
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

        if (name == u"svg" && !foundSvg) {
            foundSvg = true;
            localReport.formatVersion =
                attributes.value(QString::fromLatin1(kNamespace), QStringLiteral("format-version")).toInt();
            const std::vector<double> viewBox = parseNumberList(attributes.value(QStringLiteral("viewBox")));
            if (viewBox.size() == 4 && viewBox[2] > 0 && viewBox[3] > 0) {
                pageSize = {viewBox[2], viewBox[3]};
                context.transform = context.transform * QTransform::fromTranslate(-viewBox[0], -viewBox[1]);
            } else {
                pageSize = {props.length(QStringLiteral("width"), pageSize.width()),
                            props.length(QStringLiteral("height"), pageSize.height())};
            }
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
                shapes.push_back(std::move(shape));
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

    document.clear();
    document.setPageSize(pageSize);
    for (auto &shape : shapes)
        document.insertShape(-1, std::move(shape));
    if (report)
        *report = localReport;
    return true;
}

} // namespace opendraw::svg
