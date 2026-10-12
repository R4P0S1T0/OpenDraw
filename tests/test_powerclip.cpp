#include "core/Commands.h"
#include "core/Document.h"
#include "core/Export.h"
#include "core/SvgIO.h"

#include <QBuffer>
#include <QUndoStack>
#include <QtTest>

using namespace opendraw;

namespace {

Style solid(const QColor &fill, const QColor &stroke = Qt::transparent, double width = 1.0)
{
    Style style;
    style.fill = fill;
    style.stroke = stroke;
    style.strokeWidth = width;
    return style;
}

QImage render(const Document &doc)
{
    exporter::ImageOptions options;
    options.dpi = 96;
    return exporter::renderImage(doc, options);
}

std::unique_ptr<Document> roundTrip(const QByteArray &data)
{
    QByteArray copy = data;
    QBuffer in(&copy);
    in.open(QIODevice::ReadOnly);
    auto doc = std::make_unique<Document>();
    svg::read(&in, *doc);
    return doc;
}

QByteArray written(const Document &doc)
{
    QByteArray data;
    QBuffer out(&data);
    out.open(QIODevice::WriteOnly);
    svg::write(doc, &out);
    return data;
}

} // namespace

class PowerClipTests : public QObject {
    Q_OBJECT

private slots:
    void contentsShowOnlyInsideTheFrame();
    void contentsFollowTheFrame();
    void placeAndExtractUndo();
    void powerClipSurvivesSvg();
    void readsClipPathsFromOtherPrograms();
};

// A circle frame with a white fill and a black outline, holding a red square larger than it.
void PowerClipTests::contentsShowOnlyInsideTheFrame()
{
    Document doc;
    doc.setPageSize(QSizeF(200, 200));
    Shape *frame = doc.insertShape(-1, std::make_unique<Shape>(ShapeKind::Ellipse, QRectF(50, 50, 100, 100),
                                                               solid(Qt::white, Qt::black, 6)));
    std::vector<std::unique_ptr<Shape>> contents;
    contents.push_back(std::make_unique<Shape>(ShapeKind::Rectangle, QRectF(0, 0, 120, 200), solid(Qt::red)));
    frame->setClippedContents(std::move(contents));
    const QImage image = render(doc);
    QCOMPARE(image.pixelColor(100, 100), QColor(Qt::red));   // inside the circle and the square
    QCOMPARE(image.pixelColor(140, 100), QColor(Qt::white)); // inside the circle, beyond the square
    QCOMPARE(image.pixelColor(20, 20), QColor(Qt::white));   // the square outside the circle: hidden
    QCOMPARE(image.pixelColor(51, 100), QColor(Qt::black));  // the outline stays on top
    // The frame is clicked anywhere inside; its bounds are its own.
    QVERIFY(frame->contains(QPointF(100, 100), 0.5));
    QCOMPARE(frame->bounds(), QRectF(50, 50, 100, 100));
}

void PowerClipTests::contentsFollowTheFrame()
{
    Shape frame(ShapeKind::Rectangle, QRectF(0, 0, 100, 100), solid(Qt::white));
    std::vector<std::unique_ptr<Shape>> contents;
    contents.push_back(std::make_unique<Shape>(ShapeKind::Ellipse, QRectF(10, 10, 20, 20), solid(Qt::red)));
    frame.setClippedContents(std::move(contents));
    const Shape &inside = *frame.clippedContents().front();
    frame.moveBy(QPointF(50, 0));
    QCOMPARE(inside.bounds(), QRectF(60, 10, 20, 20));
    frame.setBounds(QRectF(50, 0, 200, 100)); // twice as wide
    QCOMPARE(inside.bounds(), QRectF(70, 10, 40, 20));
    // Turned there and back: the contents end where they were.
    frame.transform(QTransform().rotate(30));
    frame.transform(QTransform().rotate(-30));
    QVERIFY(std::abs(inside.bounds().left() - 70) < 1e-6);
    QVERIFY(frame.shapeTransform().isIdentity());
}

