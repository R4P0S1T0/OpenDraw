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

// A soft shadow behind an object: its silhouette, blurred, coloured and moved by `offset`.
struct DropShadow {
    QPointF offset{8.0, 8.0}; // document units
    double blur = 10.0;       // document units (about twice the Gaussian deviation)
    QColor color = QColor(0, 0, 0, 128); // alpha: how dark the shadow is
    bool operator==(const DropShadow &) const = default;
};

// How the ends of open outlines and dashes look (SVG stroke-linecap).
enum class LineCap { Butt, Round, Square };
// How outlines turn at corners (SVG stroke-linejoin).
enum class LineJoin { Miter, Round, Bevel };
// Shape drawn at an end of an open outline, in the outline's colour and sized by its width.
enum class Arrowhead { None, Triangle, Stealth, Open, Circle, Square, Diamond, Bar };

struct Style {
    QColor fill = QColor(0x4a, 0x90, 0xd9);
    QColor stroke = QColor(0x1d, 0x2b, 0x3a);
    double strokeWidth = 1.0;
    std::optional<Gradient> gradient; // when set, used instead of `fill`
    double opacity = 1.0;             // of the whole object, fill and outline together
    std::optional<PatternFill> pattern; // when set, used instead of `fill` and `gradient`
    // Dash and gap lengths, alternating, in multiples of the outline width; empty = solid.
    std::vector<double> dashes;
    LineCap cap = LineCap::Butt;
    LineJoin join = LineJoin::Miter;
    // At the first node of a path (if its first subpath is open) and at its last node (if the
    // last subpath is open), as SVG markers are.
    Arrowhead startArrow = Arrowhead::None;
    Arrowhead endArrow = Arrowhead::None;
    // The outline is drawn under the fill, which hides its inner half (SVG paint-order).
    bool strokeBehind = false;
    // Resizing the object resizes its outline width too.
    bool scaleStroke = false;
    std::optional<DropShadow> shadow;
    // Transparency that changes across the object (as the fill's gradient does): the alpha of
    // each stop's colour is how solid the object is there (255 = solid, 0 = invisible).
    std::optional<Gradient> transparency;

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

// Justify stretches the spaces of wrapped lines of paragraph text to fill the frame (the last
// line of each paragraph stays left-aligned); artistic text treats it as Left.
enum class TextAlign { Left, Center, Right, Justify };

struct TextFormat {
    QString family = QStringLiteral("Arial");
    double size = 32.0; // document units (px); 24 pt
    bool bold = false;
    bool italic = false;
    // The rest applies to the whole text.
    TextAlign align = TextAlign::Left;
    double lineSpacing = 1.2; // multiple of the font size
    double spaceBefore = 0.0; // extra space above each paragraph but the first, multiple of the font size
    double spaceAfter = 0.0;  // extra space below each paragraph but the last, multiple of the font size
    // Paragraph text only, in document units from the frame's edges: the first line of each
    // paragraph starts at firstIndent, the others at leftIndent.
    double firstIndent = 0.0;
    double leftIndent = 0.0;
    double rightIndent = 0.0;

    // Copies the settings that apply to the whole text (alignment, spacing, indents).
    void setParagraphFormat(const TextFormat &other)
    {
        align = other.align;
        lineSpacing = other.lineSpacing;
        spaceBefore = other.spaceBefore;
        spaceAfter = other.spaceAfter;
        firstIndent = other.firstIndent;
        leftIndent = other.leftIndent;
        rightIndent = other.rightIndent;
    }
    bool sameParagraphFormat(const TextFormat &other) const
    {
        return align == other.align && lineSpacing == other.lineSpacing && spaceBefore == other.spaceBefore
               && spaceAfter == other.spaceAfter && firstIndent == other.firstIndent && leftIndent == other.leftIndent
               && rightIndent == other.rightIndent;
    }

    bool operator==(const TextFormat &) const = default;
};

// Formatting of part of a text. Fields left unset come from the text's TextFormat (and the
// colour from its fill).
struct CharFormat {
    std::optional<QString> family;
    std::optional<double> size;
    std::optional<bool> bold;
    std::optional<bool> italic;
    std::optional<QColor> color;

    bool isEmpty() const { return !family && !size && !bold && !italic && !color; }
    bool operator==(const CharFormat &) const = default;
};

// A stretch of characters [start, start + length) with its own formatting.
struct TextRun {
    int start = 0;
    int length = 0;
    CharFormat format;
    bool operator==(const TextRun &) const = default;
};

// Content of a text object.
struct TextData {
    QString text; // paragraphs (artistic text: lines) separated by '\n'
    TextFormat format;
    // Paragraph text flows inside a frame this size, wrapping at its width; lines that do not
    // fit its height are hidden. Empty for artistic text, which only breaks at '\n'.
    QSizeF frame;
    // Formatting of parts of the text, sorted and not overlapping (see core/RichText.h).
    std::vector<TextRun> runs;

    bool isParagraph() const { return frame.width() > 0.0; }
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

    // Name given by the user in the Objects docker (empty: described by its kind).
    const QString &name() const { return m_name; }
    void setName(const QString &name) { m_name = name; }

    // Bounding box on the page.
    QRectF bounds() const;
    // Rectangles and ellipses take the new bounds; paths and groups are scaled to fit them.
    void setBounds(const QRectF &bounds);
    void moveBy(const QPointF &delta);
    // Moves, scales, rotates or skews the geometry.
    void transform(const QTransform &t);

    // Rectangles, ellipses and texts keep their own geometry (box, text origin and size) and
    // carry rotation and skew here, mapping that geometry onto the page, so they stay
    // editable as what they are. Identity unless the shape was rotated or skewed.
    QTransform shapeTransform() const { return m_shapeTransform; }
    void setShapeTransform(const QTransform &t) { m_shapeTransform = t; }
    // Geometry before shapeTransform(): the outline and its bounding box.
    QPainterPath localPath() const;
    QRectF localBounds() const;
    // Rotation of the shape in degrees (clockwise on screen), as far as it has one: taken from
    // shapeTransform() for rectangles, ellipses and texts, from the placement for bitmaps, 0 otherwise.
    double rotation() const;

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
    // A group of two objects with blend steps > 0 is a blend: that many shapes in between
    // change step by step from the first child into the second (see core/Blend.h).
    int blendSteps() const { return m_blendSteps; }
    void setBlendSteps(int steps) { m_blendSteps = std::max(steps, 0); }
    bool isBlend() const { return m_kind == ShapeKind::Group && m_blendSteps > 0 && m_children.size() == 2; }
    // The shapes between the two ends of a blend (empty for anything else).
    std::vector<std::unique_ptr<Shape>> blendIntermediates() const;

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
    QString m_name;
    QRectF m_bounds;                              // rectangles and ellipses
    double m_cornerRadius = 0.0;                  // rectangles
    QTransform m_shapeTransform;                  // rectangles, ellipses, text
    PathData m_path;                              // paths
    std::vector<std::unique_ptr<Shape>> m_children; // groups
    int m_blendSteps = 0;                           // groups
    TextData m_text;                                // text
    QPointF m_textOrigin;                           // text
    QImage m_image;                                 // image
    QTransform m_imagePlacement;                    // image
    Style m_style;
};

} // namespace opendraw
