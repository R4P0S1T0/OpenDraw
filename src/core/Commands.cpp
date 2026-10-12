#include "core/Commands.h"

#include "core/Document.h"
#include "core/PathOps.h"

#include <QCoreApplication>

#include <algorithm>

namespace opendraw {

namespace {
} // namespace

// --- Shapes

AddShapeCommand::AddShapeCommand(Document *document, std::unique_ptr<Shape> shape, QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_document(document)
    , m_layer(document->activeLayer())
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
    case ShapeKind::Image:
        setText(QCoreApplication::translate("Commands", "Add bitmap"));
        break;
    }
}

void AddShapeCommand::redo()
{
    m_document->insertShape(m_layer, m_index, std::move(m_owned));
    m_index = m_document->indexOf(m_shape);
}

void AddShapeCommand::undo()
{
    m_owned = m_document->takeShape(m_shape);
}

RemoveShapeCommand::RemoveShapeCommand(Document *document, Shape *shape, QUndoCommand *parent)
    : QUndoCommand(QCoreApplication::translate("Commands", "Delete"), parent)
    , m_document(document)
    , m_layer(document->layerOf(shape))
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
    m_document->insertShape(m_layer, m_index, std::move(m_owned));
}

MoveShapeCommand::MoveShapeCommand(Document *document, Shape *shape, const QPointF &delta, bool alreadyApplied,
                                   QUndoCommand *parent)
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
    , m_layer(document->layerOf(oldShape))
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
    m_document->insertShape(m_layer, m_index, std::move(incoming));
}

void ReplaceShapeCommand::undo()
{
    std::unique_ptr<Shape> incoming = std::move(m_owned);
    m_owned = m_document->takeShape(m_newShape);
    m_document->insertShape(m_layer, m_index, std::move(incoming));
}

// --- Grouping and order

GroupCommand::GroupCommand(Document *document, std::vector<Shape *> shapes, QUndoCommand *parent)
    : QUndoCommand(QCoreApplication::translate("Commands", "Group"), parent)
    , m_document(document)
{
    for (Shape *shape : shapes) {
        Layer *layer = document->layerOf(shape);
        m_positions.push_back({shape, layer, document->indexOfLayer(layer), document->indexOf(shape)});
    }
    std::sort(m_positions.begin(), m_positions.end(), [](const Position &a, const Position &b) {
        return a.layerIndex != b.layerIndex ? a.layerIndex < b.layerIndex : a.index < b.index;
    });
    m_ownedGroup = std::make_unique<Shape>(ShapeList{});
    m_group = m_ownedGroup.get();
}

void GroupCommand::redo()
{
    const Position &top = m_positions.back();
    // The topmost shape's old index, shifted down by the grouped shapes taken out below it.
    const int below = int(std::count_if(m_positions.begin(), m_positions.end(),
                                        [&top](const Position &p) { return p.layer == top.layer && p.index < top.index; }));
    ShapeList children;
    for (const Position &p : m_positions)
        children.push_back(m_document->takeShape(p.shape));
    m_ownedGroup->setChildren(std::move(children));
    m_document->insertShape(top.layer, top.index - below, std::move(m_ownedGroup));
}

void GroupCommand::undo()
{
    m_ownedGroup = m_document->takeShape(m_group);
    ShapeList children = m_ownedGroup->takeChildren();
    // Ascending positions per layer, so every shape lands where it was.
    for (std::size_t i = 0; i < children.size(); ++i)
        m_document->insertShape(m_positions[i].layer, m_positions[i].index, std::move(children[i]));
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
    m_layer = m_document->layerOf(m_group);
    m_index = m_document->indexOf(m_group);
    m_ownedGroup = m_document->takeShape(m_group);
    ShapeList children = m_ownedGroup->takeChildren();
    for (std::size_t i = 0; i < children.size(); ++i)
        m_document->insertShape(m_layer, m_index + int(i), std::move(children[i]));
}

