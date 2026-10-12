#include "ObjectsPanel.h"

#include "CanvasWidget.h"
#include "Icons.h"
#include "core/Commands.h"
#include "core/Document.h"

#include <QDropEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMenu>
#include <QScrollBar>
#include <QTimer>
#include <QToolButton>
#include <QUndoStack>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace opendraw {

namespace {
constexpr int kNameColumn = 0;
constexpr int kVisibleColumn = 1;
constexpr int kLockColumn = 2;
constexpr int kKindRole = Qt::UserRole;
constexpr int kPointerRole = Qt::UserRole + 1;
constexpr int kTopShapeRole = Qt::UserRole + 2;

QVariant pointer(const void *p)
{
    return QVariant::fromValue(const_cast<void *>(p));
}

Icon iconFor(const Shape &shape)
{
    switch (shape.kind()) {
    case ShapeKind::Rectangle:
        return Icon::Rectangle;
    case ShapeKind::Ellipse:
        return Icon::Ellipse;
    case ShapeKind::Path:
        return Icon::Bezier;
    case ShapeKind::Group:
        return shape.isBlend() ? Icon::Blend : Icon::Group;
    case ShapeKind::Text:
        return Icon::Text;
    case ShapeKind::Image:
        return Icon::Bitmap;
    }
    return Icon::Bezier;
}

// Everything the tree shows, so it is only rebuilt when that changes (not on every move).
size_t signatureOf(const Document &document)
{
    size_t seed = qHash(document.currentPageIndex());
    std::function<void(const Shape &)> addShape = [&](const Shape &shape) {
        seed = qHashMulti(seed, &shape, ObjectsPanel::describe(shape));
        for (const auto &child : shape.children())
            addShape(*child);
        seed = qHash(seed + 1);
    };
    for (const auto &page : document.pages()) {
        seed = qHashMulti(seed, page.get(), page->name, page->activeLayer);
        for (const auto &layer : page->layers) {
            seed = qHashMulti(seed, layer.get(), layer->name, layer->visible, layer->locked);
            for (const auto &shape : layer->shapes)
                addShape(*shape);
        }
    }
    return seed;
}
} // namespace

void ObjectsTree::dropEvent(QDropEvent *event)
{
    QTreeWidgetItem *target = itemAt(event->position().toPoint());
    DropPosition position = DropPosition::On;
    if (dropIndicatorPosition() == AboveItem)
        position = DropPosition::Above;
    else if (dropIndicatorPosition() == BelowItem)
        position = DropPosition::Below;
    const QList<QTreeWidgetItem *> items = selectedItems();
    // The panel restacks the document; the tree itself must not move anything.
    event->setDropAction(Qt::IgnoreAction);
    event->accept();
    emit itemsDropped(items, target, position);
}

QString ObjectsPanel::describe(const Shape &shape)
{
    if (!shape.name().isEmpty())
        return shape.name();
    switch (shape.kind()) {
    case ShapeKind::Rectangle:
        return tr("Rectangle");
    case ShapeKind::Ellipse:
        return tr("Ellipse");
    case ShapeKind::Path:
        return tr("Curve");
    case ShapeKind::Group:
        if (shape.isBlend())
            return tr("Blend of %n step(s)", nullptr, shape.blendSteps());
        return tr("Group of %n object(s)", nullptr, int(shape.children().size()));
    case ShapeKind::Text: {
        QString first = shape.textData().text.section(QLatin1Char('\n'), 0, 0);
        if (first.size() > 30)
            first = first.left(29) + QChar(0x2026);
        return tr("Text: %1").arg(first);
    }
    case ShapeKind::Image:
        return tr("Bitmap");
    }
    return {};
}

