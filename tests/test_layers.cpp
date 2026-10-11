#include "core/Commands.h"
#include "core/Document.h"
#include "core/Export.h"
#include "core/SvgIO.h"

#include <QBuffer>
#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QUndoStack>
#include <QtTest>

using namespace opendraw;

namespace {

std::unique_ptr<Shape> square(double x)
{
    return std::make_unique<Shape>(ShapeKind::Rectangle, QRectF(x, 0, 10, 10));
}

} // namespace

class LayerTests : public QObject {
    Q_OBJECT

private slots:
    void newDocumentHasOnePageAndLayer();
    void addLayerBecomesActive();
    void hiddenAndLockedLayersAreNotSelectable();
    void removeLayerAndUndo();
    void moveShapesBetweenLayers();
    void groupAcrossLayers();
    void pagesAddRemoveRename();
    void svgKeepsPagesAndLayers();
    void inkscapeLayersBecomeLayers();
    void pdfHasOnePagePerPage();
};

void LayerTests::newDocumentHasOnePageAndLayer()
{
    Document doc;
    QCOMPARE(doc.pageCount(), 1);
    QCOMPARE(doc.currentPage()->name, QStringLiteral("Page 1"));
    QCOMPARE(doc.currentPage()->layers.size(), std::size_t(1));
    QCOMPARE(doc.activeLayer()->name, QStringLiteral("Layer 1"));
}

void LayerTests::addLayerBecomesActive()
{
    Document doc;
    QUndoStack stack;
    Layer *first = doc.activeLayer();
    auto *add = new AddLayerCommand(&doc, doc.currentPage());
    Layer *second = add->layer();
    stack.push(add);
    QCOMPARE(doc.activeLayer(), second);
    QCOMPARE(second->name, QStringLiteral("Layer 2"));
    QCOMPARE(doc.indexOfLayer(second), 1); // above the first

    stack.push(new AddShapeCommand(&doc, square(0)));
    QCOMPARE(second->shapes.size(), std::size_t(1));
    QCOMPARE(first->shapes.size(), std::size_t(0));

    stack.undo();
    stack.undo();
    QCOMPARE(doc.activeLayer(), first);
    QCOMPARE(doc.currentPage()->layers.size(), std::size_t(1));
}

void LayerTests::hiddenAndLockedLayersAreNotSelectable()
{
    Document doc;
    QUndoStack stack;
    Shape *bottom = doc.insertShape(-1, square(0));
    auto *add = new AddLayerCommand(&doc, doc.currentPage());
    Layer *top = add->layer();
    stack.push(add);
    Shape *above = doc.insertShape(-1, square(0)); // same place, upper layer

    QCOMPARE(doc.shapeAt({5, 5}, 0.5), above);
    stack.push(new LayerPropertiesCommand(&doc, top, top->name, false, false, QStringLiteral("Hide")));
    QCOMPARE(doc.shapeAt({5, 5}, 0.5), bottom);
    QCOMPARE(doc.editableShapes(), std::vector<Shape *>{bottom});

    stack.undo();
    stack.push(new LayerPropertiesCommand(&doc, top, top->name, true, true, QStringLiteral("Lock")));
    QCOMPARE(doc.shapeAt({5, 5}, 0.5), bottom);
    stack.undo();
    QCOMPARE(doc.shapeAt({5, 5}, 0.5), above);
}

void LayerTests::removeLayerAndUndo()
{
    Document doc;
    QUndoStack stack;
    QVERIFY(!doc.takeLayer(doc.activeLayer())); // the last layer stays

    auto *add = new AddLayerCommand(&doc, doc.currentPage());
    Layer *layer = add->layer();
    stack.push(add);
    Shape *shape = doc.insertShape(-1, square(0));

    stack.push(new RemoveLayerCommand(&doc, layer));
    QCOMPARE(doc.currentPage()->layers.size(), std::size_t(1));
    QVERIFY(!doc.layerOf(shape));
    stack.undo();
    QCOMPARE(doc.layerOf(shape), layer);
    QCOMPARE(doc.activeLayer(), layer);
}

void LayerTests::moveShapesBetweenLayers()
{
    Document doc;
    QUndoStack stack;
    Layer *first = doc.activeLayer();
    Shape *a = doc.insertShape(-1, square(0));
    Shape *b = doc.insertShape(-1, square(20));
    auto *add = new AddLayerCommand(&doc, doc.currentPage());
    Layer *second = add->layer();
    stack.push(add);

    stack.push(new MoveToLayerCommand(&doc, a, second));
    QCOMPARE(doc.layerOf(a), second);
    QCOMPARE(doc.order(first), std::vector<Shape *>{b});
    stack.undo();
    QCOMPARE(doc.order(first), (std::vector<Shape *>{a, b}));

    stack.push(new ReorderCommand(&doc, first, {b, a}, QStringLiteral("Reorder")));
    QCOMPARE(doc.order(first), (std::vector<Shape *>{b, a}));
}

void LayerTests::groupAcrossLayers()
{
    Document doc;
    QUndoStack stack;
    Layer *first = doc.activeLayer();
    Shape *low = doc.insertShape(-1, square(0));
    auto *add = new AddLayerCommand(&doc, doc.currentPage());
    Layer *second = add->layer();
    stack.push(add);
    Shape *high = doc.insertShape(-1, square(20));

    auto *group = new GroupCommand(&doc, {high, low});
    Shape *g = group->group();
    stack.push(group);
    QCOMPARE(doc.layerOf(g), second); // where the topmost shape was
    QVERIFY(first->shapes.empty());

    stack.undo();
    QCOMPARE(doc.layerOf(low), first);
    QCOMPARE(doc.layerOf(high), second);
}

