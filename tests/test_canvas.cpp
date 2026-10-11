#include "CanvasWidget.h"
#include "core/Document.h"

#include <QMouseEvent>
#include <QUndoStack>
#include <QtTest>

using namespace opendraw;

namespace {

void sendMouse(QWidget *widget, QEvent::Type type, const QPointF &pos, Qt::MouseButton button,
               Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    QMouseEvent event(type, pos, widget->mapToGlobal(pos), button, buttons, modifiers);
    QApplication::sendEvent(widget, &event);
}

void press(QWidget *w, const QPointF &p)
{
    sendMouse(w, QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::LeftButton);
}

void drag(QWidget *w, const QPointF &p)
{
    sendMouse(w, QEvent::MouseMove, p, Qt::NoButton, Qt::LeftButton);
}

void release(QWidget *w, const QPointF &p)
{
    sendMouse(w, QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton);
}

void click(QWidget *w, const QPointF &p)
{
    press(w, p);
    release(w, p);
}

void doubleClick(QWidget *w, const QPointF &p)
{
    click(w, p);
    sendMouse(w, QEvent::MouseButtonDblClick, p, Qt::LeftButton, Qt::LeftButton);
    release(w, p);
}

void dragFromTo(QWidget *w, const QPointF &from, const QPointF &to, int steps = 10)
{
    press(w, from);
    for (int i = 1; i <= steps; ++i)
        drag(w, from + (to - from) * (double(i) / steps));
    release(w, to);
}

int nodeCount(const Shape &shape)
{
    int n = 0;
    for (const SubPath &sp : shape.pathData())
        n += int(sp.nodes.size());
    return n;
}

} // namespace

class CanvasTests : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void bezierToolBuildsCurveWithSmoothNode();
    void bezierToolClosesOnFirstNode();
    void bezierUndoRemovesCurve();
    void freehandToolCreatesSimplifiedCurve();
    void convertToCurvesAndEditNode();
    void doubleClickAddsAndRemovesNodes();
    void deleteKeyRemovesActiveNode();
    void breakThenJoinWithShiftClick();
    void nodeTypeAndSegmentCommands();
    void screenshot();

private:
    Document *m_doc = nullptr;
    QUndoStack *m_undo = nullptr;
    CanvasWidget *m_canvas = nullptr;
};

void CanvasTests::init()
{
    m_doc = new Document;
    m_undo = new QUndoStack;
    m_canvas = new CanvasWidget(m_doc, m_undo);
    m_canvas->resize(900, 700);
    m_canvas->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_canvas));
}

void CanvasTests::cleanup()
{
    delete m_canvas;
    delete m_undo;
    delete m_doc;
}

void CanvasTests::bezierToolBuildsCurveWithSmoothNode()
{
    m_canvas->setTool(CanvasWidget::Tool::Bezier);
    click(m_canvas, {300, 300});                        // corner node
    dragFromTo(m_canvas, {400, 250}, {450, 250});       // smooth node with handles
    click(m_canvas, {500, 350});                        // corner node
    QTest::keyClick(m_canvas, Qt::Key_Return);

    QCOMPARE(m_doc->count(), 1);
    const Shape &curve = *m_doc->shapes()[0];
    QCOMPARE(curve.kind(), ShapeKind::Path);
    QCOMPARE(nodeCount(curve), 3);
    QVERIFY(!curve.pathData()[0].closed);
    QCOMPARE(curve.pathData()[0].nodes[0].type, NodeType::Corner);
    QCOMPARE(curve.pathData()[0].nodes[1].type, NodeType::Smooth);
    QVERIFY(!curve.style().hasFill()); // open curves are not filled
    QCOMPARE(m_canvas->selectedShape(), &curve);
}

