#pragma once

#include "core/SvgIO.h"

#include <QByteArray>
#include <QString>

namespace opendraw {

class Document;

// CorelDRAW (.cdr, .cdt) import through libcdr. libcdr turns each page into SVG, which the
// regular SVG reader then imports, so everything it supports (paths, groups, gradients, text)
// comes across as editable objects.
namespace cdr {

// Whether this build can import CorelDRAW files (libcdr was available at build time).
bool isAvailable();

// Replaces the document's contents with the CorelDRAW file's pages.
bool read(const QByteArray &data, Document &document, QString *errorMessage = nullptr,
          svg::ReadReport *report = nullptr);

} // namespace cdr
} // namespace opendraw
