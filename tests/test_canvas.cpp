#include "CanvasView.h"
#include "CanvasWidget.h"
#include "FillDialog.h"
#include "Language.h"
#include "LayersPanel.h"
#include "MainWindow.h"
#include "PageBar.h"
#include "PropertyBar.h"
#include "Units.h"
#include "core/Commands.h"
#include "core/Document.h"
#include "core/Export.h"

#include <QDoubleSpinBox>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QToolBar>
#include <QToolButton>
#include <QTranslator>
#include <QTreeWidget>
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
    void hiddenLayersAndPagesLimitSelection();
    void layersPanelEditsLayers();
    void fillDialogBuildsGradients();
    void propertyBarSetsOpacity();
    void propertyBarEditsThePage();
    void copyPasteAndDuplicate();
    void arrowKeysNudgeAndMirror();
    void zoomAndPanTools();
    void mainWindowWorkspace();
    void portugueseTranslation();
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

void CanvasTests::hiddenLayersAndPagesLimitSelection()
{
    m_canvas->setTool(CanvasWidget::Tool::Rectangle);
    dragFromTo(m_canvas, {300, 200}, {350, 250});
    Layer *first = m_doc->activeLayer();

    m_canvas->setTool(CanvasWidget::Tool::Select);
    m_canvas->selectAll();
    QCOMPARE(m_canvas->selection().size(), std::size_t(1));

    // Hiding the layer drops its shapes from the selection and from clicks.
    m_undo->push(new LayerPropertiesCommand(m_doc, first, first->name, false, false, QStringLiteral("Hide")));
    QVERIFY(m_canvas->selection().empty());
    click(m_canvas, {325, 225});
    QVERIFY(m_canvas->selection().empty());
    m_undo->undo();
    click(m_canvas, {325, 225});
    QCOMPARE(m_canvas->selection().size(), std::size_t(1));

    // A new page starts empty; drawing there does not touch page 1.
    m_undo->push(new AddPageCommand(m_doc, 1));
    QVERIFY(m_canvas->selection().empty());
    m_canvas->setTool(CanvasWidget::Tool::Ellipse);
    dragFromTo(m_canvas, {300, 200}, {350, 250});
    QCOMPARE(m_doc->pages()[1]->layers[0]->shapes.size(), std::size_t(1));
    QCOMPARE(first->shapes.size(), std::size_t(1));

    m_doc->setCurrentPage(0);
    m_canvas->selectAll();
    QCOMPARE(m_canvas->selection().size(), std::size_t(1));
    QCOMPARE(m_canvas->selection().front()->kind(), ShapeKind::Rectangle);
}

void CanvasTests::layersPanelEditsLayers()
{
    LayersPanel panel(m_doc, m_undo, m_canvas);
    auto button = [&panel](const QString &tip) {
        for (QToolButton *b : panel.findChildren<QToolButton *>()) {
            if (b->toolTip() == tip)
                return b;
        }
        return static_cast<QToolButton *>(nullptr);
    };
    auto *tree = panel.findChild<QTreeWidget *>();
    QVERIFY(tree);
    QCOMPARE(tree->topLevelItemCount(), 1);

    button(QStringLiteral("New layer"))->click();
    QCoreApplication::processEvents(); // the panel refreshes on the next event loop pass
    QCOMPARE(m_doc->currentPage()->layers.size(), std::size_t(2));
    QCOMPARE(tree->topLevelItemCount(), 2);
    QCOMPARE(tree->topLevelItem(0)->text(0), QStringLiteral("Layer 2")); // topmost first

    // Unchecking the box hides the layer, as one undoable step.
    tree->topLevelItem(0)->setCheckState(0, Qt::Unchecked);
    QVERIFY(!m_doc->activeLayer()->visible);
    m_undo->undo();
    QVERIFY(m_doc->activeLayer()->visible);

    // Draw on the new layer, then move the object down to the first layer.
    m_canvas->setTool(CanvasWidget::Tool::Rectangle);
    dragFromTo(m_canvas, {300, 200}, {350, 250});
    Layer *first = m_doc->currentPage()->layers[0].get();
    m_doc->setActiveLayer(first);
    QCoreApplication::processEvents();
    button(QStringLiteral("Move the selected objects to this layer"))->click();
    QCOMPARE(first->shapes.size(), std::size_t(1));

    button(QStringLiteral("Delete layer"))->click(); // deletes the active (first) layer
    QCOMPARE(m_doc->currentPage()->layers.size(), std::size_t(1));
    m_undo->undo();
    QCOMPARE(m_doc->currentPage()->layers.size(), std::size_t(2));
}