ObjectsPanel::ObjectsPanel(Document *document, QUndoStack *undoStack, CanvasWidget *canvas, QWidget *parent)
    : QWidget(parent)
    , m_document(document)
    , m_undoStack(undoStack)
    , m_canvas(canvas)
{
    m_tree = new ObjectsTree(this);
    m_tree->setColumnCount(3);
    m_tree->setHeaderHidden(true);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(kNameColumn, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(kVisibleColumn, QHeaderView::Fixed);
    m_tree->header()->setSectionResizeMode(kLockColumn, QHeaderView::Fixed);
    m_tree->header()->resizeSection(kVisibleColumn, 26);
    m_tree->header()->resizeSection(kLockColumn, 26);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    m_tree->setDragDropMode(QAbstractItemView::InternalMove);
    m_tree->setDefaultDropAction(Qt::MoveAction);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setUniformRowHeights(true);
    m_tree->setToolTip(tr("Click to select, double-click to rename, drag objects to restack them or move them to "
                          "another layer. The bold layer receives new objects."));

    auto makeButton = [this](Icon id, const QString &tip) {
        auto *button = new QToolButton(this);
        button->setIcon(icon(id));
        button->setToolTip(tip);
        button->setAutoRaise(true);
        return button;
    };
    m_add = makeButton(Icon::Layer, tr("New layer"));
    m_remove = new QToolButton(this);
    m_remove->setText(QStringLiteral("−"));
    m_remove->setToolTip(tr("Delete the selected objects, or the layer"));
    m_remove->setAutoRaise(true);
    m_up = makeButton(Icon::ScrollUp, tr("Move up: selected objects one step forward, or the layer up"));
    m_down = makeButton(Icon::ScrollDown, tr("Move down: selected objects one step back, or the layer down"));
    m_moveHere = new QToolButton(this);
    m_moveHere->setText(tr("Move here"));
    m_moveHere->setToolTip(tr("Move the selected objects to this layer"));
    m_moveHere->setAutoRaise(true);

    auto *buttons = new QHBoxLayout;
    buttons->setContentsMargins(0, 0, 0, 0);
    for (QToolButton *button : {m_add, m_remove, m_up, m_down})
        buttons->addWidget(button);
    buttons->addStretch();
    buttons->addWidget(m_moveHere);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(m_tree);
    layout->addLayout(buttons);

    connect(m_add, &QToolButton::clicked, this,
            [this] { m_undoStack->push(new AddLayerCommand(m_document, m_document->currentPage())); });
    connect(m_remove, &QToolButton::clicked, this, &ObjectsPanel::deleteSelected);
    connect(m_up, &QToolButton::clicked, this, [this] { moveUp(true); });
    connect(m_down, &QToolButton::clicked, this, [this] { moveUp(false); });
    connect(m_moveHere, &QToolButton::clicked, this,
            [this] { m_canvas->moveSelectionToLayer(m_document->activeLayer()); });

    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &ObjectsPanel::selectionChangedInTree);
    connect(m_tree, &QTreeWidget::itemChanged, this, &ObjectsPanel::onItemChanged);
    connect(m_tree, &QTreeWidget::itemClicked, this, &ObjectsPanel::onItemClicked);
    connect(m_tree, &ObjectsTree::itemsDropped, this, &ObjectsPanel::onDrop);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &ObjectsPanel::showContextMenu);
    connect(m_tree, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem *item) {
        if (m_updating)
            return;
        m_expanded.insert(pointerOf(item));
        m_collapsed.remove(pointerOf(item));
    });
    connect(m_tree, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem *item) {
        if (m_updating)
            return;
        m_collapsed.insert(pointerOf(item));
        m_expanded.remove(pointerOf(item));
    });

    // Rebuilt a moment after changes, never from inside the tree's own handlers (that would
    // delete the item being handled).
    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setSingleShot(true);
    m_refreshTimer->setInterval(60);
    connect(m_refreshTimer, &QTimer::timeout, this, [this] {
        const size_t signature = signatureOf(*m_document);
        if (signature != m_tree->property("signature").value<size_t>() || m_tree->topLevelItemCount() == 0)
            refresh();
        else
            updateButtons();
    });
    connectDocument();
    connect(m_canvas, &CanvasWidget::selectionChanged, this, [this] {
        syncSelectionFromCanvas();
        updateButtons();
    });
    refresh();
}

