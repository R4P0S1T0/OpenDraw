#include "core/Arrange.h"
#include "core/Commands.h"
#include "core/Document.h"
#include "core/PathOps.h"
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
    void setBoundsUndo();
    void writesFormatVersion();
    void opensNewerFormatAndReportsSkipped();
    void importsGroupsTransformsAndInheritedStyle();
    void pathsSurviveSaveAndOpen();
    void groupsSurviveSaveAndOpen();
    void groupMovesAndScalesChildren();
    void groupAndUngroupKeepStackingOrder();
    void reorderUndo();
    void alignAndDistribute();
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
    QCOMPARE(doc.count(), 2);
    QCOMPARE(doc.shapes()[1]->kind(), ShapeKind::Text);
    QCOMPARE(doc.shapes()[1]->textData().text, QStringLiteral("ignored"));
    QCOMPARE(doc.shapes()[0]->kind(), ShapeKind::Group);
    const Shape &circle = *doc.shapes()[0]->children()[0];
    QCOMPARE(circle.kind(), ShapeKind::Ellipse);
    QCOMPARE(circle.bounds(), QRectF(5, 15, 10, 10));
    QCOMPARE(circle.style().fill, QColor(0, 255, 0));
    QVERIFY(!circle.style().hasStroke());
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

void CoreTests::setBoundsUndo()
{
    Document doc;
    QUndoStack stack;
    Shape *shape = doc.insertShape(-1, std::make_unique<Shape>(ShapeKind::Rectangle, QRectF(0, 0, 10, 10)));

    stack.push(new SetBoundsCommand(&doc, shape, shape->bounds(), QRectF(5, 5, 40, 20)));
    QCOMPARE(shape->bounds(), QRectF(5, 5, 40, 20));
    stack.undo();
    QCOMPARE(shape->bounds(), QRectF(0, 0, 10, 10));
}

void CoreTests::writesFormatVersion()
{
    Document doc;
    QBuffer buffer;
    QVERIFY(buffer.open(QIODevice::ReadWrite));
    QVERIFY(svg::write(doc, &buffer));
    buffer.seek(0);

    Document loaded;
    svg::ReadReport report;
    QVERIFY(svg::read(&buffer, loaded, nullptr, &report));
    QCOMPARE(report.formatVersion, svg::kFormatVersion);
    QVERIFY(!report.isNewerFormat());
    QCOMPARE(report.skippedCount(), 0);
}

void CoreTests::opensNewerFormatAndReportsSkipped()
{
    // A file from a hypothetical future release: must still open, keeping what we understand.
    QByteArray data = R"(<svg xmlns="http://www.w3.org/2000/svg"
                              xmlns:opendraw="https://github.com/R4P0S1T0/OpenDraw/ns/1"
                              opendraw:format-version="99" viewBox="0 0 200 100">
        <rect x="1" y="2" width="3" height="4" fill="#123456"/>
        <path d="M0 0 L10 10"/>
        <image href="photo.png"/>
        <image href="photo2.png"/>
        <text x="0" y="0">Hello</text>
    </svg>)";
    QBuffer buffer(&data);
    QVERIFY(buffer.open(QIODevice::ReadOnly));

    Document doc;
    svg::ReadReport report;
    QVERIFY(svg::read(&buffer, doc, nullptr, &report));
    QCOMPARE(doc.count(), 3); // rect, path and text
    QCOMPARE(doc.pageSize(), QSizeF(200, 100));
    QCOMPARE(report.formatVersion, 99);
    QVERIFY(report.isNewerFormat());
    QCOMPARE(report.skippedElements.value(QStringLiteral("<image> linked to a separate file")), 2);
    QCOMPARE(report.skippedCount(), 2);
}

