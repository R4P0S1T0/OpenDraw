#include "CanvasView.h"
#include "CanvasWidget.h"
#include "FillDialog.h"
#include "Language.h"
#include "ObjectsPanel.h"
#include "MainWindow.h"
#include "PageBar.h"
#include "PropertiesPanel.h"
#include "PropertyBar.h"
#include "Theme.h"
#include "Units.h"
#include "core/Generators.h"
#include "core/RichText.h"
#include "core/TextLayout.h"
#include "core/Commands.h"
#include "core/Document.h"
#include "core/Export.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
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
    void objectsDockerEditsLayersAndObjects();
    void fillDialogBuildsGradients();
    void propertyBarSetsOpacity();
    void propertyBarEditsThePage();
    void copyPasteAndDuplicate();
    void arrowKeysNudgeAndMirror();
    void zoomAndPanTools();
    void mainWindowWorkspace();
    void portugueseTranslation();
    void objectToolsWithModifiers();
    void lineToolSnapsAngles();
    void eyedropperPicksAndApplies();
    void guidelinesFromRulersAndSnapping();
    void spaceTogglesThePickTool();
    void secondClickRotatesAndSkews();
    void propertiesDockerAppliesStyles();
    void textToolDrawsParagraphFrames();
    void formatPartOfAText();
    void shadowAndTransparencyTools();
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

