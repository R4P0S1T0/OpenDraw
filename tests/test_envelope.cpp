#include "core/Commands.h"
#include "core/Document.h"
#include "core/Envelope.h"
#include "core/Render.h"
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

bool nearRect(const QRectF &a, const QRectF &b, double tolerance = 0.5)
{
    return near(a.left(), b.left(), tolerance) && near(a.top(), b.top(), tolerance) && near(a.right(), b.right(), tolerance)
           && near(a.bottom(), b.bottom(), tolerance);
}

// The top edge bowed upwards by a third of the height, the right edge straight but its
// lower corner pulled out to the right.
Envelope arch()
{
    Envelope e = Envelope::identity();
    e.points[1].ry() = -1.0 / 3;
    e.points[2].ry() = -1.0 / 3;
    e.points[6].rx() = 1.5;
    e.points[5].rx() = 1.25;
    e.points[7].rx() = 1.0;
    return e;
}

std::unique_ptr<Document> roundTrip(const Document &doc, QByteArray *written = nullptr)
{
    QByteArray data;
    QBuffer out(&data);
    out.open(QIODevice::WriteOnly);
    svg::write(doc, &out);
    if (written)
        *written = data;
    QBuffer in(&data);
    in.open(QIODevice::ReadOnly);
    auto copy = std::make_unique<Document>();
    svg::read(&in, *copy);
    return copy;
}

} // namespace

class EnvelopeTests : public QObject {
    Q_OBJECT

private slots:
    void identityKeepsTheShape();
    void edgesAndCornersBend();
    void followsMovesTurnsAndMirrors();
    void curvesTurnWithTheirEnvelope();
    void removingBakesTheTurn();
    void arrowheadsFollowTheBend();
    void envelopeSurvivesSvg();
};

void EnvelopeTests::identityKeepsTheShape()
{
    Shape rect(ShapeKind::Rectangle, QRectF(10, 20, 100, 60));
    rect.setEnvelope(Envelope::identity());
    QVERIFY(nearRect(rect.bounds(), QRectF(10, 20, 100, 60), 0.01));
    QCOMPARE(envelope::map(Envelope::identity(), QPointF(0.25, 0.75)), QPointF(0.25, 0.75));
    // Groups and bitmaps take no envelope.
    std::vector<std::unique_ptr<Shape>> none;
    Shape group(std::move(none));
    group.setEnvelope(Envelope::identity());
    QVERIFY(!group.envelope());
}

void EnvelopeTests::edgesAndCornersBend()
{
    Shape rect(ShapeKind::Rectangle, QRectF(0, 0, 90, 90));
    rect.setEnvelope(arch());
    const QRectF b = rect.bounds();
    // The middle of the top edge rises by a quarter of a third... of the height (cubic curve).
    QVERIFY(near(b.top(), -22.5, 1.0));
    QVERIFY(near(b.right(), 135, 1.0)); // the pulled-out corner
    QVERIFY(near(b.left(), 0, 0.01) && near(b.bottom(), 90, 0.01));
    // The local geometry itself is untouched.
    QCOMPARE(rect.localBounds(), QRectF(0, 0, 90, 90));
    QVERIFY(rect.contains(QPointF(45, -10), 0.0)); // inside the arch
    QVERIFY(!rect.contains(QPointF(5, -10), 0.0));
}

void EnvelopeTests::followsMovesTurnsAndMirrors()
{
    Shape rect(ShapeKind::Rectangle, QRectF(0, 0, 90, 90));
    rect.setEnvelope(arch());
    const QRectF before = rect.bounds();
    rect.moveBy(QPointF(100, 50));
    QVERIFY(nearRect(rect.bounds(), before.translated(100, 50)));
    rect.moveBy(QPointF(-100, -50));
    // Doubling the size doubles the bent shape.
    rect.transform(QTransform::fromScale(2, 2));
    QVERIFY(nearRect(rect.bounds(), QRectF(before.topLeft() * 2, before.size() * 2), 1.0));
    rect.transform(QTransform::fromScale(0.5, 0.5));
    // A quarter turn turns the bent shape.
    const QPointF centre = before.center();
    QTransform turn;
    turn.translate(centre.x(), centre.y());
    turn.rotate(90);
    turn.translate(-centre.x(), -centre.y());
    const QPainterPath expected = turn.map(rect.path());
    rect.transform(turn);
    QVERIFY(nearRect(rect.bounds(), expected.boundingRect(), 1.0));
    rect.transform(turn.inverted());
    QVERIFY(nearRect(rect.bounds(), before, 1.0));
    // Mirroring mirrors the bend: the pulled-out corner goes to the left.
    QTransform mirror;
    mirror.translate(centre.x(), 0);
    mirror.scale(-1, 1);
    mirror.translate(-centre.x(), 0);
    const QRectF mirrored = mirror.mapRect(before);
    rect.transform(mirror);
    QVERIFY(nearRect(rect.bounds(), mirrored, 1.0));
}

