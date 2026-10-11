#include "core/Document.h"

#include <QCoreApplication>

#include <algorithm>

namespace opendraw {

namespace {

template<typename T>
int indexIn(const std::vector<std::unique_ptr<T>> &items, const T *item)
{
    const auto it = std::find_if(items.begin(), items.end(), [item](const auto &p) { return p.get() == item; });
    return it == items.end() ? -1 : int(it - items.begin());
}

} // namespace

Document::Document(QObject *parent)
    : QObject(parent)
    , m_pageSize(defaultPageSize())
{
    m_pages.push_back(makePage(QCoreApplication::translate("Document", "Page 1")));
}

Document::~Document() = default;

std::unique_ptr<Page> Document::makePage(const QString &name)
{
    auto page = std::make_unique<Page>();
    page->name = name;
    page->layers.push_back(makeLayer(QCoreApplication::translate("Document", "Layer 1")));
    page->activeLayer = page->layers.front().get();
    return page;
}

std::unique_ptr<Layer> Document::makeLayer(const QString &name)
{
    auto layer = std::make_unique<Layer>();
    layer->name = name;
    return layer;
}

void Document::setPageSize(const QSizeF &size)
{
    if (size == m_pageSize || size.isEmpty())
        return;
    m_pageSize = size;
    emit changed();
}

// ---------------------------------------------------------------------------------------------
// Pages

void Document::setCurrentPage(int index)
{
    index = std::clamp(index, 0, pageCount() - 1);
    if (index == m_currentPage)
        return;
    m_currentPage = index;
    emit currentPageChanged();
    emit changed();
}

int Document::indexOfPage(const Page *page) const
{
    return indexIn(m_pages, page);
}

Page *Document::insertPage(int index, std::unique_ptr<Page> page)
{
    Page *raw = page.get();
    Page *current = currentPage();
    if (index < 0 || index > pageCount())
        index = pageCount();
    m_pages.insert(m_pages.begin() + index, std::move(page));
    m_currentPage = indexOfPage(current);
    emit structureChanged();
    emit changed();
    return raw;
}

std::unique_ptr<Page> Document::takePage(Page *page)
{
    const int index = indexOfPage(page);
    if (index < 0 || pageCount() == 1)
        return nullptr;
    Page *current = currentPage();
    std::unique_ptr<Page> taken = std::move(m_pages[index]);
    m_pages.erase(m_pages.begin() + index);
    const bool currentRemoved = current == page;
    m_currentPage = currentRemoved ? std::min(index, pageCount() - 1) : indexOfPage(current);
    if (currentRemoved)
        emit currentPageChanged();
    emit structureChanged();
    emit changed();
    return taken;
}

void Document::renamePage(Page *page, const QString &name)
{
    if (page->name == name)
        return;
    page->name = name;
    emit structureChanged();
}

void Document::setPages(std::vector<std::unique_ptr<Page>> pages)
{
    if (pages.empty())
        pages.push_back(makePage(QCoreApplication::translate("Document", "Page 1")));
    for (auto &page : pages) {
        if (page->layers.empty())
            page->layers.push_back(makeLayer(QCoreApplication::translate("Document", "Layer 1")));
        if (!page->activeLayer || indexIn(page->layers, page->activeLayer) < 0)
            page->activeLayer = page->layers.back().get();
    }
    emit cleared();
    m_pages = std::move(pages);
    m_currentPage = 0;
    emit currentPageChanged();
    emit structureChanged();
    emit changed();
}

std::vector<std::unique_ptr<Page>> Document::releasePages()
{
    std::vector<std::unique_ptr<Page>> released;
    released.push_back(makePage(QCoreApplication::translate("Document", "Page 1")));
    released.swap(m_pages);
    m_currentPage = 0;
    emit cleared();
    emit currentPageChanged();
    emit structureChanged();
    emit changed();
    return released;
}

QString Document::newPageName() const
{
    for (int n = pageCount() + 1;; ++n) {
        const QString name = QCoreApplication::translate("Document", "Page %1").arg(n);
        if (std::none_of(m_pages.begin(), m_pages.end(), [&](const auto &p) { return p->name == name; }))
            return name;
    }
}

// ---------------------------------------------------------------------------------------------
// Layers

void Document::setActiveLayer(Layer *layer)
{
    Page *page = pageOf(layer);
    if (!page || page->activeLayer == layer)
        return;
    page->activeLayer = layer;
    emit structureChanged();
}

Page *Document::pageOf(const Layer *layer) const
{
    for (const auto &page : m_pages) {
        if (indexIn(page->layers, layer) >= 0)
            return page.get();
    }
    return nullptr;
}

int Document::indexOfLayer(const Layer *layer) const
{
    const Page *page = pageOf(layer);
    return page ? indexIn(page->layers, layer) : -1;
}

Layer *Document::insertLayer(Page *page, int index, std::unique_ptr<Layer> layer)
{
    Layer *raw = layer.get();
    if (index < 0 || index > int(page->layers.size()))
        index = int(page->layers.size());
    page->layers.insert(page->layers.begin() + index, std::move(layer));
    emit structureChanged();
    emit changed();
    return raw;
}

std::unique_ptr<Layer> Document::takeLayer(Layer *layer)
{
    Page *page = pageOf(layer);
    if (!page || page->layers.size() == 1)
        return nullptr;
    const int index = indexIn(page->layers, layer);
    std::unique_ptr<Layer> taken = std::move(page->layers[index]);
    page->layers.erase(page->layers.begin() + index);
    if (page->activeLayer == layer)
        page->activeLayer = page->layers[std::min(index, int(page->layers.size()) - 1)].get();
    for (const auto &shape : taken->shapes)
        emit shapeRemoved(shape.get());
    emit structureChanged();
    emit changed();
    return taken;
}

void Document::moveLayer(Layer *layer, int index)
{
    Page *page = pageOf(layer);
    if (!page)
        return;
    const int from = indexIn(page->layers, layer);
    index = std::clamp(index, 0, int(page->layers.size()) - 1);
    if (from == index)
        return;
    std::unique_ptr<Layer> moving = std::move(page->layers[from]);
    page->layers.erase(page->layers.begin() + from);
    page->layers.insert(page->layers.begin() + index, std::move(moving));
    emit structureChanged();
    emit changed();
}

void Document::setLayerProperties(Layer *layer, const QString &name, bool visible, bool locked)
{
    if (layer->name == name && layer->visible == visible && layer->locked == locked)
        return;
    layer->name = name;
    layer->visible = visible;
    layer->locked = locked;
    emit structureChanged();
    emit changed();
}

QString Document::newLayerName(const Page *page) const
{
    for (int n = int(page->layers.size()) + 1;; ++n) {
        const QString name = QCoreApplication::translate("Document", "Layer %1").arg(n);
        if (std::none_of(page->layers.begin(), page->layers.end(), [&](const auto &l) { return l->name == name; }))
            return name;
    }
}

// ---------------------------------------------------------------------------------------------
// Shapes

Layer *Document::layerOf(const Shape *shape) const
{
    for (const auto &page : m_pages) {
        for (const auto &layer : page->layers) {
            if (indexIn(layer->shapes, shape) >= 0)
                return layer.get();
        }
    }
    return nullptr;
}

int Document::indexOf(const Shape *shape) const
{
    const Layer *layer = layerOf(shape);
    return layer ? indexIn(layer->shapes, shape) : -1;
}

Shape *Document::insertShape(int index, std::unique_ptr<Shape> shape)
{
    return insertShape(activeLayer(), index, std::move(shape));
}

Shape *Document::insertShape(Layer *layer, int index, std::unique_ptr<Shape> shape)
{
    Shape *raw = shape.get();
    ShapeList &shapes = layer->shapes;
    if (index < 0 || index > int(shapes.size()))
        index = int(shapes.size());
    shapes.insert(shapes.begin() + index, std::move(shape));
    emit changed();
    return raw;
}

std::unique_ptr<Shape> Document::takeShape(Shape *shape)
{
    Layer *layer = layerOf(shape);
    if (!layer)
        return nullptr;
    const int index = indexIn(layer->shapes, shape);
    std::unique_ptr<Shape> taken = std::move(layer->shapes[index]);
    layer->shapes.erase(layer->shapes.begin() + index);
    emit shapeRemoved(shape);
    emit changed();
    return taken;
}

std::vector<Shape *> Document::order(const Layer *layer) const
{
    if (!layer)
        layer = activeLayer();
    std::vector<Shape *> result;
    result.reserve(layer->shapes.size());
    for (const auto &shape : layer->shapes)
        result.push_back(shape.get());
    return result;
}

void Document::setOrder(Layer *layer, const std::vector<Shape *> &order)
{
    Q_ASSERT(order.size() == layer->shapes.size());
    ShapeList reordered;
    reordered.reserve(layer->shapes.size());
    for (Shape *shape : order) {
        const int index = indexIn(layer->shapes, shape);
        Q_ASSERT(index >= 0);
        reordered.push_back(std::move(layer->shapes[index]));
    }
    layer->shapes = std::move(reordered);
    emit changed();
}

std::vector<Shape *> Document::editableShapes() const
{
    std::vector<Shape *> result;
    for (const auto &layer : currentPage()->layers) {
        if (!layer->isEditable())
            continue;
        for (const auto &shape : layer->shapes)
            result.push_back(shape.get());
    }
    return result;
}

Shape *Document::shapeAt(const QPointF &point, double tolerance) const
{
    const auto &layers = currentPage()->layers;
    for (auto layer = layers.rbegin(); layer != layers.rend(); ++layer) {
        if (!(*layer)->isEditable())
            continue;
        for (auto it = (*layer)->shapes.rbegin(); it != (*layer)->shapes.rend(); ++it) {
            if ((*it)->contains(point, tolerance))
                return it->get();
        }
    }
    return nullptr;
}

void Document::clear()
{
    std::vector<std::unique_ptr<Page>> pages;
    pages.push_back(makePage(QCoreApplication::translate("Document", "Page 1")));
    m_pageSize = defaultPageSize();
    setPages(std::move(pages));
}

void Document::notifyShapeChanged(Shape *shape)
{
    Q_UNUSED(shape);
    emit changed();
}

} // namespace opendraw
