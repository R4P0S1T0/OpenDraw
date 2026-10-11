#pragma once

#include <QImage>
#include <QRectF>
#include <QString>

#include <optional>
#include <vector>

namespace opendraw {

class Document;
class Shape;

namespace exporter {

struct ImageOptions {
    double dpi = 300.0;
    bool transparent = false;
    // Region to export in document units; the whole page when unset.
    std::optional<QRectF> area;
    // Shapes to draw; every shape in the document when empty.
    std::vector<const Shape *> shapes;
};

// Largest width or height, in pixels, an exported image may have.
inline constexpr int kMaxImageSide = 30000;

// Pixel size the image would have; may exceed kMaxImageSide.
QSize imageSize(const Document &document, const ImageOptions &options);

// Renders to an image. Returns a null image when it would be larger than kMaxImageSide.
QImage renderImage(const Document &document, const ImageOptions &options);

// Writes a one-page vector PDF with the document's page size.
bool writePdf(const Document &document, const QString &path, QString *errorMessage = nullptr);

} // namespace exporter
} // namespace opendraw
