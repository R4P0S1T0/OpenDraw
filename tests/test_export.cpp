#include "core/Document.h"
#include "core/Export.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace opendraw;

class ExportTests : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void pngMatchesPageAtChosenDpi();
    void pngPixelsShowTheDrawing();
    void transparentBackground();
    void exportOnlySomeShapesOfAnArea();
    void tooLargeImageIsRefused();
    void pdfIsWritten();

private:
    Document *m_doc = nullptr;
    Shape *m_red = nullptr;
    Shape *m_blue = nullptr;
};

void ExportTests::init()
{
    m_doc = new Document;
    m_doc->setPageSize({960, 480}); // 10 x 5 inches
    Style red{QColor(255, 0, 0), QColor(Qt::transparent), 0};
    Style blue{QColor(0, 0, 255), QColor(Qt::transparent), 0};
    m_red = m_doc->insertShape(-1, std::make_unique<Shape>(ShapeKind::Rectangle, QRectF(0, 0, 96, 96), red));
    m_blue = m_doc->insertShape(-1, std::make_unique<Shape>(ShapeKind::Ellipse, QRectF(480, 192, 96, 96), blue));
}

void ExportTests::cleanup()
{
    delete m_doc;
}

void ExportTests::pngMatchesPageAtChosenDpi()
{
    exporter::ImageOptions options;
    options.dpi = 72;
    QCOMPARE(exporter::imageSize(*m_doc, options), QSize(720, 360));

    options.dpi = 300;
    const QImage image = exporter::renderImage(*m_doc, options);
    QCOMPARE(image.size(), QSize(3000, 1500));
    QCOMPARE(qRound(image.dotsPerMeterX() * 0.0254), 300);
}

void ExportTests::pngPixelsShowTheDrawing()
{
    exporter::ImageOptions options;
    options.dpi = 96; // one pixel per document unit
    const QImage image = exporter::renderImage(*m_doc, options);
    QCOMPARE(image.pixelColor(48, 48), QColor(255, 0, 0));    // inside the red square
    QCOMPARE(image.pixelColor(528, 240), QColor(0, 0, 255));  // centre of the blue circle
    QCOMPARE(image.pixelColor(300, 300), QColor(Qt::white));  // empty page
}

void ExportTests::transparentBackground()
{
    exporter::ImageOptions options;
    options.dpi = 96;
    options.transparent = true;
    const QImage image = exporter::renderImage(*m_doc, options);
    QCOMPARE(image.pixelColor(300, 300).alpha(), 0);
    QCOMPARE(image.pixelColor(48, 48).alpha(), 255);
}

void ExportTests::exportOnlySomeShapesOfAnArea()
{
    exporter::ImageOptions options;
    options.dpi = 96;
    options.area = m_blue->bounds();
    options.shapes = {m_blue};
    const QImage image = exporter::renderImage(*m_doc, options);
    QCOMPARE(image.size(), QSize(96, 96));
    QCOMPARE(image.pixelColor(48, 48), QColor(0, 0, 255));
    QCOMPARE(image.pixelColor(1, 1), QColor(Qt::white)); // corner outside the circle
}

void ExportTests::tooLargeImageIsRefused()
{
    exporter::ImageOptions options;
    options.dpi = 2400; // 24000 x 12000 would fit, so make the page bigger
    m_doc->setPageSize({2000, 1000});
    QVERIFY(exporter::imageSize(*m_doc, options).width() > exporter::kMaxImageSide);
    QVERIFY(exporter::renderImage(*m_doc, options).isNull());
}

void ExportTests::pdfIsWritten()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("drawing.pdf"));
    QString error;
    QVERIFY2(exporter::writePdf(*m_doc, path, &error), qPrintable(error));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray data = file.readAll();
    QVERIFY(data.startsWith("%PDF-"));

    // Page size in points: 10 x 5 inches = 720 x 360.
    static const QRegularExpression mediaBox(QStringLiteral(R"(/MediaBox\s*\[\s*([-\d.]+)\s+([-\d.]+)\s+([-\d.]+)\s+([-\d.]+)\s*\])"));
    const QRegularExpressionMatch match = mediaBox.match(QString::fromLatin1(data));
    QVERIFY2(match.hasMatch(), "no MediaBox in the PDF");
    const double width = match.captured(3).toDouble() - match.captured(1).toDouble();
    const double height = match.captured(4).toDouble() - match.captured(2).toDouble();
    QVERIFY2(qAbs(width - 720) < 1 && qAbs(height - 360) < 1, qPrintable(match.captured(0)));
}

QTEST_MAIN(ExportTests)
#include "test_export.moc"
