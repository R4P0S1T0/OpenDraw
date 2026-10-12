#pragma once

#include "core/Arrange.h"
#include "core/Document.h"
#include "core/Shape.h"
#include "ToolSettings.h"

#include <QPointF>
#include <QRectF>
#include <QTransform>
#include <QWidget>

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

class QPainter;
class QUndoStack;

namespace opendraw {

// Draws the page and its shapes, and turns mouse input into document edits.
class CanvasWidget : public QWidget {
    Q_OBJECT

public:
    enum class Tool {
        Select, Node, Zoom, Pan, Freehand, Line, Bezier, Rectangle, Ellipse,
        Polygon, Star, Spiral, GraphPaper, Text, DropShadow, Transparency, Eyedropper, Blend, Contour,
    };

    CanvasWidget(Document *document, QUndoStack *undoStack, QWidget *parent = nullptr);

    Tool tool() const { return m_tool; }
    void setTool(Tool tool);

    // Top-level shapes, in the order they were selected.
    const std::vector<Shape *> &selection() const { return m_selection; }
    // The selected shape when exactly one is selected, otherwise nullptr.
    Shape *selectedShape() const { return m_selection.size() == 1 ? m_selection.front() : nullptr; }
    void setSelectedShape(Shape *shape);
    void setSelection(std::vector<Shape *> shapes);
    // Second click on the selection: rotate and skew handles instead of resize handles.
    bool rotateMode() const { return m_rotateMode; }

    const Style &defaultStyle() const { return m_defaultStyle; }
    void setDefaultStyle(const Style &style);
    // The style shown for the selection (its first plain object), or the default style for
    // new shapes when nothing is selected.
    const Style &currentStyle() const;
    // Changes the style of every selected object (inside groups too) as one undoable step,
    // or the default style for new shapes when nothing is selected.
    void restyle(const QString &text, const std::function<void(Style &)> &change);

    // Arrow keys move the selection by this much (document units); Shift moves ten times further.
    double nudgeDistance() const { return m_nudge; }
    void setNudgeDistance(double distance) { m_nudge = distance; }
    QPointF duplicateOffset() const { return m_duplicateOffset; }
    void setDuplicateOffset(const QPointF &offset) { m_duplicateOffset = offset; }

    double zoom() const { return m_zoom; }
    // Widget position of the document origin.
    QPointF pan() const { return m_pan; }
    void setPan(const QPointF &pan);
    // Document → widget coordinates.
    QTransform viewTransform() const;
    // The part of the document currently visible.
    QRectF visibleDocumentRect() const;

    bool hasActiveNode() const { return activeNodeIsValid(); }
    bool hasNodePairToJoin() const;

    // Text being typed into, if any (it may not be in the document yet).
    Shape *editedText() const { return m_editText; }
    // Format of the text being edited (at the caret or the start of the selected part) or the
    // selected text; otherwise the one new text gets.
    TextFormat currentTextFormat() const;
    // Part of the edited text selected with Shift+arrows or the mouse: [start, end), or empty.
    std::pair<int, int> textSelection() const;
    bool hasTextSelection() const { return textSelection().first < textSelection().second; }
    int textCursor() const { return m_textCursor; }
    void setTextCursor(int position, bool extendSelection = false);

    QPointF documentToWidget(const QPointF &documentPosition) const { return toWidget(documentPosition); }

    const ToolSettings &toolSettings() const { return m_toolSettings; }
    void setToolSettings(const ToolSettings &settings) { m_toolSettings = settings; }
    const SnapSettings &snapSettings() const { return m_snap; }
    void setSnapSettings(const SnapSettings &settings) { m_snap = settings; }
    const GridSettings &gridSettings() const { return m_grid; }
    void setGridSettings(const GridSettings &settings);
    bool guidesVisible() const { return m_guidesVisible; }
    void setGuidesVisible(bool visible);

    // Colour eyedropper: the colour picked up, applied by clicking objects afterwards.
    std::optional<QColor> eyedropperColor() const { return m_eyedropperColor; }
    void setEyedropperColor(const std::optional<QColor> &color);

    // A guideline being dragged in from a ruler; positions are canvas widget coordinates.
    void beginGuideDrag(Qt::Orientation orientation, const QPointF &widgetPosition);
    void updateGuideDrag(const QPointF &widgetPosition);
    void endGuideDrag(const QPointF &widgetPosition);

    // Adds shapes to the active layer as one undoable step and selects them.
    void addShapes(std::vector<std::unique_ptr<Shape>> shapes, const QPointF &offset, const QString &text);

public slots:
    void zoomIn();
    void zoomOut();
    void zoomToActualSize();
    void fitPage();
    void fitPageWidth();
    void fitPageHeight();
    void zoomToSelection();
    // Zoom factor (1 = 100 %), keeping the centre of the view in place.
    void setZoom(double zoom);
    void zoomToRect(const QRectF &documentRect);

