#pragma once

#include <QMap>
#include <QSizeF>
#include <QString>

#include <memory>
#include <vector>

class QIODevice;

namespace opendraw {

class Document;
class Shape;

// SVG is OpenDraw's native file format for now.
namespace svg {

// Bumped whenever OpenDraw starts writing something older releases cannot represent.
// Older releases still open newer files: they keep what they understand and report the rest.
// Version 2 added pages and layers.
inline constexpr int kFormatVersion = 2;
inline constexpr char kNamespace[] = "https://github.com/R4P0S1T0/OpenDraw/ns/1";
inline constexpr char kInkscapeNamespace[] = "http://www.inkscape.org/namespaces/inkscape";
inline constexpr char kSodipodiNamespace[] = "http://sodipodi.sourceforge.net/DTD/sodipodi-0.dtd";

struct ReadReport {
    int formatVersion = 0;               // 0 when the file was not written by OpenDraw
    QMap<QString, int> skippedElements;  // what was left out or simplified ("<text>", ...) -> count

    bool isNewerFormat() const { return formatVersion > kFormatVersion; }
    int skippedCount() const;
};

bool write(const Document &document, QIODevice *device);

// Replaces the document's contents, pages and layers included. Imports <rect>, <circle>, <ellipse>, <line>, <polyline>,
// <polygon> and <path>, including transforms, groups and inherited fill/stroke. Text, images
// and other elements not supported yet are skipped and listed in `report`.
bool read(QIODevice *device, Document &document, QString *errorMessage = nullptr,
          ReadReport *report = nullptr);

// A standalone SVG holding just these shapes (used for the clipboard).
QByteArray writeShapes(const std::vector<const Shape *> &shapes, const QSizeF &pageSize);
// Every shape on the first page of an SVG, all layers, bottom to top.
std::vector<std::unique_ptr<Shape>> readShapes(const QByteArray &data);

} // namespace svg
} // namespace opendraw
