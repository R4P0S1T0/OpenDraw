#pragma once

#include "core/Shape.h"

#include <QPointF>
#include <QRectF>
#include <QWidget>

#include <array>
#include <memory>
#include <optional>
#include <vector>

class QPainter;
class QUndoStack;

namespace opendraw {

class Document;

// Draws the page and its shapes, and turns mouse input into document edits.
class CanvasWidget : public QWidget {
    Q_OBJECT

public:
    enum class Tool { Select, Node, Freehand, Bezier, Rectangle, Ellipse };

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
    // Deletes the selected node (shape tool), the last node of a curve being drawn, or the selected shape.
    void deleteSelection();
    void convertSelectionToCurves();
    // Completes the curve being drawn with the Bézier tool.
    void finishCurve();

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
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;

private:
    enum class Drag { None, Pan, Move, Resize, Create, NodeMove, NodeHandle, BezierHandle, Freehand };
    enum class NodeHandle { In, Out };

    struct NodeRef {
        int subpath = -1;
        int node = -1;
        bool isValid() const { return subpath >= 0 && node >= 0; }
    };

    QTransform viewTransform() const;
    QPointF toDocument(const QPointF &widgetPosition) const;
    QPointF toWidget(const QPointF &documentPosition) const;
    void zoomAround(const QPointF &widgetPosition, double factor);
    void updateCursor();

    // Select tool: bounding-box handles in widget coordinates, clockwise from the top-left corner.
    std::array<QRectF, 8> resizeHandleRects() const;
    int resizeHandleAt(const QPointF &widgetPosition) const;
    void setHoverCursor(const QPointF &widgetPosition);

    // Shape tool
    Shape *selectedPath() const;
    NodeRef nodeAt(const QPointF &widgetPosition) const;
    std::optional<NodeHandle> nodeHandleAt(const QPointF &widgetPosition) const;
    bool activeNodeIsValid() const;
    void commitPathEdit(const PathData &before, const QString &text);
    void removeActiveNode();

    // Style given to new curves: open curves are never filled.
    Style curveStyle(bool closed) const;
    void addNewShape(std::unique_ptr<Shape> shape);

    void paintSelection(QPainter &painter);
    void paintNodes(QPainter &painter, const Shape &shape);
    void paintBezierPreview(QPainter &painter);

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
    QPointF m_dragStart; // document coordinates
    QPointF m_dragLast;  // document coordinates
    QPointF m_panLast;   // widget coordinates
    QPointF m_cursor;    // document coordinates, last known mouse position
    QRectF m_creating;   // rectangle/ellipse being drawn
    int m_resizeHandle = -1;
    QRectF m_resizeOriginal;

    NodeRef m_activeNode;
    NodeHandle m_draggedHandle = NodeHandle::Out;
    PathData m_pathBeforeEdit;

    SubPath m_bezier; // curve being drawn with the Bézier tool; empty when idle
    std::vector<QPointF> m_freehand;
};

} // namespace opendraw
