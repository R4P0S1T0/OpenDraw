#include "core/Blend.h"
#include "core/Commands.h"
#include "core/Document.h"
#include "core/Export.h"
#include "core/Generators.h"
#include "core/SvgIO.h"

#include <QBuffer>
#include <QUndoStack>
#include <QtTest>

using namespace opendraw;

namespace {

Style filled(const QColor &color)
{
    Style style;
    style.fill = color;
    style.stroke = Qt::transparent;
    return style;
}

bool near(double a, double b, double tolerance = 0.01)
{
    return std::abs(a - b) <= tolerance;
}

// A blend of a small red square on the left and a larger blue one on the right.
std::unique_ptr<Shape> squaresBlend(int steps)
{
    std::vector<std::unique_ptr<Shape>> ends;
    ends.push_back(std::make_unique<Shape>(ShapeKind::Rectangle, QRectF(0, 0, 20, 20), filled(Qt::red)));
    ends.push_back(std::make_unique<Shape>(ShapeKind::Rectangle, QRectF(200, 0, 40, 40), filled(Qt::blue)));
    auto blend = std::make_unique<Shape>(std::move(ends));
    blend->setBlendSteps(steps);
    return blend;
}

} // namespace

class BlendTests : public QObject {
    Q_OBJECT

private slots:
    void stepsChangeShapeAndColour();
    void differentShapesBlend();
    void blendFollowsItsEnds();
    void blendIsDrawnAndClickable();
    void blendSurvivesSvg();
    void stepsAndBreakingApartUndo();
};

void BlendTests::stepsChangeShapeAndColour()
{
    const auto blend = squaresBlend(3);
    QVERIFY(blend->isBlend());
    const auto steps = blend->blendIntermediates();
    QCOMPARE(steps.size(), std::size_t(3));
    // The middle step is halfway in position, size and colour.
    const QRectF middle = steps[1]->bounds();
    QVERIFY(near(middle.left(), 100) && near(middle.width(), 30) && near(middle.height(), 30));
    const QColor colour = steps[1]->style().fill;
    QVERIFY(std::abs(colour.red() - 128) <= 1 && colour.green() == 0 && std::abs(colour.blue() - 128) <= 1);
    // Evenly spaced from the start to the end.
    QVERIFY(near(steps[0]->bounds().left(), 50) && near(steps[2]->bounds().left(), 150));
}

void BlendTests::differentShapesBlend()
{
    // Different node counts, kinds and subpath counts all give whole shapes in between.
    const Shape star(generators::star(QRectF(0, 0, 100, 100), 5, 50), filled(Qt::yellow));
    const Shape ellipse(ShapeKind::Ellipse, QRectF(300, 0, 100, 60), filled(Qt::green));
    const Shape triangle(generators::polygon(QRectF(0, 200, 80, 80), 3), filled(Qt::black));
    PathData twoParts = generators::polygon(QRectF(300, 200, 50, 50), 4);
    const PathData second = generators::polygon(QRectF(370, 200, 50, 50), 6);
    twoParts.insert(twoParts.end(), second.begin(), second.end());
    const Shape pair(twoParts, filled(Qt::black));
    TextData text{QStringLiteral("Hi"), {}};
    const Shape word(text, QPointF(0, 400), filled(Qt::black));

    for (const auto &[a, b] : {std::pair{&star, &ellipse}, std::pair{&triangle, &pair}, std::pair{&word, &ellipse}}) {
        const auto steps = blend::intermediates(*a, *b, 5);
        QCOMPARE(steps.size(), std::size_t(5));
        for (const auto &step : steps) {
            QVERIFY(!step->path().isEmpty());
            QVERIFY(step->bounds().width() > 1.0);
        }
    }
    // Two closed subpaths running opposite ways still blend without turning inside out:
    // the middle of two equal squares is that square.
    PathData clockwise = generators::polygon(QRectF(0, 0, 100, 100), 4);
    PathData counter = clockwise;
    std::reverse(counter.front().nodes.begin(), counter.front().nodes.end());
    const Shape square(clockwise, Style{});
    const auto same = blend::intermediates(square, Shape(counter, Style{}), 1);
    QVERIFY(near(same.front()->bounds().width(), square.bounds().width()));
    QVERIFY(near(same.front()->bounds().height(), square.bounds().height()));

    // Groups and bitmaps cannot blend.
    std::vector<std::unique_ptr<Shape>> none;
    QVERIFY(!blend::canBlend(Shape(std::move(none))));
    QVERIFY(blend::canBlend(star));
}

