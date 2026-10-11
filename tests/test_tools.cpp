#include "core/Commands.h"
#include "core/Document.h"
#include "core/Generators.h"
#include "core/PathOps.h"
#include "core/SvgIO.h"

#include <QBuffer>
#include <QUndoStack>
#include <QtTest>

using namespace opendraw;

namespace {

bool near(double a, double b, double tolerance = 1e-6)
{
    return std::abs(a - b) <= tolerance;
}

QByteArray writeSvg(const Document &doc)
{
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    svg::write(doc, &buffer);
    return data;
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

class ToolTests : public QObject {
    Q_OBJECT

private slots:
    void polygonHasItsCornersOnTheBox();
    void starAlternatesTipsAndNotches();
    void spiralStaysInsideItsBox();
    void graphPaperIsAGroupOfCells();
    void roundedRectangles();
    void guidelinesUndoAndSvg();
    void readsInkscapeGuides();
};

void ToolTests::polygonHasItsCornersOnTheBox()
{
    const QRectF box(10, 20, 100, 100);
    const PathData hexagon = generators::polygon(box, 6);
    QCOMPARE(hexagon.size(), std::size_t(1));
    QVERIFY(hexagon.front().closed);
    QCOMPARE(hexagon.front().nodes.size(), std::size_t(6));
    // First corner at the top centre; all corners on the circle.
    QVERIFY(near(hexagon.front().nodes.front().point.x(), 60));
    QVERIFY(near(hexagon.front().nodes.front().point.y(), 20));
    for (const PathNode &node : hexagon.front().nodes)
        QVERIFY(near(std::hypot(node.point.x() - 60, node.point.y() - 70), 50));
    QCOMPARE(generators::polygon(box, 1).front().nodes.size(), std::size_t(3)); // at least a triangle
}

void ToolTests::starAlternatesTipsAndNotches()
{
    const PathData star = generators::star(QRectF(0, 0, 100, 100), 5, 50);
    const auto &nodes = star.front().nodes;
    QCOMPARE(nodes.size(), std::size_t(10));
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const double r = std::hypot(nodes[i].point.x() - 50, nodes[i].point.y() - 50);
        QVERIFY(near(r, i % 2 ? 25.0 : 50.0));
    }
}

void ToolTests::spiralStaysInsideItsBox()
{
    const QRectF box(0, 0, 200, 100);
    const PathData spiral = generators::spiral(box, 3);
    QVERIFY(!spiral.front().closed);
    QVERIFY(spiral.front().nodes.size() >= 4);
    const QRectF bounds = pathops::toPainterPath(spiral).boundingRect();
    QVERIFY(box.adjusted(-1, -1, 1, 1).contains(bounds));
    QVERIFY(bounds.width() > 150 && bounds.height() > 75); // and fills most of it
}

void ToolTests::graphPaperIsAGroupOfCells()
{
    auto grid = generators::graphPaper(QRectF(0, 0, 40, 30), 4, 3, Style{});
    QCOMPARE(grid->kind(), ShapeKind::Group);
    QCOMPARE(grid->children().size(), std::size_t(12));
    QCOMPARE(grid->children().back()->bounds(), QRectF(30, 20, 10, 10));
    QCOMPARE(grid->bounds(), QRectF(0, 0, 40, 30));
}

void ToolTests::roundedRectangles()
{
    Document doc;
    Shape *rect = doc.insertShape(-1, std::make_unique<Shape>(ShapeKind::Rectangle, QRectF(0, 0, 100, 50)));
    QUndoStack undo;
    undo.push(new SetCornerRadiusCommand(&doc, rect, 10));
    QCOMPARE(rect->cornerRadius(), 10.0);
    QVERIFY(!rect->path().contains(QPointF(1, 1)));  // the corner is cut off
    QVERIFY(rect->path().contains(QPointF(50, 25)));

    // Saved as rx/ry and read back as an editable rounded rectangle.
    auto copy = readSvg(writeSvg(doc));
    const Shape &read = *copy->shapes().front();
    QCOMPARE(read.kind(), ShapeKind::Rectangle);
    QVERIFY(near(read.cornerRadius(), 10));

    // Scaling scales the corners with the shorter side's factor.
    rect->transform(QTransform::fromScale(2, 3));
    QVERIFY(near(rect->cornerRadius(), 20));
    undo.undo();
    QCOMPARE(rect->cornerRadius(), 0.0);
}

void ToolTests::guidelinesUndoAndSvg()
{
    Document doc;
    QUndoStack undo;
    undo.push(new GuideCommand(&doc, 0, std::nullopt, Guide{Qt::Vertical, 30}));
    undo.push(new GuideCommand(&doc, 1, std::nullopt, Guide{Qt::Horizontal, 40}));
    QCOMPARE(doc.guides().size(), std::size_t(2));
    undo.push(new GuideCommand(&doc, 0, doc.guides()[0], Guide{Qt::Vertical, 35}));
    QCOMPARE(doc.guides()[0].position, 35.0);
    undo.undo();
    QCOMPARE(doc.guides()[0].position, 30.0);

    const QByteArray data = writeSvg(doc);
    QVERIFY(data.contains("sodipodi:guide"));
    auto copy = readSvg(data);
    QCOMPARE(copy->guides().size(), std::size_t(2));
    QCOMPARE(copy->guides()[0].orientation, Qt::Vertical);
    QVERIFY(near(copy->guides()[0].position, 30));
    QCOMPARE(copy->guides()[1].orientation, Qt::Horizontal);
    QVERIFY(near(copy->guides()[1].position, 40));

    undo.undo();
    undo.undo();
    QVERIFY(doc.guides().empty());
    doc.insertGuide(-1, Guide{});
    doc.clear();
    QVERIFY(doc.guides().empty());
}

// Inkscape measures guide positions upwards from the bottom of the page.
void ToolTests::readsInkscapeGuides()
{
    auto doc = readSvg(QByteArrayLiteral(
        R"svg(<svg xmlns="http://www.w3.org/2000/svg" xmlns:sodipodi="http://sodipodi.sourceforge.net/DTD/sodipodi-0.dtd"
                   width="200" height="100" viewBox="0 0 200 100">
                <sodipodi:namedview>
                  <sodipodi:guide position="0,75" orientation="0,1"/>
                  <sodipodi:guide position="120,0" orientation="-1,0"/>
                  <sodipodi:guide position="10,10" orientation="0.7,0.7"/>
                </sodipodi:namedview>
              </svg>)svg"));
    QCOMPARE(doc->guides().size(), std::size_t(2)); // the slanted one is not supported
    QCOMPARE(doc->guides()[0].orientation, Qt::Horizontal);
    QVERIFY(near(doc->guides()[0].position, 25));
    QCOMPARE(doc->guides()[1].orientation, Qt::Vertical);
    QVERIFY(near(doc->guides()[1].position, 120));
}

QTEST_MAIN(ToolTests)
#include "test_tools.moc"
