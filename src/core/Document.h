#pragma once

#include "core/Shape.h"

#include <QObject>
#include <QSizeF>

#include <memory>
#include <vector>

namespace opendraw {

// Document units are SVG user units: 96 per inch.
inline constexpr double kUnitsPerMm = 96.0 / 25.4;

class Document : public QObject {
    Q_OBJECT

public:
    explicit Document(QObject *parent = nullptr);
    ~Document() override;

    // A4 portrait.
    static QSizeF defaultPageSize() { return {210.0 * kUnitsPerMm, 297.0 * kUnitsPerMm}; }

    QSizeF pageSize() const { return m_pageSize; }
    void setPageSize(const QSizeF &size);

    // Shapes in paint order: the last one is drawn on top.
    const std::vector<std::unique_ptr<Shape>> &shapes() const { return m_shapes; }
    int count() const { return static_cast<int>(m_shapes.size()); }
    int indexOf(const Shape *shape) const;

    // Inserts at `index`, or appends when `index` is out of range. Returns the inserted shape.
    Shape *insertShape(int index, std::unique_ptr<Shape> shape);
    // Removes the shape and hands ownership back to the caller.
    std::unique_ptr<Shape> takeShape(Shape *shape);

    // Topmost shape under `point`, or nullptr.
    Shape *shapeAt(const QPointF &point, double tolerance) const;

    void clear();

    // Call after changing a shape in place so views repaint.
    void notifyShapeChanged(Shape *shape);

signals:
    void changed();
    void shapeRemoved(opendraw::Shape *shape);
    void cleared();

private:
    QSizeF m_pageSize;
    std::vector<std::unique_ptr<Shape>> m_shapes;
};

} // namespace opendraw
