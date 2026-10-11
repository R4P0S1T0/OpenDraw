#pragma once

#include "core/Shape.h"

#include <QPointF>
#include <QUndoCommand>

#include <memory>

namespace opendraw {

class Document;

// Every document change goes through one of these so it can be undone.

class AddShapeCommand : public QUndoCommand {
public:
    AddShapeCommand(Document *document, std::unique_ptr<Shape> shape, QUndoCommand *parent = nullptr);

    Shape *shape() const { return m_shape; }

    void redo() override;
    void undo() override;

private:
    Document *m_document;
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
    Shape *m_oldShape;
    Shape *m_newShape;
    std::unique_ptr<Shape> m_owned; // whichever shape is currently out of the document
    int m_index;
};

// Puts shapes into a new group, which takes the stacking position of the topmost of them.
class GroupCommand : public QUndoCommand {
public:
    GroupCommand(Document *document, std::vector<Shape *> shapes, QUndoCommand *parent = nullptr);

    Shape *group() const { return m_group; }

    void redo() override;
    void undo() override;

private:
    Document *m_document;
    std::vector<Shape *> m_shapes; // in paint order
    std::vector<int> m_indices;    // their positions before grouping
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
    int m_index = -1;
};

// Changes the stacking order of the document's shapes.
class ReorderCommand : public QUndoCommand {
public:
    ReorderCommand(Document *document, std::vector<Shape *> newOrder, const QString &text,
                   QUndoCommand *parent = nullptr);

    void redo() override;
    void undo() override;

private:
    Document *m_document;
    std::vector<Shape *> m_oldOrder;
    std::vector<Shape *> m_newOrder;
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

} // namespace opendraw
