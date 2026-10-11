#include "CanvasWidget.h"

#include "core/Commands.h"
#include "core/Document.h"
#include "core/PathOps.h"
#include "core/Render.h"
#include "core/TextLayout.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QUndoStack>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace opendraw {

namespace {
constexpr double kMinZoom = 0.02;
constexpr double kMaxZoom = 64.0;
constexpr double kZoomStep = 1.25;
constexpr double kHitTolerancePx = 4.0;
constexpr double kNodeHitPx = 6.0;
constexpr double kMinCreateSizePx = 3.0;
constexpr double kMinMarqueePx = 3.0;
constexpr double kPageMarginPx = 24.0;
constexpr double kHandleSizePx = 7.0;
constexpr double kNodeSizePx = 7.0;
constexpr double kCloseDistancePx = 8.0;
constexpr double kFreehandTolerancePx = 1.5;

double distance(const QPointF &a, const QPointF &b)
{
    return std::hypot(a.x() - b.x(), a.y() - b.y());
}

QRectF squareAround(const QPointF &center, double size)
{
    return {center - QPointF(size / 2, size / 2), QSizeF(size, size)};
}
} // namespace

CanvasWidget::CanvasWidget(Document *document, QUndoStack *undoStack, QWidget *parent)
    : QWidget(parent)
    , m_document(document)
    , m_undoStack(undoStack)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);

    connect(m_document, &Document::changed, this, qOverload<>(&QWidget::update));
    connect(m_document, &Document::shapeRemoved, this, [this](Shape *shape) {
        if (shape == m_editText && !m_pendingText) {
            m_editText = nullptr;
            emit textEditingChanged();
        }
        if (!isSelected(shape))
            return;
        std::vector<Shape *> remaining = m_selection;
        std::erase(remaining, shape);
        setSelection(std::move(remaining));
    });
    connect(m_document, &Document::cleared, this, [this] {
        setSelection({});
        m_bezier = {};
    });
}

void CanvasWidget::setTool(Tool tool)
{
    if (tool == m_tool)
        return;
    finishTextEdit();
    if (m_tool == Tool::Bezier)
        finishCurve();
    m_tool = tool;
    setActiveNode({});
    updateCursor();
    update();
}

void CanvasWidget::setSelectedShape(Shape *shape)
{
    setSelection(shape ? std::vector<Shape *>{shape} : std::vector<Shape *>{});
}

void CanvasWidget::setSelection(std::vector<Shape *> shapes)
{
    if (shapes == m_selection)
        return;
    m_selection = std::move(shapes);
    setActiveNode({});
    update();
    emit selectionChanged();
}

bool CanvasWidget::isSelected(const Shape *shape) const
{
    return std::find(m_selection.begin(), m_selection.end(), shape) != m_selection.end();
}

QRectF CanvasWidget::selectionBounds() const
{
    std::vector<QRectF> boxes;
    for (const Shape *shape : m_selection)
        boxes.push_back(shape->bounds());
    return arrange::unite(boxes);
}

// ---------------------------------------------------------------------------------------------
// View

QTransform CanvasWidget::viewTransform() const
{
    return QTransform::fromTranslate(m_pan.x(), m_pan.y()).scale(m_zoom, m_zoom);
}

QPointF CanvasWidget::toDocument(const QPointF &widgetPosition) const
{
    return (widgetPosition - m_pan) / m_zoom;
}

QPointF CanvasWidget::toWidget(const QPointF &documentPosition) const
{
    return documentPosition * m_zoom + m_pan;
}

void CanvasWidget::zoomAround(const QPointF &widgetPosition, double factor)
{
    const QPointF anchor = toDocument(widgetPosition);
    m_zoom = std::clamp(m_zoom * factor, kMinZoom, kMaxZoom);
    m_pan = widgetPosition - anchor * m_zoom;
    update();
    emit zoomChanged(m_zoom);
}

void CanvasWidget::zoomIn()
{
    zoomAround(rect().center(), kZoomStep);
}

void CanvasWidget::zoomOut()
{
    zoomAround(rect().center(), 1.0 / kZoomStep);
}

void CanvasWidget::zoomToActualSize()
{
    zoomAround(rect().center(), 1.0 / m_zoom);
}

void CanvasWidget::fitPage()
{
    const QSizeF page = m_document->pageSize();
    const double availableWidth = std::max(width() - 2.0 * kPageMarginPx, 1.0);
    const double availableHeight = std::max(height() - 2.0 * kPageMarginPx, 1.0);
    m_zoom = std::clamp(std::min(availableWidth / page.width(), availableHeight / page.height()),
                        kMinZoom, kMaxZoom);
    m_pan = QPointF((width() - page.width() * m_zoom) / 2.0, (height() - page.height() * m_zoom) / 2.0);
    update();
    emit zoomChanged(m_zoom);
}

void CanvasWidget::updateCursor()
{
    if (m_spaceHeld)
        setCursor(Qt::OpenHandCursor);
    else if (m_tool == Tool::Select || m_tool == Tool::Node)
        unsetCursor();
    else if (m_tool == Tool::Text)
        setCursor(Qt::IBeamCursor);
    else
        setCursor(Qt::CrossCursor);
}

// ---------------------------------------------------------------------------------------------
// Text tool

TextFormat CanvasWidget::currentTextFormat() const
{
    if (m_editText)
        return m_editText->textData().format;
    if (const Shape *shape = selectedShape(); shape && shape->kind() == ShapeKind::Text)
        return shape->textData().format;
    return m_defaultTextFormat;
}

