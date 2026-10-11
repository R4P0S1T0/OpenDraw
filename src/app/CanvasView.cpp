#include "CanvasView.h"

#include "CanvasWidget.h"
#include "Units.h"
#include "core/Document.h"

#include <QActionGroup>
#include <QGridLayout>
#include <QLocale>
#include <QMenu>
#include <QPainter>
#include <QScrollBar>
#include <QToolButton>

#include <cmath>

namespace opendraw {

namespace {
constexpr int kRulerThickness = 22;
constexpr double kMinMajorSpacingPx = 56.0;
constexpr double kMinMinorSpacingPx = 5.0;

// Smallest of 1, 2, 5 × 10^n display units whose spacing on screen is at least `minPixels`.
double niceStep(double pixelsPerUnit, double minPixels)
{
    double magnitude = std::pow(10.0, std::floor(std::log10(minPixels / pixelsPerUnit)));
    for (;;) {
        for (double factor : {1.0, 2.0, 5.0}) {
            if (factor * magnitude * pixelsPerUnit >= minPixels)
                return factor * magnitude;
        }
        magnitude *= 10.0;
    }
}

QString label(double value)
{
    if (std::abs(value) < 1e-9)
        value = 0.0;
    QLocale locale;
    locale.setNumberOptions(QLocale::OmitGroupSeparator);
    return locale.toString(value, 'g', 7);
}
} // namespace

Ruler::Ruler(Qt::Orientation orientation, CanvasWidget *canvas, QWidget *parent)
    : QWidget(parent)
    , m_orientation(orientation)
    , m_canvas(canvas)
{
    if (orientation == Qt::Horizontal)
        setFixedHeight(kRulerThickness);
    else
        setFixedWidth(kRulerThickness);
    connect(canvas, &CanvasWidget::viewChanged, this, qOverload<>(&QWidget::update));
    connect(&Units::instance(), &Units::unitChanged, this, qOverload<>(&QWidget::update));
}

QSize Ruler::sizeHint() const
{
    return m_orientation == Qt::Horizontal ? QSize(200, kRulerThickness) : QSize(kRulerThickness, 200);
}

void Ruler::setCursorPosition(double documentPosition)
{
    m_cursor = documentPosition;
    update();
}

void Ruler::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const QPalette pal = palette();
    p.fillRect(rect(), pal.color(QPalette::Window));
    const bool horizontal = m_orientation == Qt::Horizontal;
    const int length = horizontal ? width() : height();

    // Edge next to the canvas.
    p.setPen(pal.color(QPalette::Dark));
    if (horizontal)
        p.drawLine(0, height() - 1, width(), height() - 1);
    else
        p.drawLine(width() - 1, 0, width() - 1, height());

    const Units &units = Units::instance();
    const double zoom = m_canvas->zoom();
    const double offset = horizontal ? m_canvas->pan().x() : m_canvas->pan().y();
    const double pixelsPerUnit = Units::unitsPer(units.unit()) * zoom;
    if (pixelsPerUnit <= 0.0)
        return;
    const double major = niceStep(pixelsPerUnit, kMinMajorSpacingPx);
    int subdivisions = 10;
    while (subdivisions > 1 && major / subdivisions * pixelsPerUnit < kMinMinorSpacingPx)
        subdivisions = subdivisions == 10 ? 5 : subdivisions == 5 ? 2 : 1;
    const double minor = major / subdivisions;

    auto toPixel = [&](double value) { return value * pixelsPerUnit + offset; };
    const double first = std::floor((0.0 - offset) / pixelsPerUnit / minor) * minor;
    const double last = (length - offset) / pixelsPerUnit;

    QFont font = this->font();
    font.setPixelSize(10);
    p.setFont(font);
    const QColor ink = pal.color(QPalette::WindowText);
    QColor tickColor = ink;
    tickColor.setAlpha(150);

    const long long startIndex = std::llround(first / minor);
    for (long long i = startIndex;; ++i) {
        const double value = i * minor;
        if (value > last + minor)
            break;
        const double px = std::round(toPixel(value)) + 0.5;
        const bool isMajor = i % subdivisions == 0;
        const bool isHalf = !isMajor && subdivisions % 2 == 0 && (i % subdivisions) == subdivisions / 2;
        const double tick = isMajor ? kRulerThickness : isHalf ? kRulerThickness * 0.4 : kRulerThickness * 0.25;
        p.setPen(tickColor);
        if (horizontal)
            p.drawLine(QPointF(px, height() - tick), QPointF(px, height()));
        else
            p.drawLine(QPointF(width() - tick, px), QPointF(width(), px));
        if (isMajor) {
            p.setPen(ink);
            const QString text = label(value);
            if (horizontal) {
                p.drawText(QPointF(px + 3, 10), text);
            } else {
                p.save();
                p.translate(10, px - 3);
                p.rotate(-90);
                p.drawText(QPointF(0, 0), text);
                p.restore();
            }
        }
    }

