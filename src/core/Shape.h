#pragma once

#include <QColor>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>

namespace opendraw {

struct Style {
    QColor fill = QColor(0x4a, 0x90, 0xd9);
    QColor stroke = QColor(0x1d, 0x2b, 0x3a);
    double strokeWidth = 1.0;

    bool hasFill() const { return fill.alpha() > 0; }
    bool hasStroke() const { return stroke.alpha() > 0 && strokeWidth > 0.0; }
};

enum class ShapeKind { Rectangle, Ellipse };

// A drawable object. Geometry is stored in document units (SVG user units, 96 per inch).
class Shape {
public:
    Shape(ShapeKind kind, const QRectF &bounds, const Style &style = {});

    quint64 id() const { return m_id; }
    ShapeKind kind() const { return m_kind; }

    QRectF bounds() const { return m_bounds; }
    void setBounds(const QRectF &bounds) { m_bounds = bounds.normalized(); }
    void moveBy(const QPointF &delta) { m_bounds.translate(delta); }

    const Style &style() const { return m_style; }
    void setStyle(const Style &style) { m_style = style; }

    QPainterPath path() const;

    // Hit test: inside the fill, or within `tolerance` of the outline.
    bool contains(const QPointF &point, double tolerance) const;

private:
    quint64 m_id;
    ShapeKind m_kind;
    QRectF m_bounds;
    Style m_style;
};

} // namespace opendraw