void CanvasTests::fillDialogBuildsGradients()
{
    // A three-colour gradient (e.g. from an imported file) survives OK without changes.
    Style imported;
    imported.gradient = Gradient::linear(Qt::red, Qt::blue, 0);
    imported.gradient->stops.insert(imported.gradient->stops.begin() + 1, {0.5, QColor(Qt::green)});
    FillDialog untouched(imported);
    QCOMPARE(untouched.applyTo(imported).gradient->stops.size(), std::size_t(3));

    FillDialog dialog(Style{});
    dialog.setFillType(FillDialog::FillType::Linear);
    dialog.setColors(QColor(255, 0, 0), QColor(0, 0, 255));
    dialog.setAngle(90);
    const Style linear = dialog.applyTo(Style{});
    QVERIFY(linear.gradient);
    QCOMPARE(linear.gradient->type, Gradient::Type::Linear);
    QVERIFY(qAbs(linear.gradient->angle() - 90) < 1e-6);
    QCOMPARE(linear.gradient->stops.front().color, QColor(255, 0, 0));

    dialog.setFillType(FillDialog::FillType::None);
    const Style none = dialog.applyTo(linear);
    QVERIFY(!none.hasFill());
}

void CanvasTests::propertyBarSetsOpacity()
{
    PropertyBar bar(m_doc, m_undo);
    Shape *shape = m_doc->insertShape(-1, std::make_unique<Shape>(ShapeKind::Rectangle, QRectF(0, 0, 10, 10)));
    bar.setShape(shape);

    QDoubleSpinBox *opacity = nullptr;
    for (QDoubleSpinBox *box : bar.findChildren<QDoubleSpinBox *>()) {
        if (box->suffix() == QStringLiteral(" %"))
            opacity = box;
    }
    QVERIFY(opacity);
    QCOMPARE(opacity->value(), 100.0);
    opacity->setValue(40);
    emit opacity->editingFinished();
    QCOMPARE(shape->style().opacity, 0.4);
    m_undo->undo();
    QCOMPARE(shape->style().opacity, 1.0);
}

void CanvasTests::propertyBarEditsThePage()
{
    PropertyBar bar(m_doc, m_undo);
    bar.setSelection(nullptr, false);
    QDoubleSpinBox *pageWidth = nullptr;
    for (QDoubleSpinBox *box : bar.findChildren<QDoubleSpinBox *>()) {
        if (box->toolTip() == QStringLiteral("Page width"))
            pageWidth = box;
    }
    QVERIFY(pageWidth);
    const QSizeF before = m_doc->pageSize();
    QCOMPARE(pageWidth->value(), Units::instance().toDisplay(before.width()));
    pageWidth->setValue(Units::instance().toDisplay(100 * kUnitsPerMm));
    emit pageWidth->editingFinished();
    QVERIFY(std::abs(m_doc->pageSize().width() - 100 * kUnitsPerMm) < 0.01);
    QCOMPARE(m_doc->pageSize().height(), before.height());
    m_undo->undo();
    QCOMPARE(m_doc->pageSize(), before);
}

