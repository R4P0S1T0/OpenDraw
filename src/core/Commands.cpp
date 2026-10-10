#include "core/Commands.h"

#include "core/Document.h"

#include <QCoreApplication>

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
