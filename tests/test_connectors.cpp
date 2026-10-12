#include "core/Commands.h"
#include "core/Connector.h"
#include "core/Dimension.h"
#include "core/Document.h"
#include "core/Export.h"
#include "core/SvgIO.h"

#include <QBuffer>
#include <QUndoStack>
#include <QtTest>

using namespace opendraw;

namespace {

bool near(double a, double b, double tolerance = 0.5)
{
    return std::abs(a - b) <= tolerance;
}

std::unique_ptr<Shape> connectorBetween(const Shape &a, const Shape &b, bool rightAngle = false)
{
    Style style;
    style.fill = Qt::transparent;
    auto line = std::make_unique<Shape>(connector::route(a, b, rightAngle), style);
    line->setConnector(Connector{a.id(), b.id(), rightAngle});
    return line;
}

QByteArray written(const Document &doc)
{
    QByteArray data;
    QBuffer out(&data);
    out.open(QIODevice::WriteOnly);
    svg::write(doc, &out);
    return data;
}

Dimension measure(const QPointF &from, const QPointF &to)
{
    Dimension d;
    d.from = from;
    d.to = to;
    d.offset = 20;
    d.unitFactor = 1.0;
    d.unitSuffix = QStringLiteral("px");
    d.decimals = 0;
    d.textSize = 12;
    return d;
}

} // namespace

class ConnectorTests : public QObject {
    Q_OBJECT

private slots:
    void straightConnectorsRunEdgeToEdge();
    void rightAngleConnectorsTurn();
    void connectorsFollowTheirObjects();
    void connectorsSurviveSvgAndCopies();
    void dimensionsMeasure();
    void dimensionsFollowScalingAndSurviveSvg();
};

void ConnectorTests::straightConnectorsRunEdgeToEdge()
{
    const Shape a(ShapeKind::Rectangle, QRectF(0, 0, 100, 100));
    const Shape b(ShapeKind::Ellipse, QRectF(300, 0, 100, 100));
    const PathData line = connector::route(a, b, false);
    QCOMPARE(line.front().nodes.size(), std::size_t(2));
    QVERIFY(near(line.front().nodes.front().point.x(), 100)); // the rectangle's right side
    QVERIFY(near(line.front().nodes.back().point.x(), 300));  // the circle's left edge
    QVERIFY(near(line.front().nodes.back().point.y(), 50));
}

void ConnectorTests::rightAngleConnectorsTurn()
{
    const Shape a(ShapeKind::Rectangle, QRectF(0, 0, 100, 100));
    const Shape b(ShapeKind::Rectangle, QRectF(300, 200, 100, 100));
    const PathData line = connector::route(a, b, true);
    const auto &nodes = line.front().nodes;
    QCOMPARE(nodes.size(), std::size_t(4));
    QCOMPARE(nodes[0].point, QPointF(100, 50));
    QCOMPARE(nodes[1].point, QPointF(200, 50));
    QCOMPARE(nodes[2].point, QPointF(200, 250));
    QCOMPARE(nodes[3].point, QPointF(300, 250));
    // Lined up: one straight run.
    const Shape c(ShapeKind::Rectangle, QRectF(300, 0, 100, 100));
    QCOMPARE(connector::route(a, c, true).front().nodes.size(), std::size_t(2));
}

void ConnectorTests::connectorsFollowTheirObjects()
{
    Document doc;
    QUndoStack undo;
    Shape *a = doc.insertShape(-1, std::make_unique<Shape>(ShapeKind::Rectangle, QRectF(0, 0, 100, 100)));
    Shape *b = doc.insertShape(-1, std::make_unique<Shape>(ShapeKind::Rectangle, QRectF(300, 0, 100, 100)));
    Shape *line = doc.insertShape(-1, connectorBetween(*a, *b));
    undo.push(new MoveShapeCommand(&doc, b, QPointF(0, 200), false));
    QVERIFY(near(line->pathData().front().nodes.back().point.y(), 200, 60));
    QVERIFY(line->bounds().bottom() > 150);
    undo.undo();
    QVERIFY(near(line->bounds().bottom(), 50));
    // Without one of its objects the line keeps its last course.
    undo.push(new RemoveShapeCommand(&doc, b));
    QVERIFY(near(line->bounds().right(), 300));
    undo.undo();
}

