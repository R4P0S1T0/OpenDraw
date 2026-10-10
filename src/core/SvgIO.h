#pragma once

#include <QMap>
#include <QString>

class QIODevice;

namespace opendraw {

class Document;

// SVG is OpenDraw's native file format for now.
namespace svg {

// Bumped whenever OpenDraw starts writing something older releases cannot represent.
// Older releases still open newer files: they keep what they understand and report the rest.
inline constexpr int kFormatVersion = 1;
inline constexpr char kNamespace[] = "https://github.com/R4P0S1T0/OpenDraw/ns/1";

struct ReadReport {
    int formatVersion = 0;               // 0 when the file was not written by OpenDraw
    QMap<QString, int> skippedElements;  // element name -> how many were not imported

    bool isNewerFormat() const { return formatVersion > kFormatVersion; }
    int skippedCount() const;
};

bool write(const Document &document, QIODevice *device);

// Replaces the document's contents. Supports <rect>, <ellipse> and <circle> with
// fill/stroke given as attributes or in a style attribute. Other drawing elements are
// skipped and listed in `report`.
bool read(QIODevice *device, Document &document, QString *errorMessage = nullptr,
          ReadReport *report = nullptr);

} // namespace svg
} // namespace opendraw
