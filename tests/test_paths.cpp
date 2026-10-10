#include "core/PathOps.h"
#include "core/Shape.h"

#include <QtTest>

#include <cmath>

using namespace opendraw;

namespace {

bool near(const QPointF &a, const QPointF &b, double tolerance = 1e-6)
{
    return std::hypot(a.x() - b.x(), a.y() - b.y()) <= tolerance;
}

bool near(const QRectF &a, const QRectF &b, double tolerance = 1e-3)
{
    return near(a.topLeft(), b.topLeft(), tolerance) && near(a.bottomRight(), b.bottomRight(), tolerance);
}

PathData parse(QStringView d)
{
    auto path = pathops::fromSvg(d);
    return path ? *path : PathData{};
}

} // namespace

class PathTests : public QObject {
    Q_OBJECT

private slots:
    void svgRoundTrip();
    void relativeCommandsAndShorthands();
    void compactNumbers();
    void quadraticBecomesCubic();
    void arcs();
    void malformedDataKeepsWhatWasRead();
    void insertNodeKeepsCurveShape();
    void insertNodeOnStraightSegment();
    void removeNodeDropsTooShortSubpath();
    void nearestSegment();
    void fromPainterPathRectangleAndEllipse();
    void freehandIsSimplified();
    void shapeScalesPathToNewBounds();
};

void PathTests::svgRoundTrip()
{
    const PathData original = parse(u"M 10 10 C 20 0 40 0 50 10 L 50 40 C 30 60 10 40 10 10 Z");
    QCOMPARE(original.size(), std::size_t(1));
    QVERIFY(original[0].closed);
    QCOMPARE(original[0].nodes.size(), std::size_t(3)); // closing curve merged into the first node

    const PathData again = parse(pathops::toSvg(original));
    QCOMPARE(again, original);
}

void PathTests::relativeCommandsAndShorthands()
{
    const PathData square = parse(u"m10 10 h20 v20 h-20 z");
    QCOMPARE(square.size(), std::size_t(1));
    QVERIFY(square[0].closed);
    QCOMPARE(square[0].nodes.size(), std::size_t(4));
    QCOMPARE(square[0].nodes[2].point, QPointF(30, 30));

    // Smooth curveto reflects the previous control point.
    const PathData wave = parse(u"M0 0 C 0 10 10 10 10 0 S 20 -10 20 0");
    QCOMPARE(wave[0].nodes[1].out, QPointF(10, -10));
    QCOMPARE(wave[0].nodes[1].type, NodeType::Smooth);
}

void PathTests::compactNumbers()
{
    // "3.5.5" is two numbers; numbers after a moveto are linetos.
    const PathData path = parse(u"M1-2 3.5.5 1e1,2E0");
    QCOMPARE(path[0].nodes.size(), std::size_t(3));
    QCOMPARE(path[0].nodes[0].point, QPointF(1, -2));
    QCOMPARE(path[0].nodes[1].point, QPointF(3.5, 0.5));
    QCOMPARE(path[0].nodes[2].point, QPointF(10, 2));
}

void PathTests::quadraticBecomesCubic()
{
    const PathData path = parse(u"M0 0 Q 10 10 20 0");
    QVERIFY(near(pathops::pointAt(path[0].nodes[0], path[0].nodes[1], 0.5), QPointF(10, 5)));
}

void PathTests::arcs()
{
    // Half circle of radius 10 from (0,0) to (20,0), bulging upwards.
    const PathData half = parse(u"M 0 0 A 10 10 0 0 1 20 0");
    QVERIFY(near(half[0].nodes.back().point, QPointF(20, 0)));
    QVERIFY(near(pathops::toPainterPath(half).boundingRect(), QRectF(0, -10, 20, 10), 0.05));

    // Flags written without separators.
    const PathData compact = parse(u"M0 0a10 10 0 0110 10");
    QVERIFY(near(compact[0].nodes.back().point, QPointF(10, 10)));
}

void PathTests::malformedDataKeepsWhatWasRead()
{
    const PathData path = parse(u"M0 0 L10 10 L20 # garbage");
    QCOMPARE(path[0].nodes.size(), std::size_t(2));
    QVERIFY(!pathops::fromSvg(u"garbage"));
}