void CanvasTests::objectsDockerEditsLayersAndObjects()
{
    ObjectsPanel panel(m_doc, m_undo, m_canvas);
    auto button = [&panel](const QString &tip) {
        for (QToolButton *b : panel.findChildren<QToolButton *>()) {
            if (b->toolTip() == tip)
                return b;
        }
        return static_cast<QToolButton *>(nullptr);
    };
    auto *tree = panel.findChild<ObjectsTree *>();
    QVERIFY(tree);
    QCOMPARE(tree->topLevelItemCount(), 1); // one page
    QCOMPARE(tree->topLevelItem(0)->childCount(), 1); // one layer

    button(QStringLiteral("New layer"))->click();
    panel.refresh();
    QTreeWidgetItem *page = tree->topLevelItem(0);
    QCOMPARE(page->childCount(), 2);
    QCOMPARE(page->child(0)->text(0), QStringLiteral("Layer 2")); // topmost first

    // Clicking the eye hides the layer, as one undoable step.
    emit tree->itemClicked(page->child(0), 1);
    QVERIFY(!m_doc->activeLayer()->visible);
    m_undo->undo();
    QVERIFY(m_doc->activeLayer()->visible);

    // A new object shows under its layer, selected like on the drawing.
    m_canvas->setTool(CanvasWidget::Tool::Rectangle);
    dragFromTo(m_canvas, {300, 200}, {350, 250});
    panel.refresh();
    QTreeWidgetItem *layer2 = tree->topLevelItem(0)->child(0);
    QCOMPARE(layer2->childCount(), 1);
    QTreeWidgetItem *rectItem = layer2->child(0);
    QCOMPARE(rectItem->text(0), QStringLiteral("Rectangle"));
    QVERIFY(rectItem->isSelected());
    Shape *rect = m_canvas->selection().front();

    // Renaming in the tree names the object (saved with it), undoably.
    rectItem->setText(0, QStringLiteral("Logo"));
    QCOMPARE(rect->name(), QStringLiteral("Logo"));
    m_undo->undo();
    QVERIFY(rect->name().isEmpty());

    // Selecting in the tree selects on the drawing.
    m_canvas->setSelection({});
    panel.refresh();
    tree->topLevelItem(0)->child(0)->child(0)->setSelected(true);
    QCOMPARE(m_canvas->selection().size(), std::size_t(1));
    QCOMPARE(m_canvas->selection().front(), rect);

    // Dragging the object onto the first layer moves it there.
    Layer *first = m_doc->currentPage()->layers[0].get();
    panel.refresh();
    QTreeWidgetItem *firstLayerItem = tree->topLevelItem(0)->child(1);
    emit tree->itemsDropped({tree->topLevelItem(0)->child(0)->child(0)}, firstLayerItem, ObjectsTree::DropPosition::On);
    QCOMPARE(first->shapes.size(), std::size_t(1));
    QCOMPARE(first->shapes.front().get(), rect);

    // Dropping below another object restacks.
    Shape *ellipse = m_doc->insertShape(first, -1, std::make_unique<Shape>(ShapeKind::Ellipse, QRectF(0, 0, 10, 10)));
    panel.refresh();
    firstLayerItem = tree->topLevelItem(0)->child(1);
    QCOMPARE(firstLayerItem->child(0)->text(0), QStringLiteral("Ellipse")); // on top
    emit tree->itemsDropped({firstLayerItem->child(1)}, firstLayerItem->child(0), ObjectsTree::DropPosition::Above);
    QCOMPARE(m_doc->order(first).back(), rect);
    m_undo->undo();
    QCOMPARE(m_doc->order(first).back(), ellipse);

    // Move here, and deleting the active layer when nothing is selected.
    m_canvas->setSelection({});
    m_doc->setActiveLayer(m_doc->currentPage()->layers[1].get());
    panel.refresh();
    button(QStringLiteral("Delete the selected objects, or the layer"))->click();
    QCOMPARE(m_doc->currentPage()->layers.size(), std::size_t(1));
    m_undo->undo();
    QCOMPARE(m_doc->currentPage()->layers.size(), std::size_t(2));
    m_canvas->setSelectedShape(rect);
    button(QStringLiteral("Move the selected objects to this layer"))->click();
    QCOMPARE(m_doc->layerOf(rect), m_doc->currentPage()->layers[1].get());
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

        // Object tools, guidelines and the grid, in the dark scheme.
        theme::apply(ColorScheme::Dark);
        style.fill = QColor(0xfd, 0xd8, 0x35);
        doc->insertShape(-1, std::make_unique<Shape>(generators::star(QRectF(500, 200, 220, 220), 5, 53), style));
        style.fill = QColor(0x43, 0xa0, 0x47);
        doc->insertShape(-1, std::make_unique<Shape>(generators::polygon(QRectF(150, 500, 220, 220), 6), style));
        Style line;
        line.fill = Qt::transparent;
        line.stroke = QColor(0x1e, 0x88, 0xe5);
        line.strokeWidth = 3;
        doc->insertShape(-1, std::make_unique<Shape>(generators::spiral(QRectF(500, 500, 220, 220), 4), line));
        doc->insertGuide(-1, Guide{Qt::Vertical, 150});
        doc->insertGuide(-1, Guide{Qt::Horizontal, 200});
        GridSettings grid;
        grid.visible = true;
        canvas->setGridSettings(grid);
        canvas->setSelection({});
        canvas->setTool(CanvasWidget::Tool::Star);
        QTest::qWait(50);
        QVERIFY(window.grab().save(dir + QStringLiteral("/window-tools.png")));

        // A rotated rectangle and text, with the rotate/skew handles showing.
        canvas->setTool(CanvasWidget::Tool::Select);
        TextFormat format;
        format.size = 48;
        Style textStyle;
        textStyle.fill = Qt::black;
        textStyle.stroke = Qt::transparent;
        Shape *text = doc->insertShape(-1, std::make_unique<Shape>(TextData{QStringLiteral("OpenDraw"), format},
                                                                   QPointF(180, 820), textStyle));
        canvas->setSelectedShape(text);
        canvas->rotateSelection(-20);
        canvas->setSelectedShape(rect);
        canvas->rotateSelection(25);
        // Halfway between the centre (the rotation marker) and a corner: on the rectangle,
        // away from the guidelines.
        const QPointF onRect = canvas->documentToWidget(rect->path().pointAtPercent(0.1) * 0.5
                                                        + rect->bounds().center() * 0.5);
        click(canvas, onRect);
        QVERIFY(canvas->rotateMode());
        QTest::qWait(50);
        QVERIFY(window.grab().save(dir + QStringLiteral("/window-rotate.png")));

        // Dashed, round-cornered outline, as set from the Properties docker.
        canvas->setSelectedShape(text);
        canvas->restyle(QStringLiteral("Outline"), [](Style &s) {
            s.stroke = QColor(0xe5, 0x39, 0x35);
            s.strokeWidth = 1.5;
        });
        canvas->setSelectedShape(rect);
        canvas->restyle(QStringLiteral("Outline"), [](Style &s) {
            s.stroke = QColor(0x0d, 0x47, 0xa1);
            s.strokeWidth = 6;
            s.dashes = {3, 1.5};
            s.join = LineJoin::Round;
            s.cap = LineCap::Round;
        });
        if (auto *properties = window.findChild<PropertiesPanel *>())
            properties->showSection(PropertiesPanel::Section::Outline);
        canvas->zoomToSelection();
        QTest::qWait(200); // the Objects docker refreshes shortly after changes
        QVERIFY(window.grab().save(dir + QStringLiteral("/window-properties.png")));

        // Mixed formats in one text, with part of it selected while typing.
        canvas->fitPage();
        canvas->setTool(CanvasWidget::Tool::Text);
        click(canvas, canvas->documentToWidget({150, 900}));
        QTest::keyClicks(canvas, QStringLiteral("Mixed formats in one text"));
        for (int i = 0; i < 7; ++i)
            QTest::keyClick(canvas, Qt::Key_Left, Qt::ShiftModifier);
        TextFormat big = canvas->currentTextFormat();
        big.size = 56;
        big.bold = true;
        canvas->setTextFormat(big);
        CharFormat green;
        green.color = QColor(0x2e, 0x7d, 0x32);
        canvas->applyCharFormat(green);
        for (int i = 0; i < 14; ++i)
            QTest::keyClick(canvas, Qt::Key_Left);
        for (int i = 0; i < 7; ++i)
            QTest::keyClick(canvas, Qt::Key_Left, Qt::ShiftModifier);
        QTest::qWait(150);
        QVERIFY(window.grab().save(dir + QStringLiteral("/window-richtext.png")));
        canvas->finishTextEdit();

        // Drop shadow on the star, a transparency gradient on the hexagon.
        for (const auto &shape : doc->shapes()) {
            if (shape->kind() != ShapeKind::Path || shape->style().fill.alpha() == 0)
                continue;
            Style s = shape->style();
            if (s.fill == QColor(0xfd, 0xd8, 0x35)) {
                s.shadow = DropShadow{QPointF(10, 12), 12.0, QColor(0, 0, 0, 140)};
            } else {
                Gradient fade = Gradient::linear(Qt::black, Qt::black, 0);
                fade.stops = {{0.0, QColor(0, 0, 0, 255)}, {1.0, QColor(0, 0, 0, 0)}};
                s.transparency = fade;
                shape->setStyle(s);
                canvas->setSelectedShape(shape.get());
                continue;
            }
            shape->setStyle(s);
        }
        canvas->setTool(CanvasWidget::Tool::Transparency);
        doc->notifyShapeChanged(nullptr);
        QTest::qWait(150);
        QVERIFY(window.grab().save(dir + QStringLiteral("/window-effects.png")));

        // A curve with arrowheads: the property bar offers them, as does the Outline docker.
        Style arrowStyle;
        arrowStyle.fill = Qt::transparent;
        arrowStyle.stroke = QColor(0xe5, 0x39, 0x35);
        arrowStyle.strokeWidth = 4;
        arrowStyle.startArrow = Arrowhead::Circle;
        arrowStyle.endArrow = Arrowhead::Triangle;
        SubPath curve;
        curve.nodes = {PathNode::corner(QPointF(120, 980)), PathNode{QPointF(520, 960), QPointF(300, 1100), QPointF(520, 960)}};
        Shape *arrow = doc->insertShape(-1, std::make_unique<Shape>(PathData{curve}, arrowStyle));
        canvas->setTool(CanvasWidget::Tool::Select);
        canvas->setSelectedShape(arrow);
        if (auto *properties = window.findChild<PropertiesPanel *>())
            properties->showSection(PropertiesPanel::Section::Outline);
        QTest::qWait(150);
        QVERIFY(window.grab().save(dir + QStringLiteral("/window-arrows.png")));
        theme::apply(ColorScheme::Light);
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

void CanvasTests::objectToolsWithModifiers()
{
    m_canvas->setZoom(1.0);
    auto dragWith = [this](const QPointF &from, const QPointF &to, Qt::KeyboardModifiers modifiers) {
        sendMouse(m_canvas, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton, modifiers);
        for (int i = 1; i <= 5; ++i)
            sendMouse(m_canvas, QEvent::MouseMove, from + (to - from) * (i / 5.0), Qt::NoButton, Qt::LeftButton,
                      modifiers);
        sendMouse(m_canvas, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton, modifiers);
    };

    // Ctrl: regular (as wide as high).
    ToolSettings settings;
    settings.polygonSides = 6;
    m_canvas->setToolSettings(settings);
    m_canvas->setTool(CanvasWidget::Tool::Polygon);
    dragWith({200, 200}, {300, 260}, Qt::ControlModifier);
    QCOMPARE(m_doc->count(), 1);
    const Shape *polygon = m_doc->shapes().back().get();
    QCOMPARE(polygon->kind(), ShapeKind::Path);
    QCOMPARE(polygon->pathData().front().nodes.size(), std::size_t(6));

    // Shift: from the centre outwards.
    m_canvas->setTool(CanvasWidget::Tool::Ellipse);
    dragWith({400, 400}, {450, 420}, Qt::ShiftModifier);
    const QRectF ellipse = m_canvas->viewTransform().mapRect(m_doc->shapes().back()->bounds());
    QVERIFY(std::abs(ellipse.center().x() - 400) < 1 && std::abs(ellipse.center().y() - 400) < 1);
    QVERIFY(std::abs(ellipse.width() - 100) < 1 && std::abs(ellipse.height() - 40) < 1);

    m_canvas->setTool(CanvasWidget::Tool::Star);
    dragWith({100, 100}, {160, 160}, Qt::NoModifier);
    QCOMPARE(m_doc->shapes().back()->pathData().front().nodes.size(), std::size_t(10));
    m_canvas->setTool(CanvasWidget::Tool::Spiral);
    dragWith({500, 100}, {600, 200}, Qt::NoModifier);
    QVERIFY(!m_doc->shapes().back()->pathData().front().closed);
    m_canvas->setTool(CanvasWidget::Tool::GraphPaper);
    dragWith({100, 500}, {300, 600}, Qt::NoModifier);
    QCOMPARE(m_doc->shapes().back()->children().size(), std::size_t(12));

    // New rectangles get the corner radius from the tool options.
    settings.cornerRadius = 8;
    m_canvas->setToolSettings(settings);
    m_canvas->setTool(CanvasWidget::Tool::Rectangle);
    dragWith({600, 400}, {700, 500}, Qt::NoModifier);
    QCOMPARE(m_doc->shapes().back()->cornerRadius(), 8.0);
    QCOMPARE(m_doc->count(), 6);
}

void CanvasTests::lineToolSnapsAngles()
{
    m_canvas->setZoom(1.0);
    m_canvas->setTool(CanvasWidget::Tool::Line);
    const QPointF from(200, 200), to(300, 207); // about 4°: Ctrl makes it horizontal
    sendMouse(m_canvas, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier);
    sendMouse(m_canvas, QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton, Qt::ControlModifier);
    sendMouse(m_canvas, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton, Qt::ControlModifier);
    QCOMPARE(m_doc->count(), 1);
    const auto &nodes = m_doc->shapes().front()->pathData().front().nodes;
    QCOMPARE(nodes.size(), std::size_t(2));
    QVERIFY(std::abs(nodes[0].point.y() - nodes[1].point.y()) < 1e-6);
    QVERIFY(!m_doc->shapes().front()->style().hasFill());
}

void CanvasTests::eyedropperPicksAndApplies()
{
    Style red;
    red.fill = QColor(255, 0, 0);
    red.stroke = Qt::transparent;
    m_doc->insertShape(-1, std::make_unique<Shape>(ShapeKind::Rectangle, QRectF(0, 0, 100, 100), red));
    Shape *target = m_doc->insertShape(-1, std::make_unique<Shape>(ShapeKind::Rectangle, QRectF(200, 0, 100, 100)));
    QSignalSpy picked(m_canvas, &CanvasWidget::eyedropperColorChanged);

    m_canvas->setTool(CanvasWidget::Tool::Eyedropper);
    click(m_canvas, m_canvas->documentToWidget({50, 50}));
    QCOMPARE(picked.count(), 1);
    QVERIFY(m_canvas->eyedropperColor());
    QCOMPARE(m_canvas->eyedropperColor()->rgb(), QColor(255, 0, 0).rgb());

    click(m_canvas, m_canvas->documentToWidget({250, 50})); // inside: fill
    QCOMPARE(target->style().fill.rgb(), QColor(255, 0, 0).rgb());
    m_undo->undo();
    QVERIFY(target->style().fill.rgb() != QColor(255, 0, 0).rgb());

    QTest::keyClick(m_canvas, Qt::Key_Escape); // pick again
    QVERIFY(!m_canvas->eyedropperColor());
}

void CanvasTests::guidelinesFromRulersAndSnapping()
{
    m_canvas->setZoom(1.0);
    // Pull a vertical guide out of the left ruler (starting outside the canvas).
    const double x = 300;
    m_canvas->beginGuideDrag(Qt::Vertical, {-10, 300});
    m_canvas->updateGuideDrag({x, 300});
    m_canvas->endGuideDrag({x, 300});
    QCOMPARE(m_doc->guides().size(), std::size_t(1));
    const double guideX = m_doc->guides().front().position;
    QVERIFY(std::abs(m_canvas->documentToWidget({guideX, 0}).x() - x) < 1e-6);

    // Objects dragged near the guide snap their edge onto it.
    SnapSettings snap;
    snap.guides = true;
    m_canvas->setSnapSettings(snap);
    const QPointF topLeft = m_canvas->viewTransform().inverted().map(QPointF(100, 400));
    Shape *rect = m_doc->insertShape(-1, std::make_unique<Shape>(ShapeKind::Rectangle, QRectF(topLeft, QSizeF(50, 50))));
    m_canvas->setTool(CanvasWidget::Tool::Select);
    dragFromTo(m_canvas, {120, 420}, {316, 420}); // left edge lands 4 px past the guide
    QVERIFY(std::abs(rect->bounds().left() - guideX) < 1e-6);
    m_undo->undo();

    // Drag the guide with the pick tool, then drop it on the ruler to delete it.
    dragFromTo(m_canvas, {x, 200}, {x + 50, 200});
    QVERIFY(std::abs(m_canvas->documentToWidget({m_doc->guides().front().position, 0}).x() - (x + 50)) < 1e-6);
    dragFromTo(m_canvas, {x + 50, 200}, {-5, 200});
    QVERIFY(m_doc->guides().empty());
    m_undo->undo();
    QCOMPARE(m_doc->guides().size(), std::size_t(1));

    // Ctrl keeps a move horizontal.
    m_canvas->setSnapSettings(SnapSettings{false, false, false, false});
    const QPointF before = rect->bounds().topLeft();
    press(m_canvas, {120, 420});
    sendMouse(m_canvas, QEvent::MouseMove, {200, 460}, Qt::NoButton, Qt::LeftButton, Qt::ControlModifier);
    sendMouse(m_canvas, QEvent::MouseButtonRelease, {200, 460}, Qt::LeftButton, Qt::NoButton, Qt::ControlModifier);
    QCOMPARE(rect->bounds().top(), before.y());
    QVERIFY(rect->bounds().left() > before.x());
}

void CanvasTests::spaceTogglesThePickTool()
{
    m_canvas->setTool(CanvasWidget::Tool::Rectangle);
    m_canvas->setFocus();
    QTest::keyClick(m_canvas, Qt::Key_Space);
    QCOMPARE(m_canvas->tool(), CanvasWidget::Tool::Select);
    QTest::keyClick(m_canvas, Qt::Key_Space);
    QCOMPARE(m_canvas->tool(), CanvasWidget::Tool::Rectangle);

    // Holding Space to pan keeps the tool.
    QTest::keyPress(m_canvas, Qt::Key_Space);
    dragFromTo(m_canvas, {300, 300}, {350, 300});
    QTest::keyRelease(m_canvas, Qt::Key_Space);
    QCOMPARE(m_canvas->tool(), CanvasWidget::Tool::Rectangle);
}

void CanvasTests::secondClickRotatesAndSkews()
{
    m_canvas->setZoom(1.0);
    const QTransform toDoc = m_canvas->viewTransform().inverted();
    Shape *rect = m_doc->insertShape(
        -1, std::make_unique<Shape>(ShapeKind::Rectangle, toDoc.mapRect(QRectF(300, 300, 200, 100))));
    m_canvas->setTool(CanvasWidget::Tool::Select);
    click(m_canvas, {400, 350}); // select
    QVERIFY(!m_canvas->rotateMode());
    click(m_canvas, {400, 350}); // click again: rotate/skew handles
    QVERIFY(m_canvas->rotateMode());

    // Drag the top-right corner arrow a quarter turn around the centre (400, 350), with Ctrl.
    const QRectF box = m_canvas->viewTransform().mapRect(rect->bounds()).adjusted(-3, -3, 3, 3);
    const QPointF corner = box.topRight();
    const QPointF centre(400, 350);
    const QPointF turned = centre + QTransform().rotate(88).map(corner - centre);
    sendMouse(m_canvas, QEvent::MouseButtonPress, corner, Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier);
    sendMouse(m_canvas, QEvent::MouseMove, turned, Qt::NoButton, Qt::LeftButton, Qt::ControlModifier);
    sendMouse(m_canvas, QEvent::MouseButtonRelease, turned, Qt::LeftButton, Qt::NoButton, Qt::ControlModifier);
    QCOMPARE(rect->kind(), ShapeKind::Rectangle);
    QVERIFY(std::abs(rect->rotation() - 90) < 1e-6); // 88° snapped to 90°
    const QRectF rotated = m_canvas->viewTransform().mapRect(rect->bounds());
    QVERIFY(std::abs(rotated.width() - 100) < 1e-6 && std::abs(rotated.height() - 200) < 1e-6);
    QVERIFY(std::abs(rotated.center().x() - 400) < 1e-6 && std::abs(rotated.center().y() - 350) < 1e-6);
    m_undo->undo();
    QVERIFY(rect->shapeTransform().isIdentity());

    // An edge arrow skews.
    const QRectF upright = m_canvas->viewTransform().mapRect(rect->bounds()).adjusted(-3, -3, 3, 3);
    dragFromTo(m_canvas, QPointF(upright.center().x(), upright.top()), QPointF(upright.center().x() + 40, upright.top()));
    QVERIFY(!rect->shapeTransform().isIdentity());
    QVERIFY(rect->bounds().width() > toDoc.mapRect(QRectF(0, 0, 200, 1)).width() + 1);
    m_undo->undo();

    // Clicking again (away from the centre marker) goes back to resizing; the property bar's
    // angle rotates too.
    click(m_canvas, {340, 330});
    QVERIFY(!m_canvas->rotateMode());
    m_canvas->rotateSelection(45);
    QVERIFY(std::abs(rect->rotation() - 45) < 1e-6);
    m_undo->undo();
    QVERIFY(rect->shapeTransform().isIdentity());
}

void CanvasTests::propertiesDockerAppliesStyles()
{
    PropertiesPanel panel(m_doc, m_canvas);
    auto button = [&panel](const QString &tip) {
        for (QToolButton *b : panel.findChildren<QToolButton *>()) {
            if (b->toolTip() == tip)
                return b;
        }
        return static_cast<QToolButton *>(nullptr);
    };
    auto spin = [&panel](const QString &suffix) {
        for (QSpinBox *b : panel.findChildren<QSpinBox *>()) {
            if (b->suffix() == suffix)
                return b;
        }
        return static_cast<QSpinBox *>(nullptr);
    };

    Shape *rect = m_doc->insertShape(-1, std::make_unique<Shape>(ShapeKind::Rectangle, QRectF(0, 0, 100, 50)));
    m_canvas->setSelectedShape(rect);
    const int before = m_undo->count();

    // Fill: radial gradient, then no fill.
    button(QStringLiteral("Radial gradient"))->click();
    QVERIFY(rect->style().gradient);
    QCOMPARE(rect->style().gradient->type, Gradient::Type::Radial);
    button(QStringLiteral("No fill"))->click();
    QVERIFY(!rect->style().hasFill());

    // Outline: width (in the display unit), dashes, ends and corners.
    auto *width = panel.findChild<QDoubleSpinBox *>();
    QVERIFY(width);
    width->setValue(Units::instance().toDisplay(4.0));
    QVERIFY(std::abs(rect->style().strokeWidth - 4.0) < 0.05);
    auto *dashes = panel.findChild<QComboBox *>();
    QVERIFY(dashes);
    dashes->setCurrentIndex(1);
    emit dashes->activated(1);
    QCOMPARE(rect->style().dashes, PropertiesPanel::dashPresets()[1]);
    button(QStringLiteral("Round ends"))->click();
    QCOMPARE(rect->style().cap, LineCap::Round);
    button(QStringLiteral("Round corners"))->click();
    QCOMPARE(rect->style().join, LineJoin::Round);

    // Transparency.
    spin(QStringLiteral(" %"))->setValue(30);
    QVERIFY(std::abs(rect->style().opacity - 0.7) < 1e-9);

    // Every change is its own undoable step.
    QCOMPARE(m_undo->count(), before + 7);
    while (m_undo->index() > before)
        m_undo->undo();
    QVERIFY(!rect->style().gradient);
    QVERIFY(rect->style().dashes.empty());
    QCOMPARE(rect->style().opacity, 1.0);

    // With nothing selected it sets the style of new shapes.
    m_canvas->setSelection({});
    button(QStringLiteral("Square ends"))->click();
    QCOMPARE(m_canvas->defaultStyle().cap, LineCap::Square);
    QCOMPARE(m_undo->index(), before); // not an undoable document change
}

void CanvasTests::textToolDrawsParagraphFrames()
{
    m_canvas->setZoom(1.0);
    m_canvas->setTool(CanvasWidget::Tool::Text);
    dragFromTo(m_canvas, {200, 200}, {400, 500}); // a frame
    QVERIFY(m_canvas->editedText());
    QVERIFY(m_canvas->editedText()->textData().isParagraph());
    QTest::keyClicks(m_canvas, QStringLiteral("Paragraph text wraps inside its frame when it gets too long"));
    m_canvas->finishTextEdit();
    QCOMPARE(m_doc->count(), 1);
    Shape *text = m_doc->shapes().front().get();
    const QRectF frame = m_canvas->viewTransform().mapRect(text->bounds());
    QVERIFY(std::abs(frame.width() - 200) < 1e-6 && std::abs(frame.height() - 300) < 1e-6);
    QVERIFY(textlayout::layout(text->textData(), text->textOrigin()).lines.size() > 1);

    // A click (no drag) still makes artistic text.
    click(m_canvas, {600, 200});
    QVERIFY(m_canvas->editedText() && !m_canvas->editedText()->textData().isParagraph());
    QTest::keyClicks(m_canvas, QStringLiteral("Title"));
    m_canvas->finishTextEdit();
    QCOMPARE(m_doc->count(), 2);

    // Ctrl+F8 converts both ways, undoably, keeping the words.
    m_canvas->setTool(CanvasWidget::Tool::Select);
    m_canvas->setSelectedShape(text);
    m_canvas->convertTextKind();
    Shape *artistic = m_canvas->selectedShape();
    QVERIFY(artistic && !artistic->textData().isParagraph());
    QVERIFY(artistic->textData().text.contains(QLatin1Char('\n')));
    m_canvas->convertTextKind();
    QVERIFY(m_canvas->selectedShape()->textData().isParagraph());
    m_undo->undo();
    m_undo->undo();
    QCOMPARE(m_doc->shapes().front()->textData().text,
             QStringLiteral("Paragraph text wraps inside its frame when it gets too long"));
}

void CanvasTests::formatPartOfAText()
{
    m_canvas->setZoom(1.0);
    m_canvas->setTool(CanvasWidget::Tool::Text);
    click(m_canvas, {200, 200});
    QTest::keyClicks(m_canvas, QStringLiteral("Hello world"));
    // Shift+Left five times selects "world".
    for (int i = 0; i < 5; ++i)
        QTest::keyClick(m_canvas, Qt::Key_Left, Qt::ShiftModifier);
    QCOMPARE(m_canvas->textSelection(), std::make_pair(6, 11));

    // The text bar's change applies to the selected part only.
    TextFormat format = m_canvas->currentTextFormat();
    format.bold = true;
    format.size = 60;
    m_canvas->setTextFormat(format);
    CharFormat red;
    red.color = QColor(Qt::red);
    m_canvas->applyCharFormat(red);
    const TextData &data = m_canvas->editedText()->textData();
    QVERIFY(!data.format.bold);
    QCOMPARE(data.runs.size(), std::size_t(1));
    QCOMPARE(data.runs.front().start, 6);
    QVERIFY(*data.runs.front().format.bold);
    QCOMPARE(*data.runs.front().format.size, 60.0);
    QCOMPARE(*data.runs.front().format.color, QColor(Qt::red));
    QVERIFY(m_canvas->currentTextFormat().bold);

    // Typing over the selection replaces it, in its format.
    QTest::keyClicks(m_canvas, QStringLiteral("there"));
    QCOMPARE(m_canvas->editedText()->textData().text, QStringLiteral("Hello there"));
    QVERIFY(richtext::effectiveFormat(m_canvas->editedText()->textData(), 8).bold);

    // Clicking places the caret; Ctrl+A selects everything; Ctrl+B toggles bold on it.
    const QLineF caret = m_canvas->viewTransform().map(
        textlayout::caret(m_canvas->editedText()->textData(), m_canvas->editedText()->textOrigin(), 2));
    click(m_canvas, caret.center());
    QCOMPARE(m_canvas->textCursor(), 2);
    QVERIFY(!m_canvas->hasTextSelection());
    QTest::keyClick(m_canvas, Qt::Key_A, Qt::ControlModifier);
    QCOMPARE(m_canvas->textSelection(), std::make_pair(0, 11));
    QTest::keyClick(m_canvas, Qt::Key_B, Qt::ControlModifier);
    QVERIFY(richtext::effectiveFormat(m_canvas->editedText()->textData(), 0).bold);

    m_canvas->finishTextEdit();
    QCOMPARE(m_doc->count(), 1);
    QVERIFY(!m_doc->shapes().front()->textData().runs.empty());
}

void CanvasTests::shadowAndTransparencyTools()
{
    m_canvas->setZoom(1.0);
    const QTransform toDoc = m_canvas->viewTransform().inverted();
    Shape *rect = m_doc->insertShape(-1, std::make_unique<Shape>(ShapeKind::Rectangle, toDoc.mapRect(QRectF(200, 200, 200, 100))));

    // Shadow: drag from the object.
    m_canvas->setTool(CanvasWidget::Tool::DropShadow);
    dragFromTo(m_canvas, {300, 250}, {320, 280});
    QVERIFY(rect->style().shadow);
    QVERIFY(std::abs(rect->style().shadow->offset.x() - toDoc.map(QPointF(20, 0)).x() + toDoc.map(QPointF(0, 0)).x()) < 1e-6);
    QVERIFY(std::abs(rect->style().shadow->offset.y() - (toDoc.map(QPointF(0, 30)).y() - toDoc.map(QPointF(0, 0)).y())) < 1e-6);
    m_undo->undo();
    QVERIFY(!rect->style().shadow);
    m_undo->redo();

    // The property bar edits it.
    PropertyBar bar(m_doc, m_undo);
    bar.setCanvas(m_canvas);
    bar.setTool(CanvasWidget::Tool::DropShadow);
    QSpinBox *shadowOpacity = nullptr;
    for (QSpinBox *box : bar.findChildren<QSpinBox *>()) {
        if (box->toolTip() == QStringLiteral("How dark the shadow is"))
            shadowOpacity = box;
    }
    QVERIFY(shadowOpacity);
    shadowOpacity->setValue(25);
    emit shadowOpacity->editingFinished();
    QCOMPARE(rect->style().shadow->color.alpha(), qRound(0.25 * 255));

    // Transparency: drag across the object, then move the end handle.
    m_canvas->setTool(CanvasWidget::Tool::Transparency);
    dragFromTo(m_canvas, {200, 250}, {400, 250});
    QVERIFY(rect->style().transparency);
    const Gradient fade = *rect->style().transparency;
    QVERIFY(std::abs(fade.start.x()) < 1e-6 && std::abs(fade.end.x() - 1.0) < 1e-6);
    QCOMPARE(fade.stops.front().color.alpha(), 255);
    QCOMPARE(fade.stops.back().color.alpha(), 0);
    dragFromTo(m_canvas, {400, 250}, {300, 250}); // the end square
    QVERIFY(std::abs(rect->style().transparency->end.x() - 0.5) < 1e-6);

    // Uniform transparency from the property bar replaces the gradient.
    bar.setTool(CanvasWidget::Tool::Transparency);
    QComboBox *type = bar.findChild<QComboBox *>(QString(), Qt::FindDirectChildrenOnly);
    for (QComboBox *box : bar.findChildren<QComboBox *>()) {
        if (box->toolTip() == QStringLiteral("Kind of transparency"))
            type = box;
    }
    QVERIFY(type);
    type->setCurrentIndex(1);
    emit type->activated(1);
    QVERIFY(!rect->style().transparency);
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