void UngroupCommand::undo()
{
    ShapeList children;
    for (Shape *child : m_children)
        children.push_back(m_document->takeShape(child));
    m_ownedGroup->setChildren(std::move(children));
    m_document->insertShape(m_layer, m_index, std::move(m_ownedGroup));
}

ReorderCommand::ReorderCommand(Document *document, Layer *layer, std::vector<Shape *> newOrder,
                               const QString &text, QUndoCommand *parent)
    : QUndoCommand(text, parent)
    , m_document(document)
    , m_layer(layer)
    , m_oldOrder(document->order(layer))
    , m_newOrder(std::move(newOrder))
{
}

ReorderCommand::ReorderCommand(Document *document, std::vector<Shape *> newOrder, const QString &text,
                               QUndoCommand *parent)
    : ReorderCommand(document, document->activeLayer(), std::move(newOrder), text, parent)
{
}

void ReorderCommand::redo()
{
    m_document->setOrder(m_layer, m_newOrder);
}

void ReorderCommand::undo()
{
    m_document->setOrder(m_layer, m_oldOrder);
}

MoveToLayerCommand::MoveToLayerCommand(Document *document, Shape *shape, Layer *target, QUndoCommand *parent)
    : QUndoCommand(QCoreApplication::translate("Commands", "Move to layer"), parent)
    , m_document(document)
    , m_shape(shape)
    , m_source(document->layerOf(shape))
    , m_target(target)
    , m_index(document->indexOf(shape))
{
}

void MoveToLayerCommand::redo()
{
    m_document->insertShape(m_target, -1, m_document->takeShape(m_shape));
}

void MoveToLayerCommand::undo()
{
    m_document->insertShape(m_source, m_index, m_document->takeShape(m_shape));
}

// --- Content and style

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

// --- Layers

AddLayerCommand::AddLayerCommand(Document *document, Page *page, QUndoCommand *parent)
    : QUndoCommand(QCoreApplication::translate("Commands", "New layer"), parent)
    , m_document(document)
    , m_page(page)
    , m_previousActive(page->activeLayer)
    , m_owned(Document::makeLayer(document->newLayerName(page)))
    , m_index(document->indexOfLayer(page->activeLayer) + 1)
{
    m_layer = m_owned.get();
}

void AddLayerCommand::redo()
{
    m_document->insertLayer(m_page, m_index, std::move(m_owned));
    m_document->setActiveLayer(m_layer);
}

void AddLayerCommand::undo()
{
    m_document->setActiveLayer(m_previousActive);
    m_owned = m_document->takeLayer(m_layer);
}

RemoveLayerCommand::RemoveLayerCommand(Document *document, Layer *layer, QUndoCommand *parent)
    : QUndoCommand(QCoreApplication::translate("Commands", "Delete layer"), parent)
    , m_document(document)
    , m_page(document->pageOf(layer))
    , m_layer(layer)
    , m_wasActive(m_page->activeLayer == layer)
    , m_index(document->indexOfLayer(layer))
{
}

void RemoveLayerCommand::redo()
{
    m_owned = m_document->takeLayer(m_layer);
}

void RemoveLayerCommand::undo()
{
    m_document->insertLayer(m_page, m_index, std::move(m_owned));
    if (m_wasActive)
        m_document->setActiveLayer(m_layer);
}

MoveLayerCommand::MoveLayerCommand(Document *document, Layer *layer, int newIndex, QUndoCommand *parent)
    : QUndoCommand(QCoreApplication::translate("Commands", "Move layer"), parent)
    , m_document(document)
    , m_layer(layer)
    , m_oldIndex(document->indexOfLayer(layer))
    , m_newIndex(newIndex)
{
}

void MoveLayerCommand::redo()
{
    m_document->moveLayer(m_layer, m_newIndex);
}

void MoveLayerCommand::undo()
{
    m_document->moveLayer(m_layer, m_oldIndex);
}

LayerPropertiesCommand::LayerPropertiesCommand(Document *document, Layer *layer, const QString &name, bool visible,
                                               bool locked, const QString &text, QUndoCommand *parent)
    : QUndoCommand(text, parent)
    , m_document(document)
    , m_layer(layer)
    , m_oldName(layer->name)
    , m_newName(name)
    , m_oldVisible(layer->visible)
    , m_newVisible(visible)
    , m_oldLocked(layer->locked)
    , m_newLocked(locked)
{
}

