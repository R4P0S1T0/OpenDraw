#include "LayersPanel.h"

#include "CanvasWidget.h"
#include "core/Commands.h"
#include "core/Document.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QToolButton>
#include <QTreeWidget>
#include <QUndoStack>
#include <QVBoxLayout>

namespace opendraw {

namespace {
constexpr int kNameColumn = 0; // checkbox = visible
constexpr int kLockColumn = 1; // checkbox = locked
constexpr int kLayerRole = Qt::UserRole;
} // namespace

LayersPanel::LayersPanel(Document *document, QUndoStack *undoStack, CanvasWidget *canvas, QWidget *parent)
    : QWidget(parent)
    , m_document(document)
    , m_undoStack(undoStack)
    , m_canvas(canvas)
{
    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels({tr("Layer (visible)"), tr("Locked")});
    m_tree->setRootIsDecorated(false);
    m_tree->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    m_tree->header()->setSectionResizeMode(kNameColumn, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(kLockColumn, QHeaderView::ResizeToContents);
    m_tree->setToolTip(tr("Double-click a name to rename the layer. The highlighted layer receives new objects."));

    auto makeButton = [this](const QString &text, const QString &tip) {
        auto *button = new QToolButton(this);
        button->setText(text);
        button->setToolTip(tip);
        button->setAutoRaise(true);
        return button;
    };
    m_add = makeButton(QStringLiteral("+"), tr("New layer"));
    m_remove = makeButton(QStringLiteral("−"), tr("Delete layer"));
    m_up = makeButton(QStringLiteral("↑"), tr("Move layer up"));
    m_down = makeButton(QStringLiteral("↓"), tr("Move layer down"));
    m_moveHere = makeButton(tr("Move here"), tr("Move the selected objects to this layer"));

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
    connect(m_remove, &QToolButton::clicked, this, [this] {
        if (m_document->currentPage()->layers.size() > 1)
            m_undoStack->push(new RemoveLayerCommand(m_document, m_document->activeLayer()));
    });
    connect(m_up, &QToolButton::clicked, this, [this] { moveActiveLayer(+1); });
    connect(m_down, &QToolButton::clicked, this, [this] { moveActiveLayer(-1); });
    connect(m_moveHere, &QToolButton::clicked, this, [this] { m_canvas->moveSelectionToLayer(m_document->activeLayer()); });
    connect(m_tree, &QTreeWidget::itemChanged, this, &LayersPanel::onItemChanged);
    connect(m_tree, &QTreeWidget::currentItemChanged, this, &LayersPanel::onCurrentItemChanged);

    // Queued: these signals can fire from inside the tree's own handlers (clicking a layer makes
    // it active), and rebuilding the tree there would delete the item being handled.
    connect(m_document, &Document::structureChanged, this, &LayersPanel::refresh, Qt::QueuedConnection);
    connect(m_document, &Document::currentPageChanged, this, &LayersPanel::refresh, Qt::QueuedConnection);
    connect(m_canvas, &CanvasWidget::selectionChanged, this, &LayersPanel::refresh, Qt::QueuedConnection);
    refresh();
}

Layer *LayersPanel::layerFor(const QTreeWidgetItem *item) const
{
    return item ? static_cast<Layer *>(item->data(kNameColumn, kLayerRole).value<void *>()) : nullptr;
}

void LayersPanel::refresh()
{
    m_updating = true;
    m_tree->clear();
    const Page *page = m_document->currentPage();
    QTreeWidgetItem *activeItem = nullptr;
    // Topmost layer first, as it is stacked on the page.
    for (auto it = page->layers.rbegin(); it != page->layers.rend(); ++it) {
        Layer *layer = it->get();
        auto *item = new QTreeWidgetItem(m_tree);
        item->setText(kNameColumn, layer->name);
        item->setData(kNameColumn, kLayerRole, QVariant::fromValue(static_cast<void *>(layer)));
        item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable | Qt::ItemIsUserCheckable);
        item->setCheckState(kNameColumn, layer->visible ? Qt::Checked : Qt::Unchecked);
        item->setCheckState(kLockColumn, layer->locked ? Qt::Checked : Qt::Unchecked);
        if (layer == page->activeLayer) {
            QFont bold = item->font(kNameColumn);
            bold.setBold(true);
            item->setFont(kNameColumn, bold);
            activeItem = item;
        }
    }
    m_tree->setCurrentItem(activeItem);

    const int count = int(page->layers.size());
    const int active = m_document->indexOfLayer(page->activeLayer);
    m_remove->setEnabled(count > 1);
    m_up->setEnabled(active < count - 1);
    m_down->setEnabled(active > 0);
    m_moveHere->setEnabled(!m_canvas->selection().empty());
    m_updating = false;
}

void LayersPanel::onItemChanged(QTreeWidgetItem *item, int)
{
    if (m_updating)
        return;
    Layer *layer = layerFor(item);
    if (!layer)
        return;
    const QString name = item->text(kNameColumn).trimmed().isEmpty() ? layer->name : item->text(kNameColumn).trimmed();
    const bool visible = item->checkState(kNameColumn) == Qt::Checked;
    const bool locked = item->checkState(kLockColumn) == Qt::Checked;
    QString text = tr("Rename layer");
    if (visible != layer->visible)
        text = visible ? tr("Show layer") : tr("Hide layer");
    else if (locked != layer->locked)
        text = locked ? tr("Lock layer") : tr("Unlock layer");
    m_undoStack->push(new LayerPropertiesCommand(m_document, layer, name, visible, locked, text));
}

void LayersPanel::onCurrentItemChanged(QTreeWidgetItem *current)
{
    if (m_updating)
        return;
    if (Layer *layer = layerFor(current))
        m_document->setActiveLayer(layer);
}

void LayersPanel::moveActiveLayer(int step)
{
    Layer *layer = m_document->activeLayer();
    const int index = m_document->indexOfLayer(layer) + step;
    if (index >= 0 && index < int(m_document->currentPage()->layers.size()))
        m_undoStack->push(new MoveLayerCommand(m_document, layer, index));
}

} // namespace opendraw
