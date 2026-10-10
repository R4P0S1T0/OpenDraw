#include "core/SvgIO.h"

#include "core/Document.h"

#include <QCoreApplication>
#include <QHash>
#include <QRegularExpression>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <algorithm>
#include <optional>

namespace opendraw::svg {

namespace {

QString number(double value)
{
    return QString::number(value, 'g', 10);
}

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

std::optional<QColor> parsePaint(QStringView text)
{
    text = text.trimmed();
    if (text.isEmpty())
        return std::nullopt;
    if (text == u"none")
        return QColor(Qt::transparent);
    const QColor color = QColor::fromString(text);
    if (!color.isValid())
        return std::nullopt;
    return color;
}

// Presentation properties of one element: the style attribute wins over plain attributes.
class Properties {
public:
    explicit Properties(const QXmlStreamAttributes &attributes)
        : m_attributes(attributes)
    {
        const QStringView style = attributes.value(QStringLiteral("style"));
        for (QStringView declaration : style.split(u';')) {
            const qsizetype colon = declaration.indexOf(u':');
            if (colon < 0)
                continue;
            m_style.insert(declaration.left(colon).trimmed().toString(),
                           declaration.mid(colon + 1).trimmed().toString());
        }
    }

    QString value(const QString &name) const
    {
        const auto it = m_style.constFind(name);
        if (it != m_style.constEnd())
            return *it;
        return m_attributes.value(name).toString();
    }

    double length(const QString &name, double fallback = 0.0) const
    {
        return parseLength(value(name)).value_or(fallback);
    }

    Style style() const
    {
        // SVG defaults: black fill, no stroke, width 1.
        Style style;
        style.fill = parsePaint(value(QStringLiteral("fill"))).value_or(QColor(Qt::black));
        style.stroke = parsePaint(value(QStringLiteral("stroke"))).value_or(QColor(Qt::transparent));
        style.strokeWidth = length(QStringLiteral("stroke-width"), 1.0);
        applyOpacity(style.fill, QStringLiteral("fill-opacity"));
        applyOpacity(style.stroke, QStringLiteral("stroke-opacity"));
        return style;
    }

private:
    void applyOpacity(QColor &color, const QString &name) const
    {
        bool ok = false;
        const double opacity = value(name).toDouble(&ok);
        if (ok)
            color.setAlphaF(color.alphaF() * std::clamp(opacity, 0.0, 1.0));
    }

    const QXmlStreamAttributes &m_attributes;
    QHash<QString, QString> m_style;
};

QString tr(const char *text)
{
    return QCoreApplication::translate("SvgIO", text);
}

} // namespace

bool write(const Document &document, QIODevice *device)
{
    QXmlStreamWriter w(device);
    w.setAutoFormatting(true);
    w.writeStartDocument();

    const QSizeF page = document.pageSize();
    w.writeStartElement(QStringLiteral("svg"));
    w.writeDefaultNamespace(QStringLiteral("http://www.w3.org/2000/svg"));
    w.writeAttribute(QStringLiteral("version"), QStringLiteral("1.1"));
    w.writeAttribute(QStringLiteral("width"), number(page.width()));
    w.writeAttribute(QStringLiteral("height"), number(page.height()));
    w.writeAttribute(QStringLiteral("viewBox"),
                     QStringLiteral("0 0 %1 %2").arg(number(page.width()), number(page.height())));

    for (const auto &shape : document.shapes()) {
        const QRectF r = shape->bounds();
        switch (shape->kind()) {
        case ShapeKind::Rectangle:
            w.writeStartElement(QStringLiteral("rect"));
            w.writeAttribute(QStringLiteral("x"), number(r.x()));
            w.writeAttribute(QStringLiteral("y"), number(r.y()));
            w.writeAttribute(QStringLiteral("width"), number(r.width()));
            w.writeAttribute(QStringLiteral("height"), number(r.height()));
            break;
        case ShapeKind::Ellipse:
            w.writeStartElement(QStringLiteral("ellipse"));
            w.writeAttribute(QStringLiteral("cx"), number(r.center().x()));
            w.writeAttribute(QStringLiteral("cy"), number(r.center().y()));
            w.writeAttribute(QStringLiteral("rx"), number(r.width() / 2.0));
            w.writeAttribute(QStringLiteral("ry"), number(r.height() / 2.0));
            break;
        }
        writeStyle(w, shape->style());
        w.writeEndElement();
    }

    w.writeEndElement();
    w.writeEndDocument();
    return !w.hasError();
}

bool read(QIODevice *device, Document &document, QString *errorMessage)
{
    QXmlStreamReader r(device);
    std::vector<std::unique_ptr<Shape>> shapes;
    QSizeF pageSize = Document::defaultPageSize();
    bool foundSvg = false;

    while (!r.atEnd()) {
        r.readNext();
        if (!r.isStartElement())
            continue;

        const QStringView name = r.name();
        const Properties props(r.attributes());

        if (name == u"svg" && !foundSvg) {
            foundSvg = true;
            const QStringList viewBox = props.value(QStringLiteral("viewBox"))
                                            .split(QRegularExpression(QStringLiteral("[\\s,]+")),
                                                   Qt::SkipEmptyParts);
            if (viewBox.size() == 4) {
                pageSize = {viewBox[2].toDouble(), viewBox[3].toDouble()};
            } else {
                pageSize = {props.length(QStringLiteral("width"), pageSize.width()),
                            props.length(QStringLiteral("height"), pageSize.height())};
            }
        } else if (name == u"rect") {
            const QRectF bounds(props.length(QStringLiteral("x")), props.length(QStringLiteral("y")),
                                props.length(QStringLiteral("width")), props.length(QStringLiteral("height")));
            shapes.push_back(std::make_unique<Shape>(ShapeKind::Rectangle, bounds, props.style()));
        } else if (name == u"ellipse" || name == u"circle") {
            const double cx = props.length(QStringLiteral("cx"));
            const double cy = props.length(QStringLiteral("cy"));
            const double rx = name == u"circle" ? props.length(QStringLiteral("r"))
                                                : props.length(QStringLiteral("rx"));
            const double ry = name == u"circle" ? rx : props.length(QStringLiteral("ry"));
            shapes.push_back(std::make_unique<Shape>(
                ShapeKind::Ellipse, QRectF(cx - rx, cy - ry, 2.0 * rx, 2.0 * ry), props.style()));
        }
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
    return true;
}

} // namespace opendraw::svg