void LayerPropertiesCommand::redo()
{
    m_document->setLayerProperties(m_layer, m_newName, m_newVisible, m_newLocked);
}

void LayerPropertiesCommand::undo()
{
    m_document->setLayerProperties(m_layer, m_oldName, m_oldVisible, m_oldLocked);
}

// --- Pages

AddPageCommand::AddPageCommand(Document *document, int index, QUndoCommand *parent)
    : QUndoCommand(QCoreApplication::translate("Commands", "Insert page"), parent)
    , m_document(document)
    , m_owned(Document::makePage(document->newPageName()))
    , m_index(index)
    , m_previousCurrent(document->currentPageIndex())
{
    m_page = m_owned.get();
}

void AddPageCommand::redo()
{
    m_previousCurrent = m_document->currentPageIndex();
    m_document->insertPage(m_index, std::move(m_owned));
    m_document->setCurrentPage(m_document->indexOfPage(m_page));
}

void AddPageCommand::undo()
{
    m_owned = m_document->takePage(m_page);
    m_document->setCurrentPage(m_previousCurrent);
}

RemovePageCommand::RemovePageCommand(Document *document, Page *page, QUndoCommand *parent)
    : QUndoCommand(QCoreApplication::translate("Commands", "Delete page"), parent)
    , m_document(document)
    , m_page(page)
    , m_index(document->indexOfPage(page))
    , m_previousCurrent(document->currentPageIndex())
{
}

void RemovePageCommand::redo()
{
    m_previousCurrent = m_document->currentPageIndex();
    m_owned = m_document->takePage(m_page);
}

void RemovePageCommand::undo()
{
    m_document->insertPage(m_index, std::move(m_owned));
    m_document->setCurrentPage(m_previousCurrent);
}

RenamePageCommand::RenamePageCommand(Document *document, Page *page, const QString &name, QUndoCommand *parent)
    : QUndoCommand(QCoreApplication::translate("Commands", "Rename page"), parent)
    , m_document(document)
    , m_page(page)
    , m_oldName(page->name)
    , m_newName(name)
{
}

void RenamePageCommand::redo()
{
    m_document->renamePage(m_page, m_newName);
}

void RenamePageCommand::undo()
{
    m_document->renamePage(m_page, m_oldName);
}

TransformShapeCommand::TransformShapeCommand(Document *document, Shape *shape, const QTransform &transform,
                                             const QString &text, bool alreadyApplied, QUndoCommand *parent)
    : QUndoCommand(text, parent)
    , m_document(document)
    , m_shape(shape)
    , m_transform(transform)
    , m_skipRedo(alreadyApplied)
{
}

void TransformShapeCommand::redo()
{
    if (m_skipRedo) {
        m_skipRedo = false;
        return;
    }
    m_shape->transform(m_transform);
    m_document->notifyShapeChanged(m_shape);
}

void TransformShapeCommand::undo()
{
    m_shape->transform(m_transform.inverted());
    m_document->notifyShapeChanged(m_shape);
}

RenameShapeCommand::RenameShapeCommand(Document *document, Shape *shape, const QString &name, QUndoCommand *parent)
    : QUndoCommand(QCoreApplication::translate("Commands", "Rename object"), parent)
    , m_document(document)
    , m_shape(shape)
    , m_oldName(shape->name())
    , m_newName(name)
{
}

void RenameShapeCommand::redo()
{
    m_shape->setName(m_newName);
    m_document->notifyShapeChanged(m_shape);
}

void RenameShapeCommand::undo()
{
    m_shape->setName(m_oldName);
    m_document->notifyShapeChanged(m_shape);
}