void CoreTests::importsGroupsTransformsAndInheritedStyle()
{
    QByteArray data = R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 400 400">
        <defs><rect id="hidden" width="5" height="5"/></defs>
        <g fill="#ff0000" stroke="blue" transform="translate(100 50)">
            <rect x="0" y="0" width="10" height="10"/>
            <path d="M0 0 L10 0 L10 10 Z" fill="rgb(0, 128, 0)"/>
            <g transform="scale(2)"><circle cx="5" cy="5" r="5" stroke-width="3"/></g>
        </g>
        <rect x="0" y="0" width="10" height="10" transform="rotate(45)"/>
        <polygon points="0,0 10,0 5,8"/>
        <line x1="0" y1="0" x2="10" y2="10" stroke="black"/>
    </svg>)svg";
    QBuffer buffer(&data);
    QVERIFY(buffer.open(QIODevice::ReadOnly));

    Document doc;
    QVERIFY(svg::read(&buffer, doc));
    QCOMPARE(doc.count(), 4); // the <g>, rotated rect, polygon and line; <defs> is not drawn

    const Shape &group = *doc.shapes()[0];
    QCOMPARE(group.kind(), ShapeKind::Group);
    QCOMPARE(group.children().size(), std::size_t(3));

    const Shape &rect = *group.children()[0];
    QCOMPARE(rect.kind(), ShapeKind::Rectangle);
    QCOMPARE(rect.bounds(), QRectF(100, 50, 10, 10));
    QCOMPARE(rect.style().fill, QColor(255, 0, 0));
    QCOMPARE(rect.style().stroke, QColor(0, 0, 255));

    const Shape &triangle = *group.children()[1];
    QCOMPARE(triangle.kind(), ShapeKind::Path);
    QCOMPARE(triangle.bounds(), QRectF(100, 50, 10, 10));
    QCOMPARE(triangle.style().fill, QColor(0, 128, 0));
    QVERIFY(triangle.pathData().front().closed);

    const Shape &nested = *group.children()[2];
    QCOMPARE(nested.kind(), ShapeKind::Group);
    const Shape &circle = *nested.children()[0];
    QCOMPARE(circle.kind(), ShapeKind::Ellipse);
    QCOMPARE(circle.bounds(), QRectF(100, 50, 20, 20));
    QCOMPARE(circle.style().strokeWidth, 6.0); // scaled with the group

    // A rotated rectangle stays a rectangle, turned by its shape transform.
    QCOMPARE(doc.shapes()[1]->kind(), ShapeKind::Rectangle);
    QVERIFY(std::abs(doc.shapes()[1]->rotation() - 45.0) < 1e-9);
    QCOMPARE(doc.shapes()[1]->localBounds(), QRectF(0, 0, 10, 10));
    QCOMPARE(doc.shapes()[2]->pathData().front().nodes.size(), std::size_t(3));
    QVERIFY(!doc.shapes()[3]->style().hasFill());
}

void CoreTests::groupsSurviveSaveAndOpen()
{
    Document original;
    std::vector<std::unique_ptr<Shape>> children;
    children.push_back(std::make_unique<Shape>(ShapeKind::Rectangle, QRectF(0, 0, 10, 10)));
    children.push_back(std::make_unique<Shape>(ShapeKind::Ellipse, QRectF(20, 0, 10, 10)));
    original.insertShape(-1, std::make_unique<Shape>(std::move(children)));

    QBuffer buffer;
    QVERIFY(buffer.open(QIODevice::ReadWrite));
    QVERIFY(svg::write(original, &buffer));
    buffer.seek(0);

    Document loaded;
    QVERIFY(svg::read(&buffer, loaded));
    QCOMPARE(loaded.count(), 1);
    QCOMPARE(loaded.shapes()[0]->kind(), ShapeKind::Group);
    QCOMPARE(loaded.shapes()[0]->children().size(), std::size_t(2));
    QCOMPARE(loaded.shapes()[0]->bounds(), QRectF(0, 0, 30, 10));
}

void CoreTests::groupMovesAndScalesChildren()
{
    std::vector<std::unique_ptr<Shape>> children;
    children.push_back(std::make_unique<Shape>(ShapeKind::Rectangle, QRectF(0, 0, 10, 10)));
    children.push_back(std::make_unique<Shape>(*pathops::fromSvg(u"M 20 0 L 30 10")));
    Shape group(std::move(children));
    QCOMPARE(group.bounds(), QRectF(0, 0, 30, 10));
    QVERIFY(group.contains({5, 5}, 0.5));
    QVERIFY(!group.contains({15, 5}, 0.5));

    group.setBounds(QRectF(100, 100, 60, 20));
    QCOMPARE(group.bounds(), QRectF(100, 100, 60, 20));
    QCOMPARE(group.children()[0]->bounds(), QRectF(100, 100, 20, 20));

    int leaves = 0;
    group.forEachLeaf([&leaves](Shape &) { ++leaves; });
    QCOMPARE(leaves, 2);
}

