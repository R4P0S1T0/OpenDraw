#pragma once

#include "core/Shape.h"

#include <QDialog>

class QComboBox;
class QPushButton;
class QSpinBox;

namespace opendraw {

// Chooses no fill, a uniform colour, or a two-colour linear or radial gradient.
class FillDialog : public QDialog {
    Q_OBJECT

public:
    enum class FillType { None, Uniform, Linear, Radial };

    FillDialog(const Style &current, QWidget *parent = nullptr);

    // `style` with its fill replaced by the choice in the dialog. Unchanged when the user
    // changed nothing, so gradients with more than two colours are not simplified by accident.
    Style applyTo(Style style) const;

    // For tests.
    void setFillType(FillType type);
    void setColors(const QColor &first, const QColor &second);
    void setAngle(int degrees);

private:
    Style previewStyle() const;
    Style applyToUnconditionally(Style style) const;
    void chooseColor(QColor &color, QPushButton *button);
    void updateWidgets();

    QComboBox *m_type;
    QPushButton *m_firstButton;
    QPushButton *m_secondButton;
    QSpinBox *m_angle;
    QWidget *m_preview;
    QColor m_first;
    QColor m_second;
    bool m_changed = false;
};

} // namespace opendraw
