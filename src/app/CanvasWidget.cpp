#include "CanvasWidget.h"

#include "core/Commands.h"
#include "core/Document.h"

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
constexpr double kMinCreateSizePx = 3.0;
constexpr double kPageMarginPx = 24.0;
constexpr double kHandleSizePx = 7.0;
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
    connect(m_document, &Document::cleared, this, [this] { setSelectedShape(nullptr); });
}

void CanvasWidget::setTool(Tool tool)
{
    m_tool = tool;
    updateCursor();
}

void CanvasWidget::setSelectedShape(Shape *shape)
{
    if (shape == m_selected)
        return;
    m_selected = shape;
    update();
    emit selectionChanged(shape);
}

QTransform CanvasWidget::viewTransform() const
{
    return QTransform::fromTranslate(m_pan.x(), m_pan.y()).scale(m_zoom, m_zoom);
}

QPointF CanvasWidget::toDocument(const QPointF &widgetPosition) const
{
    return (widgetPosition - m_pan) / m_zoom;
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

void CanvasWidget::deleteSelection()
{
    if (m_selected)
        m_undoStack->push(new RemoveShapeCommand(m_document, m_selected));
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

    for (const auto &shape : m_document->shapes()) {
        const Style &style = shape->style();
        painter.setBrush(style.hasFill() ? QBrush(style.fill) : QBrush(Qt::NoBrush));
        painter.setPen(style.hasStroke() ? QPen(style.stroke, style.strokeWidth) : QPen(Qt::NoPen));
        painter.drawPath(shape->path());
    }

    if (m_drag == Drag::Create && !m_creating.isEmpty()) {
        QPen preview(palette().color(QPalette::Highlight), 0, Qt::DashLine);
        preview.setCosmetic(true);
        painter.setPen(preview);
        painter.setBrush(Qt::NoBrush);
        if (m_tool == Tool::Ellipse)
            painter.drawEllipse(m_creating);
        else
            painter.drawRect(m_creating);
    }

    painter.resetTransform();

    if (m_selected) {
        const QRectF box = viewTransform().mapRect(m_selected->bounds()).adjusted(-3, -3, 3, 3);
        const QColor highlight = palette().color(QPalette::Highlight);
        painter.setPen(QPen(highlight, 1, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(box);

        painter.setPen(QPen(highlight, 1));
        painter.setBrush(Qt::white);
        const QPointF handles[] = {box.topLeft(),     QPointF(box.center().x(), box.top()),
                                   box.topRight(),    QPointF(box.right(), box.center().y()),
                                   box.bottomRight(), QPointF(box.center().x(), box.bottom()),
                                   box.bottomLeft(),  QPointF(box.left(), box.center().y())};
        for (const QPointF &h : handles)
            painter.drawRect(QRectF(h - QPointF(kHandleSizePx / 2, kHandleSizePx / 2),
                                    QSizeF(kHandleSizePx, kHandleSizePx)));
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

void CanvasWidget::mousePressEvent(QMouseEvent *event)
{
    const QPointF docPos = toDocument(event->position());

    if (event->button() == Qt::MiddleButton || (event->button() == Qt::LeftButton && m_spaceHeld)) {
        m_drag = Drag::Pan;
        m_panLast = event->position();
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;

    m_dragStart = m_dragLast = docPos;
    if (m_tool == Tool::Select) {
        Shape *hit = m_document->shapeAt(docPos, kHitTolerancePx / m_zoom);
        setSelectedShape(hit);
        m_drag = hit ? Drag::Move : Drag::None;
    } else {
        m_drag = Drag::Create;
        m_creating = QRectF(docPos, docPos);
    }
}

void CanvasWidget::mouseMoveEvent(QMouseEvent *event)
{
    const QPointF docPos = toDocument(event->position());
    emit cursorMoved(docPos);

    switch (m_drag) {
    case Drag::None:
        break;
    case Drag::Pan:
        m_pan += event->position() - m_panLast;
        m_panLast = event->position();
        update();
        break;
    case Drag::Move:
        m_selected->moveBy(docPos - m_dragLast);
        m_dragLast = docPos;
        m_document->notifyShapeChanged(m_selected);
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
    }
}

void CanvasWidget::mouseReleaseEvent(QMouseEvent *event)
{
    const Drag drag = m_drag;
    m_drag = Drag::None;

    switch (drag) {
    case Drag::None:
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
    case Drag::Create: {
        const QRectF bounds = m_creating;
        m_creating = QRectF();
        if (bounds.width() * m_zoom >= kMinCreateSizePx && bounds.height() * m_zoom >= kMinCreateSizePx) {
            const ShapeKind kind = m_tool == Tool::Ellipse ? ShapeKind::Ellipse : ShapeKind::Rectangle;
            auto *command = new AddShapeCommand(m_document, std::make_unique<Shape>(kind, bounds, m_defaultStyle));
            Shape *created = command->shape();
            m_undoStack->push(command);
            setSelectedShape(created);
        }
        update();
        break;
    }
    }
    QWidget::mouseReleaseEvent(event);
}

void CanvasWidget::wheelEvent(QWheelEvent *event)
{
    const int delta = event->angleDelta().y();
    if (delta == 0)
        return;
    zoomAround(event->position(), std::pow(1.0015, delta));
    event->accept();
}

void CanvasWidget::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spaceHeld = true;
        updateCursor();
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

void CanvasWidget::updateCursor()
{
    if (m_spaceHeld)
        setCursor(Qt::OpenHandCursor);
    else if (m_tool == Tool::Select)
        unsetCursor();
    else
        setCursor(Qt::CrossCursor);
}

} // namespace opendraw