void CanvasWidget::setTextFormat(const TextFormat &format)
{
    if (m_editText) {
        TextData data = m_editText->textData();
        data.format = format;
        m_editText->setTextData(std::move(data));
        m_defaultTextFormat = format;
        if (m_pendingText)
            update();
        else
            m_document->notifyShapeChanged(m_editText);
        return;
    }

    std::vector<Shape *> texts;
    for (Shape *shape : m_selection) {
        shape->forEachLeaf([&texts](Shape &leaf) {
            if (leaf.kind() == ShapeKind::Text)
                texts.push_back(&leaf);
        });
    }
    if (texts.empty()) {
        m_defaultTextFormat = format;
        return;
    }
    m_undoStack->beginMacro(tr("Text format"));
    for (Shape *text : texts) {
        TextData data = text->textData();
        data.format = format;
        if (data != text->textData())
            m_undoStack->push(new SetTextCommand(m_document, text, text->textData(), data, tr("Text format")));
    }
    m_undoStack->endMacro();
}

void CanvasWidget::beginTextEdit(Shape *text)
{
    finishTextEdit();
    setSelectedShape(text);
    m_editText = text;
    m_textBeforeEdit = text->textData();
    m_textCursor = int(text->textData().text.size());
    update();
    emit textEditingChanged();
}

void CanvasWidget::beginNewText(const QPointF &origin)
{
    finishTextEdit();
    setSelection({});
    const Style style{QColor(Qt::black), QColor(Qt::transparent), 0.0};
    m_pendingText = std::make_unique<Shape>(TextData{QString(), m_defaultTextFormat}, origin, style);
    m_editText = m_pendingText.get();
    m_textBeforeEdit = {};
    m_textCursor = 0;
    update();
    emit textEditingChanged();
}

void CanvasWidget::finishTextEdit()
{
    if (!m_editText)
        return;
    Shape *text = m_editText;
    m_editText = nullptr;

    if (m_pendingText) {
        std::unique_ptr<Shape> pending = std::move(m_pendingText);
        if (!pending->textData().text.trimmed().isEmpty())
            addNewShape(std::move(pending));
    } else {
        const TextData now = text->textData();
        if (now.text.trimmed().isEmpty()) {
            // Typing everything away deletes the text; undo brings back the original.
            text->setTextData(m_textBeforeEdit);
            m_undoStack->push(new RemoveShapeCommand(m_document, text));
        } else if (now != m_textBeforeEdit) {
            m_undoStack->push(new SetTextCommand(m_document, text, m_textBeforeEdit, now, tr("Edit text")));
        }
    }
    update();
    emit textEditingChanged();
}

void CanvasWidget::setEditedText(const QString &text, int cursor)
{
    TextData data = m_editText->textData();
    data.text = text;
    m_editText->setTextData(std::move(data));
    m_textCursor = std::clamp(cursor, 0, int(text.size()));
    if (m_pendingText)
        update();
    else
        m_document->notifyShapeChanged(m_editText);
}

bool CanvasWidget::isTextEditingKey(const QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Backspace:
    case Qt::Key_Delete:
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Home:
    case Qt::Key_End:
    case Qt::Key_Escape:
        return true;
    default:
        break;
    }
    if (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))
        return false;
    const QString text = event->text();
    return !text.isEmpty() && text.at(0).isPrint();
}

bool CanvasWidget::handleTextKey(QKeyEvent *event)
{
    const QString text = m_editText->textData().text;
    const int cursor = m_textCursor;
    switch (event->key()) {
    case Qt::Key_Escape:
        finishTextEdit();
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        setEditedText(text.left(cursor) + QLatin1Char('\n') + text.mid(cursor), cursor + 1);
        return true;
    case Qt::Key_Backspace:
        if (cursor > 0)
            setEditedText(text.left(cursor - 1) + text.mid(cursor), cursor - 1);
        return true;
    case Qt::Key_Delete:
        if (cursor < text.size())
            setEditedText(text.left(cursor) + text.mid(cursor + 1), cursor);
        return true;
    case Qt::Key_Left:
        m_textCursor = std::max(0, cursor - 1);
        update();
        return true;
    case Qt::Key_Right:
        m_textCursor = std::min(int(text.size()), cursor + 1);
        update();
        return true;
    case Qt::Key_Home:
        m_textCursor = int(text.lastIndexOf(QLatin1Char('\n'), cursor - 1)) + 1;
        update();
        return true;
    case Qt::Key_End: {
        const int end = int(text.indexOf(QLatin1Char('\n'), cursor));
        m_textCursor = end < 0 ? int(text.size()) : end;
        update();
        return true;
    }
    default:
        break;
    }
    if (!isTextEditingKey(event))
        return false;
    const QString typed = event->text();
    setEditedText(text.left(cursor) + typed + text.mid(cursor), cursor + int(typed.size()));
    return true;
}

bool CanvasWidget::event(QEvent *event)
{
    // While typing, keys that edit text must not trigger the window's shortcuts (L aligns,
    // Delete removes the object...). Other shortcuts end the editing and then run.
    if (event->type() == QEvent::ShortcutOverride && m_editText) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (isTextEditingKey(key)) {
            event->accept();
            return true;
        }
        finishTextEdit();
    }
    return QWidget::event(event);
}

void CanvasWidget::paintTextEdit(QPainter &painter)
{
    if (!m_editText)
        return;
    const QColor highlight = palette().color(QPalette::Highlight);
    painter.setPen(QPen(highlight, 1, Qt::DashLine));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(viewTransform().mapRect(m_editText->bounds()).adjusted(-3, -3, 3, 3));

    const QLineF caret = textlayout::caret(m_editText->textData(), m_editText->textOrigin(), m_textCursor);
    painter.setPen(QPen(Qt::black, 1.5));
    painter.drawLine(toWidget(caret.p1()), toWidget(caret.p2()));
}

// ---------------------------------------------------------------------------------------------
// Editing actions