void CanvasTests::copyPasteAndDuplicate()
{
    Style style;
    style.fill = Qt::red;
    Shape *rect = m_doc->insertShape(-1, std::make_unique<Shape>(ShapeKind::Rectangle, QRectF(10, 20, 30, 40), style));
    m_canvas->setSelectedShape(rect);

    m_canvas->copySelection();
    m_canvas->paste();
    QCOMPARE(m_doc->count(), 2);
    QCOMPARE(m_canvas->selection().size(), std::size_t(1));
    Shape *pasted = m_canvas->selection().front();
    QVERIFY(pasted != rect);
    QCOMPARE(pasted->bounds(), rect->bounds());
    QCOMPARE(pasted->style().fill, QColor(Qt::red));

    m_canvas->setDuplicateOffset(QPointF(5, 7));
    m_canvas->duplicateSelection();
    QCOMPARE(m_doc->count(), 3);
    QCOMPARE(m_canvas->selection().front()->bounds(), QRectF(15, 27, 30, 40));

    m_undo->undo();
    m_undo->undo();
    QCOMPARE(m_doc->count(), 1);

    m_canvas->setSelectedShape(rect);
    m_canvas->cutSelection();
    QCOMPARE(m_doc->count(), 0);
    m_canvas->paste();
    QCOMPARE(m_doc->count(), 1);
}

void CanvasTests::arrowKeysNudgeAndMirror()
{
    Shape *rect = m_doc->insertShape(-1, std::make_unique<Shape>(ShapeKind::Rectangle, QRectF(100, 100, 50, 50)));
    m_canvas->setSelectedShape(rect);
    m_canvas->setNudgeDistance(2.0);
    m_canvas->setFocus();
    QTest::keyClick(m_canvas, Qt::Key_Right);
    QCOMPARE(rect->bounds().topLeft(), QPointF(102, 100));
    QTest::keyClick(m_canvas, Qt::Key_Up, Qt::ShiftModifier);
    QCOMPARE(rect->bounds().topLeft(), QPointF(102, 80));
    m_undo->undo();
    QCOMPARE(rect->bounds().topLeft(), QPointF(102, 100));

    // A right triangle flipped left to right keeps its box but points the other way.
    SubPath triangle;
    triangle.closed = true;
    for (const QPointF &p : {QPointF(0, 0), QPointF(0, 100), QPointF(100, 100)})
        triangle.nodes.push_back(PathNode::corner(p));
    Shape *path = m_doc->insertShape(-1, std::make_unique<Shape>(PathData{triangle}));
    m_canvas->setSelectedShape(path);
    m_canvas->mirrorSelection(Qt::Horizontal);
    QCOMPARE(path->bounds(), QRectF(0, 0, 100, 100));
    QCOMPARE(path->pathData().front().nodes.front().point, QPointF(100, 0));
    m_undo->undo();
    QCOMPARE(path->pathData().front().nodes.front().point, QPointF(0, 0));
}

void CanvasTests::zoomAndPanTools()
{
    QSignalSpy viewChanged(m_canvas, &CanvasWidget::viewChanged);
    const double zoom = m_canvas->zoom();
    m_canvas->setTool(CanvasWidget::Tool::Zoom);
    click(m_canvas, {450, 350});
    QVERIFY(m_canvas->zoom() > zoom * 1.2);
    QVERIFY(!viewChanged.isEmpty());

    // Dragging a box zooms so the box fills the view.
    const QPointF from = m_canvas->viewTransform().inverted().map(QPointF(400, 300));
    dragFromTo(m_canvas, {400, 300}, {500, 380});
    const QRectF visible = m_canvas->visibleDocumentRect();
    QVERIFY(visible.contains(from));
    QVERIFY(m_canvas->zoom() > zoom * 5);

    m_canvas->setTool(CanvasWidget::Tool::Pan);
    const QPointF pan = m_canvas->pan();
    dragFromTo(m_canvas, {300, 300}, {340, 260});
    QCOMPARE(m_canvas->pan(), pan + QPointF(40, -40));

    m_canvas->setZoom(1.0);
    QCOMPARE(m_canvas->zoom(), 1.0);
}