void PathTests::insertNodeKeepsCurveShape()
{
    PathData path = parse(u"M 0 0 C 0 30 40 30 40 0");
    const PathNode a = path[0].nodes[0];
    const PathNode b = path[0].nodes[1];
    const QPointF splitPoint = pathops::pointAt(a, b, 0.3);
    const QPointF earlier = pathops::pointAt(a, b, 0.15);

    const int index = pathops::insertNode(path, 0, 0, 0.3);
    QCOMPARE(index, 1);
    QCOMPARE(path[0].nodes.size(), std::size_t(3));
    QVERIFY(near(path[0].nodes[1].point, splitPoint));
    QCOMPARE(path[0].nodes[1].type, NodeType::Smooth);
    QVERIFY(near(pathops::pointAt(path[0].nodes[0], path[0].nodes[1], 0.5), earlier));
}

void PathTests::insertNodeOnStraightSegment()
{
    PathData path = parse(u"M 0 0 L 10 0 L 10 10 Z");
    pathops::insertNode(path, 0, 2, 0.5); // closing segment from (10,10) back to (0,0)
    QCOMPARE(path[0].nodes.size(), std::size_t(4));
    QCOMPARE(path[0].nodes[3], PathNode::corner({5, 5}));
}

void PathTests::removeNodeDropsTooShortSubpath()
{
    PathData path = parse(u"M 0 0 L 10 0 L 10 10 M 50 50 L 60 60");
    pathops::removeNode(path, 0, 1);
    QCOMPARE(path[0].nodes.size(), std::size_t(2));
    pathops::removeNode(path, 1, 0);
    QCOMPARE(path.size(), std::size_t(1));
}

void PathTests::nearestSegment()
{
    const PathData path = parse(u"M 0 0 L 100 0 L 100 100");
    const auto hit = pathops::nearestSegment(path, {100.5, 40}, 2.0);
    QVERIFY(hit);
    QCOMPARE(hit->segment, 1);
    QVERIFY(std::abs(hit->t - 0.4) < 1e-3);
    QVERIFY(near(hit->point, QPointF(100, 40), 1e-3));
    QVERIFY(!pathops::nearestSegment(path, {50, 50}, 2.0));
}

void PathTests::fromPainterPathRectangleAndEllipse()
{
    QPainterPath rect;
    rect.addRect(0, 0, 10, 20);
    const PathData r = pathops::fromPainterPath(rect);
    QCOMPARE(r.size(), std::size_t(1));
    QVERIFY(r[0].closed);
    QCOMPARE(r[0].nodes.size(), std::size_t(4));
    QCOMPARE(r[0].nodes[0].type, NodeType::Corner);

    QPainterPath ellipse;
    ellipse.addEllipse(0, 0, 10, 20);
    const PathData e = pathops::fromPainterPath(ellipse);
    QVERIFY(e[0].closed);
    QCOMPARE(e[0].nodes.size(), std::size_t(4));
    for (const PathNode &node : e[0].nodes)
        QCOMPARE(node.type, NodeType::Smooth);
    QVERIFY(near(pathops::toPainterPath(e).boundingRect(), QRectF(0, 0, 10, 20)));
}

void PathTests::freehandIsSimplified()
{
    std::vector<QPointF> line;
    for (int i = 0; i <= 100; ++i)
        line.emplace_back(i, 0.001 * (i % 2));
    const SubPath straight = pathops::fitFreehand(line, 0.5, false);
    QCOMPARE(straight.nodes.size(), std::size_t(2));
    QCOMPARE(straight.nodes.front().point, QPointF(0, 0));

    std::vector<QPointF> wave;
    for (int i = 0; i <= 200; ++i)
        wave.emplace_back(i, 20 * std::sin(i / 15.0));
    const SubPath curve = pathops::fitFreehand(wave, 0.5, false);
    QVERIFY(curve.nodes.size() > 4);
    QVERIFY(curve.nodes.size() < 60);
    QCOMPARE(curve.nodes.front().point, wave.front());
    QCOMPARE(curve.nodes.back().point, wave.back());
}

void PathTests::shapeScalesPathToNewBounds()
{
    Shape shape(parse(u"M 0 0 L 10 0 L 10 10 L 0 10 Z"));
    QCOMPARE(shape.bounds(), QRectF(0, 0, 10, 10));
    shape.setBounds(QRectF(10, 10, 20, 40));
    QVERIFY(near(shape.bounds(), QRectF(10, 10, 20, 40)));
    shape.moveBy({5, 5});
    QVERIFY(near(shape.bounds(), QRectF(15, 15, 20, 40)));
}

QTEST_GUILESS_MAIN(PathTests)
#include "test_paths.moc"