Style CanvasWidget::curveStyle(bool closed) const
{
    Style style = m_defaultStyle;
    if (!closed)
        style.fill = Qt::transparent;
    if (!style.hasStroke()) {
        style.stroke = Qt::black;
        style.strokeWidth = std::max(style.strokeWidth, 1.0);
    }
    return style;
}

void CanvasWidget::addNewShape(std::unique_ptr<Shape> shape)
{
    auto *command = new AddShapeCommand(m_document, std::move(shape));
    Shape *created = command->shape();
    m_undoStack->push(command);
    setSelectedShape(created);
}

void CanvasWidget::selectAll()
{
    setSelection(m_document->order());
}

void CanvasWidget::deleteSelection()
{
    if (!m_bezier.nodes.empty()) {
        m_bezier.nodes.pop_back();
        update();
        return;
    }
    if (m_tool == Tool::Node && activeNodeIsValid()) {
        removeActiveNode();
        return;
    }
    if (m_selection.empty())
        return;
    const std::vector<Shape *> shapes = m_selection;
    m_undoStack->beginMacro(tr("Delete"));
    for (Shape *shape : shapes)
        m_undoStack->push(new RemoveShapeCommand(m_document, shape));
    m_undoStack->endMacro();
}

void CanvasWidget::convertSelectionToCurves()
{
    Shape *shape = selectedShape();
    if (!shape || shape->kind() == ShapeKind::Path || shape->kind() == ShapeKind::Group)
        return;
    auto curve = std::make_unique<Shape>(pathops::fromPainterPath(shape->path()), shape->style());
    auto *command = new ReplaceShapeCommand(m_document, shape, std::move(curve), tr("Convert to curves"));
    Shape *created = command->newShape();
    m_undoStack->push(command);
    setSelectedShape(created);
}

void CanvasWidget::finishCurve()
{
    SubPath curve = std::move(m_bezier);
    m_bezier = {};
    if (curve.nodes.size() >= 2) {
        const bool closed = curve.closed;
        addNewShape(std::make_unique<Shape>(PathData{std::move(curve)}, curveStyle(closed)));
    }
    update();
}

// ---------------------------------------------------------------------------------------------
// Arrange

void CanvasWidget::groupSelection()
{
    if (m_selection.size() < 2)
        return;
    auto *command = new GroupCommand(m_document, m_selection);
    Shape *group = command->group();
    m_undoStack->push(command);
    setSelectedShape(group);
}

void CanvasWidget::ungroupSelection()
{
    std::vector<Shape *> groups;
    for (Shape *shape : m_selection) {
        if (shape->kind() == ShapeKind::Group)
            groups.push_back(shape);
    }
    if (groups.empty())
        return;
    std::vector<Shape *> released;
    m_undoStack->beginMacro(tr("Ungroup"));
    for (Shape *group : groups) {
        auto *command = new UngroupCommand(m_document, group);
        released.insert(released.end(), command->children().begin(), command->children().end());
        m_undoStack->push(command);
    }
    m_undoStack->endMacro();
    setSelection(std::move(released));
}

void CanvasWidget::reorder(std::vector<Shape *> order, const QString &text)
{
    if (order != m_document->order())
        m_undoStack->push(new ReorderCommand(m_document, std::move(order), text));
}

void CanvasWidget::orderToFront()
{
    std::vector<Shape *> order;
    std::vector<Shape *> selected;
    for (Shape *shape : m_document->order())
        (isSelected(shape) ? selected : order).push_back(shape);
    order.insert(order.end(), selected.begin(), selected.end());
    reorder(std::move(order), tr("To front"));
}

void CanvasWidget::orderToBack()
{
    std::vector<Shape *> order;
    std::vector<Shape *> others;
    for (Shape *shape : m_document->order())
        (isSelected(shape) ? order : others).push_back(shape);
    order.insert(order.end(), others.begin(), others.end());
    reorder(std::move(order), tr("To back"));
}

void CanvasWidget::orderForwardOne()
{
    std::vector<Shape *> order = m_document->order();
    // From the top down, so a selected shape never jumps over another selected one.
    for (int i = int(order.size()) - 2; i >= 0; --i) {
        if (isSelected(order[i]) && !isSelected(order[i + 1]))
            std::swap(order[i], order[i + 1]);
    }
    reorder(std::move(order), tr("Forward one"));
}

void CanvasWidget::orderBackOne()
{
    std::vector<Shape *> order = m_document->order();
    for (int i = 1; i < int(order.size()); ++i) {
        if (isSelected(order[i]) && !isSelected(order[i - 1]))
            std::swap(order[i], order[i - 1]);
    }
    reorder(std::move(order), tr("Back one"));
}

void CanvasWidget::moveSelection(const std::vector<QPointF> &offsets, const QString &text)
{
    if (std::all_of(offsets.begin(), offsets.end(), [](const QPointF &p) { return p.isNull(); }))
        return;
    m_undoStack->beginMacro(text);
    for (std::size_t i = 0; i < m_selection.size(); ++i) {
        if (!offsets[i].isNull())
            m_undoStack->push(new MoveShapeCommand(m_document, m_selection[i], offsets[i], false));
    }
    m_undoStack->endMacro();
}

void CanvasWidget::align(arrange::Alignment alignment)
{
    if (m_selection.empty())
        return;
    std::vector<QRectF> boxes;
    for (const Shape *shape : m_selection)
        boxes.push_back(shape->bounds());
    const QRectF reference = m_selection.size() == 1 ? QRectF(QPointF(0, 0), m_document->pageSize())
                                                     : arrange::unite(boxes);
    moveSelection(arrange::alignOffsets(boxes, alignment, reference), tr("Align"));
}

void CanvasWidget::centerOnPage()
{
    if (m_selection.empty())
        return;
    // The selection moves as a block, keeping its layout.
    const QRectF page(QPointF(0, 0), m_document->pageSize());
    const QPointF offset = page.center() - selectionBounds().center();
    moveSelection(std::vector<QPointF>(m_selection.size(), offset), tr("Center on page"));
}

