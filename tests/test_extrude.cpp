#include "core/Document.h"
#include "core/Export.h"
#include "core/Extrude.h"
#include "core/Render.h"
#include "core/SvgIO.h"

#include <QBuffer>
#include <QtTest>

using namespace opendraw;

namespace {

Style extruded(const QPointF &vanishingPoint, double depth)
{
    Style style;
    style.fill = QColor(0x40, 0x80, 0xff);
    style.stroke = Qt::transparent;
    style.extrusion = Extrusion{vanishingPoint, depth, QColor()};
    return style;
}

} // namespace

class ExtrudeTests : public QObject {
    Q_OBJECT

private slots:
    void facesJoinFrontAndBack();
    void facesAreShadedByTheLight();
    void sidesAreDrawnBehind();
    void extrusionSurvivesSvg();
};

void ExtrudeTests::facesJoinFrontAndBack()
{
    // A 100 × 100 square, vanishing point 200 to the right of its centre, half way.
    const Shape square(ShapeKind::Rectangle, QRectF(0, 0, 100, 100), extruded(QPointF(200, 0), 0.5));
    QCOMPARE(extrude::vanishingPoint(square), QPointF(250, 50));
    QCOMPARE(extrude::back(square, QPointF(0, 0)), QPointF(125, 25));
    const auto faces = extrude::faces(square);
    QCOMPARE(faces.size(), std::size_t(5)); // the back and four sides
    QCOMPARE(faces.front().polygon.boundingRect(), QRectF(125, 25, 50, 50));
    // The extrusion moves with the object.
    Shape moved(ShapeKind::Rectangle, QRectF(0, 0, 100, 100), square.style());
    moved.moveBy(QPointF(10, 0));
    QCOMPARE(extrude::faces(moved).front().polygon.boundingRect(), QRectF(135, 25, 50, 50));
    QVERIFY(render::paintedBounds(square).right() >= 175);
}

void ExtrudeTests::facesAreShadedByTheLight()
{
    // Going up and to the right: the top face (lit from above) is lighter than the right one.
    const Shape square(ShapeKind::Rectangle, QRectF(0, 0, 100, 100), extruded(QPointF(150, -150), 0.4));
    const auto faces = extrude::faces(square);
    const extrude::Face *top = nullptr, *right = nullptr;
    for (const auto &face : faces) {
        const QRectF box = face.polygon.boundingRect();
        if (std::abs(box.bottom()) < 1e-6 && box.left() < 1)
            top = &face;
        if (std::abs(box.left() - 100) < 1e-6 && box.width() > 1)
            right = &face;
    }
    QVERIFY(top && right);
    QVERIFY(top->color.lightness() > right->color.lightness());
    // A chosen colour is used as it is, shaded.
    Style style = square.style();
    style.extrusion->color = QColor(200, 0, 0);
    const Shape red(ShapeKind::Rectangle, QRectF(0, 0, 100, 100), style);
    for (const auto &face : extrude::faces(red))
        QVERIFY(face.color.green() == 0 && face.color.blue() == 0);
}

void ExtrudeTests::sidesAreDrawnBehind()
{
    Document doc;
    doc.setPageSize(QSizeF(300, 200));
    doc.insertShape(-1, std::make_unique<Shape>(ShapeKind::Rectangle, QRectF(20, 50, 100, 100),
                                                extruded(QPointF(250, 0), 0.5)));
    exporter::ImageOptions options;
    options.dpi = 96;
    const QImage image = exporter::renderImage(doc, options);
    QCOMPARE(image.pixelColor(70, 100), QColor(0x40, 0x80, 0xff)); // the front, on top
    const QColor side = image.pixelColor(150, 100);                // the right-hand side
    QVERIFY(side != QColor(Qt::white) && side.blue() > side.red());
    QCOMPARE(image.pixelColor(250, 100), QColor(Qt::white));
}

void ExtrudeTests::extrusionSurvivesSvg()
{
    Document doc;
    Style style = extruded(QPointF(-80, 120), 0.25);
    style.extrusion->color = QColor(10, 200, 30);
    doc.insertShape(-1, std::make_unique<Shape>(ShapeKind::Ellipse, QRectF(0, 0, 80, 60), style));
    doc.insertShape(-1, std::make_unique<Shape>(ShapeKind::Rectangle, QRectF(100, 0, 50, 50),
                                                extruded(QPointF(10, 20), 0.6)));
    QByteArray data;
    QBuffer out(&data);
    out.open(QIODevice::WriteOnly);
    QVERIFY(svg::write(doc, &out));
    QVERIFY(data.contains("<polygon")); // the sides, for other programs
    QBuffer in(&data);
    in.open(QIODevice::ReadOnly);
    Document copy;
    QVERIFY(svg::read(&in, copy));
    QCOMPARE(copy.count(), 2);
    const Style &a = copy.shapes()[0]->style();
    QVERIFY(a.extrusion);
    QCOMPARE(a.extrusion->vanishingPoint, QPointF(-80, 120));
    QCOMPARE(a.extrusion->depth, 0.25);
    QCOMPARE(a.extrusion->color, QColor(10, 200, 30));
    QVERIFY(!copy.shapes()[1]->style().extrusion->color.isValid()); // shaded
}

QTEST_MAIN(ExtrudeTests)
#include "test_extrude.moc"
