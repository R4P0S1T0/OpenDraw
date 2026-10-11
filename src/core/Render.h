#pragma once

class QBrush;
class QPainter;

// Draws shapes with a QPainter in document coordinates. Used by the canvas and by every
// export, so what is exported looks exactly like what is on screen.
namespace opendraw {

class Document;
class Shape;
struct Page;
struct Style;

namespace render {

// Solid colour, gradient or no brush, as the style says.
QBrush fillBrush(const Style &style);

void paintShape(QPainter &painter, const Shape &shape);
// Every visible layer of the page, bottom to top.
void paintPage(QPainter &painter, const Page &page);
// The current page.
void paintDocument(QPainter &painter, const Document &document);

} // namespace render
} // namespace opendraw
