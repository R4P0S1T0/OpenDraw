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
    void rotatedShapesStayEditable();
    void rotatedTextAndSvg();
    void objectNamesAreSaved();
};

// Names given in the Objects docker are stored as Inkscape labels.
void ToolTests::objectNamesAreSaved()
{
    Document doc;
    Shape *rect = doc.insertShape(-1, std::make_unique<Shape>(ShapeKind::Rectangle, QRectF(0, 0, 10, 10)));
    std::vector<std::unique_ptr<Shape>> members;
    members.push_back(std::make_unique<Shape>(ShapeKind::Ellipse, QRectF(20, 0, 10, 10)));
    Shape *group = doc.insertShape(-1, std::make_unique<Shape>(std::move(members)));
    QUndoStack undo;
    undo.push(new RenameShapeCommand(&doc, rect, QStringLiteral("Logo")));
    group->setName(QStringLiteral("Ícones"));
    group->children().front()->setName(QStringLiteral("Círculo"));

    const QByteArray data = writeSvg(doc);
    QVERIFY(data.contains("inkscape:label=\"Logo\""));
    auto copy = readSvg(data);
    QCOMPARE(copy->shapes()[0]->name(), QStringLiteral("Logo"));
    QCOMPARE(copy->shapes()[1]->name(), QStringLiteral("Ícones"));
    QCOMPARE(copy->shapes()[1]->children().front()->name(), QStringLiteral("Círculo"));
    undo.undo();
    QVERIFY(rect->name().isEmpty());
}

namespace {
QTransform rotationAround(const QPointF &c, double degrees)
{
    return QTransform::fromTranslate(-c.x(), -c.y()) * QTransform().rotate(degrees) * QTransform::fromTranslate(c.x(), c.y());
}

bool sameRect(const QRectF &a, const QRectF &b)
{
    return near(a.x(), b.x(), 1e-6) && near(a.y(), b.y(), 1e-6) && near(a.width(), b.width(), 1e-6)
           && near(a.height(), b.height(), 1e-6);
}
} // namespace

void ToolTests::rotatedShapesStayEditable()
{
    Style style;
    style.strokeWidth = 2;
    Shape rect(ShapeKind::Rectangle, QRectF(0, 0, 100, 50), style);
    rect.setCornerRadius(5);
    rect.transform(rotationAround({50, 25}, 90));
    QCOMPARE(rect.kind(), ShapeKind::Rectangle);
    QVERIFY(near(rect.rotation(), 90));
    QVERIFY(sameRect(rect.bounds(), QRectF(25, -25, 50, 100)));
    QVERIFY(sameRect(rect.localBounds(), QRectF(0, 0, 100, 50)));
    QCOMPARE(rect.cornerRadius(), 5.0);

    // Moving and scaling a turned rectangle keep it turned.
    rect.moveBy({10, 0});
    QVERIFY(sameRect(rect.bounds(), QRectF(35, -25, 50, 100)));
    rect.setBounds(QRectF(35, -25, 50, 200));
    QVERIFY(sameRect(rect.bounds(), QRectF(35, -25, 50, 200)));

    // Turned back upright, it is an ordinary rectangle again.
    Shape ellipse(ShapeKind::Ellipse, QRectF(0, 0, 40, 20));
    ellipse.transform(rotationAround({20, 10}, 30));
    QVERIFY(!ellipse.shapeTransform().isIdentity());
    ellipse.transform(rotationAround({20, 10}, -30));
    QVERIFY(ellipse.shapeTransform().isIdentity());
    QVERIFY(sameRect(ellipse.bounds(), QRectF(0, 0, 40, 20)));

    // Rotated rectangles are saved as rectangles with a transform, outline width unchanged.
    Document doc;
    auto owned = std::make_unique<Shape>(ShapeKind::Rectangle, QRectF(0, 0, 100, 50), style);
    owned->transform(rotationAround({50, 25}, 30) * QTransform::fromScale(2, 2));
    doc.insertShape(-1, std::move(owned));
    const QByteArray data = writeSvg(doc);
    QVERIFY(data.contains("non-scaling-stroke"));
    auto copy = readSvg(data);
    const Shape &read = *copy->shapes().front();
    QCOMPARE(read.kind(), ShapeKind::Rectangle);
    QVERIFY(near(read.rotation(), 30, 1e-6));
    QVERIFY(near(read.style().strokeWidth, 2));
    QVERIFY(sameRect(read.bounds(), doc.shapes().front()->bounds()));
}

void ToolTests::rotatedTextAndSvg()
{
    TextFormat format;
    format.size = 20;
    Shape text(TextData{QStringLiteral("Hello"), format}, QPointF(10, 50), Style{});
    const QRectF upright = text.bounds();
    text.transform(rotationAround(upright.center(), 90));
    QVERIFY(near(text.rotation(), 90));
    QVERIFY(near(text.bounds().width(), upright.height(), 1e-3));
    QVERIFY(near(text.textData().format.size, 20)); // the font keeps its size
    QVERIFY(text.contains(text.bounds().center(), 0));

    Document doc;
    auto owned = std::make_unique<Shape>(TextData{QStringLiteral("Hello"), format}, QPointF(10, 50), Style{});
    owned->transform(rotationAround(upright.center(), 45));
    doc.insertShape(-1, std::move(owned));
    auto copy = readSvg(writeSvg(doc));
    const Shape &read = *copy->shapes().front();
    QCOMPARE(read.kind(), ShapeKind::Text);
    QVERIFY(near(read.rotation(), 45, 1e-6));
    QVERIFY(near(read.textData().format.size, 20));
    QVERIFY(sameRect(read.bounds(), doc.shapes().front()->bounds()));
}

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
