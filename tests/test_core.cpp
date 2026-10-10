#include "core/Commands.h"
#include "core/Document.h"
#include "core/SvgIO.h"

#include <QBuffer>
#include <QUndoStack>
#include <QtTest>

using namespace opendraw;

class CoreTests : public QObject {
    Q_OBJECT

private slots:
    void svgRoundTrip();
    void readsCircleUnitsAndStyleAttribute();
    void rejectsNonSvg();
    void shapeAtReturnsTopmost();
    void hitTestIgnoresEmptyFill();
    void undoRedoAddMoveRemove();
    void setStyleUndo();
};

void CoreTests::svgRoundTrip()
{
    Document original;
    original.setPageSize({500, 400});

    Style red;
    red.fill = QColor(255, 0, 0);
    red.stroke = Qt::transparent;
    original.insertShape(-1, std::make_unique<Shape>(ShapeKind::Rectangle, QRectF(10, 20, 100, 50), red));

    Style outline;
    outline.fill = Qt::transparent;
    outline.stroke = QColor(0, 128, 0);
    outline.strokeWidth = 3;
    original.insertShape(-1, std::make_unique<Shape>(ShapeKind::Ellipse, QRectF(200, 100, 80, 40), outline));

    QBuffer buffer;
    QVERIFY(buffer.open(QIODevice::ReadWrite));
    QVERIFY(svg::write(original, &buffer));
    buffer.seek(0);

    Document loaded;
    QString error;
    QVERIFY2(svg::read(&buffer, loaded, &error), qPrintable(error));

    QCOMPARE(loaded.pageSize(), QSizeF(500, 400));
    QCOMPARE(loaded.count(), 2);

    const Shape &rect = *loaded.shapes()[0];
    QCOMPARE(rect.kind(), ShapeKind::Rectangle);
    QCOMPARE(rect.bounds(), QRectF(10, 20, 100, 50));
    QCOMPARE(rect.style().fill, QColor(255, 0, 0));
    QVERIFY(!rect.style().hasStroke());

    const Shape &ellipse = *loaded.shapes()[1];
    QCOMPARE(ellipse.kind(), ShapeKind::Ellipse);
    QCOMPARE(ellipse.bounds(), QRectF(200, 100, 80, 40));
    QVERIFY(!ellipse.style().hasFill());
    QCOMPARE(ellipse.style().stroke, QColor(0, 128, 0));
    QCOMPARE(ellipse.style().strokeWidth, 3.0);
}

void CoreTests::readsCircleUnitsAndStyleAttribute()
{
    QByteArray data = R"(<svg xmlns="http://www.w3.org/2000/svg" width="100mm" height="50mm">
        <g><circle cx="10" cy="20" r="5" style="fill: #00ff00; stroke: none"/></g>
        <text>ignored</text>
    </svg>)";
    QBuffer buffer(&data);
    QVERIFY(buffer.open(QIODevice::ReadOnly));

    Document doc;
    QVERIFY(svg::read(&buffer, doc));
    QCOMPARE(doc.pageSize(), QSizeF(100 * kUnitsPerMm, 50 * kUnitsPerMm));
    QCOMPARE(doc.count(), 1);
    QCOMPARE(doc.shapes()[0]->kind(), ShapeKind::Ellipse);
    QCOMPARE(doc.shapes()[0]->bounds(), QRectF(5, 15, 10, 10));
    QCOMPARE(doc.shapes()[0]->style().fill, QColor(0, 255, 0));
    QVERIFY(!doc.shapes()[0]->style().hasStroke());
}

void CoreTests::rejectsNonSvg()
{
    QByteArray data = "<html><body/></html>";
    QBuffer buffer(&data);
    QVERIFY(buffer.open(QIODevice::ReadOnly));

    Document doc;
    doc.insertShape(-1, std::make_unique<Shape>(ShapeKind::Rectangle, QRectF(0, 0, 1, 1)));
    QString error;
    QVERIFY(!svg::read(&buffer, doc, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(doc.count(), 1); // unchanged on failure
}

void CoreTests::shapeAtReturnsTopmost()
{
    Document doc;
    Shape *bottom = doc.insertShape(-1, std::make_unique<Shape>(ShapeKind::Rectangle, QRectF(0, 0, 100, 100)));
    Shape *top = doc.insertShape(-1, std::make_unique<Shape>(ShapeKind::Ellipse, QRectF(50, 50, 100, 100)));

    QCOMPARE(doc.shapeAt({100, 100}, 0.5), top);
    QCOMPARE(doc.shapeAt({10, 10}, 0.5), bottom);
    QCOMPARE(doc.shapeAt({300, 300}, 0.5), nullptr);
}

void CoreTests::hitTestIgnoresEmptyFill()
{
    Style outlineOnly;
    outlineOnly.fill = Qt::transparent;
    outlineOnly.strokeWidth = 2;
    const Shape shape(ShapeKind::Rectangle, QRectF(0, 0, 100, 100), outlineOnly);

    QVERIFY(!shape.contains({50, 50}, 2));
    QVERIFY(shape.contains({0.5, 50}, 2));
}

void CoreTests::undoRedoAddMoveRemove()
{
    Document doc;
    QUndoStack stack;

    auto *add = new AddShapeCommand(&doc, std::make_unique<Shape>(ShapeKind::Rectangle, QRectF(0, 0, 10, 10)));
    Shape *shape = add->shape();
    stack.push(add);
    QCOMPARE(doc.count(), 1);

    stack.push(new MoveShapeCommand(&doc, shape, {5, 7}, false));
    QCOMPARE(shape->bounds(), QRectF(5, 7, 10, 10));

    stack.push(new RemoveShapeCommand(&doc, shape));
    QCOMPARE(doc.count(), 0);

    stack.undo();
    QCOMPARE(doc.count(), 1);
    QCOMPARE(doc.shapes()[0].get(), shape);
    stack.undo();
    QCOMPARE(shape->bounds(), QRectF(0, 0, 10, 10));
    stack.undo();
    QCOMPARE(doc.count(), 0);

    stack.redo();
    stack.redo();
    QCOMPARE(doc.count(), 1);
    QCOMPARE(shape->bounds(), QRectF(5, 7, 10, 10));
}

void CoreTests::setStyleUndo()
{
    Document doc;
    QUndoStack stack;
    Shape *shape = doc.insertShape(-1, std::make_unique<Shape>(ShapeKind::Ellipse, QRectF(0, 0, 10, 10)));
    const QColor before = shape->style().fill;

    Style style = shape->style();
    style.fill = Qt::yellow;
    stack.push(new SetStyleCommand(&doc, shape, style));
    QCOMPARE(shape->style().fill, QColor(Qt::yellow));

    stack.undo();
    QCOMPARE(shape->style().fill, before);
}

QTEST_GUILESS_MAIN(CoreTests)
#include "test_core.moc"
