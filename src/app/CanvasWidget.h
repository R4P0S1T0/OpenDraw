#pragma once

#include "core/Arrange.h"
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

    // Top-level shapes, in the order they were selected.
    const std::vector<Shape *> &selection() const { return m_selection; }
    // The selected shape when exactly one is selected, otherwise nullptr.
    Shape *selectedShape() const { return m_selection.size() == 1 ? m_selection.front() : nullptr; }
    void setSelectedShape(Shape *shape);
    void setSelection(std::vector<Shape *> shapes);

    const Style &defaultStyle() const { return m_defaultStyle; }
    void setDefaultStyle(const Style &style) { m_defaultStyle = style; }

    double zoom() const { return m_zoom; }

    bool hasActiveNode() const { return activeNodeIsValid(); }
    bool hasNodePairToJoin() const;

public slots:
    void zoomIn();
    void zoomOut();
    void zoomToActualSize();
    void fitPage();

    void selectAll();
    // Deletes the selected node (shape tool), the last node of a curve being drawn, or the selection.
    void deleteSelection();
    void convertSelectionToCurves();
    // Completes the curve being drawn with the Bézier tool.
    void finishCurve();

    void groupSelection();
    void ungroupSelection();
    void orderToFront();
    void orderToBack();
    void orderForwardOne();
    void orderBackOne();
    // Aligns the selection to its own bounds, or a single object to the page.
    void align(arrange::Alignment alignment);
    void centerOnPage();
    void distribute(Qt::Orientation orientation);

    // Node commands for the shape tool. They act on the active node; joining also needs a
    // second end node, picked with Shift+click.
    void makeNodeSmooth();
    void makeNodeCorner();
    void segmentToLine();
    void segmentToCurve();
    void breakNode();
    void joinNodes();

signals:
    void zoomChanged(double zoom);
    void cursorMoved(const QPointF &documentPosition);
    void selectionChanged();
    void nodeSelectionChanged();

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
    enum class Drag { None, Pan, Move, Resize, Marquee, Create, NodeMove, NodeHandle, BezierHandle, Freehand };
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

    bool isSelected(const Shape *shape) const;
    QRectF selectionBounds() const;
    // Moves each selected shape by its offset as one undoable step.
    void moveSelection(const std::vector<QPointF> &offsets, const QString &text);
    void reorder(std::vector<Shape *> order, const QString &text);

    // Select tool: bounding-box handles in widget coordinates, clockwise from the top-left corner.
    std::array<QRectF, 8> resizeHandleRects() const;
    int resizeHandleAt(const QPointF &widgetPosition) const;
    void setHoverCursor(const QPointF &widgetPosition);

    // Shape tool
    Shape *selectedPath() const;
    NodeRef nodeAt(const QPointF &widgetPosition) const;
    std::optional<NodeHandle> nodeHandleAt(const QPointF &widgetPosition) const;
    bool activeNodeIsValid() const;
    bool isValidNode(const NodeRef &ref) const;
    void setActiveNode(const NodeRef &node);
    void setActiveNode(const NodeRef &node, const NodeRef &secondary);
    void commitPathEdit(const PathData &before, const QString &text);
    void removeActiveNode();
    // Applies `edit` to a copy of the selected path and pushes it as one undoable step.
    template<typename Edit>
    bool editPath(const QString &text, Edit edit);

    // Style given to new curves: open curves are never filled.
    Style curveStyle(bool closed) const;
    void addNewShape(std::unique_ptr<Shape> shape);

    void paintShape(QPainter &painter, const Shape &shape);
    void paintSelection(QPainter &painter);
    void paintNodes(QPainter &painter, const Shape &shape);
    void paintBezierPreview(QPainter &painter);

    Document *m_document;
    QUndoStack *m_undoStack;
    Tool m_tool = Tool::Select;
    std::vector<Shape *> m_selection;
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
    QRectF m_marquee;    // rubber-band selection, document coordinates
    bool m_marqueeAdds = false;
    int m_resizeHandle = -1;
    QRectF m_resizeOriginal;                // selection bounds when the resize started
    std::vector<QRectF> m_resizeOriginals; // each selected shape's bounds when the resize started

    NodeRef m_activeNode;
    NodeRef m_secondaryNode; // Shift+clicked node, for joining
    NodeHandle m_draggedHandle = NodeHandle::Out;
    PathData m_pathBeforeEdit;

    SubPath m_bezier; // curve being drawn with the Bézier tool; empty when idle
    std::vector<QPointF> m_freehand;
};

} // namespace opendraw
