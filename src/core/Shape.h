#pragma once

#include <QColor>
#include <QImage>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QTransform>

#include <algorithm>
#include <memory>
#include <optional>
#include <vector>

namespace opendraw {

struct GradientStop {
    double offset = 0.0; // 0..1 along the gradient
    QColor color;

    bool operator==(const GradientStop &) const = default;
};

// Gradient fill. Positions are fractions of the object's bounding box (0,0 top-left,
// 1,1 bottom-right), so the gradient moves and scales with the object.
struct Gradient {
    enum class Type { Linear, Radial };

    Type type = Type::Linear;
    std::vector<GradientStop> stops;
    QPointF start{0.0, 0.5}; // linear
    QPointF end{1.0, 0.5};   // linear
    QPointF center{0.5, 0.5}; // radial
    double radius = 0.5;      // radial

    // Two-colour gradients across the box; 0 degrees runs left to right, 90 top to bottom.
    static Gradient linear(const QColor &from, const QColor &to, double angleDegrees);
    static Gradient radial(const QColor &inner, const QColor &outer);
    // Direction of a linear gradient in degrees.
    double angle() const;

    bool operator==(const Gradient &) const = default;
};

// A bitmap repeated across the shape (bitmap or texture fill). `origin` is where a tile
// starts, relative to the top-left of the shape's bounding box, so the texture moves with it.
struct PatternFill {
    QImage image;
    QSizeF tileSize; // document units
    QPointF origin;

    bool operator==(const PatternFill &other) const
    {
        return image == other.image && tileSize == other.tileSize && origin == other.origin;
    }
};

struct Style {
    QColor fill = QColor(0x4a, 0x90, 0xd9);
    QColor stroke = QColor(0x1d, 0x2b, 0x3a);
    double strokeWidth = 1.0;
    std::optional<Gradient> gradient; // when set, used instead of `fill`
    double opacity = 1.0;             // of the whole object, fill and outline together
    std::optional<PatternFill> pattern; // when set, used instead of `fill` and `gradient`

    bool hasFill() const { return pattern.has_value() || gradient.has_value() || fill.alpha() > 0; }
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

enum class TextAlign { Left, Center, Right };

struct TextFormat {
    QString family = QStringLiteral("Arial");
    double size = 32.0; // document units (px); 24 pt
    bool bold = false;
    bool italic = false;
    TextAlign align = TextAlign::Left;
    double lineSpacing = 1.2; // multiple of the font size

    bool operator==(const TextFormat &) const = default;
};

// Content of a text object.
struct TextData {
    QString text; // lines separated by '\n'
    TextFormat format;

    bool operator==(const TextData &) const = default;
};

enum class ShapeKind { Rectangle, Ellipse, Path, Group, Text, Image };

// A drawable object. Geometry is stored in document units (SVG user units, 96 per inch).
class Shape {
public:
    // Rectangle or ellipse filling `bounds`.
    Shape(ShapeKind kind, const QRectF &bounds, const Style &style = {});
    // Free-form path.
    explicit Shape(PathData path, const Style &style = {});
    // Group of shapes, in paint order.
    explicit Shape(std::vector<std::unique_ptr<Shape>> children);
    // Text whose first line sits on a baseline through `origin`; `origin.x` is the left edge,
    // centre or right edge depending on the alignment.
    Shape(TextData text, const QPointF &origin, const Style &style);
    // Bitmap. `placement` maps the unit square (0,0)-(1,1) onto the page, so it can hold
    // position, size, rotation and skew.
    Shape(QImage image, const QTransform &placement);

    Shape(const Shape &) = delete;
    Shape &operator=(const Shape &) = delete;
    ~Shape();

    quint64 id() const { return m_id; }
    ShapeKind kind() const { return m_kind; }

    QRectF bounds() const;
    // Rectangles and ellipses take the new bounds; paths and groups are scaled to fit them.
    void setBounds(const QRectF &bounds);
    void moveBy(const QPointF &delta);
    // Moves and scales the geometry. Only translation and scaling are supported for now.
    void transform(const QTransform &t);

    // A group has no style of its own; see forEachLeaf.
    const Style &style() const { return m_style; }
    void setStyle(const Style &style) { m_style = style; }

    // Only meaningful for ShapeKind::Rectangle: radius of the rounded corners (0 = square),
    // limited to half the shorter side when drawn.
    double cornerRadius() const { return m_cornerRadius; }
    void setCornerRadius(double radius) { m_cornerRadius = std::max(radius, 0.0); }

    // Only meaningful for ShapeKind::Path.
    const PathData &pathData() const { return m_path; }
    void setPathData(PathData path) { m_path = std::move(path); }

    // Only meaningful for ShapeKind::Text.
    const TextData &textData() const { return m_text; }
    void setTextData(TextData text) { m_text = std::move(text); }
    QPointF textOrigin() const { return m_textOrigin; }

    // Only meaningful for ShapeKind::Image.
    const QImage &image() const { return m_image; }
    QTransform imagePlacement() const { return m_imagePlacement; }

    // Only meaningful for ShapeKind::Group.
    const std::vector<std::unique_ptr<Shape>> &children() const { return m_children; }
    std::vector<std::unique_ptr<Shape>> takeChildren() { return std::move(m_children); }
    void setChildren(std::vector<std::unique_ptr<Shape>> children) { m_children = std::move(children); }

    // Outline of a single shape; for a group, the union of its children's outlines.
    QPainterPath path() const;

    // Hit test: inside the fill, or within `tolerance` of the outline (any child for a group).
    bool contains(const QPointF &point, double tolerance) const;

    // Calls `f` for this shape, or for every non-group shape inside it.
    template<typename F>
    void forEachLeaf(F &&f)
    {
        if (m_kind != ShapeKind::Group) {
            f(*this);
            return;
        }
        for (auto &child : m_children)
            child->forEachLeaf(f);
    }

private:
    quint64 m_id;
    ShapeKind m_kind;
    QRectF m_bounds;                              // rectangles and ellipses
    double m_cornerRadius = 0.0;                  // rectangles
    PathData m_path;                              // paths
    std::vector<std::unique_ptr<Shape>> m_children; // groups
    TextData m_text;                                // text
    QPointF m_textOrigin;                           // text
    QImage m_image;                                 // image
    QTransform m_imagePlacement;                    // image
    Style m_style;
};

} // namespace opendraw
