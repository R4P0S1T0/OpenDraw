#include "core/Document.h"
#include "core/Export.h"
#include "core/SvgIO.h"

#include <QBuffer>
#include <QtTest>

using namespace opendraw;

namespace {

bool closeTo(const QColor &a, const QColor &b, int tolerance = 16)
{
    return std::abs(a.red() - b.red()) <= tolerance && std::abs(a.green() - b.green()) <= tolerance
           && std::abs(a.blue() - b.blue()) <= tolerance;
}

QImage render(const Document &doc)
{
    exporter::ImageOptions options;
    options.dpi = 96; // one pixel per unit
    return exporter::renderImage(doc, options);
}

QColor pixel(const QImage &image, double x, double y)
{
    return image.pixelColor(int(x), int(y));
}

QByteArray writeSvg(const Document &doc)
{
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    svg::write(doc, &buffer);
    return data;
}

std::unique_ptr<Document> readSvg(QByteArray data, svg::ReadReport *report = nullptr)
{
    QBuffer buffer(&data);
    buffer.open(QIODevice::ReadOnly);
    auto doc = std::make_unique<Document>();
    svg::read(&buffer, *doc, nullptr, report);
    return doc;
}

// Red, green / blue, white.
QImage quadrants()
{
    QImage image(2, 2, QImage::Format_ARGB32);
    image.setPixelColor(0, 0, Qt::red);
    image.setPixelColor(1, 0, Qt::green);
    image.setPixelColor(0, 1, Qt::blue);
    image.setPixelColor(1, 1, Qt::white);
    return image;
}

QImage checker()
{
    QImage image(2, 2, QImage::Format_ARGB32);
    image.fill(Qt::white);
    image.setPixelColor(0, 0, Qt::black);
    image.setPixelColor(1, 1, Qt::black);
    return image;
}

// The bitmap at (10, 10), 40 × 20.
QTransform placement()
{
    return QTransform(40, 0, 0, 20, 10, 10);
}

void expectQuadrants(const QImage &image)
{
    // Sampled near the corners: smooth scaling blends the middle of a 2 × 2 bitmap.
    QVERIFY(closeTo(pixel(image, 11, 11), Qt::red));
    QVERIFY(closeTo(pixel(image, 48, 11), Qt::green));
    QVERIFY(closeTo(pixel(image, 11, 28), Qt::blue));
    QVERIFY(closeTo(pixel(image, 48, 28), Qt::white));
    QVERIFY(closeTo(pixel(image, 5, 5), Qt::white)); // page background outside the bitmap
}

} // namespace

class BitmapTests : public QObject {
    Q_OBJECT

private slots:
    void bitmapRendersWhereItIsPlaced();
    void bitmapRoundTrip();
    void bitmapFollowsTransforms();
    void patternTilesAndFollowsTheShape();
    void patternRoundTrip();
    void importsBmpWithAlpha();
    void viewBoxScalesToThePageSize();
};

void BitmapTests::bitmapRendersWhereItIsPlaced()
{
    Document doc;
    doc.setPageSize(QSizeF(60, 40));
    doc.insertShape(-1, std::make_unique<Shape>(quadrants(), placement()));
    QCOMPARE(doc.shapes().front()->bounds(), QRectF(10, 10, 40, 20));
    expectQuadrants(render(doc));
}

void BitmapTests::bitmapRoundTrip()
{
    Document doc;
    doc.setPageSize(QSizeF(60, 40));
    doc.insertShape(-1, std::make_unique<Shape>(quadrants(), placement()));
    const QByteArray data = writeSvg(doc);
    QVERIFY(data.contains("data:image/png;base64,"));

    svg::ReadReport report;
    auto copy = readSvg(data, &report);
    QCOMPARE(report.skippedCount(), 0);
    QCOMPARE(copy->count(), 1);
    const Shape &shape = *copy->shapes().front();
    QCOMPARE(shape.kind(), ShapeKind::Image);
    QCOMPARE(shape.image().size(), QSize(2, 2));
    QCOMPARE(shape.image().pixelColor(1, 0), QColor(Qt::green));
    QCOMPARE(shape.bounds(), QRectF(10, 10, 40, 20));
    expectQuadrants(render(*copy));
}

void BitmapTests::bitmapFollowsTransforms()
{
    Shape shape(quadrants(), placement());
    shape.transform(QTransform::fromTranslate(5, -5));
    QCOMPARE(shape.bounds(), QRectF(15, 5, 40, 20));
    shape.setBounds(QRectF(0, 0, 20, 40));
    QCOMPARE(shape.bounds(), QRectF(0, 0, 20, 40));

    // A mirrored bitmap shows its right column on the left.
    Document doc;
    doc.setPageSize(QSizeF(60, 40));
    auto mirrored = std::make_unique<Shape>(quadrants(), placement());
    mirrored->transform(QTransform(-1, 0, 0, 1, 60, 0));
    QCOMPARE(mirrored->bounds(), QRectF(10, 10, 40, 20));
    doc.insertShape(-1, std::move(mirrored));
    const QImage image = render(doc);
    QVERIFY(closeTo(pixel(image, 11, 11), Qt::green));
    QVERIFY(closeTo(pixel(image, 48, 11), Qt::red));
}

