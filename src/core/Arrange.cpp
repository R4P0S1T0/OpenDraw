#include "core/Arrange.h"

#include <algorithm>
#include <numeric>

namespace opendraw::arrange {

std::vector<QPointF> alignOffsets(const std::vector<QRectF> &boxes, Alignment alignment, const QRectF &reference)
{
    std::vector<QPointF> offsets;
    offsets.reserve(boxes.size());
    for (const QRectF &box : boxes) {
        switch (alignment) {
        case Alignment::Left:
            offsets.emplace_back(reference.left() - box.left(), 0.0);
            break;
        case Alignment::Right:
            offsets.emplace_back(reference.right() - box.right(), 0.0);
            break;
        case Alignment::Top:
            offsets.emplace_back(0.0, reference.top() - box.top());
            break;
        case Alignment::Bottom:
            offsets.emplace_back(0.0, reference.bottom() - box.bottom());
            break;
        case Alignment::CentersHorizontally:
            offsets.emplace_back(reference.center().x() - box.center().x(), 0.0);
            break;
        case Alignment::CentersVertically:
            offsets.emplace_back(0.0, reference.center().y() - box.center().y());
            break;
        }
    }
    return offsets;
}

std::vector<QPointF> distributeOffsets(const std::vector<QRectF> &boxes, Qt::Orientation orientation)
{
    std::vector<QPointF> offsets(boxes.size());
    if (boxes.size() < 3)
        return offsets;

    const bool horizontal = orientation == Qt::Horizontal;
    auto start = [horizontal](const QRectF &r) { return horizontal ? r.left() : r.top(); };
    auto size = [horizontal](const QRectF &r) { return horizontal ? r.width() : r.height(); };

    std::vector<std::size_t> order(boxes.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(),
                     [&](std::size_t a, std::size_t b) { return start(boxes[a]) < start(boxes[b]); });

    const QRectF &first = boxes[order.front()];
    double spanEnd = start(first) + size(first);
    double totalSize = 0.0;
    for (std::size_t i : order) {
        spanEnd = std::max(spanEnd, start(boxes[i]) + size(boxes[i]));
        totalSize += size(boxes[i]);
    }
    const double gap = (spanEnd - start(first) - totalSize) / double(boxes.size() - 1);

    double position = start(first);
    for (std::size_t i : order) {
        const double delta = position - start(boxes[i]);
        offsets[i] = horizontal ? QPointF(delta, 0.0) : QPointF(0.0, delta);
        position += size(boxes[i]) + gap;
    }
    return offsets;
}

QRectF unite(const std::vector<QRectF> &boxes)
{
    QRectF result;
    for (const QRectF &box : boxes)
        result = result.isNull() ? box : result.united(box);
    return result;
}

} // namespace opendraw::arrange
