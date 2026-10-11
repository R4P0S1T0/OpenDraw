#include "ColorPalette.h"

#include "Icons.h"

#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>
#include <QWheelEvent>

namespace opendraw {

namespace {
constexpr int kSwatchAlong = 18;  // along the strip
constexpr int kSwatchAcross = 18; // across the strip
constexpr int kArrow = 14;        // scroll arrow at each end
constexpr int kMargin = 2;
} // namespace

ColorPalette::ColorPalette(Qt::Orientation orientation, QWidget *parent)
    : QWidget(parent)
    , m_orientation(orientation)
{
    setColors(orientation == Qt::Vertical ? defaultColors() : QList<QColor>{});
    if (orientation == Qt::Vertical)
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    else
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

// Greys from black to white, then twelve hues in three shades each (strong, dark, light).
QList<QColor> ColorPalette::defaultColors()
{
    QList<QColor> colors;
    for (int i = 0; i <= 10; ++i)
        colors.append(QColor::fromRgbF(i / 10.0, i / 10.0, i / 10.0));
    for (int hue = 0; hue < 360; hue += 30) {
        colors.append(QColor::fromHsvF(hue / 360.0, 1.0, 1.0));
        colors.append(QColor::fromHsvF(hue / 360.0, 1.0, 0.6));
        colors.append(QColor::fromHsvF(hue / 360.0, 0.35, 1.0));
    }
    // A few everyday colours the hue wheel misses.
    for (const char *hex : {"#8b5a2b", "#c69c6d", "#f5deb3", "#ff7f50", "#ffd700", "#40e0d0", "#000080", "#800000"})
        colors.append(QColor::fromString(QLatin1StringView(hex)));
    return colors;
}

void ColorPalette::setColors(const QList<QColor> &colors)
{
    m_colors.clear();
    m_colors.append(QColor(Qt::transparent));
    for (const QColor &c : colors) {
        if (c.alpha() > 0)
            m_colors.append(c);
    }
    m_offset = std::clamp(m_offset, 0, maxOffset());
    updateGeometry();
    update();
}

int ColorPalette::along(const QSize &size) const
{
    return m_orientation == Qt::Vertical ? size.height() : size.width();
}

int ColorPalette::along(const QPoint &point) const
{
    return m_orientation == Qt::Vertical ? point.y() : point.x();
}

QSize ColorPalette::sizeHint() const
{
    const int length = 2 * kMargin + int(m_colors.size()) * kSwatchAlong;
    const int thickness = 2 * kMargin + kSwatchAcross;
    return m_orientation == Qt::Vertical ? QSize(thickness, length) : QSize(length, thickness);
}

QSize ColorPalette::minimumSizeHint() const
{
    const int thickness = 2 * kMargin + kSwatchAcross;
    const int length = 2 * kArrow + 3 * kSwatchAlong;
    return m_orientation == Qt::Vertical ? QSize(thickness, length) : QSize(length, thickness);
}

bool ColorPalette::overflows() const
{
    return 2 * kMargin + m_colors.size() * kSwatchAlong > along(size());
}

int ColorPalette::visibleCount() const
{
    const int space = along(size()) - 2 * kMargin - (overflows() ? 2 * kArrow : 0);
    return std::max(1, space / kSwatchAlong);
}

int ColorPalette::maxOffset() const
{
    return overflows() ? std::max(0, int(m_colors.size()) - visibleCount()) : 0;
}

void ColorPalette::scrollBy(int swatches)
{
    const int offset = std::clamp(m_offset + swatches, 0, maxOffset());
    if (offset != m_offset) {
        m_offset = offset;
        update();
    }
}

QRect ColorPalette::swatchRect(int index) const
{
    const int start = kMargin + (overflows() ? kArrow : 0) + (index - m_offset) * kSwatchAlong;
    if (m_orientation == Qt::Vertical)
        return {kMargin, start, kSwatchAcross, kSwatchAlong};
    return {start, kMargin, kSwatchAlong, kSwatchAcross};
}

QRect ColorPalette::arrowRect(bool backwards) const
{
    const int start = backwards ? kMargin : along(size()) - kMargin - kArrow;
    if (m_orientation == Qt::Vertical)
        return {kMargin, start, kSwatchAcross, kArrow};
    return {start, kMargin, kArrow, kSwatchAcross};
}

int ColorPalette::swatchAt(const QPoint &pos) const
{
    const int last = std::min(int(m_colors.size()), m_offset + visibleCount());
    for (int i = m_offset; i < last; ++i) {
        if (swatchRect(i).contains(pos))
            return i;
    }
    return -1;
}

void ColorPalette::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const QColor border = palette().color(QPalette::Shadow);
    const int last = std::min(int(m_colors.size()), m_offset + visibleCount());
    for (int i = m_offset; i < last; ++i) {
        const QRect r = swatchRect(i).adjusted(1, 1, -1, -1);
        if (m_colors[i].alpha() == 0) {
            p.fillRect(r, Qt::white);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(QPen(QColor(0xd0, 0x30, 0x30), 1.5));
            p.drawLine(QPointF(r.left() + 2, r.bottom() - 1), QPointF(r.right() - 1, r.top() + 2));
            p.setRenderHint(QPainter::Antialiasing, false);
        } else {
            p.fillRect(r, m_colors[i]);
        }
        p.setPen(border);
        p.setBrush(Qt::NoBrush);
        p.drawRect(r.adjusted(0, 0, -1, -1));
    }
    if (overflows()) {
        const bool vertical = m_orientation == Qt::Vertical;
        const QIcon back = icon(vertical ? Icon::ScrollUp : Icon::ScrollLeft);
        const QIcon forward = icon(vertical ? Icon::ScrollDown : Icon::ScrollRight);
        const auto mode = [](bool enabled) { return enabled ? QIcon::Normal : QIcon::Disabled; };
        back.paint(&p, arrowRect(true), Qt::AlignCenter, mode(m_offset > 0));
        forward.paint(&p, arrowRect(false), Qt::AlignCenter, mode(m_offset < maxOffset()));
    }
}

void ColorPalette::mousePressEvent(QMouseEvent *event)
{
    const QPoint pos = event->position().toPoint();
    if (overflows()) {
        if (arrowRect(true).contains(pos)) {
            scrollBy(-std::max(1, visibleCount() - 1));
            return;
        }
        if (arrowRect(false).contains(pos)) {
            scrollBy(std::max(1, visibleCount() - 1));
            return;
        }
    }
    const int index = swatchAt(pos);
    if (index < 0)
        return;
    if (event->button() == Qt::LeftButton)
        emit fillChosen(m_colors[index]);
    else if (event->button() == Qt::RightButton)
        emit outlineChosen(m_colors[index]);
}

void ColorPalette::wheelEvent(QWheelEvent *event)
{
    const QPoint delta = event->angleDelta();
    const int steps = (delta.y() != 0 ? delta.y() : delta.x()) / 40;
    scrollBy(-steps);
    event->accept();
}

void ColorPalette::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    m_offset = std::clamp(m_offset, 0, maxOffset());
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