void CanvasWidget::distribute(Qt::Orientation orientation)
{
    std::vector<QRectF> boxes;
    for (const Shape *shape : m_selection)
        boxes.push_back(shape->bounds());
    moveSelection(arrange::distributeOffsets(boxes, orientation), tr("Distribute"));
}

// ---------------------------------------------------------------------------------------------
// Shape tool (node editing)

Shape *CanvasWidget::selectedPath() const
{
    Shape *shape = selectedShape();
    return shape && shape->kind() == ShapeKind::Path ? shape : nullptr;
}

bool CanvasWidget::isValidNode(const NodeRef &ref) const
{
    const Shape *path = selectedPath();
    if (!path || !ref.isValid())
        return false;
    const PathData &data = path->pathData();
    return ref.subpath < int(data.size()) && ref.node < int(data[ref.subpath].nodes.size());
}

bool CanvasWidget::activeNodeIsValid() const
{
    return isValidNode(m_activeNode);
}

bool CanvasWidget::hasNodePairToJoin() const
{
    return activeNodeIsValid() && isValidNode(m_secondaryNode);
}

void CanvasWidget::setActiveNode(const NodeRef &node)
{
    setActiveNode(node, NodeRef());
}

void CanvasWidget::setActiveNode(const NodeRef &node, const NodeRef &secondary)
{
    m_activeNode = node;
    m_secondaryNode = secondary;
    update();
    emit nodeSelectionChanged();
}

CanvasWidget::NodeRef CanvasWidget::nodeAt(const QPointF &widgetPosition) const
{
    const Shape *path = selectedPath();
    if (!path)
        return {};
    NodeRef best;
    double bestDistance = kNodeHitPx;
    const PathData &data = path->pathData();
    for (int s = 0; s < int(data.size()); ++s) {
        for (int n = 0; n < int(data[s].nodes.size()); ++n) {
            const double d = distance(toWidget(data[s].nodes[n].point), widgetPosition);
            if (d <= bestDistance) {
                bestDistance = d;
                best = {s, n};
            }
        }
    }
    return best;
}

std::optional<CanvasWidget::NodeHandle> CanvasWidget::nodeHandleAt(const QPointF &widgetPosition) const
{
    if (!activeNodeIsValid())
        return std::nullopt;
    const PathNode &node = selectedPath()->pathData()[m_activeNode.subpath].nodes[m_activeNode.node];
    if (node.out != node.point && distance(toWidget(node.out), widgetPosition) <= kNodeHitPx)
        return NodeHandle::Out;
    if (node.in != node.point && distance(toWidget(node.in), widgetPosition) <= kNodeHitPx)
        return NodeHandle::In;
    return std::nullopt;
}

void CanvasWidget::commitPathEdit(const PathData &before, const QString &text)
{
    Shape *path = selectedPath();
    if (path && path->pathData() != before)
        m_undoStack->push(new SetPathCommand(m_document, path, before, path->pathData(), text));
}

void CanvasWidget::removeActiveNode()
{
    Shape *path = selectedPath();
    PathData data = path->pathData();
    pathops::removeNode(data, m_activeNode.subpath, m_activeNode.node);
    setActiveNode({});
    if (data.empty()) {
        m_undoStack->push(new RemoveShapeCommand(m_document, path));
        return;
    }
    m_undoStack->push(new SetPathCommand(m_document, path, path->pathData(), data, tr("Delete node")));
}

template<typename Edit>
bool CanvasWidget::editPath(const QString &text, Edit edit)
{
    Shape *path = selectedPath();
    if (!path || !activeNodeIsValid())
        return false;
    PathData data = path->pathData();
    if (!edit(data))
        return false;
    m_undoStack->push(new SetPathCommand(m_document, path, path->pathData(), std::move(data), text));
    return true;
}

void CanvasWidget::makeNodeSmooth()
{
    const NodeRef n = m_activeNode;
    editPath(tr("Make node smooth"),
             [n](PathData &d) { return pathops::setNodeType(d, n.subpath, n.node, NodeType::Smooth); });
}

void CanvasWidget::makeNodeCorner()
{
    const NodeRef n = m_activeNode;
    editPath(tr("Make node corner"),
             [n](PathData &d) { return pathops::setNodeType(d, n.subpath, n.node, NodeType::Corner); });
}

void CanvasWidget::segmentToLine()
{
    const NodeRef n = m_activeNode;
    editPath(tr("To line"), [n](PathData &d) {
        const auto segment = pathops::segmentAtNode(d, n.subpath, n.node);
        return segment && pathops::segmentToLine(d, n.subpath, *segment);
    });
}

void CanvasWidget::segmentToCurve()
{
    const NodeRef n = m_activeNode;
    editPath(tr("To curve"), [n](PathData &d) {
        const auto segment = pathops::segmentAtNode(d, n.subpath, n.node);
        return segment && pathops::segmentToCurve(d, n.subpath, *segment);
    });
}

void CanvasWidget::breakNode()
{
    const NodeRef n = m_activeNode;
    if (editPath(tr("Break node"), [n](PathData &d) { return pathops::breakAtNode(d, n.subpath, n.node); }))
        setActiveNode({});
}

void CanvasWidget::joinNodes()
{
    if (!hasNodePairToJoin())
        return;
    const NodeRef a = m_activeNode;
    const NodeRef b = m_secondaryNode;
    if (editPath(tr("Join nodes"),
                 [a, b](PathData &d) { return pathops::joinNodes(d, a.subpath, a.node, b.subpath, b.node); }))
        setActiveNode({});
}

// ---------------------------------------------------------------------------------------------
// Select tool resize handles