void BitmapTests::patternTilesAndFollowsTheShape()
{
    Style style;
    style.stroke = Qt::transparent;
    style.pattern = PatternFill{checker(), QSizeF(10, 10), QPointF(0, 0)};

    Document doc;
    doc.setPageSize(QSizeF(50, 40));
    doc.insertShape(-1, std::make_unique<Shape>(ShapeKind::Rectangle, QRectF(0, 0, 40, 40), style));
    QVERIFY(doc.shapes().front()->style().hasFill());
    QImage image = render(doc);
    QVERIFY(closeTo(pixel(image, 2.5, 2.5), Qt::black));
    QVERIFY(closeTo(pixel(image, 7.5, 2.5), Qt::white));
    QVERIFY(closeTo(pixel(image, 12.5, 2.5), Qt::black));
    QVERIFY(closeTo(pixel(image, 12.5, 17.5), Qt::white));

    // Moving the shape moves the texture with it.
    doc.shapes().front()->transform(QTransform::fromTranslate(5, 0));
    image = render(doc);
    QVERIFY(closeTo(pixel(image, 7.5, 2.5), Qt::black));
    QVERIFY(closeTo(pixel(image, 12.5, 2.5), Qt::white));

    // Scaling scales the tiles.
    Shape scaled(ShapeKind::Rectangle, QRectF(0, 0, 40, 40), style);
    scaled.setBounds(QRectF(0, 0, 80, 80));
    QVERIFY(scaled.style().pattern);
    QCOMPARE(scaled.style().pattern->tileSize, QSizeF(20, 20));
}

void BitmapTests::patternRoundTrip()
{
    Style style;
    style.stroke = Qt::transparent;
    style.pattern = PatternFill{checker(), QSizeF(10, 10), QPointF(0, 0)};
    Document doc;
    doc.setPageSize(QSizeF(50, 40));
    doc.insertShape(-1, std::make_unique<Shape>(ShapeKind::Rectangle, QRectF(5, 0, 40, 40), style));

    svg::ReadReport report;
    auto copy = readSvg(writeSvg(doc), &report);
    QCOMPARE(report.skippedCount(), 0);
    QCOMPARE(copy->count(), 1);
    const Style &read = copy->shapes().front()->style();
    QVERIFY(read.pattern);
    QCOMPARE(read.pattern->image.size(), QSize(2, 2));
    QCOMPARE(read.pattern->tileSize, QSizeF(10, 10));
    const QImage image = render(*copy);
    QVERIFY(closeTo(pixel(image, 7.5, 2.5), Qt::black));
    QVERIFY(closeTo(pixel(image, 12.5, 2.5), Qt::white));
}

void BitmapTests::importsBmpWithAlpha()
{
    // 2 × 1 uncompressed 32-bit BMP: an opaque red pixel and a fully transparent one.
    QByteArray bmp;
    QDataStream out(&bmp, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::LittleEndian);
    out.writeRawData("BM", 2);
    out << quint32(54 + 8) << quint32(0) << quint32(54);
    out << quint32(40) << qint32(2) << qint32(1) << quint16(1) << quint16(32) << quint32(0) << quint32(8)
        << qint32(2835) << qint32(2835) << quint32(0) << quint32(0);
    out << quint8(0) << quint8(0) << quint8(255) << quint8(255); // blue, green, red, alpha
    out << quint8(0) << quint8(255) << quint8(0) << quint8(0);
    const QByteArray svgData = QByteArrayLiteral(
        R"svg(<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="20" height="10">)svg"
        R"svg(<image x="0" y="0" width="20" height="10" xlink:href="data:image/bmp;base64,)svg")
                               + bmp.toBase64() + QByteArrayLiteral(R"svg("/></svg>)svg");
    auto doc = readSvg(svgData);
    QCOMPARE(doc->count(), 1);
    const QImage image = doc->shapes().front()->image();
    QCOMPARE(image.size(), QSize(2, 1));
    QCOMPARE(image.pixelColor(0, 0), QColor(255, 0, 0, 255));
    QCOMPARE(image.pixelColor(1, 0).alpha(), 0);
}

void BitmapTests::viewBoxScalesToThePageSize()
{
    // Two inches by one inch, drawn in points: the drawing must fill the page, not a corner of it.
    auto doc = readSvg(QByteArrayLiteral(
        R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="2in" height="1in" viewBox="0 0 144 72">
                <rect x="72" y="0" width="72" height="72" fill="red"/>
              </svg>)svg"));
    QCOMPARE(doc->pageSize(), QSizeF(192, 96));
    QCOMPARE(doc->count(), 1);
    const QRectF bounds = doc->shapes().front()->bounds();
    QVERIFY2(std::abs(bounds.x() - 96) < 1e-6 && std::abs(bounds.width() - 96) < 1e-6
                 && std::abs(bounds.height() - 96) < 1e-6,
             qPrintable(QStringLiteral("%1 %2 %3 %4").arg(bounds.x()).arg(bounds.y()).arg(bounds.width()).arg(bounds.height())));
}

QTEST_MAIN(BitmapTests)
#include "test_bitmaps.moc"