// The whole workspace builds, and its toolbars drive the canvas.
void CanvasTests::mainWindowWorkspace()
{
    MainWindow window;
    window.resize(1400, 900);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *canvas = window.findChild<CanvasWidget *>();
    QVERIFY(canvas);
    QVERIFY(window.findChild<CanvasView *>());
    QVERIFY(window.findChild<PageBar *>());
    QVERIFY(window.findChild<QToolBar *>(QStringLiteral("standard")));
    QVERIFY(window.findChild<QToolBar *>(QStringLiteral("tools")));

    // Picking a tool from the toolbox (including one inside a flyout) switches the canvas tool.
    QAction *pan = nullptr;
    for (QAction *action : window.findChildren<QAction *>()) {
        if (action->data().toInt() == int(CanvasWidget::Tool::Pan) && action->isCheckable())
            pan = action;
    }
    QVERIFY(pan);
    pan->trigger();
    QCOMPARE(canvas->tool(), CanvasWidget::Tool::Pan);
    canvas->setTool(CanvasWidget::Tool::Select);
    QVERIFY(!pan->isChecked());

    const QString dir = qEnvironmentVariable("OPENDRAW_SCREENSHOT_DIR");
    if (!dir.isEmpty()) {
        auto *doc = window.findChild<Document *>();
        Style style;
        style.fill = QColor(0x1e, 0x88, 0xe5);
        style.stroke = Qt::black;
        Shape *rect = doc->insertShape(-1, std::make_unique<Shape>(ShapeKind::Rectangle, QRectF(150, 200, 300, 200), style));
        canvas->setSelectedShape(rect);
        QTest::qWait(50); // let the toolbars lay out again
        QVERIFY(window.grab().save(dir + QStringLiteral("/window-selection.png")));
    }
}

// The Brazilian Portuguese translation is built into the program and covers the window, the
// core's messages and the default page and layer names.
void CanvasTests::portugueseTranslation()
{
    if (!QFile::exists(QStringLiteral(":/i18n/opendraw_pt_BR.qm")))
        QSKIP("Built without Qt LinguistTools");
    QVERIFY(language::available().contains(QStringLiteral("pt_BR")));
    QTranslator translator;
    QVERIFY(translator.load(QStringLiteral(":/i18n/opendraw_pt_BR.qm")));
    QCoreApplication::installTranslator(&translator);
    QCOMPARE(MainWindow::tr("&File"), QStringLiteral("&Arquivo"));
    QCOMPARE(QCoreApplication::translate("Commands", "Add rectangle"), QStringLiteral("Adicionar retângulo"));
    QCOMPARE(PropertyBar::tr("Page width"), QStringLiteral("Largura da página"));
    QCOMPARE(MainWindow::tr("%n objects selected", nullptr, 3), QStringLiteral("3 objetos selecionados"));
    Document document;
    QCOMPARE(document.currentPage()->name, QStringLiteral("Página 1"));
    QCOMPARE(document.activeLayer()->name, QStringLiteral("Camada 1"));
    QCoreApplication::removeTranslator(&translator);
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

    // Gradients and transparency.
    m_canvas->finishTextEdit();
    Style linear;
    linear.gradient = Gradient::linear(QColor(0xe5, 0x39, 0x35), QColor(0x1e, 0x88, 0xe5), 30);
    m_doc->insertShape(-1, std::make_unique<Shape>(ShapeKind::Rectangle, QRectF(40, 900, 300, 150), linear));
    Style radial;
    radial.gradient = Gradient::radial(QColor(0xff, 0xf5, 0x9d), QColor(0xfb, 0x8c, 0x00));
    radial.opacity = 0.7;
    m_doc->insertShape(-1, std::make_unique<Shape>(ShapeKind::Ellipse, QRectF(250, 850, 250, 250), radial));
    QVERIFY(m_canvas->grab().save(dir + QStringLiteral("/canvas-gradients.png")));

    // The same drawing exported, to compare with what the canvas shows.
    exporter::ImageOptions options;
    options.dpi = 72;
    QVERIFY(exporter::renderImage(*m_doc, options).save(dir + QStringLiteral("/export.png")));
    QVERIFY(exporter::writePdf(*m_doc, dir + QStringLiteral("/export.pdf")));
}

QTEST_MAIN(CanvasTests)
#include "test_canvas.moc"
