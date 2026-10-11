#include "core/CdrImport.h"
#include "core/Document.h"
#include "core/Export.h"

#include <QDirIterator>
#include <QFile>
#include <QtTest>

using namespace opendraw;

namespace {

int countShapes(const Document &doc)
{
    int n = 0;
    for (const auto &page : doc.pages()) {
        for (const auto &layer : page->layers)
            n += int(layer->shapes.size());
    }
    return n;
}

} // namespace

class CdrTests : public QObject {
    Q_OBJECT

private slots:
    void rejectsOtherFiles();
    void importsSampleFiles();
};

void CdrTests::rejectsOtherFiles()
{
    if (!cdr::isAvailable())
        QSKIP("Built without libcdr");
    Document doc;
    doc.insertShape(-1, std::make_unique<Shape>(ShapeKind::Rectangle, QRectF(0, 0, 1, 1)));
    QString error;
    QVERIFY(!cdr::read(QByteArray("this is not a drawing"), doc, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(doc.count(), 1); // untouched
}

// CorelDRAW files cannot be shipped with OpenDraw (they belong to their authors), so this test
// runs on a folder you point it to: OPENDRAW_CDR_SAMPLES=/path/to/cdr/files.
void CdrTests::importsSampleFiles()
{
    if (!cdr::isAvailable())
        QSKIP("Built without libcdr");
    const QString dir = qEnvironmentVariable("OPENDRAW_CDR_SAMPLES");
    if (dir.isEmpty())
        QSKIP("Set OPENDRAW_CDR_SAMPLES to a folder of .cdr files");

    int imported = 0;
    int empty = 0;
    QStringList failed;
    QDirIterator it(dir, {QStringLiteral("*.cdr"), QStringLiteral("*.cdt")}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        Document doc;
        QString error;
        if (!cdr::read(file.readAll(), doc, &error)) {
            failed << QStringLiteral("%1: %2").arg(QFileInfo(path).fileName(), error);
            continue;
        }
        ++imported;
        if (countShapes(doc) == 0)
            ++empty;
        // Optional pictures for comparing with CorelDRAW's own thumbnails.
        const QString shots = qEnvironmentVariable("OPENDRAW_SCREENSHOT_DIR");
        if (!shots.isEmpty() && imported <= 200) {
            exporter::ImageOptions options;
            options.dpi = 24;
            exporter::renderImage(doc, options).save(shots + QLatin1Char('/') + QFileInfo(path).completeBaseName()
                                                      + QStringLiteral(".png"));
        }
    }
    qInfo("Imported %d files (%d without any shapes), %lld failed", imported, empty, qsizetype(failed.size()));
    for (const QString &f : std::as_const(failed))
        qInfo("  failed: %s", qPrintable(f));
    QVERIFY(imported > 0);
}

QTEST_MAIN(CdrTests)
#include "test_cdr.moc"