void PowerClipTests::placeAndExtractUndo()
{
    Document doc;
    QUndoStack undo;
    Shape *a = doc.insertShape(-1, std::make_unique<Shape>(ShapeKind::Rectangle, QRectF(0, 0, 10, 10)));
    Shape *frame = doc.insertShape(-1, std::make_unique<Shape>(ShapeKind::Ellipse, QRectF(0, 0, 50, 50)));
    Shape *b = doc.insertShape(-1, std::make_unique<Shape>(ShapeKind::Rectangle, QRectF(20, 0, 10, 10)));
    undo.push(new PowerClipCommand(&doc, frame, {b, a}));
    QCOMPARE(doc.count(), 1);
    QCOMPARE(frame->clippedContents().size(), std::size_t(2));
    QCOMPARE(frame->clippedContents()[0].get(), a); // stacking order kept
    undo.undo();
    QCOMPARE(doc.count(), 3);
    QCOMPARE(doc.indexOf(a), 0);
    QCOMPARE(doc.indexOf(frame), 1);
    QCOMPARE(doc.indexOf(b), 2);
    undo.redo();
    undo.push(new ExtractContentsCommand(&doc, frame));
    QCOMPARE(doc.count(), 3);
    QVERIFY(frame->clippedContents().empty());
    QCOMPARE(doc.indexOf(frame), 0); // the contents land above the frame
    undo.undo();
    QCOMPARE(doc.count(), 1);
    QCOMPARE(frame->clippedContents().size(), std::size_t(2));
}

void PowerClipTests::powerClipSurvivesSvg()
{
    Document doc;
    Shape *frame = doc.insertShape(-1, std::make_unique<Shape>(ShapeKind::Ellipse, QRectF(0, 0, 100, 60),
                                                               solid(Qt::yellow, Qt::blue, 2)));
    std::vector<std::unique_ptr<Shape>> contents;
    contents.push_back(std::make_unique<Shape>(ShapeKind::Rectangle, QRectF(-20, 10, 80, 30), solid(Qt::red)));
    TextData words{QStringLiteral("inside"), {}};
    contents.push_back(std::make_unique<Shape>(words, QPointF(10, 40), solid(Qt::black)));
    frame->setClippedContents(std::move(contents));
    const QByteArray data = written(doc);
    QVERIFY(data.contains("<clipPath"));
    QVERIFY(data.contains("clip-path=\"url(#"));
    auto copy = roundTrip(data);
    QCOMPARE(copy->count(), 1);
    const Shape &read = *copy->shapes().front();
    QCOMPARE(read.kind(), ShapeKind::Ellipse);
    QCOMPARE(read.style().fill, QColor(Qt::yellow));
    QCOMPARE(read.clippedContents().size(), std::size_t(2));
    QCOMPARE(read.clippedContents()[0]->bounds(), QRectF(-20, 10, 80, 30));
    QCOMPARE(read.clippedContents()[1]->kind(), ShapeKind::Text);
}

void PowerClipTests::readsClipPathsFromOtherPrograms()
{
    // A group clipped by a circle and a bitmap-like rectangle clipped by a moved rectangle.
    const QByteArray other(R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="200" height="200">
        <defs>
          <clipPath id="round"><circle cx="100" cy="100" r="50"/></clipPath>
          <clipPath id="box"><rect x="0" y="0" width="20" height="20" transform="translate(10,10)"/></clipPath>
        </defs>
        <g clip-path="url(#round)"><rect width="200" height="200" fill="red"/><rect width="10" height="10"/></g>
        <rect width="100" height="100" fill="blue" clip-path="url(#box)"/>
        <rect width="5" height="5" clip-path="url(#nowhere)"/>
        </svg>)svg");
    QByteArray copy = other;
    QBuffer in(&copy);
    in.open(QIODevice::ReadOnly);
    Document doc;
    svg::ReadReport report;
    QVERIFY(svg::read(&in, doc, nullptr, &report));
    QCOMPARE(doc.count(), 3);
    const Shape &round = *doc.shapes()[0];
    QCOMPARE(round.clippedContents().size(), std::size_t(2));
    QVERIFY(std::abs(round.bounds().width() - 100) < 0.5);
    QVERIFY(!round.style().hasFill() && !round.style().hasStroke());
    const Shape &box = *doc.shapes()[1];
    QCOMPARE(box.bounds(), QRectF(10, 10, 20, 20));
    QCOMPARE(box.clippedContents().size(), std::size_t(1));
    QCOMPARE(report.skippedCount(), 1); // the missing clip path
    doc.setPageSize(QSizeF(200, 200));
    const QImage image = render(doc);
    QCOMPARE(image.pixelColor(100, 100), QColor(Qt::red));
    QCOMPARE(image.pixelColor(170, 170), QColor(Qt::white)); // clipped away
    QCOMPARE(image.pixelColor(20, 20), QColor(Qt::blue));
    QCOMPARE(image.pixelColor(40, 40), QColor(Qt::white));
}

QTEST_MAIN(PowerClipTests)
#include "test_powerclip.moc"