void ObjectsPanel::setDocument(Document *document, QUndoStack *undoStack)
{
    if (document == m_document && undoStack == m_undoStack)
        return;
    m_document->disconnect(this);
    m_document = document;
    m_undoStack = undoStack;
    m_expanded.clear();
    m_collapsed.clear();
    connectDocument();
    refresh();
}

void ObjectsPanel::connectDocument()
{
    auto schedule = [this] { m_refreshTimer->start(); };
    connect(m_document, &Document::changed, this, schedule);
    connect(m_document, &Document::structureChanged, this, schedule);
    connect(m_document, &Document::currentPageChanged, this, schedule);
    connect(m_document, &Document::cleared, this, schedule);
}

ObjectsPanel::Kind ObjectsPanel::kindOf(const QTreeWidgetItem *item)
{
    return Kind(item->data(kNameColumn, kKindRole).toInt());
}

void *ObjectsPanel::pointerOf(const QTreeWidgetItem *item)
{
    return item ? item->data(kNameColumn, kPointerRole).value<void *>() : nullptr;
}

Shape *ObjectsPanel::topShapeOf(const QTreeWidgetItem *item) const
{
    return item && kindOf(item) == Kind::Shape ? static_cast<Shape *>(item->data(kNameColumn, kTopShapeRole).value<void *>())
                                               : nullptr;
}

QTreeWidgetItem *ObjectsPanel::addShapeItem(QTreeWidgetItem *parent, Shape *shape, Shape *top)
{
    auto *item = new QTreeWidgetItem(parent);
    item->setText(kNameColumn, describe(*shape));
    item->setIcon(kNameColumn, icon(iconFor(*shape)));
    item->setData(kNameColumn, kKindRole, int(Kind::Shape));
    item->setData(kNameColumn, kPointerRole, pointer(shape));
    item->setData(kNameColumn, kTopShapeRole, pointer(top));
    Qt::ItemFlags flags = Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable;
    if (shape == top)
        flags |= Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled;
    item->setFlags(flags);
    m_shapeItems.insert(shape, item);
    // Topmost first, as they are stacked.
    const auto &children = shape->children();
    for (auto it = children.rbegin(); it != children.rend(); ++it)
        addShapeItem(item, it->get(), top);
    item->setExpanded(m_expanded.contains(shape));
    return item;
}

