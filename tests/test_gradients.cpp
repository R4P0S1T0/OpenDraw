#include "core/Document.h"
#include "core/Export.h"
#include "core/Render.h"
#include "core/SvgIO.h"

#include <QBuffer>
#include <QtTest>

using namespace opendraw;

namespace {

bool closeTo(const QColor &a, const QColor &b, int tolerance = 12)
{
    return std::abs(a.red() - b.red()) <= tolerance && std::abs(a.green() - b.green()) <= tolerance
           && std::abs(a.blue() - b.blue()) <= tolerance;
}

QImage renderOne(const Style &style, const QRectF &rect, ShapeKind kind = ShapeKind::Rectangle)
{
    Document doc;
    doc.setPageSize(rect.size() + QSizeF(rect.x() * 2, rect.y() * 2));
    doc.insertShape(-1, std::make_unique<Shape>(kind, rect, style));
    exporter::ImageOptions options;
    options.dpi = 96; // one pixel per unit
    return exporter::renderImage(doc, options);
}

// Files store about ten significant digits; compare at a precision that matters for drawing.
bool sameGradient(const Gradient &a, const Gradient &b)
{
    auto near = [](const QPointF &p, const QPointF &q) { return std::hypot(p.x() - q.x(), p.y() - q.y()) < 1e-6; };
    if (a.type != b.type || a.stops.size() != b.stops.size() || !near(a.start, b.start) || !near(a.end, b.end)
        || !near(a.center, b.center) || std::abs(a.radius - b.radius) > 1e-6)
        return false;
    for (std::size_t i = 0; i < a.stops.size(); ++i) {
        if (std::abs(a.stops[i].offset - b.stops[i].offset) > 1e-6 || a.stops[i].color.rgba() != b.stops[i].color.rgba())
            return false;
    }
    return true;
}

Style noOutline()
{
    Style style;
    style.stroke = Qt::transparent;
    return style;
}

std::unique_ptr<Document> readSvg(QByteArray data)
{
    QBuffer buffer(&data);
    buffer.open(QIODevice::ReadOnly);
    auto doc = std::make_unique<Document>();
    svg::read(&buffer, *doc);
    return doc;
}

} // namespace

class GradientTests : public QObject {
    Q_OBJECT

private slots:
    void linearGradientAngles();
    void linearGradientRendersAcrossTheShape();
    void radialGradientRendersFromTheCentre();
    void opacityBlendsTheWholeObject();
    void gradientsFollowTheShape();
    void svgRoundTrip();
    void importsUserSpaceAndInheritedStops();
    void importsPercentagesAndLateDefinitions();
    void missingGradientIsReported();
    void dropShadowRendersBehind();
    void transparencyGradientFades();
    void effectsSurviveSvg();
    void arrowheadsFollowTheLine();
    void outlineBehindFill();
    void outlineScalesWithObject();
    void outlineSettingsSurviveSvg();
};

void GradientTests::linearGradientAngles()
{
    const Gradient across = Gradient::linear(Qt::red, Qt::blue, 0);
    QCOMPARE(across.start, QPointF(0, 0.5));
    QCOMPARE(across.end, QPointF(1, 0.5));
    QVERIFY(qAbs(across.angle()) < 1e-9);
    QVERIFY(qAbs(Gradient::linear(Qt::red, Qt::blue, 90).angle() - 90) < 1e-9);
    QVERIFY(qAbs(Gradient::linear(Qt::red, Qt::blue, 225).angle() - 225) < 1e-9);
}

void GradientTests::linearGradientRendersAcrossTheShape()
{
    Style style = noOutline();
    style.gradient = Gradient::linear(QColor(255, 0, 0), QColor(0, 0, 255), 0);
    const QImage image = renderOne(style, QRectF(0, 0, 100, 10));
    QVERIFY(closeTo(image.pixelColor(1, 5), QColor(255, 0, 0)));
    QVERIFY(closeTo(image.pixelColor(98, 5), QColor(0, 0, 255)));
    QVERIFY(closeTo(image.pixelColor(50, 5), QColor(128, 0, 128), 20));
}

