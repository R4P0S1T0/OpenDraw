#include "core/Document.h"

#include <algorithm>

namespace opendraw {

Document::Document(QObject *parent)
    : QObject(parent)
    , m_pageSize(defaultPageSize())
{
}

Document::~Document() = default;

void Document::setPageSize(const QSizeF &size)
{
    if (size == m_pageSize || size.isEmpty())
        return;
    m_pageSize = size;
    emit changed();
}

int Document::indexOf(const Shape *shape) const
{
    const auto it = std::find_if(m_shapes.begin(), m_shapes.end(),
                                 [shape](const auto &s) { return s.get() == shape; });
    return it == m_shapes.end() ? -1 : static_cast<int>(it - m_shapes.begin());
}

Shape *Document::insertShape(int index, std::unique_ptr<Shape> shape)
{
    Shape *raw = shape.get();
    if (index < 0 || index > count())
        index = count();
    m_shapes.insert(m_shapes.begin() + index, std::move(shape));
    emit changed();
    return raw;
}

std::unique_ptr<Shape> Document::takeShape(Shape *shape)
{
    const int index = indexOf(shape);
    if (index < 0)
        return nullptr;
    std::unique_ptr<Shape> taken = std::move(m_shapes[index]);
    m_shapes.erase(m_shapes.begin() + index);
    emit shapeRemoved(shape);
    emit changed();
    return taken;
}

Shape *Document::shapeAt(const QPointF &point, double tolerance) const
{
    for (auto it = m_shapes.rbegin(); it != m_shapes.rend(); ++it) {
        if ((*it)->contains(point, tolerance))
            return it->get();
    }
    return nullptr;
}

void Document::clear()
{
    emit cleared();
    m_shapes.clear();
    m_pageSize = defaultPageSize();
    emit changed();
}

void Document::notifyShapeChanged(Shape *shape)
{
    Q_UNUSED(shape);
    emit changed();
}

} // namespace opendraw
