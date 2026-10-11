#pragma once

#include "core/Export.h"

#include <QDialog>

class QCheckBox;
class QLabel;
class QPushButton;
class QSpinBox;

namespace opendraw {

class Document;
class Shape;

// Options for exporting a bitmap: resolution, background and what to include.
class ExportImageDialog : public QDialog {
    Q_OBJECT

public:
    ExportImageDialog(const Document &document, std::vector<const Shape *> selection, QWidget *parent = nullptr);

    exporter::ImageOptions options() const;

private:
    void updateSizeLabel();

    const Document &m_document;
    std::vector<const Shape *> m_selection;
    QSpinBox *m_dpi;
    QCheckBox *m_transparent;
    QCheckBox *m_selectedOnly;
    QLabel *m_sizeLabel;
    QPushButton *m_okButton = nullptr;
};

} // namespace opendraw
