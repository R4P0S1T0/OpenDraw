#include "ExportImageDialog.h"

#include "core/Document.h"
#include "core/Render.h"
#include "core/Shape.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>

namespace opendraw {

ExportImageDialog::ExportImageDialog(const Document &document, std::vector<const Shape *> selection, QWidget *parent)
    : QDialog(parent)
    , m_document(document)
    , m_selection(std::move(selection))
{
    setWindowTitle(tr("Export PNG"));

    QSettings settings;
    m_dpi = new QSpinBox(this);
    m_dpi->setRange(36, 2400);
    m_dpi->setSuffix(tr(" dpi"));
    m_dpi->setValue(settings.value(QStringLiteral("export/png/dpi"), 300).toInt());

    m_transparent = new QCheckBox(tr("Transparent background"), this);
    m_transparent->setChecked(settings.value(QStringLiteral("export/png/transparent"), false).toBool());

    m_selectedOnly = new QCheckBox(tr("Selected objects only"), this);
    m_selectedOnly->setEnabled(!m_selection.empty());
    m_selectedOnly->setChecked(!m_selection.empty());

    m_sizeLabel = new QLabel(this);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Export"));
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        QSettings settings;
        settings.setValue(QStringLiteral("export/png/dpi"), m_dpi->value());
        settings.setValue(QStringLiteral("export/png/transparent"), m_transparent->isChecked());
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *form = new QFormLayout(this);
    form->addRow(tr("Resolution:"), m_dpi);
    form->addRow(QString(), m_transparent);
    form->addRow(QString(), m_selectedOnly);
    form->addRow(tr("Image size:"), m_sizeLabel);
    form->addRow(buttons);

    connect(m_dpi, &QSpinBox::valueChanged, this, &ExportImageDialog::updateSizeLabel);
    connect(m_selectedOnly, &QCheckBox::toggled, this, &ExportImageDialog::updateSizeLabel);
    m_okButton = buttons->button(QDialogButtonBox::Ok);
    updateSizeLabel();
}

exporter::ImageOptions ExportImageDialog::options() const
{
    exporter::ImageOptions options;
    options.dpi = m_dpi->value();
    options.transparent = m_transparent->isChecked();
    if (m_selectedOnly->isChecked()) {
        options.shapes = m_selection;
        QRectF area;
        for (const Shape *shape : m_selection) {
            const QRectF painted = render::paintedBounds(*shape);
            area = area.isNull() ? painted : area.united(painted);
        }
        // A little margin for antialiased edges.
        const double margin = 1.0;
        options.area = area.adjusted(-margin, -margin, margin, margin);
    }
    return options;
}

void ExportImageDialog::updateSizeLabel()
{
    const QSize size = exporter::imageSize(m_document, options());
    const bool tooLarge = size.width() > exporter::kMaxImageSide || size.height() > exporter::kMaxImageSide;
    m_sizeLabel->setText(tooLarge ? tr("%1 × %2 pixels — too large, lower the resolution")
                                        .arg(size.width())
                                        .arg(size.height())
                                  : tr("%1 × %2 pixels").arg(size.width()).arg(size.height()));
    m_okButton->setEnabled(!tooLarge);
}

} // namespace opendraw
