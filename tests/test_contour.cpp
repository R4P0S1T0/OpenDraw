#include "core/ContourEffect.h"
#include "core/Document.h"
#include "core/Export.h"
#include "core/Render.h"
#include "core/SvgIO.h"

#include <QBuffer>
#include <QtTest>

using namespace opendraw;

namespace {

bool near(double a, double b, double tolerance = 0.5)
{
    return std::abs(a - b) <= tolerance;
}

Style withContour(Contour::Direction direction, int steps, double offset)
{
    Style style;
    style.fill = QColor(0, 0, 255);
    style.stroke = Qt::transparent;
    Contour c;
    c.direction = direction;
    c.steps = steps;
    c.offset = offset;
    c.fill = QColor(255, 0, 0);
    c.stroke = Qt::transparent;
    style.contour = c;
    return style;
}

} // namespace

class ContourTests : public QObject {
    Q_OBJECT

private slots:
    void offsetGrowsAndShrinks();
    void outsideRingsRunOutwards();
    void insideAndToCentre();
    void ringsFollowTheObject();
    void ringsAreDrawn();
    void contourSurvivesSvg();
    void paintedBoundsIncludeEffects();
};

void ContourTests::offsetGrowsAndShrinks()
{
    QPainterPath square;
    square.addRect(0, 0, 100, 100);
    const QRectF grown = contour::offsetPath(square, 10).boundingRect();
    QVERIFY(near(grown.left(), -10) && near(grown.width(), 120));
    const QRectF shrunk = contour::offsetPath(square, -10).boundingRect();
    QVERIFY(near(shrunk.left(), 10) && near(shrunk.width(), 80));
    QVERIFY(contour::offsetPath(square, -60).isEmpty());
}

void ContourTests::outsideRingsRunOutwards()
{
    const Shape rect(ShapeKind::Rectangle, QRectF(0, 0, 100, 50), withContour(Contour::Direction::Outside, 3, 5));
    const auto rings = contour::rings(rect);
    QCOMPARE(rings.size(), std::size_t(3));
    // Outermost first (painted under the others), in the contour's own colour.
    QVERIFY(near(rings[0].path.boundingRect().width(), 130));
    QVERIFY(near(rings[2].path.boundingRect().width(), 110));
    QCOMPARE(rings[0].style.fill, QColor(255, 0, 0));
    // The ring next to the object is a third of the way from its colour.
    const QColor first = rings[2].style.fill;
    QVERIFY(std::abs(first.red() - 85) <= 1 && std::abs(first.blue() - 170) <= 1);
    QVERIFY(near(contour::reach(rect.style()), 16));
}

void ContourTests::insideAndToCentre()
{
    const Shape circle(ShapeKind::Ellipse, QRectF(0, 0, 100, 100), withContour(Contour::Direction::Inside, 2, 10));
    const auto inside = contour::rings(circle);
    QCOMPARE(inside.size(), std::size_t(2));
    QVERIFY(near(inside[0].path.boundingRect().width(), 80));
    QVERIFY(near(inside[1].path.boundingRect().width(), 60));
    QCOMPARE(contour::reach(circle.style()), 0.0);

    // To the centre: as many rings as fit, the last one in the contour's colour.
    const Shape full(ShapeKind::Ellipse, QRectF(0, 0, 100, 100), withContour(Contour::Direction::ToCenter, 1, 10));
    const auto rings = contour::rings(full);
    QCOMPARE(rings.size(), std::size_t(4));
    QCOMPARE(rings.back().style.fill, QColor(255, 0, 0));
}

void ContourTests::ringsFollowTheObject()
{
    Shape rect(ShapeKind::Rectangle, QRectF(0, 0, 100, 50), withContour(Contour::Direction::Outside, 1, 10));
    QVERIFY(near(contour::rings(rect).front().path.boundingRect().left(), -10));
    rect.moveBy(QPointF(200, 0));
    QVERIFY(near(contour::rings(rect).front().path.boundingRect().left(), 190));
    Style style = rect.style();
    style.contour->offset = 20;
    rect.setStyle(style);
    QVERIFY(near(contour::rings(rect).front().path.boundingRect().left(), 180));
}

void ContourTests::ringsAreDrawn()
{
    Document doc;
    doc.setPageSize(QSizeF(200, 200));
    doc.insertShape(-1, std::make_unique<Shape>(ShapeKind::Rectangle, QRectF(50, 50, 100, 100),
                                                withContour(Contour::Direction::Outside, 2, 10)));
    exporter::ImageOptions options;
    options.dpi = 96;
    QImage image = exporter::renderImage(doc, options);
    QCOMPARE(image.pixelColor(100, 100), QColor(0, 0, 255)); // the object, on top
    QCOMPARE(image.pixelColor(100, 35), QColor(255, 0, 0));  // the outer ring
    QCOMPARE(image.pixelColor(100, 20), QColor(Qt::white));

    Document inside;
    inside.setPageSize(QSizeF(200, 200));
    inside.insertShape(-1, std::make_unique<Shape>(ShapeKind::Rectangle, QRectF(50, 50, 100, 100),
                                                   withContour(Contour::Direction::Inside, 1, 20)));
    image = exporter::renderImage(inside, options);
    QCOMPARE(image.pixelColor(100, 100), QColor(255, 0, 0)); // the ring, over the object
    QCOMPARE(image.pixelColor(100, 60), QColor(0, 0, 255));  // the object's edge
}

void ContourTests::contourSurvivesSvg()
{
    Document doc;
    Style style = withContour(Contour::Direction::ToCenter, 4, 7.5);
    style.contour->stroke = QColor(0, 128, 0, 200);
    doc.insertShape(-1, std::make_unique<Shape>(ShapeKind::Ellipse, QRectF(0, 0, 80, 60), style));
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    QVERIFY(svg::write(doc, &buffer));
    QVERIFY(data.contains("opendraw:contour="));
    QVERIFY(data.count("<path") >= 3); // the rings, for other programs
    buffer.close();
    buffer.open(QIODevice::ReadOnly);
    Document copy;
    QVERIFY(svg::read(&buffer, copy));
    QCOMPARE(copy.count(), 1); // the rings are not read back as objects
    const Style &read = copy.shapes().front()->style();
    QVERIFY(read.contour);
    QCOMPARE(read.contour->direction, Contour::Direction::ToCenter);
    QCOMPARE(read.contour->offset, 7.5);
    QCOMPARE(read.contour->fill, QColor(255, 0, 0));
    QCOMPARE(read.contour->stroke.rgb(), QColor(0, 128, 0).rgb());
    QCOMPARE(read.contour->stroke.alpha(), 200);
}

void ContourTests::paintedBoundsIncludeEffects()
{
    const Shape plain(ShapeKind::Rectangle, QRectF(0, 0, 100, 100), Style{});
    QVERIFY(render::paintedBounds(plain).contains(plain.bounds()));
    const Shape ringed(ShapeKind::Rectangle, QRectF(0, 0, 100, 100), withContour(Contour::Direction::Outside, 2, 10));
    QVERIFY(render::paintedBounds(ringed).left() <= -20);
    Style shadowed;
    shadowed.shadow = DropShadow{QPointF(30, 40), 4, Qt::black};
    const Shape withShadow(ShapeKind::Rectangle, QRectF(0, 0, 100, 100), shadowed);
    QVERIFY(render::paintedBounds(withShadow).bottom() >= 140);
}

QTEST_MAIN(ContourTests)
#include "test_contour.moc"