void CoreTests::groupAndUngroupKeepStackingOrder()
{
    Document doc;
    QUndoStack stack;
    std::vector<Shape *> s;
    for (int i = 0; i < 5; ++i)
        s.push_back(doc.insertShape(-1, std::make_unique<Shape>(ShapeKind::Rectangle, QRectF(i * 20, 0, 10, 10))));

    // Group the 2nd and 4th: the group takes the 4th's place in the stack.
    auto *group = new GroupCommand(&doc, {s[3], s[1]});
    Shape *g = group->group();
    stack.push(group);
    QCOMPARE(doc.order(), (std::vector<Shape *>{s[0], s[2], g, s[4]}));
    QCOMPARE(g->children()[0].get(), s[1]);
    QCOMPARE(g->children()[1].get(), s[3]);

    stack.undo();
    QCOMPARE(doc.order(), s);

    stack.redo();
    auto *ungroup = new UngroupCommand(&doc, g);
    stack.push(ungroup);
    QCOMPARE(doc.order(), (std::vector<Shape *>{s[0], s[2], s[1], s[3], s[4]}));
    stack.undo();
    QCOMPARE(doc.order(), (std::vector<Shape *>{s[0], s[2], g, s[4]}));
}

void CoreTests::reorderUndo()
{
    Document doc;
    QUndoStack stack;
    Shape *a = doc.insertShape(-1, std::make_unique<Shape>(ShapeKind::Rectangle, QRectF(0, 0, 1, 1)));
    Shape *b = doc.insertShape(-1, std::make_unique<Shape>(ShapeKind::Rectangle, QRectF(0, 0, 1, 1)));
    Shape *c = doc.insertShape(-1, std::make_unique<Shape>(ShapeKind::Rectangle, QRectF(0, 0, 1, 1)));

    stack.push(new ReorderCommand(&doc, {c, a, b}, QStringLiteral("To back")));
    QCOMPARE(doc.order(), (std::vector<Shape *>{c, a, b}));
    stack.undo();
    QCOMPARE(doc.order(), (std::vector<Shape *>{a, b, c}));
}

void CoreTests::alignAndDistribute()
{
    const std::vector<QRectF> boxes{{0, 0, 10, 10}, {30, 20, 20, 10}, {100, 5, 10, 40}};
    const QRectF all = arrange::unite(boxes);
    QCOMPARE(all, QRectF(0, 0, 110, 45));

    const auto left = arrange::alignOffsets(boxes, arrange::Alignment::Left, all);
    QCOMPARE(left[1], QPointF(-30, 0));
    QCOMPARE(left[2], QPointF(-100, 0));

    const auto bottom = arrange::alignOffsets(boxes, arrange::Alignment::Bottom, all);
    QCOMPARE(bottom[0], QPointF(0, 35));
    QCOMPARE(bottom[2], QPointF(0, 0));

    const auto centers = arrange::alignOffsets(boxes, arrange::Alignment::CentersHorizontally, all);
    QCOMPARE(centers[0], QPointF(50, 0)); // 55 - 5

    // Widths 10 + 20 + 10 over a span of 110: gaps of 35.
    const auto spread = arrange::distributeOffsets(boxes, Qt::Horizontal);
    QCOMPARE(spread[0], QPointF(0, 0));
    QCOMPARE(spread[1], QPointF(15, 0)); // moves to x = 45
    QCOMPARE(spread[2], QPointF(0, 0));

    QCOMPARE(arrange::distributeOffsets({{0, 0, 1, 1}, {5, 0, 1, 1}}, Qt::Horizontal)[1], QPointF(0, 0));
}

void CoreTests::pathsSurviveSaveAndOpen()
{
    Document original;
    auto path = pathops::fromSvg(u"M 10 10 C 20 0 40 0 50 10 L 50 40 Z M 60 60 L 70 70");
    QVERIFY(path);
    original.insertShape(-1, std::make_unique<Shape>(*path));

    QBuffer buffer;
    QVERIFY(buffer.open(QIODevice::ReadWrite));
    QVERIFY(svg::write(original, &buffer));
    buffer.seek(0);

    Document loaded;
    QVERIFY(svg::read(&buffer, loaded));
    QCOMPARE(loaded.count(), 1);
    QCOMPARE(loaded.shapes()[0]->pathData(), original.shapes()[0]->pathData());
}

QTEST_MAIN(CoreTests)
#include "test_core.moc"
