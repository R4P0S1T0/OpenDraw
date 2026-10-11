#include "FillDialog.h"

#include "core/Render.h"

#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>

#include <functional>

namespace opendraw {

namespace {

// Paints a sample of the fill being chosen.
class FillPreview : public QWidget {
public:
    FillPreview(std::function<Style()> style, QWidget *parent)
        : QWidget(parent)
        , m_style(std::move(style))
    {
        setMinimumSize(160, 90);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        // Checkerboard, so transparent colours show as such.
        const int cell = 8;
        for (int y = 0; y < height(); y += cell) {
            for (int x = 0; x < width(); x += cell)
                p.fillRect(x, y, cell, cell, ((x + y) / cell) % 2 ? QColor(0xdd, 0xdd, 0xdd) : Qt::white);
        }
        p.setPen(QPen(palette().color(QPalette::Mid), 1));
        p.setBrush(render::fillBrush(m_style()));
        p.drawRoundedRect(QRectF(rect()).adjusted(6, 6, -6, -6), 6, 6);
    }

private:
    std::function<Style()> m_style;
};

QIcon swatch(const QColor &color)
{
    QPixmap pixmap(28, 18);
    pixmap.fill(color);
    return QIcon(pixmap);
}

} // namespace

FillDialog::FillDialog(const Style &current, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Fill"));

    m_type = new QComboBox(this);
    m_type->addItems({tr("No fill"), tr("Uniform"), tr("Linear gradient"), tr("Radial gradient")});
    m_firstButton = new QPushButton(this);
    m_secondButton = new QPushButton(this);
    m_angle = new QSpinBox(this);
    m_angle->setRange(0, 359);
    m_angle->setWrapping(true);
    m_angle->setSuffix(QStringLiteral("°"));
    m_preview = new FillPreview([this] { return previewStyle(); }, this);

    // Start from the current fill.
    m_first = current.fill.alpha() > 0 ? current.fill : QColor(Qt::black);
    m_second = Qt::white;
    if (current.gradient && !current.gradient->stops.empty()) {
        const Gradient &g = *current.gradient;
        m_type->setCurrentIndex(int(g.type == Gradient::Type::Radial ? FillType::Radial : FillType::Linear));
        m_first = g.stops.front().color;
        m_second = g.stops.back().color;
        m_angle->setValue(int(std::lround(g.angle())) % 360);
    } else {
        m_type->setCurrentIndex(int(current.fill.alpha() > 0 ? FillType::Uniform : FillType::None));
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *form = new QFormLayout(this);
    form->addRow(tr("Type:"), m_type);
    form->addRow(tr("Colour:"), m_firstButton);
    form->addRow(tr("End colour:"), m_secondButton);
    form->addRow(tr("Angle:"), m_angle);
    form->addRow(m_preview);
    form->addRow(buttons);

    connect(m_type, &QComboBox::currentIndexChanged, this, [this] {
        m_changed = true;
        updateWidgets();
    });
    connect(m_angle, &QSpinBox::valueChanged, this, [this] {
        m_changed = true;
        m_preview->update();
    });
    connect(m_firstButton, &QPushButton::clicked, this, [this] { chooseColor(m_first, m_firstButton); });
    connect(m_secondButton, &QPushButton::clicked, this, [this] { chooseColor(m_second, m_secondButton); });
    updateWidgets();
}

void FillDialog::chooseColor(QColor &color, QPushButton *button)
{
    const QColor chosen = QColorDialog::getColor(color, this, tr("Colour"), QColorDialog::ShowAlphaChannel);
    if (!chosen.isValid())
        return;
    color = chosen;
    m_changed = true;
    button->setIcon(swatch(color));
    m_preview->update();
}

void FillDialog::updateWidgets()
{
    const auto type = FillType(m_type->currentIndex());
    const bool gradient = type == FillType::Linear || type == FillType::Radial;
    m_firstButton->setEnabled(type != FillType::None);
    m_firstButton->setText(gradient ? tr("Start") : QString());
    m_secondButton->setEnabled(gradient);
    m_angle->setEnabled(type == FillType::Linear);
    m_firstButton->setIcon(swatch(m_first));
    m_secondButton->setIcon(swatch(m_second));
    m_preview->update();
}

Style FillDialog::previewStyle() const
{
    Style style;
    style.stroke = Qt::transparent;
    return applyToUnconditionally(style);
}

Style FillDialog::applyTo(Style style) const
{
    return m_changed ? applyToUnconditionally(std::move(style)) : style;
}

Style FillDialog::applyToUnconditionally(Style style) const
{
    style.gradient.reset();
    switch (FillType(m_type->currentIndex())) {
    case FillType::None:
        style.fill = Qt::transparent;
        break;
    case FillType::Uniform:
        style.fill = m_first;
        break;
    case FillType::Linear:
        style.gradient = Gradient::linear(m_first, m_second, m_angle->value());
        break;
    case FillType::Radial:
        style.gradient = Gradient::radial(m_first, m_second);
        break;
    }
    return style;
}

void FillDialog::setFillType(FillType type)
{
    m_type->setCurrentIndex(int(type));
}

void FillDialog::setColors(const QColor &first, const QColor &second)
{
    m_first = first;
    m_second = second;
    m_changed = true;
    updateWidgets();
}

void FillDialog::setAngle(int degrees)
{
    m_angle->setValue(degrees);
}

} // namespace opendraw