void GradientTests::radialGradientRendersFromTheCentre()
{
    Style style = noOutline();
    style.gradient = Gradient::radial(QColor(255, 255, 0), QColor(0, 128, 0));
    const QImage image = renderOne(style, QRectF(0, 0, 100, 100));
    QVERIFY(closeTo(image.pixelColor(50, 50), QColor(255, 255, 0)));
    QVERIFY(closeTo(image.pixelColor(1, 1), QColor(0, 128, 0)));
}

void GradientTests::opacityBlendsTheWholeObject()
{
    Style style = noOutline();
    style.fill = QColor(255, 0, 0);
    style.opacity = 0.5;
    const QImage image = renderOne(style, QRectF(10, 10, 50, 50));
    QVERIFY(closeTo(image.pixelColor(35, 35), QColor(255, 127, 127), 3));
}

void GradientTests::gradientsFollowTheShape()
{
    // Positions are box fractions, so moving or scaling the shape keeps the same look.
    Style style = noOutline();
    style.gradient = Gradient::linear(QColor(255, 0, 0), QColor(0, 0, 255), 0);
    Shape shape(ShapeKind::Rectangle, QRectF(0, 0, 10, 10), style);
    shape.setBounds(QRectF(500, 500, 300, 40));
    QCOMPARE(*shape.style().gradient, *style.gradient);
}

void GradientTests::svgRoundTrip()
{
    Document original;
    Style linear = noOutline();
    linear.gradient = Gradient::linear(QColor(255, 0, 0), QColor(0, 0, 255, 128), 45);
    linear.gradient->stops.insert(linear.gradient->stops.begin() + 1, {0.3, QColor(0, 255, 0)});
    linear.opacity = 0.75;
    original.insertShape(-1, std::make_unique<Shape>(ShapeKind::Rectangle, QRectF(0, 0, 50, 50), linear));
    Style radial = noOutline();
    radial.gradient = Gradient::radial(Qt::white, Qt::black);
    radial.gradient->center = QPointF(0.3, 0.4);
    radial.gradient->radius = 0.6;
    original.insertShape(-1, std::make_unique<Shape>(ShapeKind::Ellipse, QRectF(60, 0, 50, 30), radial));

    QBuffer buffer;
    QVERIFY(buffer.open(QIODevice::ReadWrite));
    QVERIFY(svg::write(original, &buffer));

    const auto loaded = readSvg(buffer.data());
    QCOMPARE(loaded->count(), 2);
    const Style &a = loaded->shapes()[0]->style();
    QVERIFY(a.gradient);
    QCOMPARE(a.gradient->stops.size(), std::size_t(3));
    QVERIFY(sameGradient(*a.gradient, *linear.gradient));
    QCOMPARE(a.opacity, 0.75);
    QVERIFY(sameGradient(*loaded->shapes()[1]->style().gradient, *radial.gradient));
}

void GradientTests::importsUserSpaceAndInheritedStops()
{
    const auto doc = readSvg(R"svg(<svg xmlns="http://www.w3.org/2000/svg"
            xmlns:xlink="http://www.w3.org/1999/xlink" viewBox="0 0 200 100">
        <defs>
            <linearGradient id="colours">
                <stop offset="0" stop-color="#ff0000"/>
                <stop offset="100%" style="stop-color:#0000ff;stop-opacity:0.5"/>
            </linearGradient>
            <linearGradient id="g" xlink:href="#colours" gradientUnits="userSpaceOnUse"
                            x1="50" y1="0" x2="150" y2="0"/>
        </defs>
        <rect x="50" y="0" width="100" height="100" fill="url(#g)"/>
    </svg>)svg");
    QCOMPARE(doc->count(), 1);
    const Style &style = doc->shapes()[0]->style();
    QVERIFY(style.gradient);
    QCOMPARE(style.gradient->start, QPointF(0, 0));
    QCOMPARE(style.gradient->end, QPointF(1, 0));
    QCOMPARE(style.gradient->stops.size(), std::size_t(2));
    QCOMPARE(style.gradient->stops[1].color.alpha(), 128);
}

