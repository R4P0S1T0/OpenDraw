#include "core/Export.h"

#include "core/Document.h"
#include "core/Render.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>

#include <cmath>

namespace opendraw::exporter {

namespace {

constexpr double kScreenDpi = 96.0; // document units per inch

QRectF exportArea(const Document &document, const ImageOptions &options)
{
    return options.area.value_or(QRectF(QPointF(0, 0), document.pageSize()));
}

} // namespace

QSize imageSize(const Document &document, const ImageOptions &options)
{
    const QRectF area = exportArea(document, options);
    const double scale = options.dpi / kScreenDpi;
    return {std::max(1, int(std::ceil(area.width() * scale))), std::max(1, int(std::ceil(area.height() * scale)))};
}

QImage renderImage(const Document &document, const ImageOptions &options)
{
    const QSize size = imageSize(document, options);
    if (size.width() > kMaxImageSide || size.height() > kMaxImageSide)
        return {};

    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    if (image.isNull())
        return {};
    const int dotsPerMeter = int(std::lround(options.dpi / 0.0254));
    image.setDotsPerMeterX(dotsPerMeter);
    image.setDotsPerMeterY(dotsPerMeter);
    image.fill(options.transparent ? Qt::transparent : Qt::white);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    const double scale = options.dpi / kScreenDpi;
    painter.scale(scale, scale);
    painter.translate(-exportArea(document, options).topLeft());
    if (options.shapes.empty()) {
        render::paintDocument(painter, document);
    } else {
        for (const Shape *shape : options.shapes)
            render::paintShape(painter, *shape);
    }
    return image;
}

bool writePdf(const Document &document, const QString &path, QString *errorMessage)
{
    const QSizeF page = document.pageSize();
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QSizeF(page.width() / kUnitsPerMm, page.height() / kUnitsPerMm),
                                 QPageSize::Millimeter, QString(), QPageSize::ExactMatch));
    writer.setPageMargins(QMarginsF(0, 0, 0, 0));
    writer.setResolution(1200);
    writer.setTitle(QFileInfo(path).completeBaseName());
    writer.setCreator(QStringLiteral("OpenDraw"));

    QPainter painter;
    if (!painter.begin(&writer)) {
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("Export", "The file could not be written.");
        return false;
    }
    painter.setRenderHint(QPainter::Antialiasing);
    const double scale = writer.resolution() / kScreenDpi;
    painter.scale(scale, scale);
    render::paintDocument(painter, document);
    return painter.end();
}

} // namespace opendraw::exporter