void BlendTests::blendFollowsItsEnds()
{
    auto blend = squaresBlend(1);
    // Moving the whole blend moves the steps; changing an end changes them.
    blend->moveBy(QPointF(0, 100));
    QVERIFY(near(blend->blendIntermediates().front()->bounds().top(), 100));
    Style green = filled(Qt::green);
    blend->children()[1]->setStyle(green);
    const QColor mid = blend->blendIntermediates().front()->style().fill;
    QVERIFY(std::abs(mid.red() - 128) <= 1 && std::abs(mid.green() - 128) <= 1 && mid.blue() == 0);
    // A plain group of two is not a blend.
    blend->setBlendSteps(0);
    QVERIFY(!blend->isBlend());
    QVERIFY(blend->blendIntermediates().empty());
}

void BlendTests::blendIsDrawnAndClickable()
{
    Document doc;
    doc.setPageSize(QSizeF(260, 60));
    Shape *blend = doc.insertShape(-1, squaresBlend(3));
    // Halfway between the ends: only a step is there.
    QVERIFY(blend->contains(QPointF(115, 15), 0.5));
    QVERIFY(!blend->contains(QPointF(115, 55), 0.5));
    QCOMPARE(doc.shapeAt(QPointF(115, 15), 0.5), blend);
    exporter::ImageOptions options;
    options.dpi = 96;
    const QImage image = exporter::renderImage(doc, options);
    const QColor middle = image.pixelColor(115, 15);
    QVERIFY(middle.red() > 100 && middle.blue() > 100 && middle.green() < 30);
    QCOMPARE(image.pixelColor(115, 55), QColor(Qt::white));
}

void BlendTests::blendSurvivesSvg()
{
    Document doc;
    doc.insertShape(-1, squaresBlend(4));
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    QVERIFY(svg::write(doc, &buffer));
    QVERIFY(data.contains("opendraw:blend=\"4\""));
    QCOMPARE(data.count("<rect"), 2);  // the ends
    QCOMPARE(data.count("<path"), 4);  // the steps, for other programs
    buffer.close();
    buffer.open(QIODevice::ReadOnly);
    Document copy;
    QVERIFY(svg::read(&buffer, copy));
    QCOMPARE(copy.count(), 1);
    const Shape &read = *copy.shapes().front();
    QVERIFY(read.isBlend());
    QCOMPARE(read.blendSteps(), 4);
    QCOMPARE(read.children().size(), std::size_t(2));
    QCOMPARE(read.children()[1]->bounds(), QRectF(200, 0, 40, 40));
}

void BlendTests::stepsAndBreakingApartUndo()
{
    Document doc;
    QUndoStack undo;
    Shape *blend = doc.insertShape(-1, squaresBlend(5));
    undo.push(new SetBlendStepsCommand(&doc, blend, 2));
    QCOMPARE(blend->blendIntermediates().size(), std::size_t(2));
    undo.undo();
    QCOMPARE(blend->blendSteps(), 5);

    undo.push(new BreakBlendCommand(&doc, blend));
    QVERIFY(!blend->isBlend());
    QCOMPARE(blend->children().size(), std::size_t(3));
    QCOMPARE(blend->children()[1]->children().size(), std::size_t(5)); // the steps, now real shapes
    undo.undo();
    QVERIFY(blend->isBlend());
    QCOMPARE(blend->children().size(), std::size_t(2));
    undo.redo();
    QCOMPARE(blend->children().size(), std::size_t(3));
}

QTEST_MAIN(BlendTests)
#include "test_blend.moc"