    void cutSelection();
    void copySelection();
    void paste();
    // Copies the selection, offset by duplicateOffset(), and selects the copies.
    void duplicateSelection();
    void mirrorSelection(Qt::Orientation orientation);
    // Turns the selection by `degrees` (clockwise on screen) around its rotation centre.
    void rotateSelection(double degrees);
    void nudgeSelection(const QPointF &offset);

    void selectAll();
    // Deletes the selected node (shape tool), the last node of a curve being drawn, or the selection.
    void deleteSelection();
    void convertSelectionToCurves();
    // Artistic text becomes paragraph text in a frame around it, and the other way round.
    void convertTextKind();
    // Completes the curve being drawn with the Bézier tool.
    void finishCurve();

    void groupSelection();
    void ungroupSelection();
    // Blends two objects (the lower one is the start) with toolSettings().blendSteps steps.
    void blendShapes(opendraw::Shape *from, opendraw::Shape *to);
    // Number of steps of the selected blends.
    void setBlendSteps(int steps);
    // Selected blends become their start, a group of their steps, and their end.
    void breakBlendApart();
    void orderToFront();
    void orderToBack();
    void orderForwardOne();
    void orderBackOne();
    // Aligns the selection to its own bounds, or a single object to the page.
    void align(arrange::Alignment alignment);
    void centerOnPage();
    void distribute(Qt::Orientation orientation);
    void moveSelectionToLayer(opendraw::Layer *layer);

    // Node commands for the shape tool. They act on the active node; joining also needs a
    // second end node, picked with Shift+click.
    void makeNodeSmooth();
    void makeNodeCorner();
    void segmentToLine();
    void segmentToCurve();
    void breakNode();
    void joinNodes();

    // Applies what differs from currentTextFormat(): font, size, weight and slant to the selected
    // part of the edited text (or all of it), else to the selected texts, else to new text;
    // alignment and line spacing to whole texts.
    void setTextFormat(const TextFormat &format);
    // Changes the letters: the selected part of the edited text, or whole texts as above.
    void applyCharFormat(const CharFormat &change);
    void finishTextEdit();

signals:
    void zoomChanged(double zoom);
    // Zoom or scroll position changed.
    void viewChanged();
    void toolChanged(opendraw::CanvasWidget::Tool tool);
    // The selection switched between resize handles and rotate/skew handles.
    void transformModeChanged(bool rotating);
    void cursorMoved(const QPointF &documentPosition);
    void selectionChanged();
    void nodeSelectionChanged();
    void textEditingChanged();
    void eyedropperColorChanged(const QColor &color);
    // The default style for new shapes changed.
    void defaultStyleChanged();

protected:
    bool event(QEvent *event) override;
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
    enum class Drag {
        None, Pan, Move, Resize, Marquee, Create, NodeMove, NodeHandle, BezierHandle, Freehand, ZoomBox, Guide,
        Rotate, Skew, RotationCenter, TextFrame, TextSelect, Effect, EffectHandle, Blend,
    };
    enum class NodeHandle { In, Out };

    struct NodeRef {
        int subpath = -1;
        int node = -1;
        bool isValid() const { return subpath >= 0 && node >= 0; }
    };

    void viewUpdated(bool zoomChanged);
    QPointF toDocument(const QPointF &widgetPosition) const;
    QPointF toWidget(const QPointF &documentPosition) const;
    void zoomAround(const QPointF &widgetPosition, double factor);
    void updateCursor();

    bool isSelected(const Shape *shape) const;
    QRectF selectionBounds() const;
    // Moves each selected shape by its offset as one undoable step.
    void moveSelection(const std::vector<QPointF> &offsets, const QString &text);
    void reorder(const std::function<std::vector<Shape *>(std::vector<Shape *>)> &rearrange, const QString &text);

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

    // Snapping. Returns the point moved onto the nearest guide, grid line, page edge or object
    // edge within reach, axis by axis.
    QPointF snapPoint(const QPointF &point) const;
    // Offset that brings a moving box (its edges or centre) onto something to snap to.
    QPointF snapBox(const QRectF &box) const;
    std::vector<double> snapTargets(Qt::Orientation axis) const;
    int guideAt(const QPointF &widgetPosition) const;
    // Rotate/skew mode: the point the selection turns around (document coordinates).
    QPointF rotationCenter() const;
    bool rotationCenterAt(const QPointF &widgetPosition) const;
    void setRotateMode(bool rotating);
    // Applies `t` to the selection live, replacing what the current drag applied so far.
    void applyLiveTransform(const QTransform &t);
    void commitLiveTransform(const QString &text);
    void paintTransformHandles(QPainter &painter, const QRectF &box);