SetCornerRadiusCommand::SetCornerRadiusCommand(Document *document, Shape *shape, double radius, QUndoCommand *parent)
    : QUndoCommand(QCoreApplication::translate("Commands", "Corner radius"), parent)
    , m_document(document)
    , m_shape(shape)
    , m_oldRadius(shape->cornerRadius())
    , m_newRadius(radius)
{
}

void SetCornerRadiusCommand::redo()
{
    m_shape->setCornerRadius(m_newRadius);
    m_document->notifyShapeChanged(m_shape);
}

void SetCornerRadiusCommand::undo()
{
    m_shape->setCornerRadius(m_oldRadius);
    m_document->notifyShapeChanged(m_shape);
}

PowerClipCommand::PowerClipCommand(Document *document, Shape *frame, std::vector<Shape *> contents,
                                   QUndoCommand *parent)
    : QUndoCommand(QCoreApplication::translate("Commands", "Place inside frame"), parent)
    , m_document(document)
    , m_frame(frame)
{
    for (Shape *shape : contents) {
        Layer *layer = document->layerOf(shape);
        m_positions.push_back({shape, layer, document->indexOfLayer(layer), document->indexOf(shape)});
    }
    std::sort(m_positions.begin(), m_positions.end(), [](const Position &a, const Position &b) {
        return a.layerIndex != b.layerIndex ? a.layerIndex < b.layerIndex : a.index < b.index;
    });
}

void PowerClipCommand::redo()
{
    ShapeList contents = m_frame->takeClippedContents();
    for (const Position &p : m_positions)
        contents.push_back(m_document->takeShape(p.shape));
    m_frame->setClippedContents(std::move(contents));
    m_document->notifyShapeChanged(m_frame);
}

void PowerClipCommand::undo()
{
    ShapeList contents = m_frame->takeClippedContents();
    std::vector<std::unique_ptr<Shape>> placed;
    for (const Position &p : m_positions) {
        const auto it = std::find_if(contents.begin(), contents.end(),
                                     [&p](const std::unique_ptr<Shape> &s) { return s.get() == p.shape; });
        placed.push_back(std::move(*it));
        contents.erase(it);
    }
    m_frame->setClippedContents(std::move(contents));
    // Ascending positions per layer, so every shape lands where it was.
    for (std::size_t i = 0; i < placed.size(); ++i)
        m_document->insertShape(m_positions[i].layer, m_positions[i].index, std::move(placed[i]));
}

ExtractContentsCommand::ExtractContentsCommand(Document *document, Shape *frame, QUndoCommand *parent)
    : QUndoCommand(QCoreApplication::translate("Commands", "Extract contents"), parent)
    , m_document(document)
    , m_frame(frame)
{
    for (const auto &content : frame->clippedContents())
        m_contents.push_back(content.get());
}

void ExtractContentsCommand::redo()
{
    Layer *layer = m_document->layerOf(m_frame);
    const int index = m_document->indexOf(m_frame);
    ShapeList contents = m_frame->takeClippedContents();
    for (std::size_t i = 0; i < contents.size(); ++i)
        m_document->insertShape(layer, index + 1 + int(i), std::move(contents[i]));
    m_document->notifyShapeChanged(m_frame);
}

void ExtractContentsCommand::undo()
{
    ShapeList contents;
    for (Shape *shape : m_contents)
        contents.push_back(m_document->takeShape(shape));
    m_frame->setClippedContents(std::move(contents));
    m_document->notifyShapeChanged(m_frame);
}

SetEnvelopeCommand::SetEnvelopeCommand(Document *document, Shape *shape, std::optional<Envelope> envelope,
                                       QUndoCommand *parent)
    : QUndoCommand(envelope ? QCoreApplication::translate("Commands", "Envelope")
                            : QCoreApplication::translate("Commands", "Remove envelope"),
                   parent)
    , m_document(document)
    , m_shape(shape)
    , m_old(shape->envelope())
    , m_new(std::move(envelope))
    , m_oldPath(shape->pathData())
    , m_oldTransform(shape->shapeTransform())
{
}