void LayerTests::pagesAddRemoveRename()
{
    Document doc;
    QUndoStack stack;
    Shape *onFirst = doc.insertShape(-1, square(0));

    stack.push(new AddPageCommand(&doc, 1));
    QCOMPARE(doc.pageCount(), 2);
    QCOMPARE(doc.currentPageIndex(), 1);
    QCOMPARE(doc.currentPage()->name, QStringLiteral("Page 2"));
    QVERIFY(doc.editableShapes().empty());
    Shape *onSecond = doc.insertShape(-1, square(0));
    QCOMPARE(doc.pageOf(doc.layerOf(onSecond)), doc.pages()[1].get());

    stack.push(new RenamePageCommand(&doc, doc.currentPage(), QStringLiteral("Back cover")));
    QCOMPARE(doc.currentPage()->name, QStringLiteral("Back cover"));

    stack.push(new RemovePageCommand(&doc, doc.currentPage()));
    QCOMPARE(doc.pageCount(), 1);
    QCOMPARE(doc.currentPageIndex(), 0);
    QCOMPARE(doc.editableShapes(), std::vector<Shape *>{onFirst});

    stack.undo();
    QCOMPARE(doc.pageCount(), 2);
    QCOMPARE(doc.currentPage()->name, QStringLiteral("Back cover"));
}

void LayerTests::svgKeepsPagesAndLayers()
{
    Document original;
    QUndoStack stack;
    original.activeLayer()->name = QStringLiteral("Background");
    original.insertShape(-1, square(0));
    auto *add = new AddLayerCommand(&original, original.currentPage());
    stack.push(add);
    add->layer()->visible = false;
    add->layer()->locked = true;
    original.insertShape(-1, square(20));
    stack.push(new AddPageCommand(&original, 1));
    original.currentPage()->name = QStringLiteral("Second");
    original.insertShape(-1, square(40));
    original.insertShape(-1, square(60));

    QBuffer buffer;
    QVERIFY(buffer.open(QIODevice::ReadWrite));
    QVERIFY(svg::write(original, &buffer));
    buffer.seek(0);

    Document loaded;
    svg::ReadReport report;
    QVERIFY(svg::read(&buffer, loaded, nullptr, &report));
    QCOMPARE(report.formatVersion, 2);
    QCOMPARE(loaded.pageCount(), 2);
    const Page &first = *loaded.pages()[0];
    QCOMPARE(first.name, QStringLiteral("Page 1"));
    QCOMPARE(first.layers.size(), std::size_t(2));
    QCOMPARE(first.layers[0]->name, QStringLiteral("Background"));
    QCOMPARE(first.layers[0]->shapes.size(), std::size_t(1));
    QCOMPARE(first.layers[1]->name, QStringLiteral("Layer 2"));
    QVERIFY(!first.layers[1]->visible);
    QVERIFY(first.layers[1]->locked);
    QCOMPARE(first.layers[1]->shapes.size(), std::size_t(1));
    const Page &second = *loaded.pages()[1];
    QCOMPARE(second.name, QStringLiteral("Second"));
    QCOMPARE(second.layers.size(), std::size_t(1));
    QCOMPARE(second.layers[0]->shapes.size(), std::size_t(2));
}

void LayerTests::inkscapeLayersBecomeLayers()
{
    QByteArray data = R"svg(<svg xmlns="http://www.w3.org/2000/svg"
            xmlns:inkscape="http://www.inkscape.org/namespaces/inkscape"
            xmlns:sodipodi="http://sodipodi.sourceforge.net/DTD/sodipodi-0.dtd" viewBox="0 0 100 100">
        <g inkscape:groupmode="layer" inkscape:label="Sketch" style="display:none">
            <rect width="5" height="5"/>
        </g>
        <g inkscape:groupmode="layer" inkscape:label="Ink" sodipodi:insensitive="true">
            <g inkscape:groupmode="layer" inkscape:label="Sublayer"><rect width="5" height="5"/></g>
            <rect width="5" height="5" style="display:none"/>
        </g>
    </svg>)svg";
    QBuffer buffer(&data);
    QVERIFY(buffer.open(QIODevice::ReadOnly));

    Document doc;
    QVERIFY(svg::read(&buffer, doc));
    const Page &page = *doc.currentPage();
    QCOMPARE(page.layers.size(), std::size_t(2));
    QCOMPARE(page.layers[0]->name, QStringLiteral("Sketch"));
    QVERIFY(!page.layers[0]->visible);
    QCOMPARE(page.layers[0]->shapes.size(), std::size_t(1));
    QCOMPARE(page.layers[1]->name, QStringLiteral("Ink"));
    QVERIFY(page.layers[1]->locked);
    // The sublayer becomes a group; the hidden rectangle is not drawn, so not imported.
    QCOMPARE(page.layers[1]->shapes.size(), std::size_t(1));
    QCOMPARE(page.layers[1]->shapes[0]->kind(), ShapeKind::Group);
}

void LayerTests::pdfHasOnePagePerPage()
{
    Document doc;
    QUndoStack stack;
    doc.insertShape(-1, square(0));
    stack.push(new AddPageCommand(&doc, 1));
    stack.push(new AddPageCommand(&doc, 2));

    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("pages.pdf"));
    QVERIFY(exporter::writePdf(doc, path));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QString pdf = QString::fromLatin1(file.readAll());
    static const QRegularExpression pageObject(QStringLiteral(R"(/Type\s*/Page(?!s))"));
    QCOMPARE(int(pdf.count(pageObject)), 3);
}

QTEST_MAIN(LayerTests)
#include "test_layers.moc"