    // Shadow and transparency tools.
    void captureEffectStyles();
    void applyEffectDrag(const QPointF &documentPosition, Qt::KeyboardModifiers modifiers);
    void commitEffectDrag();
    // The selected object whose transparency gradient shows handles, if any.
    Shape *transparencyShape() const;
    // Page position of a fraction of a shape's own box (0..1 each way), and back.
    static QPointF fromFraction(const Shape &shape, const QPointF &fraction);
    static QPointF toFraction(const Shape &shape, const QPointF &documentPosition);
    std::array<QPointF, 2> transparencyHandles(const Shape &shape) const;
    int transparencyHandleAt(const QPointF &widgetPosition) const;
    void paintEffectHandles(QPainter &painter);
    QPainterPath toolShapePreview(const QRectF &box) const;
    std::unique_ptr<Shape> createToolShape(const QRectF &box) const;
    QColor colorAt(const QPointF &widgetPosition) const;
    void applyEyedropper(const QPointF &documentPosition);
    void paintGrid(QPainter &painter);
    void paintGuides(QPainter &painter);

    // Style given to new curves: open curves are never filled.
    Style curveStyle(bool closed) const;
    void addNewShape(std::unique_ptr<Shape> shape);

    // Text tool
    void beginTextEdit(Shape *text);
    // Artistic text with its first baseline through `origin`, or paragraph text in a frame
    // whose top-left corner is `origin`.
    void beginNewText(const QPointF &origin, const QSizeF &frame = QSizeF());
    void setEditedData(TextData data, int cursor);
    // Replaces the selected part of the edited text (or inserts at the caret).
    void replaceTextSelection(const QString &inserted);
    // Character index in the edited text nearest to a widget position.
    int textIndexAt(const QPointF &widgetPosition) const;
    bool handleTextKey(QKeyEvent *event);
    static bool isTextEditingKey(const QKeyEvent *event);
    void paintTextEdit(QPainter &painter);

    void paintShape(QPainter &painter, const Shape &shape);
    void paintSelection(QPainter &painter);
    void paintNodes(QPainter &painter, const Shape &shape);
    void paintBezierPreview(QPainter &painter);

    Document *m_document;
    QUndoStack *m_undoStack;
    Tool m_tool = Tool::Select;
    std::vector<Shape *> m_selection;
    Style m_defaultStyle;
    double m_nudge = kUnitsPerMm;               // 1 mm
    QPointF m_duplicateOffset{5 * kUnitsPerMm, 5 * kUnitsPerMm};

    double m_zoom = 1.0;
    QPointF m_pan; // widget position of the document origin
    bool m_needsInitialFit = true;
    bool m_fitOnResize = true; // the page was fitted and the user has not zoomed or scrolled since
    bool m_spaceHeld = false;
    bool m_spacePanned = false;          // Space was used for panning, so releasing it keeps the tool
    std::optional<Tool> m_toolBeforeSpace; // Space switches to the pick tool and back

    ToolSettings m_toolSettings;
    SnapSettings m_snap;
    GridSettings m_grid;
    bool m_guidesVisible = true;
    std::optional<QColor> m_eyedropperColor;
    int m_guide = -1;                    // guide being dragged (-1: a new one from a ruler)
    std::optional<Guide> m_guideBefore;  // its position before the drag
    std::optional<Guide> m_draggedGuide; // where it is now
    QPointF m_lineEnd;                   // 2-point line being drawn

    Drag m_drag = Drag::None;
    QPointF m_dragStart; // document coordinates
    QPointF m_dragLast;  // document coordinates
    QPointF m_panLast;   // widget coordinates
    QPointF m_cursor;    // document coordinates, last known mouse position
    QRectF m_creating;   // rectangle/ellipse being drawn
    QRectF m_marquee;    // rubber-band selection, document coordinates
    QRectF m_zoomBox;    // zoom tool rectangle, document coordinates
    QRectF m_moveOriginal; // selection bounds when a move started
    bool m_pressedSelected = false;        // the click landed on an object that was already selected
    bool m_rotateMode = false;             // second click: rotate and skew handles
    std::optional<QPointF> m_rotationCenter; // moved by the user; otherwise the selection centre
    QTransform m_liveTransform;            // applied to the selection by the current rotate/skew drag
    QRectF m_transformBox;                 // selection bounds when that drag started
    QPointF m_transformCenter;             // rotation centre when that drag started
    std::vector<std::pair<Shape *, Style>> m_effectBefore; // styles before a shadow/transparency drag
    int m_effectHandle = -1;               // transparency handle being dragged: 0 start, 1 end
    Shape *m_blendStart = nullptr;         // object a blend drag started on
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

    Shape *m_editText = nullptr;           // text receiving key presses
    std::unique_ptr<Shape> m_pendingText;  // new text, added to the document when editing ends
    TextData m_textBeforeEdit;
    int m_textCursor = 0;
    int m_textAnchor = -1; // other end of the selected part of the edited text; -1: nothing selected
    TextFormat m_defaultTextFormat;
};

} // namespace opendraw