void CanvasTests::bezierToolClosesOnFirstNode()
{
    m_canvas->setTool(CanvasWidget::Tool::Bezier);
    click(m_canvas, {300, 300});
    click(m_canvas, {400, 300});
    click(m_canvas, {350, 380});
    click(m_canvas, {301, 301}); // back on the first node

    QCOMPARE(m_doc->count(), 1);
    QVERIFY(m_doc->shapes()[0]->pathData()[0].closed);
    QCOMPARE(nodeCount(*m_doc->shapes()[0]), 3);
}

void CanvasTests::bezierUndoRemovesCurve()
{
    m_canvas->setTool(CanvasWidget::Tool::Bezier);
    click(m_canvas, {300, 300});
    doubleClick(m_canvas, {400, 320}); // double-click places the last node and finishes
    QCOMPARE(m_doc->count(), 1);
    QCOMPARE(nodeCount(*m_doc->shapes()[0]), 2);

    m_undo->undo();
    QCOMPARE(m_doc->count(), 0);
    QCOMPARE(m_canvas->selectedShape(), nullptr);
}

void CanvasTests::freehandToolCreatesSimplifiedCurve()
{
    m_canvas->setTool(CanvasWidget::Tool::Freehand);
    press(m_canvas, {200, 300});
    for (int i = 1; i <= 120; ++i)
        drag(m_canvas, {200.0 + i * 4, 300 + 60 * std::sin(i / 12.0)});
    release(m_canvas, {680, 300 + 60 * std::sin(10.0)});

    QCOMPARE(m_doc->count(), 1);
    const int nodes = nodeCount(*m_doc->shapes()[0]);
    QVERIFY2(nodes >= 3 && nodes < 40, qPrintable(QString::number(nodes)));
}

void CanvasTests::convertToCurvesAndEditNode()
{
    m_canvas->setTool(CanvasWidget::Tool::Rectangle);
    dragFromTo(m_canvas, {300, 200}, {500, 400});
    QCOMPARE(m_doc->count(), 1);
    QCOMPARE(m_doc->shapes()[0]->kind(), ShapeKind::Rectangle);

    m_canvas->convertSelectionToCurves();
    QCOMPARE(m_doc->count(), 1);
    Shape *curve = m_doc->shapes()[0].get();
    QCOMPARE(curve->kind(), ShapeKind::Path);
    QCOMPARE(m_canvas->selectedShape(), curve);
    QCOMPARE(nodeCount(*curve), 4);

    // Drag the top-left node outwards with the shape tool.
    const QRectF before = curve->bounds();
    m_canvas->setTool(CanvasWidget::Tool::Node);
    dragFromTo(m_canvas, {300, 200}, {260, 160});
    QVERIFY(curve->bounds().width() > before.width());
    QVERIFY(curve->bounds().height() > before.height());

    m_undo->undo(); // node move
    QCOMPARE(curve->bounds(), before);
    m_undo->undo(); // convert to curves
    QCOMPARE(m_doc->shapes()[0]->kind(), ShapeKind::Rectangle);
}

void CanvasTests::doubleClickAddsAndRemovesNodes()
{
    m_canvas->setTool(CanvasWidget::Tool::Bezier);
    click(m_canvas, {300, 300});
    doubleClick(m_canvas, {500, 300});
    Shape *curve = m_doc->shapes()[0].get();

    m_canvas->setTool(CanvasWidget::Tool::Node);
    doubleClick(m_canvas, {400, 300}); // on the segment: adds a node
    QCOMPARE(nodeCount(*curve), 3);

    doubleClick(m_canvas, {400, 300}); // on the new node: removes it
    QCOMPARE(nodeCount(*curve), 2);
}

void CanvasTests::deleteKeyRemovesActiveNode()
{
    m_canvas->setTool(CanvasWidget::Tool::Bezier);
    click(m_canvas, {300, 300});
    click(m_canvas, {400, 300});
    doubleClick(m_canvas, {500, 300});
    Shape *curve = m_doc->shapes()[0].get();
    QCOMPARE(nodeCount(*curve), 3);

    m_canvas->setTool(CanvasWidget::Tool::Node);
    click(m_canvas, {400, 300}); // select the middle node
    m_canvas->deleteSelection();
    QCOMPARE(m_doc->count(), 1);
    QCOMPARE(nodeCount(*curve), 2);
}

