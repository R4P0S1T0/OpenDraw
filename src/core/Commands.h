#pragma once

#include "core/Document.h"
#include "core/Shape.h"

#include <QPointF>
#include <QSizeF>
#include <QTransform>
#include <QUndoCommand>

#include <memory>
#include <vector>

namespace opendraw {

class Document;
struct Layer;
struct Page;

// Every document change goes through one of these so it can be undone.

// Adds a shape to the layer that is active when the command is created.
class AddShapeCommand : public QUndoCommand {
public:
    AddShapeCommand(Document *document, std::unique_ptr<Shape> shape, QUndoCommand *parent = nullptr);

    Shape *shape() const { return m_shape; }

    void redo() override;
    void undo() override;

private:
    Document *m_document;
    Layer *m_layer;
    Shape *m_shape;
    std::unique_ptr<Shape> m_owned;
    int m_index = -1;
};

class RemoveShapeCommand : public QUndoCommand {
public:
    RemoveShapeCommand(Document *document, Shape *shape, QUndoCommand *parent = nullptr);

    void redo() override;
    void undo() override;

private:
    Document *m_document;
    Layer *m_layer;
    Shape *m_shape;
    std::unique_ptr<Shape> m_owned;
    int m_index;
};

class MoveShapeCommand : public QUndoCommand {
public:
    // Pass `alreadyApplied` when the shape was moved interactively before the command was pushed.
    MoveShapeCommand(Document *document, Shape *shape, const QPointF &delta, bool alreadyApplied,
                     QUndoCommand *parent = nullptr);

    void redo() override;
    void undo() override;

private:
    Document *m_document;
    Shape *m_shape;
    QPointF m_delta;
    bool m_skipNextRedo;
};

class SetBoundsCommand : public QUndoCommand {
public:
    // The shape may already have `newBounds` (interactive resize); redo simply applies it again.
    SetBoundsCommand(Document *document, Shape *shape, const QRectF &oldBounds, const QRectF &newBounds,
                     QUndoCommand *parent = nullptr);

    void redo() override;
    void undo() override;

private:
    Document *m_document;
    Shape *m_shape;
    QRectF m_oldBounds;
    QRectF m_newBounds;
};

// Replaces a path's nodes (node editing). The shape may already hold `newPath`.
class SetPathCommand : public QUndoCommand {
public:
    SetPathCommand(Document *document, Shape *shape, PathData oldPath, PathData newPath, const QString &text,
                   QUndoCommand *parent = nullptr);

    void redo() override;
    void undo() override;

private:
    Document *m_document;
    Shape *m_shape;
    PathData m_oldPath;
    PathData m_newPath;
};

// Swaps one shape for another at the same stacking position (e.g. "convert to curves").
class ReplaceShapeCommand : public QUndoCommand {
public:
    ReplaceShapeCommand(Document *document, Shape *oldShape, std::unique_ptr<Shape> newShape, const QString &text,
                        QUndoCommand *parent = nullptr);

    Shape *newShape() const { return m_newShape; }

    void redo() override;
    void undo() override;

private:
    Document *m_document;
    Layer *m_layer;
    Shape *m_oldShape;
    Shape *m_newShape;
    std::unique_ptr<Shape> m_owned; // whichever shape is currently out of the document
    int m_index;
};

// Puts shapes into a new group. The group goes into the layer and stacking position of the
// topmost of them; the shapes may come from different layers.
class GroupCommand : public QUndoCommand {
public:
    GroupCommand(Document *document, std::vector<Shape *> shapes, QUndoCommand *parent = nullptr);

    Shape *group() const { return m_group; }

    void redo() override;
    void undo() override;

private:
    struct Position {
        Shape *shape;
        Layer *layer;
        int layerIndex; // stacking of the layer on its page
        int index;      // position in the layer before grouping
    };
    Document *m_document;
    std::vector<Position> m_positions; // bottom to top
    Shape *m_group;
    std::unique_ptr<Shape> m_ownedGroup; // while the group is not in the document
};

// Replaces a group with its children, at the group's stacking position.
class UngroupCommand : public QUndoCommand {
public:
    UngroupCommand(Document *document, Shape *group, QUndoCommand *parent = nullptr);

    const std::vector<Shape *> &children() const { return m_children; }

    void redo() override;
    void undo() override;

private:
    Document *m_document;
    Shape *m_group;
    std::vector<Shape *> m_children;
    std::unique_ptr<Shape> m_ownedGroup;
    Layer *m_layer = nullptr;
    int m_index = -1;
};

// Changes the stacking order of one layer's shapes.
class ReorderCommand : public QUndoCommand {
public:
    ReorderCommand(Document *document, Layer *layer, std::vector<Shape *> newOrder, const QString &text,
                   QUndoCommand *parent = nullptr);
    // On the active layer.
    ReorderCommand(Document *document, std::vector<Shape *> newOrder, const QString &text,
                   QUndoCommand *parent = nullptr);

    void redo() override;
    void undo() override;

private:
    Document *m_document;
    Layer *m_layer;
    std::vector<Shape *> m_oldOrder;
    std::vector<Shape *> m_newOrder;
};

// Moves a shape to the top of another layer.
class MoveToLayerCommand : public QUndoCommand {
public:
    MoveToLayerCommand(Document *document, Shape *shape, Layer *target, QUndoCommand *parent = nullptr);