std::array<QRectF, 8> CanvasWidget::resizeHandleRects() const
{
    std::array<QRectF, 8> rects;
    if (m_selection.empty())
        return rects;
    const QRectF box = viewTransform().mapRect(selectionBounds()).adjusted(-3, -3, 3, 3);
    const QPointF centers[] = {box.topLeft(),     QPointF(box.center().x(), box.top()),
                               box.topRight(),    QPointF(box.right(), box.center().y()),
                               box.bottomRight(), QPointF(box.center().x(), box.bottom()),
                               box.bottomLeft(),  QPointF(box.left(), box.center().y())};
    for (int i = 0; i < 8; ++i)
        rects[i] = squareAround(centers[i], kHandleSizePx);
    return rects;
}

int CanvasWidget::resizeHandleAt(const QPointF &widgetPosition) const
{
    if (m_selection.empty() || m_tool != Tool::Select)
        return -1;
    const auto rects = resizeHandleRects();
    for (int i = 0; i < 8; ++i) {
        if (rects[i].adjusted(-2, -2, 2, 2).contains(widgetPosition))
            return i;
    }
    return -1;
}

void CanvasWidget::setHoverCursor(const QPointF &widgetPosition)
{
    static const Qt::CursorShape shapes[] = {Qt::SizeFDiagCursor, Qt::SizeVerCursor, Qt::SizeBDiagCursor,
                                             Qt::SizeHorCursor,   Qt::SizeFDiagCursor, Qt::SizeVerCursor,
                                             Qt::SizeBDiagCursor, Qt::SizeHorCursor};
    const int handle = m_spaceHeld ? -1 : resizeHandleAt(widgetPosition);
    if (handle >= 0)
        setCursor(shapes[handle]);
    else if (m_tool == Tool::Node && !m_spaceHeld && (nodeAt(widgetPosition).isValid() || nodeHandleAt(widgetPosition)))
        setCursor(Qt::SizeAllCursor);
    else
        updateCursor();
}

// ---------------------------------------------------------------------------------------------
// Painting

void CanvasWidget::paintShape(QPainter &painter, const Shape &shape)
{
    render::paintShape(painter, shape);
}

void CanvasWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), palette().color(QPalette::Mid));

    painter.setTransform(viewTransform());

    const QRectF page(QPointF(0, 0), m_document->pageSize());
    const double onePixel = 1.0 / m_zoom;
    painter.fillRect(page.translated(4 * onePixel, 4 * onePixel), QColor(0, 0, 0, 50));
    painter.fillRect(page, Qt::white);
    painter.setPen(QPen(QColor(0, 0, 0, 90), 0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(page);

    for (const auto &shape : m_document->shapes())
        paintShape(painter, *shape);
    if (m_pendingText)
        paintShape(painter, *m_pendingText);

    QPen preview(palette().color(QPalette::Highlight), 0, Qt::DashLine);
    preview.setCosmetic(true);
    if (m_drag == Drag::Create && !m_creating.isEmpty()) {
        painter.setPen(preview);
        painter.setBrush(Qt::NoBrush);
        if (m_tool == Tool::Ellipse)
            painter.drawEllipse(m_creating);
        else
            painter.drawRect(m_creating);
    }
    if (m_drag == Drag::Freehand && m_freehand.size() > 1) {
        const Style style = curveStyle(false);
        painter.setPen(QPen(style.stroke, style.strokeWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPolyline(m_freehand.data(), int(m_freehand.size()));
    }

    painter.resetTransform();
    if (m_editText)
        paintTextEdit(painter);
    else
        paintSelection(painter);
    paintBezierPreview(painter);

    if (m_drag == Drag::Marquee) {
        QColor fill = palette().color(QPalette::Highlight);
        fill.setAlpha(40);
        painter.setPen(QPen(palette().color(QPalette::Highlight), 1, Qt::DashLine));
        painter.setBrush(fill);
        painter.drawRect(viewTransform().mapRect(m_marquee));
    }
}

void CanvasWidget::paintSelection(QPainter &painter)
{
    if (m_selection.empty())
        return;
    const QColor highlight = palette().color(QPalette::Highlight);

    if (m_tool == Tool::Node) {
        Shape *single = selectedShape();
        if (single && single->kind() == ShapeKind::Path) {
            paintNodes(painter, *single);
        } else {
            // Only a single curve has editable nodes; rectangles and ellipses need "Convert to curves".
            painter.setPen(QPen(highlight, 1, Qt::DotLine));
            painter.setBrush(Qt::NoBrush);
            for (const Shape *shape : m_selection)
                painter.drawRect(viewTransform().mapRect(shape->bounds()).adjusted(-3, -3, 3, 3));
        }
        return;
    }

    // With several objects, show each one's box faintly and the combined box with handles.
    if (m_selection.size() > 1) {
        QColor faint = highlight;
        faint.setAlpha(120);
        painter.setPen(QPen(faint, 1, Qt::DotLine));
        painter.setBrush(Qt::NoBrush);
        for (const Shape *shape : m_selection)
            painter.drawRect(viewTransform().mapRect(shape->bounds()));
    }

    const QRectF box = viewTransform().mapRect(selectionBounds()).adjusted(-3, -3, 3, 3);
    painter.setPen(QPen(highlight, 1, Qt::DashLine));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(box);
    if (m_tool == Tool::Select) {
        painter.setPen(QPen(highlight, 1));
        painter.setBrush(Qt::white);
        for (const QRectF &handle : resizeHandleRects())
            painter.drawRect(handle);
    }
}

void CanvasWidget::paintNodes(QPainter &painter, const Shape &shape)
{
    const QColor highlight = palette().color(QPalette::Highlight);

    // Outline of the path on top, so it is visible even without a stroke.
    QPen outline(highlight, 1);
    outline.setCosmetic(true);
    painter.setPen(outline);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(viewTransform().map(shape.path()));

    const PathData &data = shape.pathData();
    if (activeNodeIsValid()) {
        const PathNode &node = data[m_activeNode.subpath].nodes[m_activeNode.node];
        painter.setPen(QPen(highlight, 1));
        for (const QPointF &handle : {node.in, node.out}) {
            if (handle == node.point)
                continue;
            painter.drawLine(toWidget(node.point), toWidget(handle));
            painter.setBrush(highlight);
            painter.drawEllipse(toWidget(handle), 3.5, 3.5);
            painter.setBrush(Qt::NoBrush);
        }
    }

    for (int s = 0; s < int(data.size()); ++s) {
        for (int n = 0; n < int(data[s].nodes.size()); ++n) {
            const bool active = m_activeNode.subpath == s && m_activeNode.node == n;
            const bool secondary = m_secondaryNode.subpath == s && m_secondaryNode.node == n;
            const PathNode &node = data[s].nodes[n];
            painter.setPen(QPen(highlight, 1));
            QColor fill = Qt::white;
            if (active)
                fill = highlight;
            else if (secondary)
                fill = highlight.lighter(160);
            painter.setBrush(fill);
            const QRectF r = squareAround(toWidget(node.point), kNodeSizePx);
            if (node.type == NodeType::Smooth)
                painter.drawEllipse(r);
            else
                painter.drawRect(r);
        }
    }
}

void CanvasWidget::paintBezierPreview(QPainter &painter)
{
    if (m_bezier.nodes.empty())
        return;
    const QColor highlight = palette().color(QPalette::Highlight);

    // The curve so far, plus the segment that would be added at the mouse position.
    PathData preview{m_bezier};
    if (m_drag != Drag::BezierHandle)
        preview.front().nodes.push_back(PathNode::corner(m_cursor));
    QPen pen(curveStyle(false).stroke, 1);
    pen.setCosmetic(true);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(viewTransform().map(pathops::toPainterPath(preview)));

    const PathNode &last = m_bezier.nodes.back();
    painter.setPen(QPen(highlight, 1));
    for (const QPointF &handle : {last.in, last.out}) {
        if (handle == last.point)
            continue;
        painter.drawLine(toWidget(last.point), toWidget(handle));
        painter.setBrush(highlight);
        painter.drawEllipse(toWidget(handle), 3.5, 3.5);
        painter.setBrush(Qt::NoBrush);
    }
    for (std::size_t i = 0; i < m_bezier.nodes.size(); ++i) {
        painter.setBrush(i == 0 ? QBrush(highlight) : QBrush(Qt::white));
        painter.drawRect(squareAround(toWidget(m_bezier.nodes[i].point), kNodeSizePx));
    }
}

void CanvasWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_needsInitialFit && width() > 0 && height() > 0) {
        m_needsInitialFit = false;
        fitPage();
    }
}

// ---------------------------------------------------------------------------------------------
// Mouse

void CanvasWidget::mousePressEvent(QMouseEvent *event)
{
    const QPointF pos = event->position();
    const QPointF docPos = toDocument(pos);
    const bool shift = event->modifiers() & Qt::ShiftModifier;

    if (event->button() == Qt::MiddleButton || (event->button() == Qt::LeftButton && m_spaceHeld)) {
        m_drag = Drag::Pan;
        m_panLast = pos;
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;

    if (m_editText) {
        // Clicking inside the text being edited keeps editing; anywhere else ends it.
        if (m_editText->bounds().contains(docPos)) {
            m_textCursor = int(m_editText->textData().text.size());
            update();
            return;
        }
        finishTextEdit();
    }

    m_dragStart = m_dragLast = docPos;
    const double tolerance = kHitTolerancePx / m_zoom;

    switch (m_tool) {
    case Tool::Text: {
        Shape *hit = m_document->shapeAt(docPos, tolerance);
        if (hit && hit->kind() == ShapeKind::Text) {
            beginTextEdit(hit);
        } else {
            // The click marks the top-left of the text; the first baseline sits one ascent below.
            const double ascent = m_defaultTextFormat.size * 0.8;
            beginNewText(docPos + QPointF(0, ascent));
        }
        break;
    }

    case Tool::Select: {
        if (const int handle = resizeHandleAt(pos); handle >= 0 && !shift) {
            m_drag = Drag::Resize;
            m_resizeHandle = handle;
            m_resizeOriginal = selectionBounds();
            m_resizeOriginals.clear();
            for (const Shape *shape : m_selection)
                m_resizeOriginals.push_back(shape->bounds());
            break;
        }
        Shape *hit = m_document->shapeAt(docPos, tolerance);
        if (!hit) {
            // Empty space: start a rubber band. Shift keeps the current selection.
            m_drag = Drag::Marquee;
            m_marquee = QRectF(docPos, docPos);
            m_marqueeAdds = shift;
            break;
        }
        if (shift) {
            std::vector<Shape *> selection = m_selection;
            if (isSelected(hit))
                std::erase(selection, hit);
            else
                selection.push_back(hit);
            setSelection(std::move(selection));
            break;
        }
        if (!isSelected(hit))
            setSelectedShape(hit);
        m_drag = Drag::Move; // moves everything selected
        break;
    }

    case Tool::Node:
        if (const auto handle = nodeHandleAt(pos)) {
            m_drag = Drag::NodeHandle;
            m_draggedHandle = *handle;
            m_pathBeforeEdit = selectedPath()->pathData();
        } else if (const NodeRef node = nodeAt(pos); node.isValid()) {
            if (shift) {
                // Shift+click picks a second node (for joining) and keeps the first one active.
                if (activeNodeIsValid() && !(node.subpath == m_activeNode.subpath && node.node == m_activeNode.node))
                    setActiveNode(m_activeNode, node);
                else
                    setActiveNode(node);
                break;
            }
            setActiveNode(node);
            m_drag = Drag::NodeMove;
            m_pathBeforeEdit = selectedPath()->pathData();
        } else if (selectedPath() && pathops::nearestSegment(selectedPath()->pathData(), docPos, tolerance)) {
            setActiveNode({}); // clicked the outline: keep the curve selected
        } else {
            setSelectedShape(m_document->shapeAt(docPos, tolerance));
        }
        break;

    case Tool::Bezier:
        if (m_bezier.nodes.size() >= 2
            && distance(toWidget(m_bezier.nodes.front().point), pos) <= kCloseDistancePx) {
            m_bezier.closed = true;
            finishCurve();
            return;
        }
        m_bezier.nodes.push_back(PathNode::corner(docPos));
        m_drag = Drag::BezierHandle;
        update();
        break;

    case Tool::Freehand:
        m_freehand = {docPos};
        m_drag = Drag::Freehand;
        break;

    case Tool::Rectangle:
    case Tool::Ellipse:
        m_drag = Drag::Create;
        m_creating = QRectF(docPos, docPos);
        break;
    }
}

void CanvasWidget::mouseMoveEvent(QMouseEvent *event)
{
    const QPointF pos = event->position();
    const QPointF docPos = toDocument(pos);
    m_cursor = docPos;
    emit cursorMoved(docPos);

    switch (m_drag) {
    case Drag::None:
        setHoverCursor(pos);
        if (!m_bezier.nodes.empty())
            update();
        break;

    case Drag::Pan:
        m_pan += pos - m_panLast;
        m_panLast = pos;
        update();
        break;

    case Drag::Move: {
        const QPointF delta = docPos - m_dragLast;
        for (Shape *shape : m_selection)
            shape->moveBy(delta);
        m_dragLast = docPos;
        if (!m_selection.empty())
            m_document->notifyShapeChanged(m_selection.front());
        break;
    }

    case Drag::Resize: {
        // Handles 0-7 go clockwise from top-left; each moves the edges it sits on.
        const int h = m_resizeHandle;
        double left = m_resizeOriginal.left(), right = m_resizeOriginal.right();
        double top = m_resizeOriginal.top(), bottom = m_resizeOriginal.bottom();
        if (h == 0 || h == 6 || h == 7)
            left = docPos.x();
        if (h == 2 || h == 3 || h == 4)
            right = docPos.x();
        if (h == 0 || h == 1 || h == 2)
            top = docPos.y();
        if (h == 4 || h == 5 || h == 6)
            bottom = docPos.y();
        QRectF bounds = QRectF(QPointF(left, top), QPointF(right, bottom)).normalized();
        const double minimum = 1.0 / m_zoom;
        bounds.setWidth(std::max(bounds.width(), minimum));
        bounds.setHeight(std::max(bounds.height(), minimum));

        // Every selected shape keeps its place inside the combined box.
        const QRectF &from = m_resizeOriginal;
        const double sx = from.width() > 0 ? bounds.width() / from.width() : 1.0;
        const double sy = from.height() > 0 ? bounds.height() / from.height() : 1.0;
        QTransform t;
        t.translate(bounds.left(), bounds.top());
        t.scale(sx, sy);
        t.translate(-from.left(), -from.top());
        for (std::size_t i = 0; i < m_selection.size(); ++i)
            m_selection[i]->setBounds(t.mapRect(m_resizeOriginals[i]));
        if (!m_selection.empty())
            m_document->notifyShapeChanged(m_selection.front());
        break;
    }

    case Drag::Marquee:
        m_marquee = QRectF(m_dragStart, docPos).normalized();
        update();
        break;

    case Drag::Create: {
        QPointF end = docPos;
        if (event->modifiers() & Qt::ShiftModifier) {
            // Constrain to a square / circle.
            const QPointF d = docPos - m_dragStart;
            const double side = std::max(std::abs(d.x()), std::abs(d.y()));
            end = m_dragStart + QPointF(std::copysign(side, d.x()), std::copysign(side, d.y()));
        }
        m_creating = QRectF(m_dragStart, end).normalized();
        update();
        break;
    }

    case Drag::NodeMove: {
        Shape *path = selectedPath();
        PathData data = path->pathData();
        PathNode &node = data[m_activeNode.subpath].nodes[m_activeNode.node];
        const QPointF delta = docPos - m_dragLast;
        node.point += delta;
        node.in += delta;
        node.out += delta;
        m_dragLast = docPos;
        path->setPathData(std::move(data));
        m_document->notifyShapeChanged(path);
        break;
    }

    case Drag::NodeHandle: {
        Shape *path = selectedPath();
        PathData data = path->pathData();
        PathNode &node = data[m_activeNode.subpath].nodes[m_activeNode.node];
        QPointF &moved = m_draggedHandle == NodeHandle::Out ? node.out : node.in;
        QPointF &opposite = m_draggedHandle == NodeHandle::Out ? node.in : node.out;
        moved = docPos;
        if (event->modifiers() & Qt::AltModifier) {
            node.type = NodeType::Corner; // Alt breaks the symmetry
        } else if (node.type == NodeType::Smooth) {
            // Keep the opposite handle on the same line, with its own length.
            const double handleLength = distance(moved, node.point);
            const double oppositeLength = distance(opposite, node.point);
            if (handleLength > 0.0 && oppositeLength > 0.0)
                opposite = node.point - (moved - node.point) * (oppositeLength / handleLength);
        }
        path->setPathData(std::move(data));
        m_document->notifyShapeChanged(path);
        break;
    }

    case Drag::BezierHandle: {
        // Dragging while placing a node pulls out symmetric handles: a smooth node.
        PathNode &node = m_bezier.nodes.back();
        if (distance(toWidget(node.point), pos) > 2.0) {
            node.out = docPos;
            node.in = 2 * node.point - docPos;
            node.type = NodeType::Smooth;
        } else {
            node = PathNode::corner(node.point);
        }
        update();
        break;
    }

    case Drag::Freehand:
        if (distance(toWidget(m_freehand.back()), pos) >= 1.0) {
            m_freehand.push_back(docPos);
            update();
        }
        break;
    }
}

void CanvasWidget::mouseReleaseEvent(QMouseEvent *event)
{
    const Drag drag = m_drag;
    m_drag = Drag::None;

    switch (drag) {
    case Drag::None:
    case Drag::BezierHandle:
        break;

    case Drag::Pan:
        updateCursor();
        break;

    case Drag::Move: {
        const QPointF total = m_dragLast - m_dragStart;
        if (!total.isNull()) {
            m_undoStack->beginMacro(tr("Move"));
            for (Shape *shape : m_selection)
                m_undoStack->push(new MoveShapeCommand(m_document, shape, total, true));
            m_undoStack->endMacro();
        }
        break;
    }

    case Drag::Resize: {
        bool changed = false;
        for (std::size_t i = 0; i < m_selection.size(); ++i)
            changed = changed || m_selection[i]->bounds() != m_resizeOriginals[i];
        if (changed) {
            m_undoStack->beginMacro(tr("Resize"));
            for (std::size_t i = 0; i < m_selection.size(); ++i)
                m_undoStack->push(new SetBoundsCommand(m_document, m_selection[i], m_resizeOriginals[i],
                                                       m_selection[i]->bounds()));
            m_undoStack->endMacro();
        }
        m_resizeHandle = -1;
        setHoverCursor(event->position());
        break;
    }

    case Drag::Marquee: {
        // Objects lying completely inside the rubber band are selected.
        std::vector<Shape *> selection = m_marqueeAdds ? m_selection : std::vector<Shape *>{};
        const QRectF band = m_marquee;
        m_marquee = QRectF();
        if (band.width() * m_zoom >= kMinMarqueePx || band.height() * m_zoom >= kMinMarqueePx) {
            for (Shape *shape : m_document->order()) {
                if (band.contains(shape->bounds()) && std::find(selection.begin(), selection.end(), shape) == selection.end())
                    selection.push_back(shape);
            }
        }
        setSelection(std::move(selection));
        update();
        break;
    }

    case Drag::Create: {
        const QRectF bounds = m_creating;
        m_creating = QRectF();
        if (bounds.width() * m_zoom >= kMinCreateSizePx && bounds.height() * m_zoom >= kMinCreateSizePx) {
            const ShapeKind kind = m_tool == Tool::Ellipse ? ShapeKind::Ellipse : ShapeKind::Rectangle;
            addNewShape(std::make_unique<Shape>(kind, bounds, m_defaultStyle));
        }
        update();
        break;
    }

    case Drag::NodeMove:
        commitPathEdit(m_pathBeforeEdit, tr("Move node"));
        break;

    case Drag::NodeHandle:
        commitPathEdit(m_pathBeforeEdit, tr("Adjust curve"));
        break;

    case Drag::Freehand: {
        std::vector<QPointF> points = std::move(m_freehand);
        m_freehand.clear();
        const bool closed = points.size() > 2
                            && distance(toWidget(points.front()), toWidget(points.back())) <= kCloseDistancePx;
        SubPath curve = pathops::fitFreehand(points, kFreehandTolerancePx / m_zoom, closed);
        if (curve.nodes.size() >= 2) {
            const bool isClosed = curve.closed;
            addNewShape(std::make_unique<Shape>(PathData{std::move(curve)}, curveStyle(isClosed)));
        }
        update();
        break;
    }
    }
    QWidget::mouseReleaseEvent(event);
}

void CanvasWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;

    if (m_tool == Tool::Bezier) {
        finishCurve();
        return;
    }

    if (m_tool == Tool::Select) {
        // Double-click a text to type into it.
        Shape *hit = m_document->shapeAt(toDocument(event->position()), kHitTolerancePx / m_zoom);
        if (hit && hit->kind() == ShapeKind::Text)
            beginTextEdit(hit);
        return;
    }

    if (m_tool == Tool::Node && selectedPath()) {
        // Double-click a node to delete it, or a segment to add a node there.
        if (const NodeRef node = nodeAt(event->position()); node.isValid()) {
            setActiveNode(node);
            removeActiveNode();
            return;
        }
        Shape *path = selectedPath();
        const auto hit = pathops::nearestSegment(path->pathData(), toDocument(event->position()),
                                                 kHitTolerancePx / m_zoom);
        if (hit) {
            PathData data = path->pathData();
            const int index = pathops::insertNode(data, hit->subpath, hit->segment, hit->t);
            m_undoStack->push(new SetPathCommand(m_document, path, path->pathData(), data, tr("Add node")));
            setActiveNode({hit->subpath, index});
        }
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void CanvasWidget::wheelEvent(QWheelEvent *event)
{
    const int delta = event->angleDelta().y();
    if (delta == 0)
        return;
    zoomAround(event->position(), std::pow(1.0015, delta));
    event->accept();
}

// ---------------------------------------------------------------------------------------------
// Keyboard

void CanvasWidget::keyPressEvent(QKeyEvent *event)
{
    if (m_editText && handleTextKey(event))
        return;
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spaceHeld = true;
        updateCursor();
        return;
    }
    if (!m_bezier.nodes.empty()
        && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter || event->key() == Qt::Key_Escape)) {
        finishCurve();
        return;
    }
    QWidget::keyPressEvent(event);
}

void CanvasWidget::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spaceHeld = false;
        updateCursor();
        return;
    }
    QWidget::keyReleaseEvent(event);
}

} // namespace opendraw