void GradientTests::importsPercentagesAndLateDefinitions()
{
    const auto doc = readSvg(R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100">
        <rect width="10" height="10" fill="url(#diagonal)"/>
        <circle cx="50" cy="50" r="10" fill="url(#glow)" opacity="0.5"/>
        <linearGradient id="diagonal" x1="0%" y1="0%" x2="100%" y2="100%">
            <stop offset="0" stop-color="white"/><stop offset="1" stop-color="black"/>
        </linearGradient>
        <radialGradient id="glow"><stop offset="0" stop-color="yellow"/><stop offset="1" stop-color="red"/></radialGradient>
    </svg>)svg");
    QCOMPARE(doc->count(), 2);
    QCOMPARE(doc->shapes()[0]->style().gradient->end, QPointF(1, 1));
    QCOMPARE(doc->shapes()[1]->style().gradient->type, Gradient::Type::Radial);
    QCOMPARE(doc->shapes()[1]->style().opacity, 0.5);
}

void GradientTests::missingGradientIsReported()
{
    QByteArray data = R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 10 10">
        <rect width="10" height="10" fill="url(#nowhere)"/>
    </svg>)svg";
    QBuffer buffer(&data);
    QVERIFY(buffer.open(QIODevice::ReadOnly));
    Document doc;
    svg::ReadReport report;
    QVERIFY(svg::read(&buffer, doc, nullptr, &report));
    QVERIFY(!doc.shapes()[0]->style().gradient);
    QCOMPARE(doc.shapes()[0]->style().fill, QColor(0x80, 0x80, 0x80));
    QCOMPARE(report.skippedCount(), 1);
}

// The shadow shows where the object is not, offset and softened; the object stays on top.
void GradientTests::dropShadowRendersBehind()
{
    Style style = noOutline();
    style.fill = Qt::white;
    style.shadow = DropShadow{QPointF(20, 20), 4.0, QColor(0, 0, 0, 255)};
    Document doc;
    doc.setPageSize(QSizeF(150, 150));
    doc.insertShape(-1, std::make_unique<Shape>(ShapeKind::Rectangle, QRectF(20, 20, 60, 60), style));
    exporter::ImageOptions options;
    options.dpi = 96;
    const QImage image = exporter::renderImage(doc, options);
    QVERIFY(closeTo(image.pixelColor(50, 50), Qt::white));          // the object itself
    QVERIFY(image.pixelColor(95, 95).lightness() < 80);               // shadow beyond its corner
    QVERIFY(closeTo(image.pixelColor(10, 10), Qt::white));          // nothing up and to the left
    // Soft edge: between the shadow and the page there are greys.
    const int edge = image.pixelColor(102, 90).lightness();
    QVERIFY(edge > 20 && edge < 245);
}

void GradientTests::transparencyGradientFades()
{
    Style style = noOutline();
    style.fill = Qt::black;
    Gradient fade = Gradient::linear(Qt::black, Qt::black, 0);
    fade.stops = {{0.0, QColor(0, 0, 0, 255)}, {1.0, QColor(0, 0, 0, 0)}};
    style.transparency = fade;
    const QImage image = renderOne(style, QRectF(10, 10, 200, 40));
    const int left = image.pixelColor(14, 30).lightness();
    const int middle = image.pixelColor(110, 30).lightness();
    const int right = image.pixelColor(206, 30).lightness();
    QVERIFY(left < 30);
    QVERIFY(middle > 90 && middle < 170);
    QVERIFY(right > 225);
}