    void redo() override;
    void undo() override;

private:
    Document *m_document;
    Shape *m_shape;
    Layer *m_source;
    Layer *m_target;
    int m_index;
};

// Changes a text object's content and/or format. The shape may already hold `newText`.
class SetTextCommand : public QUndoCommand {
public:
    SetTextCommand(Document *document, Shape *shape, TextData oldText, TextData newText, const QString &text,
                   QUndoCommand *parent = nullptr);

    void redo() override;
    void undo() override;

private:
    Document *m_document;
    Shape *m_shape;
    TextData m_oldText;
    TextData m_newText;
};

class SetStyleCommand : public QUndoCommand {
public:
    SetStyleCommand(Document *document, Shape *shape, const Style &style, QUndoCommand *parent = nullptr);

    void redo() override;
    void undo() override;

private:
    Document *m_document;
    Shape *m_shape;
    Style m_newStyle;
    Style m_oldStyle;
};

// --- Layers and pages

// Adds a new layer above the active one and makes it active.
class AddLayerCommand : public QUndoCommand {
public:
    AddLayerCommand(Document *document, Page *page, QUndoCommand *parent = nullptr);

    Layer *layer() const { return m_layer; }

    void redo() override;
    void undo() override;

private:
    Document *m_document;
    Page *m_page;
    Layer *m_layer;
    Layer *m_previousActive;
    std::unique_ptr<Layer> m_owned;
    int m_index;
};

class RemoveLayerCommand : public QUndoCommand {
public:
    RemoveLayerCommand(Document *document, Layer *layer, QUndoCommand *parent = nullptr);

    void redo() override;
    void undo() override;

private:
    Document *m_document;
    Page *m_page;
    Layer *m_layer;
    bool m_wasActive;
    std::unique_ptr<Layer> m_owned;
    int m_index;
};

class MoveLayerCommand : public QUndoCommand {
public:
    MoveLayerCommand(Document *document, Layer *layer, int newIndex, QUndoCommand *parent = nullptr);

    void redo() override;
    void undo() override;

private:
    Document *m_document;
    Layer *m_layer;
    int m_oldIndex;
    int m_newIndex;
};

// Name, visibility and lock of a layer.
class LayerPropertiesCommand : public QUndoCommand {
public:
    LayerPropertiesCommand(Document *document, Layer *layer, const QString &name, bool visible, bool locked,
                           const QString &text, QUndoCommand *parent = nullptr);

    void redo() override;
    void undo() override;

private:
    Document *m_document;
    Layer *m_layer;
    QString m_oldName, m_newName;
    bool m_oldVisible, m_newVisible;
    bool m_oldLocked, m_newLocked;
};

// Inserts a new page with one empty layer at `index` and shows it.
class AddPageCommand : public QUndoCommand {
public:
    AddPageCommand(Document *document, int index, QUndoCommand *parent = nullptr);

    void redo() override;
    void undo() override;

private:
    Document *m_document;
    Page *m_page;
    std::unique_ptr<Page> m_owned;
    int m_index;
    int m_previousCurrent;
};

class RemovePageCommand : public QUndoCommand {
public:
    RemovePageCommand(Document *document, Page *page, QUndoCommand *parent = nullptr);

    void redo() override;
    void undo() override;

private:
    Document *m_document;
    Page *m_page;
    std::unique_ptr<Page> m_owned;
    int m_index;
    int m_previousCurrent;
};

class RenamePageCommand : public QUndoCommand {
public:
    RenamePageCommand(Document *document, Page *page, const QString &name, QUndoCommand *parent = nullptr);

    void redo() override;
    void undo() override;

private:
    Document *m_document;
    Page *m_page;
    QString m_oldName;
    QString m_newName;
};

// Applies an invertible transform (mirroring, rotating, skewing) to a shape. With
// `alreadyApplied`, the shape was transformed live while dragging and the first redo does nothing.
class TransformShapeCommand : public QUndoCommand {
public:
    TransformShapeCommand(Document *document, Shape *shape, const QTransform &transform, const QString &text,
                          bool alreadyApplied = false, QUndoCommand *parent = nullptr);

    void redo() override;
    void undo() override;

private:
    Document *m_document;
    Shape *m_shape;
    QTransform m_transform;
    bool m_skipRedo;
};

class SetCornerRadiusCommand : public QUndoCommand {
public:
    SetCornerRadiusCommand(Document *document, Shape *shape, double radius, QUndoCommand *parent = nullptr);

    void redo() override;
    void undo() override;

private:
    Document *m_document;
    Shape *m_shape;
    double m_oldRadius;
    double m_newRadius;
};

// Adds, moves or removes a guideline. `before` empty = add, `after` empty = remove.
class GuideCommand : public QUndoCommand {
public:
    GuideCommand(Document *document, int index, std::optional<Guide> before, std::optional<Guide> after,
                 QUndoCommand *parent = nullptr);

    void redo() override;
    void undo() override;

private:
    void apply(const std::optional<Guide> &from, const std::optional<Guide> &to);

    Document *m_document;
    int m_index;
    std::optional<Guide> m_before;
    std::optional<Guide> m_after;
};

// Changes the size of every page.
class SetPageSizeCommand : public QUndoCommand {
public:
    SetPageSizeCommand(Document *document, const QSizeF &size, QUndoCommand *parent = nullptr);

    void redo() override;
    void undo() override;

private:
    Document *m_document;
    QSizeF m_oldSize;
    QSizeF m_newSize;
};

} // namespace opendraw
