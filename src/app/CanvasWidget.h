#pragma once

#include "core/Shape.h"

#include <QPointF>
#include <QRectF>
#include <QWidget>

#include <array>

class QUndoStack;

namespace opendraw {

class Document;

// Draws the page and its shapes, and turns mouse input into document edits.
class CanvasWidget : public QWidget {
    Q_OBJECT

public:
    enum class Tool { Select, Rectangle, Ellipse };

    CanvasWidget(Document *document, QUndoStack *undoStack, QWidget *parent = nullptr);

    Tool tool() const { return m_tool; }
    void setTool(Tool tool);

    Shape *selectedShape() const { return m_selected; }
    void setSelectedShape(Shape *shape);

    const Style &defaultStyle() const { return m_defaultStyle; }
    void setDefaultStyle(const Style &style) { m_defaultStyle = style; }

    double zoom() const { return m_zoom; }

public slots:
    void zoomIn();
    void zoomOut();
    void zoomToActualSize();
    void fitPage();
    void deleteSelection();

signals:
    void zoomChanged(double zoom);
    void cursorMoved(const QPointF &documentPosition);
    void selectionChanged(opendraw::Shape *shape);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;

private:
    enum class Drag { None, Pan, Move, Create, Resize };

    QTransform viewTransform() const;
    QPointF toDocument(const QPointF &widgetPosition) const;
    void zoomAround(const QPointF &widgetPosition, double factor);
    void updateCursor();

    // Selection handles in widget coordinates, clockwise from the top-left corner.
    std::array<QRectF, 8> handleRects() const;
    int handleAt(const QPointF &widgetPosition) const;
    void setHoverCursor(const QPointF &widgetPosition);

    Document *m_document;
    QUndoStack *m_undoStack;
    Tool m_tool = Tool::Select;
    Shape *m_selected = nullptr;
    Style m_defaultStyle;

    double m_zoom = 1.0;
    QPointF m_pan; // widget position of the document origin
    bool m_needsInitialFit = true;
    bool m_spaceHeld = false;

    Drag m_drag = Drag::None;
    QPointF m_dragStart;  // document coordinates
    QPointF m_dragLast;   // document coordinates
    QPointF m_panLast;    // widget coordinates
    QRectF m_creating;    // document coordinates
    int m_resizeHandle = -1;
    QRectF m_resizeOriginal;
};

} // namespace opendraw