void SetEnvelopeCommand::redo()
{
    m_shape->setEnvelope(m_new);
    if (!m_new && m_shape->kind() == ShapeKind::Path && !m_shape->shapeTransform().isIdentity()) {
        PathData path = m_shape->pathData();
        pathops::transform(path, m_shape->shapeTransform());
        m_shape->setPathData(std::move(path));
        m_shape->setShapeTransform(QTransform());
    }
    m_document->notifyShapeChanged(m_shape);
}

void SetEnvelopeCommand::undo()
{
    if (m_shape->kind() == ShapeKind::Path) {
        m_shape->setPathData(m_oldPath);
        m_shape->setShapeTransform(m_oldTransform);
    }
    m_shape->setEnvelope(m_old);
    m_document->notifyShapeChanged(m_shape);
}

SetBlendStepsCommand::SetBlendStepsCommand(Document *document, Shape *blend, int steps, QUndoCommand *parent)
    : QUndoCommand(QCoreApplication::translate("Commands", "Blend steps"), parent)
    , m_document(document)
    , m_blend(blend)
    , m_oldSteps(blend->blendSteps())
    , m_newSteps(steps)
{
}

void SetBlendStepsCommand::redo()
{
    m_blend->setBlendSteps(m_newSteps);
    m_document->notifyShapeChanged(m_blend);
}

void SetBlendStepsCommand::undo()
{
    m_blend->setBlendSteps(m_oldSteps);
    m_document->notifyShapeChanged(m_blend);
}

BreakBlendCommand::BreakBlendCommand(Document *document, Shape *blend, QUndoCommand *parent)
    : QUndoCommand(QCoreApplication::translate("Commands", "Break blend apart"), parent)
    , m_document(document)
    , m_blend(blend)
    , m_steps(blend->blendSteps())
    , m_ownedSteps(std::make_unique<Shape>(blend->blendIntermediates()))
{
    m_stepsGroup = m_ownedSteps.get();
}

void BreakBlendCommand::redo()
{
    ShapeList children = m_blend->takeChildren();
    children.insert(children.begin() + 1, std::move(m_ownedSteps));
    m_blend->setChildren(std::move(children));
    m_blend->setBlendSteps(0);
    m_document->notifyShapeChanged(m_blend);
}

void BreakBlendCommand::undo()
{
    ShapeList children = m_blend->takeChildren();
    const auto it = std::find_if(children.begin(), children.end(),
                                 [this](const std::unique_ptr<Shape> &s) { return s.get() == m_stepsGroup; });
    m_ownedSteps = std::move(*it);
    children.erase(it);
    m_blend->setChildren(std::move(children));
    m_blend->setBlendSteps(m_steps);
    m_document->notifyShapeChanged(m_blend);
}

GuideCommand::GuideCommand(Document *document, int index, std::optional<Guide> before, std::optional<Guide> after,
                           QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_document(document)
    , m_index(index)
    , m_before(before)
    , m_after(after)
{
    if (!before)
        setText(QCoreApplication::translate("Commands", "Add guideline"));
    else if (!after)
        setText(QCoreApplication::translate("Commands", "Delete guideline"));
    else
        setText(QCoreApplication::translate("Commands", "Move guideline"));
}

void GuideCommand::apply(const std::optional<Guide> &from, const std::optional<Guide> &to)
{
    if (from && to)
        m_document->setGuide(m_index, *to);
    else if (to)
        m_document->insertGuide(m_index, *to);
    else if (from)
        m_document->takeGuide(m_index);
}

void GuideCommand::redo()
{
    apply(m_before, m_after);
}

void GuideCommand::undo()
{
    apply(m_after, m_before);
}

SetPageSizeCommand::SetPageSizeCommand(Document *document, const QSizeF &size, QUndoCommand *parent)
    : QUndoCommand(QCoreApplication::translate("Commands", "Page size"), parent)
    , m_document(document)
    , m_oldSize(document->pageSize())
    , m_newSize(size)
{
}

void SetPageSizeCommand::redo()
{
    m_document->setPageSize(m_newSize);
}

void SetPageSizeCommand::undo()
{
    m_document->setPageSize(m_oldSize);
}

} // namespace opendraw