void ObjectsPanel::refresh()
{
    const bool wasUpdating = m_updating;
    m_updating = true;
    const int scroll = m_tree->verticalScrollBar()->value();
    m_tree->clear();
    m_shapeItems.clear();
    const QColor dimmed = palette().color(QPalette::Disabled, QPalette::Text);

    const Page *current = m_document->currentPage();
    for (const auto &page : m_document->pages()) {
        auto *pageItem = new QTreeWidgetItem(m_tree);
        pageItem->setText(kNameColumn, page->name);
        pageItem->setIcon(kNameColumn, icon(Icon::Page));
        pageItem->setData(kNameColumn, kKindRole, int(Kind::Page));
        pageItem->setData(kNameColumn, kPointerRole, pointer(page.get()));
        pageItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable | Qt::ItemIsDropEnabled);
        const bool isCurrent = page.get() == current;
        if (isCurrent) {
            QFont bold = pageItem->font(kNameColumn);
            bold.setBold(true);
            pageItem->setFont(kNameColumn, bold);
        }

        for (auto it = page->layers.rbegin(); it != page->layers.rend(); ++it) {
            Layer *layer = it->get();
            auto *layerItem = new QTreeWidgetItem(pageItem);
            layerItem->setText(kNameColumn, layer->name);
            layerItem->setIcon(kNameColumn, icon(Icon::Layer));
            layerItem->setData(kNameColumn, kKindRole, int(Kind::Layer));
            layerItem->setData(kNameColumn, kPointerRole, pointer(layer));
            layerItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable | Qt::ItemIsDropEnabled);
            layerItem->setIcon(kVisibleColumn, icon(layer->visible ? Icon::Eye : Icon::EyeOff));
            layerItem->setToolTip(kVisibleColumn, layer->visible ? tr("Visible: click to hide") : tr("Hidden: click to show"));
            layerItem->setIcon(kLockColumn, icon(layer->locked ? Icon::Lock : Icon::Unlock));
            layerItem->setToolTip(kLockColumn, layer->locked ? tr("Locked: click to unlock") : tr("Editable: click to lock"));
            if (layer == page->activeLayer) {
                QFont bold = layerItem->font(kNameColumn);
                bold.setBold(true);
                layerItem->setFont(kNameColumn, bold);
            }
            for (auto s = layer->shapes.rbegin(); s != layer->shapes.rend(); ++s) {
                QTreeWidgetItem *shapeItem = addShapeItem(layerItem, s->get(), s->get());
                if (!layer->isEditable())
                    shapeItem->setForeground(kNameColumn, dimmed);
            }
            layerItem->setExpanded(!m_collapsed.contains(layer));
        }
        pageItem->setExpanded(isCurrent ? !m_collapsed.contains(page.get()) : m_expanded.contains(page.get()));
    }
    m_tree->verticalScrollBar()->setValue(scroll);
    m_tree->setProperty("signature", QVariant::fromValue(signatureOf(*m_document)));
    syncSelectionFromCanvas();
    m_updating = wasUpdating;
    updateButtons();
}

void ObjectsPanel::syncSelectionFromCanvas()
{
    const bool wasUpdating = m_updating;
    m_updating = true;
    m_tree->clearSelection();
    QTreeWidgetItem *first = nullptr;
    for (Shape *shape : m_canvas->selection()) {
        if (QTreeWidgetItem *item = m_shapeItems.value(shape)) {
            item->setSelected(true);
            if (!first)
                first = item;
        }
    }
    if (first) {
        m_tree->setCurrentItem(first, kNameColumn, QItemSelectionModel::NoUpdate);
        m_tree->scrollToItem(first);
    }
    m_updating = wasUpdating;
}

void ObjectsPanel::selectionChangedInTree()
{
    if (m_updating)
        return;
    const QList<QTreeWidgetItem *> items = m_tree->selectedItems();
    std::vector<Shape *> shapes;
    for (QTreeWidgetItem *item : items) {
        Shape *top = topShapeOf(item);
        if (top && std::find(shapes.begin(), shapes.end(), top) == shapes.end())
            shapes.push_back(top);
    }
    if (!shapes.empty()) {
        // Objects on another page: go there first.
        if (Layer *layer = m_document->layerOf(shapes.front())) {
            const Page *page = m_document->pageOf(layer);
            if (page != m_document->currentPage())
                m_document->setCurrentPage(m_document->indexOfPage(page));
        }
        // Objects on hidden or locked layers cannot be selected on the drawing.
        std::vector<Shape *> selectable;
        const std::vector<Shape *> editable = m_document->editableShapes();
        for (Shape *shape : shapes) {
            if (std::find(editable.begin(), editable.end(), shape) != editable.end())
                selectable.push_back(shape);
        }
        m_canvas->setSelection(std::move(selectable));
        return;
    }
    if (items.size() != 1)
        return;
    QTreeWidgetItem *item = items.front();
    if (kindOf(item) == Kind::Layer) {
        auto *layer = static_cast<Layer *>(pointerOf(item));
        if (const Page *page = m_document->pageOf(layer); page != m_document->currentPage())
            m_document->setCurrentPage(m_document->indexOfPage(page));
        m_document->setActiveLayer(layer);
    } else if (kindOf(item) == Kind::Page) {
        m_document->setCurrentPage(m_document->indexOfPage(static_cast<Page *>(pointerOf(item))));
    }
    m_canvas->setSelection({});
}

