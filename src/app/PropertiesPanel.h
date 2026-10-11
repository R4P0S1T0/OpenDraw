#pragma once

#include "core/Shape.h"

#include <QWidget>

#include <functional>

class QButtonGroup;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSlider;
class QSpinBox;
class QStackedWidget;
class QToolButton;

namespace opendraw {

class CanvasWidget;
class ColorPalette;
class Document;

// The Properties docker: fill, outline and transparency of the selection, applied as they are
// changed (each change one undoable step). With nothing selected it edits the style new
// shapes get.
class PropertiesPanel : public QWidget {
    Q_OBJECT

public:
    PropertiesPanel(Document *document, CanvasWidget *canvas, QWidget *parent = nullptr);

    enum class Section { Outline, Fill, Transparency };
    void showSection(Section section);

    // Dash patterns offered in the outline style list, in multiples of the outline width.
    static const std::vector<std::vector<double>> &dashPresets();

public slots:
    void refresh();

private:
    QWidget *createFillPage();
    QWidget *createOutlinePage();
    QWidget *createTransparencyPage();
    QToolButton *colorButton(const QString &toolTip);
    void apply(const QString &text, const std::function<void(Style &)> &change);
    void setFillType(int type);
    void chooseFillColor(bool second);
    void updateUnits();

    Document *m_document;
    CanvasWidget *m_canvas;
    QStackedWidget *m_pages;
    QButtonGroup *m_sections;
    bool m_updating = false;

    // Fill
    QButtonGroup *m_fillTypes;
    QToolButton *m_fillColor;
    QToolButton *m_fillEndColor;
    QLabel *m_fillEndLabel;
    QSpinBox *m_fillAngle;
    QLabel *m_fillAngleLabel;
    QLabel *m_patternNote;
    ColorPalette *m_fillPalette;

    // Outline
    QToolButton *m_outlineColor;
    QDoubleSpinBox *m_outlineWidth;
    QComboBox *m_dashes;
    QButtonGroup *m_caps;
    QButtonGroup *m_joins;
    ColorPalette *m_outlinePalette;

    // Transparency
    QSlider *m_transparencySlider;
    QSpinBox *m_transparency;
};

} // namespace opendraw
