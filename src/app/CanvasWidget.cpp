#include "CanvasWidget.h"

#include "core/Commands.h"
#include "core/Document.h"
#include "core/PathOps.h"

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
        if (shape == m_selected)
            setSelectedShape(nullptr);
    });
    connect(m_document, &Document::cleared, this, [this] {
        setSelectedShape(nullptr);
        m_bezier = {};
    });
}

void CanvasWidget::setTool(Tool tool)
{
    if (tool == m_tool)
        return;
    if (m_tool == Tool::Bezier)
        finishCurve();
    m_tool = tool;
    m_activeNode = {};
    updateCursor();
    update();
}

void CanvasWidget::setSelectedShape(Shape *shape)
{
    if (shape == m_selected)
        return;
    m_selected = shape;
    m_activeNode = {};
    update();
    emit selectionChanged(shape);
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
    else
        setCursor(Qt::CrossCursor);
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
    if (m_selected)
        m_undoStack->push(new RemoveShapeCommand(m_document, m_selected));
}

void CanvasWidget::convertSelectionToCurves()
{
    if (!m_selected || m_selected->kind() == ShapeKind::Path)
        return;
    auto curve = std::make_unique<Shape>(pathops::fromPainterPath(m_selected->path()), m_selected->style());
    auto *command = new ReplaceShapeCommand(m_document, m_selected, std::move(curve), tr("Convert to curves"));
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
// Shape tool (node editing)

Shape *CanvasWidget::selectedPath() const
{
    return m_selected && m_selected->kind() == ShapeKind::Path ? m_selected : nullptr;
}

bool CanvasWidget::activeNodeIsValid() const
{
    const Shape *path = selectedPath();
    if (!path || !m_activeNode.isValid())
        return false;
    const PathData &data = path->pathData();
    return m_activeNode.subpath < int(data.size())
           && m_activeNode.node < int(data[m_activeNode.subpath].nodes.size());
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
    m_activeNode = {};
    if (data.empty()) {
        m_undoStack->push(new RemoveShapeCommand(m_document, path));
        return;
    }
    m_undoStack->push(new SetPathCommand(m_document, path, path->pathData(), data, tr("Delete node")));
}

// ---------------------------------------------------------------------------------------------
// Select tool resize handles

std::array<QRectF, 8> CanvasWidget::resizeHandleRects() const
{
    std::array<QRectF, 8> rects;
    if (!m_selected)
        return rects;
    const QRectF box = viewTransform().mapRect(m_selected->bounds()).adjusted(-3, -3, 3, 3);
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
    if (!m_selected || m_tool != Tool::Select)
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

    for (const auto &shape : m_document->shapes()) {
        const Style &style = shape->style();
        painter.setBrush(style.hasFill() ? QBrush(style.fill) : QBrush(Qt::NoBrush));
        painter.setPen(style.hasStroke() ? QPen(style.stroke, style.strokeWidth) : QPen(Qt::NoPen));
        painter.drawPath(shape->path());
    }

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
        QPen ink(curveStyle(false).stroke, curveStyle(false).strokeWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter.setPen(ink);
        painter.drawPolyline(m_freehand.data(), int(m_freehand.size()));
    }

    painter.resetTransform();
    paintSelection(painter);
    paintBezierPreview(painter);
}

void CanvasWidget::paintSelection(QPainter &painter)
{
    if (!m_selected)
        return;
    const QColor highlight = palette().color(QPalette::Highlight);
    const QRectF box = viewTransform().mapRect(m_selected->bounds()).adjusted(-3, -3, 3, 3);

    if (m_tool == Tool::Node) {
        if (m_selected->kind() == ShapeKind::Path) {
            paintNodes(painter, *m_selected);
        } else {
            // Rectangles and ellipses need "Convert to curves" before their nodes can be edited.
            painter.setPen(QPen(highlight, 1, Qt::DotLine));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(box);
        }
        return;
    }

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
            const PathNode &node = data[s].nodes[n];
            painter.setPen(QPen(highlight, 1));
            painter.setBrush(active ? QBrush(highlight) : QBrush(Qt::white));
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

    if (event->button() == Qt::MiddleButton || (event->button() == Qt::LeftButton && m_spaceHeld)) {
        m_drag = Drag::Pan;
        m_panLast = pos;
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;

    m_dragStart = m_dragLast = docPos;
    const double tolerance = kHitTolerancePx / m_zoom;

    switch (m_tool) {
    case Tool::Select:
        if (const int handle = resizeHandleAt(pos); handle >= 0) {
            m_drag = Drag::Resize;
            m_resizeHandle = handle;
            m_resizeOriginal = m_selected->bounds();
        } else {
            Shape *hit = m_document->shapeAt(docPos, tolerance);
            setSelectedShape(hit);
            m_drag = hit ? Drag::Move : Drag::None;
        }
        break;

    case Tool::Node:
        if (const auto handle = nodeHandleAt(pos)) {
            m_drag = Drag::NodeHandle;
            m_draggedHandle = *handle;
            m_pathBeforeEdit = selectedPath()->pathData();
        } else if (const NodeRef node = nodeAt(pos); node.isValid()) {
            m_activeNode = node;
            m_drag = Drag::NodeMove;
            m_pathBeforeEdit = selectedPath()->pathData();
            update();
        } else if (selectedPath() && pathops::nearestSegment(selectedPath()->pathData(), docPos, tolerance)) {
            m_activeNode = {}; // clicked the outline: keep the curve selected
            update();
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

    case Drag::Move:
        m_selected->moveBy(docPos - m_dragLast);
        m_dragLast = docPos;
        m_document->notifyShapeChanged(m_selected);
        break;

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
        m_selected->setBounds(bounds);
        m_document->notifyShapeChanged(m_selected);
        break;
    }

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
        if (!total.isNull())
            m_undoStack->push(new MoveShapeCommand(m_document, m_selected, total, true));
        break;
    }

    case Drag::Resize:
        if (m_selected->bounds() != m_resizeOriginal)
            m_undoStack->push(new SetBoundsCommand(m_document, m_selected, m_resizeOriginal, m_selected->bounds()));
        m_resizeHandle = -1;
        setHoverCursor(event->position());
        break;

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
        const bool closed = points.size() > 2 && distance(toWidget(points.front()), toWidget(points.back())) <= kCloseDistancePx;
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

    if (m_tool == Tool::Node && selectedPath()) {
        // Double-click a node to delete it, or a segment to add a node there.
        if (const NodeRef node = nodeAt(event->position()); node.isValid()) {
            m_activeNode = node;
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
            m_activeNode = {hit->subpath, index};
            update();
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
