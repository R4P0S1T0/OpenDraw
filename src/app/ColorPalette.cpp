#include "ColorPalette.h"

#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>

namespace opendraw {

namespace {
constexpr int kSwatch = 20;
constexpr int kGap = 3;
constexpr int kColumns = 2;
constexpr int kPadding = 4;
} // namespace

ColorPalette::ColorPalette(QWidget *parent)
    : QWidget(parent)
{
    // OpenDraw's default palette: neutrals, a saturated hue wheel, light tints, then pure RGB/CMY.
    static const char *const hex[] = {
        "#000000", "#333333", "#666666", "#999999", "#cccccc", "#ffffff",
        "#e53935", "#fb8c00", "#fdd835", "#7cb342", "#43a047", "#00897b",
        "#00acc1", "#1e88e5", "#3949ab", "#8e24aa", "#d81b60", "#6d4c41",
        "#ffcdd2", "#ffe0b2", "#fff9c4", "#dcedc8", "#b2dfdb", "#b3e5fc",
        "#c5cae9", "#e1bee7", "#f8bbd0", "#d7ccc8",
        "#ff0000", "#00ff00", "#0000ff", "#00ffff", "#ff00ff", "#ffff00",
    };
    m_colors.append(QColor(Qt::transparent));
    for (const char *h : hex)
        m_colors.append(QColor::fromString(QLatin1StringView(h)));

    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
}

QSize ColorPalette::sizeHint() const
{
    const int rows = (m_colors.size() + kColumns - 1) / kColumns;
    return {2 * kPadding + kColumns * kSwatch + (kColumns - 1) * kGap,
            2 * kPadding + rows * kSwatch + (rows - 1) * kGap};
}

QSize ColorPalette::minimumSizeHint() const
{
    return {sizeHint().width(), kSwatch * 4};
}

QRect ColorPalette::swatchRect(int index) const
{
    const int row = index / kColumns;
    const int column = index % kColumns;
    return {kPadding + column * (kSwatch + kGap), kPadding + row * (kSwatch + kGap), kSwatch, kSwatch};
}

int ColorPalette::swatchAt(const QPoint &pos) const
{
    for (int i = 0; i < m_colors.size(); ++i) {
        if (swatchRect(i).contains(pos))
            return i;
    }
    return -1;
}

void ColorPalette::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const QColor border = palette().color(QPalette::Shadow);
    for (int i = 0; i < m_colors.size(); ++i) {
        const QRect r = swatchRect(i);
        if (r.top() > height())
            break;
        if (m_colors[i].alpha() == 0) {
            p.fillRect(r, Qt::white);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(QPen(QColor(0xd0, 0x30, 0x30), 2));
            p.drawLine(r.topLeft() + QPoint(3, 3), r.bottomRight() - QPoint(3, 3));
            p.drawLine(r.topRight() + QPoint(-3, 3), r.bottomLeft() + QPoint(3, -3));
            p.setRenderHint(QPainter::Antialiasing, false);
        } else {
            p.fillRect(r, m_colors[i]);
        }
        p.setPen(border);
        p.setBrush(Qt::NoBrush);
        p.drawRect(r.adjusted(0, 0, -1, -1));
    }
}

void ColorPalette::mousePressEvent(QMouseEvent *event)
{
    const int index = swatchAt(event->position().toPoint());
    if (index < 0)
        return;
    if (event->button() == Qt::LeftButton)
        emit fillChosen(m_colors[index]);
    else if (event->button() == Qt::RightButton)
        emit outlineChosen(m_colors[index]);
}

bool ColorPalette::event(QEvent *event)
{
    if (event->type() == QEvent::ToolTip) {
        auto *help = static_cast<QHelpEvent *>(event);
        const int index = swatchAt(help->pos());
        if (index >= 0) {
            const QString name = m_colors[index].alpha() == 0 ? tr("No color") : m_colors[index].name().toUpper();
            QToolTip::showText(help->globalPos(),
                               tr("%1\nLeft click: fill · Right click: outline").arg(name), this);
        } else {
            QToolTip::hideText();
        }
        return true;
    }
    return QWidget::event(event);
}

} // namespace opendraw