void ObjectsPanel::onItemClicked(QTreeWidgetItem *item, int column)
{
    if (!item || kindOf(item) != Kind::Layer || column == kNameColumn)
        return;
    auto *layer = static_cast<Layer *>(pointerOf(item));
    if (column == kVisibleColumn) {
        m_undoStack->push(new LayerPropertiesCommand(m_document, layer, layer->name, !layer->visible, layer->locked,
                                                     layer->visible ? tr("Hide layer") : tr("Show layer")));
    } else if (column == kLockColumn) {
        m_undoStack->push(new LayerPropertiesCommand(m_document, layer, layer->name, layer->visible, !layer->locked,
                                                     layer->locked ? tr("Unlock layer") : tr("Lock layer")));
    }
}

void ObjectsPanel::onItemChanged(QTreeWidgetItem *item, int column)
{
    if (m_updating || column != kNameColumn)
        return;
    const QString name = item->text(kNameColumn).trimmed();
    switch (kindOf(item)) {
    case Kind::Page: {
        auto *page = static_cast<Page *>(pointerOf(item));
        if (!name.isEmpty() && name != page->name)
            m_undoStack->push(new RenamePageCommand(m_document, page, name));
        break;
    }
    case Kind::Layer: {
        auto *layer = static_cast<Layer *>(pointerOf(item));
        if (!name.isEmpty() && name != layer->name)
            m_undoStack->push(new LayerPropertiesCommand(m_document, layer, name, layer->visible, layer->locked,
                                                         tr("Rename layer")));
        break;
    }
    case Kind::Shape: {
        auto *shape = static_cast<Shape *>(pointerOf(item));
        if (name != describe(*shape)) // an empty name goes back to describing the object
            m_undoStack->push(new RenameShapeCommand(m_document, shape, name));
        break;
    }
    }
    m_refreshTimer->start();
}

// Dropping objects on an object puts them just above or below it (in its layer); on a layer
// or page, on top of that layer (the page's active layer).
void ObjectsPanel::onDrop(const QList<QTreeWidgetItem *> &items, QTreeWidgetItem *target,
                          ObjectsTree::DropPosition position)
{
    if (!target)
        return;
    std::vector<Shape *> moving;
    for (QTreeWidgetItem *item : items) {
        Shape *top = topShapeOf(item);
        if (top && std::find(moving.begin(), moving.end(), top) == moving.end())
            moving.push_back(top);
    }
    if (moving.empty())
        return;

    Layer *layer = nullptr;
    Shape *anchor = nullptr;
    switch (kindOf(target)) {
    case Kind::Page:
        layer = static_cast<Page *>(pointerOf(target))->activeLayer;
        break;
    case Kind::Layer:
        layer = static_cast<Layer *>(pointerOf(target));
        break;
    case Kind::Shape:
        anchor = topShapeOf(target);
        layer = m_document->layerOf(anchor);
        break;
    }
    if (!layer || std::find(moving.begin(), moving.end(), anchor) != moving.end())
        return;

    // Keep the dragged objects in their current stacking order (bottom to top).
    std::vector<Shape *> ordered;
    for (const auto &page : m_document->pages()) {
        for (const auto &l : page->layers) {
            for (const auto &shape : l->shapes) {
                if (std::find(moving.begin(), moving.end(), shape.get()) != moving.end())
                    ordered.push_back(shape.get());
            }
        }
    }

    m_undoStack->beginMacro(tr("Move objects"));
    for (Shape *shape : ordered) {
        if (m_document->layerOf(shape) != layer)
            m_undoStack->push(new MoveToLayerCommand(m_document, shape, layer));
    }
    std::vector<Shape *> order = m_document->order(layer);
    std::erase_if(order, [&](Shape *s) { return std::find(ordered.begin(), ordered.end(), s) != ordered.end(); });
    auto at = order.end();
    if (anchor) {
        at = std::find(order.begin(), order.end(), anchor);
        // The tree lists the topmost object first: "above" in the tree is later in paint order.
        if (position != ObjectsTree::DropPosition::Below && at != order.end())
            ++at;
    }
    order.insert(at, ordered.begin(), ordered.end());
    if (order != m_document->order(layer))
        m_undoStack->push(new ReorderCommand(m_document, layer, order, tr("Move objects")));
    m_undoStack->endMacro();
    m_refreshTimer->start();
}

