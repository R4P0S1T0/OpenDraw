#pragma once

class QPainter;

// Draws shapes with a QPainter in document coordinates. Used by the canvas and by every
// export, so what is exported looks exactly like what is on screen.
namespace opendraw {

class Document;
class Shape;

namespace render {

void paintShape(QPainter &painter, const Shape &shape);
void paintDocument(QPainter &painter, const Document &document);

} // namespace render
} // namespace opendraw