void EnvelopeTests::curvesTurnWithTheirEnvelope()
{
    SubPath sub;
    sub.nodes = {PathNode::corner({0, 0}), PathNode::corner({100, 0}), PathNode::corner({100, 50}), PathNode::corner({0, 50})};
    sub.closed = true;
    Shape curve(PathData{sub}, Style{});
    curve.setEnvelope(arch());
    const PathData nodes = curve.pathData();
    curve.transform(QTransform().rotate(30));
    QCOMPARE(curve.pathData(), nodes); // the turn is kept apart
    QVERIFY(!curve.shapeTransform().isIdentity());
    curve.transform(QTransform().rotate(-30));
    QVERIFY(curve.shapeTransform().isIdentity()); // and folded back once upright
}

void EnvelopeTests::removingBakesTheTurn()
{
    Document doc;
    QUndoStack undo;
    SubPath sub;
    sub.nodes = {PathNode::corner({0, 0}), PathNode::corner({100, 0}), PathNode::corner({100, 50})};
    Shape *curve = doc.insertShape(-1, std::make_unique<Shape>(PathData{sub}, Style{}));
    undo.push(new SetEnvelopeCommand(&doc, curve, arch()));
    QVERIFY(curve->envelope());
    curve->transform(QTransform().rotate(90));
    const QPainterPath turnedPlain = QTransform().rotate(90).map(Shape(PathData{sub}, Style{}).path());
    undo.push(new SetEnvelopeCommand(&doc, curve, std::nullopt));
    QVERIFY(!curve->envelope());
    QVERIFY(curve->shapeTransform().isIdentity());
    QVERIFY(nearRect(curve->bounds(), turnedPlain.boundingRect(), 0.01));
    undo.undo();
    QVERIFY(curve->envelope());
    QVERIFY(!curve->shapeTransform().isIdentity());
    undo.undo();
    QVERIFY(!curve->envelope());
}

void EnvelopeTests::arrowheadsFollowTheBend()
{
    SubPath sub;
    sub.nodes = {PathNode::corner({0, 0}), PathNode::corner({100, 100})};
    Style style;
    style.endArrow = Arrowhead::Triangle;
    Shape line(PathData{sub}, style);
    Envelope e = Envelope::identity();
    for (int k : {5, 6, 7})
        e.points[k] += QPointF(1.0, 0.0); // bottom-right corner pulled far right
    line.setEnvelope(e);
    const QRectF arrow = render::arrowheads(line).boundingRect();
    QVERIFY(arrow.left() > 180); // at the moved end of the line, not at (100, 100)
}

void EnvelopeTests::envelopeSurvivesSvg()
{
    Document doc;
    Style style;
    style.fill = Qt::red;
    Shape *rect = doc.insertShape(-1, std::make_unique<Shape>(ShapeKind::Rectangle, QRectF(10, 10, 90, 60), style));
    rect->setEnvelope(arch());
    SubPath sub;
    sub.nodes = {PathNode::corner({200, 0}), PathNode::corner({300, 0}), PathNode::corner({300, 50})};
    Shape *curve = doc.insertShape(-1, std::make_unique<Shape>(PathData{sub}, Style{}));
    curve->setEnvelope(arch());
    curve->transform(QTransform().rotate(20));
    TextData data{QStringLiteral("Bend"), {}};
    Shape *text = doc.insertShape(-1, std::make_unique<Shape>(data, QPointF(20, 200), style));
    text->setEnvelope(arch());
    const QRectF rectBounds = rect->bounds(), curveBounds = curve->bounds(), textBounds = text->bounds();

    QByteArray written;
    auto copy = roundTrip(doc, &written);
    QVERIFY(written.contains("opendraw:envelope="));
    QVERIFY(written.contains("display=\"none\""));
    QCOMPARE(copy->count(), 3);
    const auto &shapes = copy->shapes();
    QCOMPARE(shapes[0]->kind(), ShapeKind::Rectangle); // still a rectangle
    QVERIFY(shapes[0]->envelope());
    for (int i = 0; i < 12; ++i) {
        QVERIFY(near(shapes[0]->envelope()->points[i].x(), rect->envelope()->points[i].x(), 1e-8));
        QVERIFY(near(shapes[0]->envelope()->points[i].y(), rect->envelope()->points[i].y(), 1e-8));
    }
    QVERIFY(nearRect(shapes[0]->bounds(), rectBounds, 0.01));
    QCOMPARE(shapes[1]->kind(), ShapeKind::Path);
    QVERIFY(!shapes[1]->shapeTransform().isIdentity());
    QVERIFY(nearRect(shapes[1]->bounds(), curveBounds, 0.01));
    QCOMPARE(shapes[2]->kind(), ShapeKind::Text);
    QCOMPARE(shapes[2]->textData().text, QStringLiteral("Bend"));
    QVERIFY(nearRect(shapes[2]->bounds(), textBounds, 0.5));
}

QTEST_MAIN(EnvelopeTests)
#include "test_envelope.moc"