void ObjectsPanel::showContextMenu(const QPoint &position)
{
    QTreeWidgetItem *item = m_tree->itemAt(position);
    if (!item)
        return;
    QMenu menu(this);
    menu.addAction(tr("Rename"), this, [this, item] { m_tree->editItem(item, kNameColumn); });
    switch (kindOf(item)) {
    case Kind::Shape:
        menu.addSeparator();
        menu.addAction(tr("Group"), m_canvas, &CanvasWidget::groupSelection)->setEnabled(m_canvas->selection().size() > 1);
        menu.addAction(tr("Ungroup"), m_canvas, &CanvasWidget::ungroupSelection);
        menu.addAction(tr("To Front"), m_canvas, &CanvasWidget::orderToFront);
        menu.addAction(tr("To Back"), m_canvas, &CanvasWidget::orderToBack);
        menu.addSeparator();
        menu.addAction(tr("Delete"), m_canvas, &CanvasWidget::deleteSelection);
        break;
    case Kind::Layer:
        menu.addAction(tr("New Layer"), this,
                       [this] { m_undoStack->push(new AddLayerCommand(m_document, m_document->currentPage())); });
        menu.addAction(tr("Delete Layer"), this, [this, item] {
            auto *layer = static_cast<Layer *>(pointerOf(item));
            if (m_document->pageOf(layer)->layers.size() > 1)
                m_undoStack->push(new RemoveLayerCommand(m_document, layer));
        });
        break;
    case Kind::Page:
        menu.addAction(tr("Delete Page"), this, [this, item] {
            if (m_document->pageCount() > 1)
                m_undoStack->push(new RemovePageCommand(m_document, static_cast<Page *>(pointerOf(item))));
        });
        break;
    }
    menu.exec(m_tree->viewport()->mapToGlobal(position));
}

void ObjectsPanel::deleteSelected()
{
    if (!m_canvas->selection().empty()) {
        m_canvas->deleteSelection();
        return;
    }
    if (m_document->currentPage()->layers.size() > 1)
        m_undoStack->push(new RemoveLayerCommand(m_document, m_document->activeLayer()));
}

void ObjectsPanel::moveUp(bool up)
{
    if (!m_canvas->selection().empty()) {
        if (up)
            m_canvas->orderForwardOne();
        else
            m_canvas->orderBackOne();
        return;
    }
    Layer *layer = m_document->activeLayer();
    const int index = m_document->indexOfLayer(layer) + (up ? 1 : -1);
    if (index >= 0 && index < int(m_document->currentPage()->layers.size()))
        m_undoStack->push(new MoveLayerCommand(m_document, layer, index));
}

void ObjectsPanel::updateButtons()
{
    const bool objects = !m_canvas->selection().empty();
    const Page *page = m_document->currentPage();
    const int count = int(page->layers.size());
    const int active = m_document->indexOfLayer(page->activeLayer);
    m_remove->setEnabled(objects || count > 1);
    m_up->setEnabled(objects || active < count - 1);
    m_down->setEnabled(objects || active > 0);
    m_moveHere->setEnabled(objects);
}

} // namespace opendraw
