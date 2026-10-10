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
    setText(m_shape->kind() == ShapeKind::Rectangle
                ? QCoreApplication::translate("Commands", "Add rectangle")
                : QCoreApplication::translate("Commands", "Add ellipse"));
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
