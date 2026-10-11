#include "PropertyBar.h"

#include "core/Commands.h"
#include "core/Document.h"

#include <QDoubleSpinBox>
#include <QLabel>
#include <QSignalBlocker>
#include <QUndoStack>

#include <cmath>

namespace opendraw {

namespace {
constexpr double kMaxMm = 100000.0;

double toMm(double units)
{
    return units / kUnitsPerMm;
}

double toUnits(double mm)
{
    return mm * kUnitsPerMm;
}

// Spin boxes show two decimals; ignore differences they cannot display.
bool differs(double a, double b)
{
    return std::abs(a - b) > 0.005;
}
} // namespace

PropertyBar::PropertyBar(Document *document, QUndoStack *undoStack, QWidget *parent)
    : QToolBar(tr("Property Bar"), parent)
    , m_document(document)
    , m_undoStack(undoStack)
{
    setObjectName(QStringLiteral("propertyBar"));
    setMovable(false);

    m_x = addField(tr("X"), tr("Horizontal position of the left edge"), -kMaxMm);
    m_y = addField(tr("Y"), tr("Vertical position of the top edge"), -kMaxMm);
    addSeparator();
    m_width = addField(tr("W"), tr("Width"), 0.0);
    m_height = addField(tr("H"), tr("Height"), 0.0);
    addSeparator();
    m_outline = addField(tr("Outline"), tr("Outline width (0 = no outline)"), 0.0);
    m_outline->setMaximum(100.0);
    addSeparator();
    m_opacity = addField(tr("Opacity"), tr("Opacity of the whole object (100 = solid)"), 0.0);
    m_opacity->setRange(0.0, 100.0);
    m_opacity->setDecimals(0);
    m_opacity->setSuffix(QStringLiteral(" %"));
    m_opacity->setMinimumWidth(70);

    for (QDoubleSpinBox *box : {m_x, m_y, m_width, m_height})
        connect(box, &QDoubleSpinBox::editingFinished, this, &PropertyBar::applyGeometry);
    connect(m_outline, &QDoubleSpinBox::editingFinished, this, &PropertyBar::applyOutlineWidth);
    connect(m_opacity, &QDoubleSpinBox::editingFinished, this, &PropertyBar::applyOpacity);
    connect(m_document, &Document::changed, this, &PropertyBar::refresh);

    refresh();
}

QDoubleSpinBox *PropertyBar::addField(const QString &label, const QString &toolTip, double minimum)
{
    auto *caption = new QLabel(label, this);
    caption->setContentsMargins(8, 0, 4, 0);
    addWidget(caption);

    auto *box = new QDoubleSpinBox(this);
    box->setRange(minimum, kMaxMm);
    box->setDecimals(2);
    box->setSuffix(tr(" mm"));
    box->setKeyboardTracking(false);
    box->setToolTip(toolTip);
    box->setMinimumWidth(96);
    addWidget(box);
    return box;
}

void PropertyBar::setShape(Shape *shape)
{
    m_shape = shape;
    refresh();
}

void PropertyBar::refresh()
{
    const bool enabled = m_shape != nullptr;
    for (QDoubleSpinBox *box : {m_x, m_y, m_width, m_height, m_outline, m_opacity}) {
        box->setEnabled(enabled);
        if (!enabled) {
            const QSignalBlocker blocker(box);
            box->setSpecialValueText(QStringLiteral(" "));
            box->setValue(box->minimum());
        }
    }
    if (!enabled)
        return;
    // A group has no outline of its own; colours and widths are set on its members.
    m_outline->setEnabled(m_shape->kind() != ShapeKind::Group);

    // For a group, the opacity shown is that of its first member.
    double opacity = 1.0;
    bool found = false;
    m_shape->forEachLeaf([&](Shape &leaf) {
        if (!found) {
            opacity = leaf.style().opacity;
            found = true;
        }
    });

    const QRectF r = m_shape->bounds();
    const std::pair<QDoubleSpinBox *, double> values[] = {
        {m_x, toMm(r.x())},           {m_y, toMm(r.y())},
        {m_width, toMm(r.width())},   {m_height, toMm(r.height())},
        {m_outline, m_shape->style().hasStroke() ? toMm(m_shape->style().strokeWidth) : 0.0},
        {m_opacity, opacity * 100.0},
    };
    for (const auto &[box, value] : values) {
        const QSignalBlocker blocker(box);
        box->setSpecialValueText(QString());
        box->setValue(value);
    }
}

void PropertyBar::applyOpacity()
{
    if (!m_shape)
        return;
    const double opacity = m_opacity->value() / 100.0;
    std::vector<Shape *> changed;
    m_shape->forEachLeaf([&](Shape &leaf) {
        if (std::abs(leaf.style().opacity - opacity) > 0.001)
            changed.push_back(&leaf);
    });
    if (changed.empty())
        return;
    m_undoStack->beginMacro(tr("Opacity"));
    for (Shape *leaf : changed) {
        Style style = leaf->style();
        style.opacity = opacity;
        m_undoStack->push(new SetStyleCommand(m_document, leaf, style));
    }
    m_undoStack->endMacro();
}

void PropertyBar::applyGeometry()
{
    if (!m_shape)
        return;
    const QRectF old = m_shape->bounds();
    if (!differs(m_x->value(), toMm(old.x())) && !differs(m_y->value(), toMm(old.y()))
        && !differs(m_width->value(), toMm(old.width())) && !differs(m_height->value(), toMm(old.height())))
        return;

    const QRectF bounds(toUnits(m_x->value()), toUnits(m_y->value()), toUnits(m_width->value()),
                        toUnits(m_height->value()));
    m_undoStack->push(new SetBoundsCommand(m_document, m_shape, old, bounds));
}

void PropertyBar::applyOutlineWidth()
{
    if (!m_shape)
        return;
    Style style = m_shape->style();
    const double current = style.hasStroke() ? toMm(style.strokeWidth) : 0.0;
    if (!differs(m_outline->value(), current))
        return;

    style.strokeWidth = toUnits(m_outline->value());
    if (style.strokeWidth > 0.0 && style.stroke.alpha() == 0)
        style.stroke = Qt::black; // giving a width to "no outline" means the user wants one
    m_undoStack->push(new SetStyleCommand(m_document, m_shape, style));
}

} // namespace opendraw