    // Cursor marker.
    if (!std::isnan(m_cursor)) {
        const double px = std::round(m_cursor * zoom + offset) + 0.5;
        p.setPen(QPen(pal.color(QPalette::Highlight).lighter(130), 1, Qt::DashLine));
        if (horizontal)
            p.drawLine(QPointF(px, 0), QPointF(px, height()));
        else
            p.drawLine(QPointF(0, px), QPointF(width(), px));
    }
}

CanvasView::CanvasView(CanvasWidget *canvas, Document *document, QWidget *parent)
    : QWidget(parent)
    , m_canvas(canvas)
    , m_document(document)
    , m_horizontalRuler(new Ruler(Qt::Horizontal, canvas, this))
    , m_verticalRuler(new Ruler(Qt::Vertical, canvas, this))
    , m_horizontalBar(new QScrollBar(Qt::Horizontal, this))
    , m_verticalBar(new QScrollBar(Qt::Vertical, this))
{
    // The corner between the rulers shows the unit and switches it.
    auto *corner = new QToolButton(this);
    corner->setObjectName(QStringLiteral("rulerUnit"));
    corner->setFixedSize(kRulerThickness, kRulerThickness);
    corner->setAutoRaise(true);
    corner->setPopupMode(QToolButton::InstantPopup);
    corner->setStyleSheet(QStringLiteral("QToolButton { font-size: 9px; padding: 0; border-radius: 0; }"
                                         "QToolButton::menu-indicator { image: none; }"));
    auto *menu = new QMenu(corner);
    auto *group = new QActionGroup(menu);
    for (Unit unit : {Unit::Millimeters, Unit::Centimeters, Unit::Inches, Unit::Points, Unit::Pixels}) {
        QAction *action = menu->addAction(Units::name(unit));
        action->setCheckable(true);
        action->setChecked(unit == Units::instance().unit());
        group->addAction(action);
        connect(action, &QAction::triggered, this, [unit] { Units::instance().setUnit(unit); });
        connect(&Units::instance(), &Units::unitChanged, action, [action, unit](Unit current) {
            action->setChecked(current == unit);
        });
    }
    corner->setMenu(menu);
    corner->setToolTip(tr("Ruler unit"));
    auto updateCorner = [corner] { corner->setText(Units::instance().suffix()); };
    updateCorner();
    connect(&Units::instance(), &Units::unitChanged, corner, updateCorner);
    m_corner = corner;

    auto *grid = new QGridLayout(this);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(0);
    grid->addWidget(m_corner, 0, 0);
    grid->addWidget(m_horizontalRuler, 0, 1);
    grid->addWidget(m_verticalRuler, 1, 0);
    grid->addWidget(canvas, 1, 1);
    grid->addWidget(m_verticalBar, 1, 2);
    grid->addWidget(m_horizontalBar, 2, 1);
    grid->setRowStretch(1, 1);
    grid->setColumnStretch(1, 1);

    connect(canvas, &CanvasWidget::cursorMoved, this, [this](const QPointF &position) {
        m_horizontalRuler->setCursorPosition(position.x());
        m_verticalRuler->setCursorPosition(position.y());
    });
    connect(canvas, &CanvasWidget::viewChanged, this, &CanvasView::syncScrollBars);
    connect(document, &Document::changed, this, &CanvasView::syncScrollBars);
    connect(m_horizontalBar, &QScrollBar::valueChanged, this, &CanvasView::scrolled);
    connect(m_verticalBar, &QScrollBar::valueChanged, this, &CanvasView::scrolled);
    syncScrollBars();
}

bool CanvasView::rulersVisible() const
{
    return m_horizontalRuler->isVisible();
}

void CanvasView::setRulersVisible(bool visible)
{
    m_horizontalRuler->setVisible(visible);
    m_verticalRuler->setVisible(visible);
    m_corner->setVisible(visible);
}

// The scrollable area is the page with a page-sized margin all round, extended to whatever is
// currently in view. Scroll bar values are widget pixels: value = -pan.
void CanvasView::syncScrollBars()
{
    if (m_syncing)
        return;
    m_syncing = true;
    const QSizeF page = m_document->pageSize();
    const QRectF world = QRectF(QPointF(-page.width(), -page.height()), page * 3.0)
                             .united(m_canvas->visibleDocumentRect());
    const double zoom = m_canvas->zoom();
    const QPointF pan = m_canvas->pan();
    m_horizontalBar->setRange(int(std::floor(world.left() * zoom)),
                              int(std::ceil(world.right() * zoom)) - m_canvas->width());
    m_horizontalBar->setPageStep(m_canvas->width());
    m_horizontalBar->setSingleStep(20);
    m_horizontalBar->setValue(int(std::round(-pan.x())));
    m_verticalBar->setRange(int(std::floor(world.top() * zoom)),
                            int(std::ceil(world.bottom() * zoom)) - m_canvas->height());
    m_verticalBar->setPageStep(m_canvas->height());
    m_verticalBar->setSingleStep(20);
    m_verticalBar->setValue(int(std::round(-pan.y())));
    m_syncing = false;
}

void CanvasView::scrolled()
{
    if (m_syncing)
        return;
    m_syncing = true;
    m_canvas->setPan(QPointF(-m_horizontalBar->value(), -m_verticalBar->value()));
    m_syncing = false;
}

} // namespace opendraw
