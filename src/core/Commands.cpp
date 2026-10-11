#include "core/Commands.h"

#include "core/Document.h"

#include <QCoreApplication>

#include <algorithm>

namespace opendraw {

AddShapeCommand::AddShapeCommand(Document *document, std::unique_ptr<Shape> shape, QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_document(document)
    , m_shape(shape.get())
    , m_owned(std::move(shape))
{
    switch (m_shape->kind()) {
    case ShapeKind::Rectangle:
        setText(QCoreApplication::translate("Commands", "Add rectangle"));
        break;
    case ShapeKind::Ellipse:
        setText(QCoreApplication::translate("Commands", "Add ellipse"));
        break;
    case ShapeKind::Path:
        setText(QCoreApplication::translate("Commands", "Add curve"));
        break;
    case ShapeKind::Group:
        setText(QCoreApplication::translate("Commands", "Add group"));
        break;
    case ShapeKind::Text:
        setText(QCoreApplication::translate("Commands", "Add text"));
        break;
    }
}

void AddShapeCommand::redo()
{
    m_document->insertShape(m_index, std::move(m_owned));
    m_index = m_document->indexOf(m_shape);
}

void AddShapeCommand::undo()
{
    m_owned = m_document->takeShape(m_shape);
}

RemoveShapeCommand::RemoveShapeCommand(Document *document, Shape *shape, QUndoCommand *parent)
    : QUndoCommand(QCoreApplication::translate("Commands", "Delete"), parent)
    , m_document(document)
    , m_shape(shape)
    , m_index(document->indexOf(shape))
{
}

void RemoveShapeCommand::redo()
{
    m_owned = m_document->takeShape(m_shape);
}

void RemoveShapeCommand::undo()
{
    m_document->insertShape(m_index, std::move(m_owned));
}

MoveShapeCommand::MoveShapeCommand(Document *document, Shape *shape, const QPointF &delta,
                                   bool alreadyApplied, QUndoCommand *parent)
    : QUndoCommand(QCoreApplication::translate("Commands", "Move"), parent)
    , m_document(document)
    , m_shape(shape)
    , m_delta(delta)
    , m_skipNextRedo(alreadyApplied)
{
}

void MoveShapeCommand::redo()
{
    if (m_skipNextRedo) {
        m_skipNextRedo = false;
        return;
    }
    m_shape->moveBy(m_delta);
    m_document->notifyShapeChanged(m_shape);
}

void MoveShapeCommand::undo()
{
    m_shape->moveBy(-m_delta);
    m_document->notifyShapeChanged(m_shape);
}

SetBoundsCommand::SetBoundsCommand(Document *document, Shape *shape, const QRectF &oldBounds,
                                   const QRectF &newBounds, QUndoCommand *parent)
    : QUndoCommand(QCoreApplication::translate("Commands", "Resize"), parent)
    , m_document(document)
    , m_shape(shape)
    , m_oldBounds(oldBounds)
    , m_newBounds(newBounds)
{
}

void SetBoundsCommand::redo()
{
    m_shape->setBounds(m_newBounds);
    m_document->notifyShapeChanged(m_shape);
}

void SetBoundsCommand::undo()
{
    m_shape->setBounds(m_oldBounds);
    m_document->notifyShapeChanged(m_shape);
}

SetPathCommand::SetPathCommand(Document *document, Shape *shape, PathData oldPath, PathData newPath,
                               const QString &text, QUndoCommand *parent)
    : QUndoCommand(text, parent)
    , m_document(document)
    , m_shape(shape)
    , m_oldPath(std::move(oldPath))
    , m_newPath(std::move(newPath))
{
}

void SetPathCommand::redo()
{
    m_shape->setPathData(m_newPath);
    m_document->notifyShapeChanged(m_shape);
}

void SetPathCommand::undo()
{
    m_shape->setPathData(m_oldPath);
    m_document->notifyShapeChanged(m_shape);
}

ReplaceShapeCommand::ReplaceShapeCommand(Document *document, Shape *oldShape, std::unique_ptr<Shape> newShape,
                                         const QString &text, QUndoCommand *parent)
    : QUndoCommand(text, parent)
    , m_document(document)
    , m_oldShape(oldShape)
    , m_newShape(newShape.get())
    , m_owned(std::move(newShape))
    , m_index(document->indexOf(oldShape))
{
}

void ReplaceShapeCommand::redo()
{
    std::unique_ptr<Shape> incoming = std::move(m_owned);
    m_owned = m_document->takeShape(m_oldShape);
    m_document->insertShape(m_index, std::move(incoming));
}

void ReplaceShapeCommand::undo()
{
    std::unique_ptr<Shape> incoming = std::move(m_owned);
    m_owned = m_document->takeShape(m_newShape);
    m_document->insertShape(m_index, std::move(incoming));
}

GroupCommand::GroupCommand(Document *document, std::vector<Shape *> shapes, QUndoCommand *parent)
    : QUndoCommand(QCoreApplication::translate("Commands", "Group"), parent)
    , m_document(document)
    , m_shapes(std::move(shapes))
{
    std::sort(m_shapes.begin(), m_shapes.end(),
              [document](Shape *a, Shape *b) { return document->indexOf(a) < document->indexOf(b); });
    for (Shape *shape : m_shapes)
        m_indices.push_back(document->indexOf(shape));
    m_ownedGroup = std::make_unique<Shape>(std::vector<std::unique_ptr<Shape>>{});
    m_group = m_ownedGroup.get();
}

void GroupCommand::redo()
{
    std::vector<std::unique_ptr<Shape>> children;
    for (Shape *shape : m_shapes)
        children.push_back(m_document->takeShape(shape));
    m_ownedGroup->setChildren(std::move(children));
    // The topmost shape's old index, shifted down by the shapes taken out below it.
    const int index = m_indices.back() - int(m_indices.size()) + 1;
    m_document->insertShape(index, std::move(m_ownedGroup));
}

void GroupCommand::undo()
{
    m_ownedGroup = m_document->takeShape(m_group);
    std::vector<std::unique_ptr<Shape>> children = m_ownedGroup->takeChildren();
    for (std::size_t i = 0; i < children.size(); ++i)
        m_document->insertShape(m_indices[i], std::move(children[i]));
}

UngroupCommand::UngroupCommand(Document *document, Shape *group, QUndoCommand *parent)
    : QUndoCommand(QCoreApplication::translate("Commands", "Ungroup"), parent)
    , m_document(document)
    , m_group(group)
{
    for (const auto &child : group->children())
        m_children.push_back(child.get());
}

void UngroupCommand::redo()
{
    m_index = m_document->indexOf(m_group);
    m_ownedGroup = m_document->takeShape(m_group);
    std::vector<std::unique_ptr<Shape>> children = m_ownedGroup->takeChildren();
    for (std::size_t i = 0; i < children.size(); ++i)
        m_document->insertShape(m_index + int(i), std::move(children[i]));
}

void UngroupCommand::undo()
{
    std::vector<std::unique_ptr<Shape>> children;
    for (Shape *child : m_children)
        children.push_back(m_document->takeShape(child));
    m_ownedGroup->setChildren(std::move(children));
    m_document->insertShape(m_index, std::move(m_ownedGroup));
}

ReorderCommand::ReorderCommand(Document *document, std::vector<Shape *> newOrder, const QString &text,
                               QUndoCommand *parent)
    : QUndoCommand(text, parent)
    , m_document(document)
    , m_oldOrder(document->order())
    , m_newOrder(std::move(newOrder))
{
}

void ReorderCommand::redo()
{
    m_document->setOrder(m_newOrder);
}

void ReorderCommand::undo()
{
    m_document->setOrder(m_oldOrder);
}

SetTextCommand::SetTextCommand(Document *document, Shape *shape, TextData oldText, TextData newText,
                               const QString &text, QUndoCommand *parent)
    : QUndoCommand(text, parent)
    , m_document(document)
    , m_shape(shape)
    , m_oldText(std::move(oldText))
    , m_newText(std::move(newText))
{
}

void SetTextCommand::redo()
{
    m_shape->setTextData(m_newText);
    m_document->notifyShapeChanged(m_shape);
}

void SetTextCommand::undo()
{
    m_shape->setTextData(m_oldText);
    m_document->notifyShapeChanged(m_shape);
}

SetStyleCommand::SetStyleCommand(Document *document, Shape *shape, const Style &style, QUndoCommand *parent)
    : QUndoCommand(QCoreApplication::translate("Commands", "Change style"), parent)
    , m_document(document)
    , m_shape(shape)
    , m_newStyle(style)
    , m_oldStyle(shape->style())
{
}

void SetStyleCommand::redo()
{
    m_shape->setStyle(m_newStyle);
    m_document->notifyShapeChanged(m_shape);
}

void SetStyleCommand::undo()
{
    m_shape->setStyle(m_oldStyle);
    m_document->notifyShapeChanged(m_shape);
}

} // namespace opendraw