void CanvasTests::breakThenJoinWithShiftClick()
{
    m_canvas->setTool(CanvasWidget::Tool::Bezier);
    click(m_canvas, {300, 300});
    click(m_canvas, {400, 300});
    doubleClick(m_canvas, {500, 300});
    Shape *curve = m_doc->shapes()[0].get();

    m_canvas->setTool(CanvasWidget::Tool::Node);
    click(m_canvas, {400, 300});
    QVERIFY(m_canvas->hasActiveNode());
    m_canvas->breakNode();
    QCOMPARE(curve->pathData().size(), std::size_t(2));

    // The two new end nodes sit on top of each other; pull one away.
    dragFromTo(m_canvas, {400, 300}, {420, 340});

    click(m_canvas, {400, 300});
    QVERIFY(!m_canvas->hasNodePairToJoin());
    sendMouse(m_canvas, QEvent::MouseButtonPress, {420, 340}, Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier);
    sendMouse(m_canvas, QEvent::MouseButtonRelease, {420, 340}, Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier);
    QVERIFY(m_canvas->hasNodePairToJoin());

    m_canvas->joinNodes();
    QCOMPARE(curve->pathData().size(), std::size_t(1));
    QCOMPARE(nodeCount(*curve), 4);

    m_undo->undo();
    QCOMPARE(curve->pathData().size(), std::size_t(2));
}

void CanvasTests::nodeTypeAndSegmentCommands()
{
    m_canvas->setTool(CanvasWidget::Tool::Bezier);
    click(m_canvas, {300, 300});
    click(m_canvas, {400, 300});
    doubleClick(m_canvas, {450, 400});
    Shape *curve = m_doc->shapes()[0].get();

    m_canvas->setTool(CanvasWidget::Tool::Node);
    click(m_canvas, {400, 300});
    m_canvas->makeNodeSmooth();
    QCOMPARE(curve->pathData()[0].nodes[1].type, NodeType::Smooth);

    m_canvas->segmentToLine(); // the segment after the node becomes straight again
    const PathNode &after = curve->pathData()[0].nodes[1];
    QCOMPARE(after.out, after.point);

    m_canvas->makeNodeCorner();
    QCOMPARE(curve->pathData()[0].nodes[1].type, NodeType::Corner);
}

// Not an assertion: saves a picture of the canvas for manual review when
// OPENDRAW_SCREENSHOT_DIR is set.
void CanvasTests::screenshot()
{
    const QString dir = qEnvironmentVariable("OPENDRAW_SCREENSHOT_DIR");
    if (dir.isEmpty())
        QSKIP("Set OPENDRAW_SCREENSHOT_DIR to save a screenshot");

    m_canvas->setTool(CanvasWidget::Tool::Bezier);
    click(m_canvas, {250, 420});
    dragFromTo(m_canvas, {380, 250}, {460, 250});
    dragFromTo(m_canvas, {560, 420}, {620, 470});
    QTest::keyClick(m_canvas, Qt::Key_Return);

    m_canvas->setTool(CanvasWidget::Tool::Freehand);
    press(m_canvas, {250, 520});
    for (int i = 1; i <= 100; ++i)
        drag(m_canvas, {250.0 + i * 4, 520 + 30 * std::sin(i / 8.0)});
    release(m_canvas, {650, 520 + 30 * std::sin(12.5)});

    m_canvas->setTool(CanvasWidget::Tool::Ellipse);
    dragFromTo(m_canvas, {300, 120}, {420, 200});
    m_canvas->convertSelectionToCurves();
    m_canvas->setTool(CanvasWidget::Tool::Node);
    click(m_canvas, {420, 160}); // right-hand node of the ellipse

    QVERIFY(m_canvas->grab().save(dir + QStringLiteral("/canvas-curves.png")));
}

QTEST_MAIN(CanvasTests)
#include "test_canvas.moc"
