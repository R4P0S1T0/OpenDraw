#pragma once

#include <QString>

class QIODevice;

namespace opendraw {

class Document;

// SVG is OpenDraw's native file format for now.
namespace svg {

bool write(const Document &document, QIODevice *device);

// Replaces the document's contents. Supports <rect>, <ellipse> and <circle> with
// fill/stroke given as attributes or in a style attribute. Other elements are skipped.
bool read(QIODevice *device, Document &document, QString *errorMessage = nullptr);

} // namespace svg
} // namespace opendraw
