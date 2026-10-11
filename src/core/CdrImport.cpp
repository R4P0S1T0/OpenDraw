#include "core/CdrImport.h"

#include "core/Document.h"

#include <QBuffer>
#include <QCoreApplication>

#ifdef OPENDRAW_HAVE_LIBCDR
#include <libcdr/libcdr.h>
#include <librevenge-generators/librevenge-generators.h>
#include <librevenge-stream/librevenge-stream.h>
#include <librevenge/librevenge.h>
#endif

namespace opendraw::cdr {

namespace {
} // namespace

bool isAvailable()
{
#ifdef OPENDRAW_HAVE_LIBCDR
    return true;
#else
    return false;
#endif
}

bool read(const QByteArray &data, Document &document, QString *errorMessage, svg::ReadReport *report)
{
#ifndef OPENDRAW_HAVE_LIBCDR
    Q_UNUSED(data);
    Q_UNUSED(document);
    Q_UNUSED(report);
    if (errorMessage)
        *errorMessage = QCoreApplication::translate("CdrImport", "This version of OpenDraw was built without CorelDRAW import (libcdr).");
    return false;
#else
    // Reading from memory avoids file-name encoding problems on Windows; the stream also
    // understands the ZIP container used by CorelDRAW X4 and later.
    librevenge::RVNGStringStream input(reinterpret_cast<const unsigned char *>(data.constData()),
                                       unsigned(data.size()));
    if (!libcdr::CDRDocument::isSupported(&input)) {
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("CdrImport", "The file is not a CorelDRAW drawing, or its version is not supported.");
        return false;
    }

    librevenge::RVNGStringVector pageSvgs;
    librevenge::RVNGSVGDrawingGenerator generator(pageSvgs, "svg");
    if (!libcdr::CDRDocument::parse(&input, &generator) || pageSvgs.empty()) {
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("CdrImport", "The CorelDRAW file could not be read.");
        return false;
    }

    // Each page arrives as an SVG document; read them one by one and gather their pages.
    std::vector<std::unique_ptr<Page>> pages;
    svg::ReadReport combined;
    QSizeF pageSize;
    for (unsigned i = 0; i < pageSvgs.size(); ++i) {
        QByteArray svgData(pageSvgs[i].cstr(), int(pageSvgs[i].size()));
        QBuffer buffer(&svgData);
        buffer.open(QIODevice::ReadOnly);
        Document page;
        svg::ReadReport pageReport;
        QString error;
        if (!svg::read(&buffer, page, &error, &pageReport)) {
            if (errorMessage)
                *errorMessage = QCoreApplication::translate("CdrImport", "Page %1 could not be converted: %2").arg(i + 1).arg(error);
            return false;
        }
        if (i == 0)
            pageSize = page.pageSize();
        for (auto it = pageReport.skippedElements.cbegin(); it != pageReport.skippedElements.cend(); ++it)
            combined.skippedElements[it.key()] += it.value();
        for (auto &released : page.releasePages()) {
            released->name = QCoreApplication::translate("CdrImport", "Page %1").arg(pages.size() + 1);
            pages.push_back(std::move(released));
        }
    }

    document.setPages(std::move(pages));
    document.setPageSize(pageSize);
    if (report)
        *report = combined;
    return true;
#endif
}

} // namespace opendraw::cdr
