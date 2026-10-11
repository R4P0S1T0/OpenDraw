#include "core/CdrImport.h"

#include "core/Document.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QRectF>

#ifdef OPENDRAW_HAVE_LIBCDR
#include <libcdr/libcdr.h>
#include <librevenge-generators/librevenge-generators.h>
#include <librevenge-stream/librevenge-stream.h>
#include <librevenge/librevenge.h>
#endif

namespace opendraw::cdr {

namespace {
#ifdef OPENDRAW_HAVE_LIBCDR

// librevenge's SVG writer drops what makes paragraph text: the frame of each text object and
// the breaks between its paragraphs. This writer notes the frames (to be added to the SVG
// afterwards) and marks each new paragraph with kParagraphMark, which the SVG reader turns
// into a line break.
class TextFrameGenerator : public librevenge::RVNGSVGDrawingGenerator {
public:
    explicit TextFrameGenerator(librevenge::RVNGStringVector &pages)
        : RVNGSVGDrawingGenerator(pages, "svg")
    {
    }

    // Frames of the text objects on each page, in points (the SVG's units), in order.
    std::vector<std::vector<QRectF>> frames;

    void startPage(const librevenge::RVNGPropertyList &propList) override
    {
        frames.emplace_back();
        RVNGSVGDrawingGenerator::startPage(propList);
    }

    void startTextObject(const librevenge::RVNGPropertyList &propList) override
    {
        auto inches = [&propList](const char *key) { return propList[key] ? propList[key]->getDouble() : 0.0; };
        if (frames.empty())
            frames.emplace_back();
        frames.back().push_back(QRectF(inches("svg:x") * 72.0, inches("svg:y") * 72.0, inches("svg:width") * 72.0,
                                       inches("svg:height") * 72.0));
        m_paragraphs = 0;
        RVNGSVGDrawingGenerator::startTextObject(propList);
    }

    void openParagraph(const librevenge::RVNGPropertyList &propList) override
    {
        RVNGSVGDrawingGenerator::openParagraph(propList);
        if (m_paragraphs++ > 0) {
            RVNGSVGDrawingGenerator::openSpan(librevenge::RVNGPropertyList());
            RVNGSVGDrawingGenerator::insertText(librevenge::RVNGString(QString(svg::kParagraphMark).toUtf8().constData()));
            RVNGSVGDrawingGenerator::closeSpan();
        }
    }

private:
    int m_paragraphs = 0;
};

// Adds the frames to the page's <svg:text> elements (one per text object, in order) as
// OpenDraw's paragraph frames.
QByteArray withTextFrames(QByteArray svg, const std::vector<QRectF> &frames)
{
    const QByteArray tag("<svg:text");
    if (svg.count(tag) != qsizetype(frames.size()) || frames.empty())
        return svg;
    const QByteArray root("<svg:svg");
    const qsizetype rootAt = svg.indexOf(root);
    if (rootAt < 0)
        return svg;
    svg.insert(rootAt + root.size(), QByteArray(" xmlns:opendraw=\"") + svg::kNamespace + "\"");
    qsizetype at = 0;
    for (const QRectF &frame : frames) {
        at = svg.indexOf(tag, at) + tag.size();
        if (frame.width() <= 0.0)
            continue;
        const QByteArray attribute = QStringLiteral(" opendraw:frame=\"%1 %2 %3 %4\"")
                                         .arg(frame.x(), 0, 'f', 4)
                                         .arg(frame.y(), 0, 'f', 4)
                                         .arg(frame.width(), 0, 'f', 4)
                                         .arg(frame.height(), 0, 'f', 4)
                                         .toUtf8();
        svg.insert(at, attribute);
    }
    return svg;
}

#endif
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
    TextFrameGenerator generator(pageSvgs);
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
        if (i < generator.frames.size())
            svgData = withTextFrames(std::move(svgData), generator.frames[i]);
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
