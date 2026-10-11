#include "core/Document.h"
#include "core/Export.h"
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

QTEST_MAIN(GradientTests)
#include "test_gradients.moc"