void GradientTests::effectsSurviveSvg()
{
    Style style = noOutline();
    style.shadow = DropShadow{QPointF(5, -3), 7.0, QColor(10, 20, 30, 100)};
    Gradient fade = Gradient::radial(Qt::black, Qt::black);
    fade.stops = {{0.0, QColor(0, 0, 0, 255)}, {1.0, QColor(0, 0, 0, 51)}};
    style.transparency = fade;
    Document doc;
    doc.insertShape(-1, std::make_unique<Shape>(ShapeKind::Ellipse, QRectF(10, 10, 80, 50), style));
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    svg::write(doc, &buffer);
    QVERIFY(data.contains("<filter"));
    QVERIFY(data.contains("<mask"));
    auto copy = readSvg(data);
    const Style &read = copy->shapes().front()->style();
    QVERIFY(read.shadow);
    QCOMPARE(read.shadow->offset, QPointF(5, -3));
    QCOMPARE(read.shadow->blur, 7.0);
    QCOMPARE(read.shadow->color.rgb(), QColor(10, 20, 30).rgb());
    QCOMPARE(read.shadow->color.alpha(), 100);
    QVERIFY(read.transparency);
    QCOMPARE(read.transparency->type, Gradient::Type::Radial);
    QCOMPARE(read.transparency->stops.back().color.alpha(), 51);

    // Filters and masks from other programs are not read, and the report says so.
    QByteArray other(R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="100" height="100">
        <rect width="10" height="10" filter="url(#blur)"/></svg>)svg");
    QBuffer otherBuffer(&other);
    otherBuffer.open(QIODevice::ReadOnly);
    Document otherDoc;
    svg::ReadReport report;
    svg::read(&otherBuffer, otherDoc, nullptr, &report);
    QCOMPARE(report.skippedCount(), 1);
}

namespace {
PathData line(const QPointF &from, const QPointF &to)
{
    SubPath sub;
    sub.nodes = {PathNode::corner(from), PathNode::corner(to)};
    return {sub};
}
} // namespace

// Arrowheads sit at the open ends, point away from the line and take the outline colour.
void GradientTests::arrowheadsFollowTheLine()
{
    Style style;
    style.stroke = Qt::red;
    style.strokeWidth = 2.0;
    style.endArrow = Arrowhead::Triangle;
    Shape shape(line(QPointF(20, 50), QPointF(120, 50)), style);
    const QRectF arrow = render::arrowheads(shape).boundingRect();
    QVERIFY(arrow.left() >= 119.0 && arrow.right() > 125.0); // beyond the end, pointing right
    QVERIFY(arrow.top() < 47.0 && arrow.bottom() > 53.0);    // wider than the line

    style.endArrow = Arrowhead::None;
    style.startArrow = Arrowhead::Triangle;
    shape.setStyle(style);
    const QRectF start = render::arrowheads(shape).boundingRect();
    QVERIFY(start.right() <= 21.0 && start.left() < 15.0); // pointing left from the first node

    // Closed paths and shapes without an outline have none.
    PathData closed = line(QPointF(0, 0), QPointF(10, 10));
    closed.front().closed = true;
    QVERIFY(render::arrowheads(Shape(closed, style)).isEmpty());
    style.stroke = Qt::transparent;
    QVERIFY(render::arrowheads(Shape(line(QPointF(0, 0), QPointF(10, 0)), style)).isEmpty());

    // Drawn: the arrow tip past the end of the line is red.
    style.stroke = Qt::red;
    style.startArrow = Arrowhead::None;
    style.endArrow = Arrowhead::Triangle;
    Document doc;
    doc.setPageSize(QSizeF(160, 100));
    doc.insertShape(-1, std::make_unique<Shape>(line(QPointF(20, 50), QPointF(120, 50)), style));
    exporter::ImageOptions options;
    options.dpi = 96;
    const QImage image = exporter::renderImage(doc, options);
    QVERIFY(closeTo(image.pixelColor(123, 50), Qt::red));
    QVERIFY(closeTo(image.pixelColor(121, 47), Qt::red)); // the wide back of the arrow
    QVERIFY(closeTo(image.pixelColor(100, 47), Qt::white));
}

