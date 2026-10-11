#pragma once

#include "core/Shape.h"

#include <QObject>
#include <QSizeF>
#include <QString>

#include <memory>
#include <vector>

namespace opendraw {

// Document units are SVG user units: 96 per inch.
inline constexpr double kUnitsPerMm = 96.0 / 25.4;

// Shapes in paint order: the last one is drawn on top.
using ShapeList = std::vector<std::unique_ptr<Shape>>;

struct Layer {
    QString name;
    bool visible = true;
    bool locked = false;
    ShapeList shapes;

    // Shapes of a visible, unlocked layer can be selected and edited.
    bool isEditable() const { return visible && !locked; }
};

struct Page {
    QString name;
    std::vector<std::unique_ptr<Layer>> layers; // bottom to top
    Layer *activeLayer = nullptr;               // where new shapes go
};

// Pages, each with layers of shapes. All pages share one page size.
// Shape operations without an explicit layer act on the current page's active layer.
class Document : public QObject {
    Q_OBJECT

public:
    explicit Document(QObject *parent = nullptr);
    ~Document() override;

    // A4 portrait.
    static QSizeF defaultPageSize() { return {210.0 * kUnitsPerMm, 297.0 * kUnitsPerMm}; }
    static std::unique_ptr<Page> makePage(const QString &name);
    static std::unique_ptr<Layer> makeLayer(const QString &name);

    QSizeF pageSize() const { return m_pageSize; }
    void setPageSize(const QSizeF &size);

    // --- Pages
    const std::vector<std::unique_ptr<Page>> &pages() const { return m_pages; }
    int pageCount() const { return int(m_pages.size()); }
    int currentPageIndex() const { return m_currentPage; }
    Page *currentPage() const { return m_pages[m_currentPage].get(); }
    void setCurrentPage(int index);
    int indexOfPage(const Page *page) const;
    Page *insertPage(int index, std::unique_ptr<Page> page);
    std::unique_ptr<Page> takePage(Page *page); // never takes the last page
    void renamePage(Page *page, const QString &name);
    // Replaces every page (used when loading a file).
    void setPages(std::vector<std::unique_ptr<Page>> pages);
    // Hands over every page, leaving one empty page behind (used to merge documents).
    std::vector<std::unique_ptr<Page>> releasePages();

    // --- Layers
    Layer *activeLayer() const { return currentPage()->activeLayer; }
    void setActiveLayer(Layer *layer);
    Page *pageOf(const Layer *layer) const;
    int indexOfLayer(const Layer *layer) const; // within its page
    Layer *insertLayer(Page *page, int index, std::unique_ptr<Layer> layer);
    std::unique_ptr<Layer> takeLayer(Layer *layer); // never takes a page's last layer
    void moveLayer(Layer *layer, int index);
    void setLayerProperties(Layer *layer, const QString &name, bool visible, bool locked);
    // A name like "Layer 3" that is not used on the page yet.
    QString newLayerName(const Page *page) const;
    QString newPageName() const;

    // --- Shapes
    const ShapeList &shapes() const { return activeLayer()->shapes; } // active layer
    int count() const { return int(shapes().size()); }                 // active layer
    Layer *layerOf(const Shape *shape) const;                          // searches every page
    int indexOf(const Shape *shape) const;                             // within its layer

    // Inserts at `index` (appends when out of range) into `layer`, or the active layer.
    Shape *insertShape(int index, std::unique_ptr<Shape> shape);
    Shape *insertShape(Layer *layer, int index, std::unique_ptr<Shape> shape);
    // Removes the shape from its layer and hands ownership back to the caller.
    std::unique_ptr<Shape> takeShape(Shape *shape);

    // Paint order of a layer (the active layer by default), bottom to top.
    std::vector<Shape *> order(const Layer *layer = nullptr) const;
    // Rearranges a layer's shapes into `order`, which must hold exactly that layer's shapes.
    void setOrder(Layer *layer, const std::vector<Shape *> &order);
    void setOrder(const std::vector<Shape *> &order) { setOrder(activeLayer(), order); }

    // Shapes on the current page that can be selected: in visible, unlocked layers, bottom to top.
    std::vector<Shape *> editableShapes() const;
    // Topmost selectable shape under `point` on the current page, or nullptr.
    Shape *shapeAt(const QPointF &point, double tolerance) const;

    // One empty page with one layer.
    void clear();

    // Call after changing a shape in place so views repaint.
    void notifyShapeChanged(Shape *shape);

signals:
    void changed();
    void shapeRemoved(opendraw::Shape *shape);
    void cleared();
    void currentPageChanged();
    // Pages or layers were added, removed, reordered, renamed, hidden or locked.
    void structureChanged();

private:
    QSizeF m_pageSize;
    std::vector<std::unique_ptr<Page>> m_pages;
    int m_currentPage = 0;
};

} // namespace opendraw
