#pragma once

class QBrush;
class QPainterPath;
class QPen;
class QPainter;
class QPointF;
class QRectF;

// Draws shapes with a QPainter in document coordinates. Used by the canvas and by every
// export, so what is exported looks exactly like what is on screen.
namespace opendraw {

class Document;
class Shape;
struct Page;
struct Style;
enum class Arrowhead;

namespace render {

// Solid colour, gradient, texture or no brush, as the style says. `bounds` is the bounding box
// of the shape being filled (textures are positioned against it).
QBrush fillBrush(const Style &style, const QRectF &bounds);
// Outline pen: colour, width, dashes, caps and joins, or no pen.
QPen outlinePen(const Style &style);

// An arrowhead for an outline `width` wide, attached at the origin and pointing along +x
// (the line arrives from -x), as a filled outline.
QPainterPath arrowheadShape(Arrowhead type, double width);
// The same, attached at `end` and pointing along `direction`.
QPainterPath arrowheadPath(Arrowhead type, const QPointF &end, const QPointF &direction, double width);
// The arrowheads of a shape's outline on the page (empty when it has none), filled with the
// outline colour.
QPainterPath arrowheads(const Shape &shape);
// How far the outline, arrowheads included, can reach outside the shape's bounds.
double outlineReach(const Style &style);
// The part of the page a shape paints on: its bounds grown by the outline, arrowheads,
// contour rings and shadow.
QRectF paintedBounds(const Shape &shape);

void paintShape(QPainter &painter, const Shape &shape);
// Every visible layer of the page, bottom to top.
void paintPage(QPainter &painter, const Page &page);
// The current page.
void paintDocument(QPainter &painter, const Document &document);

} // namespace render
} // namespace opendraw
