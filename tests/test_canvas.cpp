#include "CanvasWidget.h"
#include "core/Document.h"
#include "core/Export.h"

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
    void marqueeAndShiftClickSelection();
    void moveAndResizeSeveralShapes();
    void groupUngroupAlignAndOrder();
    void textToolTypesAndFinishes();
    void emptyTextIsDiscarded();
    void shortcutsWaitWhileTyping();
    void doubleClickEditsTextAndUndo();
    void textConvertsToCurves();
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

void CanvasTests::marqueeAndShiftClickSelection()
{
    m_canvas->setTool(CanvasWidget::Tool::Rectangle);
    dragFromTo(m_canvas, {300, 200}, {350, 250});
    dragFromTo(m_canvas, {400, 200}, {450, 250});
    dragFromTo(m_canvas, {600, 500}, {650, 550});
    QCOMPARE(m_doc->count(), 3);

    m_canvas->setTool(CanvasWidget::Tool::Select);
    dragFromTo(m_canvas, {280, 180}, {470, 270}); // encloses the first two only
    QCOMPARE(m_canvas->selection().size(), std::size_t(2));

    // Shift+click adds the third, then removes the first.
    sendMouse(m_canvas, QEvent::MouseButtonPress, {625, 525}, Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier);
    sendMouse(m_canvas, QEvent::MouseButtonRelease, {625, 525}, Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier);
    QCOMPARE(m_canvas->selection().size(), std::size_t(3));
    sendMouse(m_canvas, QEvent::MouseButtonPress, {325, 225}, Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier);
    sendMouse(m_canvas, QEvent::MouseButtonRelease, {325, 225}, Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier);
    QCOMPARE(m_canvas->selection().size(), std::size_t(2));

    click(m_canvas, {150, 650}); // empty space clears the selection
    QVERIFY(m_canvas->selection().empty());
}

void CanvasTests::moveAndResizeSeveralShapes()
{
    m_canvas->setTool(CanvasWidget::Tool::Rectangle);
    dragFromTo(m_canvas, {300, 200}, {350, 250});
    dragFromTo(m_canvas, {400, 200}, {450, 250});
    Shape *a = m_doc->shapes()[0].get();
    Shape *b = m_doc->shapes()[1].get();
    const QRectF aBefore = a->bounds();
    const QRectF bBefore = b->bounds();

    m_canvas->setTool(CanvasWidget::Tool::Select);
    m_canvas->selectAll();
    dragFromTo(m_canvas, {325, 225}, {325, 325}); // drag one, both move
    QCOMPARE(a->bounds().top() - aBefore.top(), b->bounds().top() - bBefore.top());
    QVERIFY(a->bounds().top() > aBefore.top());
    m_undo->undo(); // one step undoes the whole move
    QCOMPARE(a->bounds(), aBefore);
    QCOMPARE(b->bounds(), bBefore);

    // Drag the right-middle handle of the combined box (widget x 450 + 3) to the right:
    // the gap between the two grows along with their widths.
    dragFromTo(m_canvas, {453, 225}, {603, 225});
    QVERIFY(b->bounds().width() > bBefore.width() * 1.5);
    QVERIFY(b->bounds().left() - a->bounds().right() > bBefore.left() - aBefore.right());
    m_undo->undo();
    QCOMPARE(b->bounds(), bBefore);
}

void CanvasTests::groupUngroupAlignAndOrder()
{
    m_canvas->setTool(CanvasWidget::Tool::Rectangle);
    dragFromTo(m_canvas, {300, 200}, {350, 250});
    dragFromTo(m_canvas, {420, 260}, {440, 300});
    Shape *a = m_doc->shapes()[0].get();
    Shape *b = m_doc->shapes()[1].get();

    m_canvas->setTool(CanvasWidget::Tool::Select);
    m_canvas->selectAll();
    m_canvas->align(arrange::Alignment::Left);
    QCOMPARE(b->bounds().left(), a->bounds().left());
    m_canvas->align(arrange::Alignment::Bottom);
    QCOMPARE(a->bounds().bottom(), b->bounds().bottom());

    m_canvas->groupSelection();
    QCOMPARE(m_doc->count(), 1);
    Shape *group = m_doc->shapes()[0].get();
    QCOMPARE(group->kind(), ShapeKind::Group);
    QCOMPARE(m_canvas->selectedShape(), group);

    m_canvas->ungroupSelection();
    QCOMPARE(m_doc->count(), 2);
    QCOMPARE(m_canvas->selection().size(), std::size_t(2));

    m_canvas->setSelectedShape(a);
    m_canvas->orderToFront();
    QCOMPARE(m_doc->shapes().back().get(), a);
    m_undo->undo();
    QCOMPARE(m_doc->shapes().back().get(), b);
}