// Behind the fill, the inner half of a thick outline is hidden.
void GradientTests::outlineBehindFill()
{
    Style style;
    style.fill = Qt::blue;
    style.stroke = Qt::red;
    style.strokeWidth = 10.0;
    QImage front = renderOne(style, QRectF(20, 20, 60, 60));
    QVERIFY(closeTo(front.pixelColor(23, 50), Qt::red)); // inside the edge: outline on top
    style.strokeBehind = true;
    QImage behind = renderOne(style, QRectF(20, 20, 60, 60));
    QVERIFY(closeTo(behind.pixelColor(23, 50), Qt::blue)); // now the fill covers it
    QVERIFY(closeTo(behind.pixelColor(17, 50), Qt::red));  // the outer half still shows
}

void GradientTests::outlineScalesWithObject()
{
    Style style;
    style.strokeWidth = 2.0;
    Shape fixed(ShapeKind::Rectangle, QRectF(0, 0, 10, 10), style);
    fixed.setBounds(QRectF(0, 0, 30, 30));
    QCOMPARE(fixed.style().strokeWidth, 2.0);

    style.scaleStroke = true;
    Shape rect(ShapeKind::Rectangle, QRectF(0, 0, 10, 10), style);
    rect.setBounds(QRectF(0, 0, 30, 30));
    QCOMPARE(rect.style().strokeWidth, 6.0);
    Shape path(line(QPointF(0, 0), QPointF(10, 10)), style);
    path.transform(QTransform::fromScale(2, 2));
    QCOMPARE(path.style().strokeWidth, 4.0);
    path.transform(QTransform::fromScale(0.5, 0.5));
    QCOMPARE(path.style().strokeWidth, 2.0);
    // Turning does not change the size, there and back.
    Shape ellipse(ShapeKind::Ellipse, QRectF(0, 0, 20, 10), style);
    ellipse.transform(QTransform().rotate(30));
    ellipse.transform(QTransform().rotate(-30));
    QVERIFY(std::abs(ellipse.style().strokeWidth - 2.0) < 1e-9);
}

void GradientTests::outlineSettingsSurviveSvg()
{
    Style style;
    style.stroke = QColor(0, 128, 0);
    style.strokeWidth = 3.0;
    style.startArrow = Arrowhead::Circle;
    style.endArrow = Arrowhead::Stealth;
    style.strokeBehind = true;
    style.scaleStroke = true;
    Document doc;
    doc.insertShape(-1, std::make_unique<Shape>(line(QPointF(10, 10), QPointF(90, 40)), style));
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    svg::write(doc, &buffer);
    QVERIFY(data.contains("<marker"));
    QVERIFY(data.contains("marker-end="));
    QVERIFY(data.contains("paint-order=\"stroke\""));
    auto copy = readSvg(data);
    const Style &read = copy->shapes().front()->style();
    QCOMPARE(read.startArrow, Arrowhead::Circle);
    QCOMPARE(read.endArrow, Arrowhead::Stealth);
    QVERIFY(read.strokeBehind);
    QVERIFY(read.scaleStroke);

    // Another program's markers become triangles (reported); paint-order in a style attribute.
    QByteArray other(R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="100" height="100">
        <path d="M 0 0 L 50 50" stroke="black" style="marker-end:url(#a);paint-order:stroke fill"/></svg>)svg");
    QBuffer otherBuffer(&other);
    otherBuffer.open(QIODevice::ReadOnly);
    Document otherDoc;
    svg::ReadReport report;
    svg::read(&otherBuffer, otherDoc, nullptr, &report);
    const Style &foreign = otherDoc.shapes().front()->style();
    QCOMPARE(foreign.endArrow, Arrowhead::Triangle);
    QCOMPARE(foreign.startArrow, Arrowhead::None);
    QVERIFY(foreign.strokeBehind);
    QCOMPARE(report.skippedCount(), 1);
}

QTEST_MAIN(GradientTests)
#include "test_gradients.moc"
