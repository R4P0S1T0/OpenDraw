#pragma once

#include <QColor>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>

#include <vector>

namespace opendraw {

struct Style {
    QColor fill = QColor(0x4a, 0x90, 0xd9);
    QColor stroke = QColor(0x1d, 0x2b, 0x3a);
    double strokeWidth = 1.0;

    bool hasFill() const { return fill.alpha() > 0; }
    bool hasStroke() const { return stroke.alpha() > 0 && strokeWidth > 0.0; }
};

// A smooth node keeps its two handles on one line; a corner node lets them point anywhere.
enum class NodeType { Corner, Smooth };

// One point of a path. `in` and `out` are absolute control points of the curve arriving at
// and leaving the node; a handle equal to `point` means that side of the node is straight.
struct PathNode {
    QPointF point;
    QPointF in;
    QPointF out;
    NodeType type = NodeType::Corner;

    static PathNode corner(const QPointF &p) { return {p, p, p, NodeType::Corner}; }
    bool operator==(const PathNode &) const = default;
};

// Consecutive nodes joined by cubic Bézier segments. A closed subpath also joins the last
// node back to the first.
struct SubPath {
    std::vector<PathNode> nodes;
    bool closed = false;

    int segmentCount() const
    {
        const int n = static_cast<int>(nodes.size());
        return n < 2 ? 0 : (closed ? n : n - 1);
    }
    bool operator==(const SubPath &) const = default;
};

using PathData = std::vector<SubPath>;

enum class ShapeKind { Rectangle, Ellipse, Path };

// A drawable object. Geometry is stored in document units (SVG user units, 96 per inch).
class Shape {
public:
    // Rectangle or ellipse filling `bounds`.
    Shape(ShapeKind kind, const QRectF &bounds, const Style &style = {});
    // Free-form path.
    explicit Shape(PathData path, const Style &style = {});

    quint64 id() const { return m_id; }
    ShapeKind kind() const { return m_kind; }

    QRectF bounds() const;
    // Rectangles and ellipses take the new bounds; paths are scaled to fit them.
    void setBounds(const QRectF &bounds);
    void moveBy(const QPointF &delta);

    const Style &style() const { return m_style; }
    void setStyle(const Style &style) { m_style = style; }

    // Only meaningful for ShapeKind::Path.
    const PathData &pathData() const { return m_path; }
    void setPathData(PathData path) { m_path = std::move(path); }

    QPainterPath path() const;

    // Hit test: inside the fill, or within `tolerance` of the outline.
    bool contains(const QPointF &point, double tolerance) const;

private:
    quint64 m_id;
    ShapeKind m_kind;
    QRectF m_bounds; // rectangles and ellipses
    PathData m_path; // paths
    Style m_style;
};

} // namespace opendraw