void CanvasTests::textToolTypesAndFinishes()
{
    m_canvas->setTool(CanvasWidget::Tool::Text);
    click(m_canvas, {300, 300});
    QVERIFY(m_canvas->editedText());
    QTest::keyClicks(m_canvas, QStringLiteral("Hello"));
    QTest::keyClick(m_canvas, Qt::Key_Return);
    QTest::keyClicks(m_canvas, QStringLiteral("World"));
    QTest::keyClick(m_canvas, Qt::Key_Backspace);
    QCOMPARE(m_doc->count(), 0); // added only when editing ends

    QTest::keyClick(m_canvas, Qt::Key_Escape);
    QVERIFY(!m_canvas->editedText());
    QCOMPARE(m_doc->count(), 1);
    const Shape &text = *m_doc->shapes()[0];
    QCOMPARE(text.kind(), ShapeKind::Text);
    QCOMPARE(text.textData().text, QStringLiteral("Hello\nWorl"));
    QCOMPARE(m_canvas->selectedShape(), &text);

    m_undo->undo();
    QCOMPARE(m_doc->count(), 0);
}

void CanvasTests::emptyTextIsDiscarded()
{
    m_canvas->setTool(CanvasWidget::Tool::Text);
    click(m_canvas, {300, 300});
    QTest::keyClicks(m_canvas, QStringLiteral("   "));
    QTest::keyClick(m_canvas, Qt::Key_Escape);
    QCOMPARE(m_doc->count(), 0);
    QCOMPARE(m_undo->count(), 0);
}

void CanvasTests::shortcutsWaitWhileTyping()
{
    m_canvas->setTool(CanvasWidget::Tool::Text);
    click(m_canvas, {300, 300});
    QTest::keyClicks(m_canvas, QStringLiteral("Hi"));

    // A plain letter belongs to the text, so the window must not treat it as a shortcut.
    QKeyEvent letter(QEvent::ShortcutOverride, Qt::Key_L, Qt::NoModifier, QStringLiteral("l"));
    QApplication::sendEvent(m_canvas, &letter);
    QVERIFY(letter.isAccepted());
    QVERIFY(m_canvas->editedText());

    // Ctrl+Z is a real shortcut: the text is committed first so undo has something to undo.
    QKeyEvent undo(QEvent::ShortcutOverride, Qt::Key_Z, Qt::ControlModifier);
    QApplication::sendEvent(m_canvas, &undo);
    QVERIFY(!m_canvas->editedText());
    QCOMPARE(m_doc->count(), 1);
}

void CanvasTests::doubleClickEditsTextAndUndo()
{
    m_canvas->setTool(CanvasWidget::Tool::Text);
    click(m_canvas, {300, 300});
    QTest::keyClicks(m_canvas, QStringLiteral("Hello"));
    QTest::keyClick(m_canvas, Qt::Key_Escape);
    Shape *text = m_doc->shapes()[0].get();

    m_canvas->setTool(CanvasWidget::Tool::Select);
    doubleClick(m_canvas, m_canvas->documentToWidget(text->bounds().center()));
    QCOMPARE(m_canvas->editedText(), text);
    QTest::keyClick(m_canvas, Qt::Key_Home);
    QTest::keyClicks(m_canvas, QStringLiteral(">"));
    QTest::keyClick(m_canvas, Qt::Key_End);
    QTest::keyClicks(m_canvas, QStringLiteral("!"));
    QTest::keyClick(m_canvas, Qt::Key_Escape);
    QCOMPARE(text->textData().text, QStringLiteral(">Hello!"));

    m_undo->undo();
    QCOMPARE(text->textData().text, QStringLiteral("Hello"));

    // Changing the format of the selected text is undoable too.
    m_canvas->setSelectedShape(text);
    TextFormat bold = m_canvas->currentTextFormat();
    bold.bold = true;
    m_canvas->setTextFormat(bold);
    QVERIFY(text->textData().format.bold);
    m_undo->undo();
    QVERIFY(!text->textData().format.bold);
}

void CanvasTests::textConvertsToCurves()
{
    m_canvas->setTool(CanvasWidget::Tool::Text);
    click(m_canvas, {300, 300});
    QTest::keyClicks(m_canvas, QStringLiteral("OK"));
    QTest::keyClick(m_canvas, Qt::Key_Escape);

    m_canvas->convertSelectionToCurves();
    QCOMPARE(m_doc->count(), 1);
    QCOMPARE(m_doc->shapes()[0]->kind(), ShapeKind::Path);
    QVERIFY(m_doc->shapes()[0]->pathData().size() >= 2); // the outlines of O (two) and K
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

    m_canvas->setTool(CanvasWidget::Tool::Select);
    m_canvas->selectAll();
    QVERIFY(m_canvas->grab().save(dir + QStringLiteral("/canvas-selection.png")));

    m_canvas->setTool(CanvasWidget::Tool::Text);
    click(m_canvas, {470, 120});
    QTest::keyClicks(m_canvas, QStringLiteral("OpenDraw"));
    QTest::keyClick(m_canvas, Qt::Key_Return);
    QTest::keyClicks(m_canvas, QStringLiteral("text tool"));
    QVERIFY(m_canvas->grab().save(dir + QStringLiteral("/canvas-text.png")));

    // The same drawing exported, to compare with what the canvas shows.
    m_canvas->finishTextEdit();
    exporter::ImageOptions options;
    options.dpi = 72;
    QVERIFY(exporter::renderImage(*m_doc, options).save(dir + QStringLiteral("/export.png")));
    QVERIFY(exporter::writePdf(*m_doc, dir + QStringLiteral("/export.pdf")));
}

QTEST_MAIN(CanvasTests)
#include "test_canvas.moc"