void ConnectorTests::connectorsSurviveSvgAndCopies()
{
    Document doc;
    Shape *a = doc.insertShape(-1, std::make_unique<Shape>(ShapeKind::Rectangle, QRectF(0, 0, 100, 100)));
    Shape *b = doc.insertShape(-1, std::make_unique<Shape>(ShapeKind::Ellipse, QRectF(300, 200, 100, 100)));
    Shape *line = doc.insertShape(-1, connectorBetween(*a, *b, true));
    const QByteArray data = written(doc);
    QVERIFY(data.contains("opendraw:connector="));
    QBuffer in(const_cast<QByteArray *>(&data));
    in.open(QIODevice::ReadOnly);
    Document copy;
    QVERIFY(svg::read(&in, copy));
    QCOMPARE(copy.count(), 3);
    Shape *readA = copy.shapes()[0].get();
    Shape *readB = copy.shapes()[1].get();
    Shape *readLine = copy.shapes()[2].get();
    QVERIFY(readLine->connector());
    QCOMPARE(readLine->connector()->from, readA->id());
    QCOMPARE(readLine->connector()->to, readB->id());
    QVERIFY(readLine->connector()->rightAngle);
    readB->moveBy(QPointF(0, 100));
    copy.notifyShapeChanged(readB);
    QVERIFY(readLine->bounds().bottom() > 300);

    // Copied together, the copies are joined to each other; a connector copied alone is a line.
    auto both = svg::readShapes(svg::writeShapes({a, b, line}, doc.pageSize()));
    QCOMPARE(both[2]->connector()->from, both[0]->id());
    auto alone = svg::readShapes(svg::writeShapes({line}, doc.pageSize()));
    QVERIFY(!alone.front()->connector());
    QCOMPARE(alone.front()->kind(), ShapeKind::Path);
}

void ConnectorTests::dimensionsMeasure()
{
    const Dimension d = measure({0, 100}, {200, 100});
    QVERIFY(dimension::label(d).startsWith(QStringLiteral("200")));
    QVERIFY(dimension::label(d).endsWith(QStringLiteral("px")));
    // The dimension line runs 20 above the measured points; the label sits above it.
    const QRectF lines = dimension::lines(d).boundingRect();
    QVERIFY(near(lines.top(), 100 - 20 - 12 * 0.4, 0.01));
    QVERIFY(dimension::labelBox(d).boundingRect().bottom() < 80);
    QVERIFY(near(dimension::labelBox(d).boundingRect().center().x(), 100, 1));
    // Measured right to left, the label is still upright.
    const Dimension back = measure({200, 100}, {0, 100});
    const QPolygonF box = dimension::labelBox(back);
    QVERIFY(box[0].x() < box[1].x());

    // Drawn: lines and figures in the outline colour.
    Document doc;
    doc.setPageSize(QSizeF(260, 160));
    Style style;
    style.stroke = Qt::red;
    auto shape = std::make_unique<Shape>(PathData{}, style);
    shape->setDimension(measure({30, 120}, {230, 120}));
    doc.insertShape(-1, std::move(shape));
    exporter::ImageOptions options;
    options.dpi = 96;
    const QImage image = exporter::renderImage(doc, options);
    int red = 0;
    for (int x = 100; x < 160; ++x) {
        for (int y = 60; y < 100; ++y)
            red += image.pixelColor(x, y).red() > 200 && image.pixelColor(x, y).green() < 100;
    }
    QVERIFY(red > 20); // the figures
}

void ConnectorTests::dimensionsFollowScalingAndSurviveSvg()
{
    Shape shape(PathData{}, Style{});
    shape.setDimension(measure({0, 0}, {100, 0}));
    shape.transform(QTransform::fromScale(2, 2));
    QCOMPARE(shape.dimension()->to, QPointF(200, 0));
    QVERIFY(dimension::label(*shape.dimension()).startsWith(QStringLiteral("200")));
    QCOMPARE(shape.dimension()->textSize, 24.0);

    Document doc;
    auto copy = std::make_unique<Shape>(PathData{}, Style{});
    copy->setDimension(measure({10, 50}, {110, 80}));
    doc.insertShape(-1, std::move(copy));
    const QByteArray data = written(doc);
    QVERIFY(data.contains("opendraw:dimension="));
    QBuffer in(const_cast<QByteArray *>(&data));
    in.open(QIODevice::ReadOnly);
    Document read;
    QVERIFY(svg::read(&in, read));
    QCOMPARE(read.count(), 1);
    QVERIFY(read.shapes().front()->dimension());
    QCOMPARE(*read.shapes().front()->dimension(), measure({10, 50}, {110, 80}));
}

QTEST_MAIN(ConnectorTests)
#include "test_connectors.moc"
