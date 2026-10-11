#include "PropertyBar.h"

#include "Icons.h"
#include "Units.h"
#include "core/Commands.h"
#include "core/Document.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QSignalBlocker>
#include <QToolButton>
#include <QUndoStack>

#include <cmath>

namespace opendraw {

namespace {
constexpr double kMaxDisplay = 1000000.0;

struct Paper {
    const char *name;
    double widthMm;
    double heightMm;
};

// Portrait sizes; landscape is the same paper turned.
constexpr Paper kPapers[] = {
    {"A3", 297, 420},          {"A4", 210, 297},          {"A5", 148, 210},      {"A6", 105, 148},
    {"Letter", 215.9, 279.4},  {"Legal", 215.9, 355.6},   {"Tabloid", 279.4, 431.8},
};

const Units &units()
{
    return Units::instance();
}

// Spin boxes show a few decimals; ignore differences they cannot display.
bool differs(double a, double b)
{
    return std::abs(a - b) > 0.5 * std::pow(10.0, -units().decimals());
}

void setValueQuietly(QDoubleSpinBox *box, double value)
{
    const QSignalBlocker blocker(box);
    box->setSpecialValueText(QString());
    box->setValue(value);
}
} // namespace

PropertyBar::PropertyBar(Document *document, QUndoStack *undoStack, QWidget *parent)
    : QToolBar(tr("Property Bar"), parent)
    , m_document(document)
    , m_undoStack(undoStack)
{
    setObjectName(QStringLiteral("propertyBar"));
    setMovable(false);
    setIconSize(QSize(20, 20));

    // --- Page (nothing selected) ---
    m_paper = new QComboBox(this);
    m_paper->setToolTip(tr("Paper type"));
    for (const Paper &paper : kPapers)
        m_paper->addItem(QString::fromLatin1(paper.name));
    m_paper->addItem(tr("Custom"));
    m_paper->setMinimumWidth(110);
    m_pageActions << addWidget(m_paper);
    m_pageWidth = addField(tr("W"), tr("Page width"), 1.0, m_pageActions);
    m_pageHeight = addField(tr("H"), tr("Page height"), 1.0, m_pageActions);
    m_portrait = addButton(icon(Icon::Portrait), tr("Portrait"), m_pageActions);
    m_landscape = addButton(icon(Icon::Landscape), tr("Landscape"), m_pageActions);
    m_portrait->setCheckable(true);
    m_landscape->setCheckable(true);
    m_pageActions << addSeparator();

    auto *unitLabel = new QLabel(tr("Units:"), this);
    unitLabel->setContentsMargins(6, 0, 4, 0);
    m_pageActions << addWidget(unitLabel);
    m_unit = new QComboBox(this);
    m_unit->setToolTip(tr("Unit for measurements and rulers"));
    for (Unit unit : {Unit::Millimeters, Unit::Centimeters, Unit::Inches, Unit::Points, Unit::Pixels})
        m_unit->addItem(Units::name(unit), int(unit));
    m_pageActions << addWidget(m_unit);
    m_pageActions << addSeparator();

    m_nudge = addField(tr("Nudge"), tr("Distance the arrow keys move the selection (Shift: ten times)"), 0.0,
                       m_pageActions);
    m_duplicateX = addField(tr("Duplicate X"), tr("Horizontal offset of duplicates (Ctrl+D)"), -kMaxDisplay,
                            m_pageActions);
    m_duplicateY = addField(tr("Y"), tr("Vertical offset of duplicates (Ctrl+D)"), -kMaxDisplay, m_pageActions);

    // --- Object (something selected) ---
    m_x = addField(tr("X"), tr("Horizontal position of the left edge"), -kMaxDisplay, m_objectActions);
    m_y = addField(tr("Y"), tr("Vertical position of the top edge"), -kMaxDisplay, m_objectActions);
    m_objectActions << addSeparator();
    m_width = addField(tr("W"), tr("Width"), 0.0, m_objectActions);
    m_height = addField(tr("H"), tr("Height"), 0.0, m_objectActions);
    m_lockRatio = addButton(icon(Icon::LockRatio), tr("Lock ratio: width and height change together"),
                            m_objectActions);
    m_lockRatio->setCheckable(true);
    m_objectActions << addSeparator();
    QToolButton *mirrorH = addButton(icon(Icon::MirrorHorizontal), tr("Mirror horizontally"), m_objectActions);
    QToolButton *mirrorV = addButton(icon(Icon::MirrorVertical), tr("Mirror vertically"), m_objectActions);
    m_objectActions << addSeparator();
    m_outline = addField(tr("Outline"), tr("Outline width (0 = no outline)"), 0.0, m_objectActions);
    m_objectActions << addSeparator();
    m_opacity = addField(tr("Opacity"), tr("Opacity of the whole object (100 = solid)"), 0.0, m_objectActions);
    m_opacity->setRange(0.0, 100.0);
    m_opacity->setDecimals(0);
    m_opacity->setSuffix(QStringLiteral(" %"));
    m_opacity->setMinimumWidth(70);

    for (QDoubleSpinBox *box : {m_x, m_y, m_width, m_height})
        connect(box, &QDoubleSpinBox::editingFinished, this, &PropertyBar::applyGeometry);
    connect(m_outline, &QDoubleSpinBox::editingFinished, this, &PropertyBar::applyOutlineWidth);
    connect(m_opacity, &QDoubleSpinBox::editingFinished, this, &PropertyBar::applyOpacity);
    connect(mirrorH, &QToolButton::clicked, this, [this] { emit mirrorRequested(Qt::Horizontal); });
    connect(mirrorV, &QToolButton::clicked, this, [this] { emit mirrorRequested(Qt::Vertical); });

    connect(m_paper, &QComboBox::activated, this, [this](int index) {
        if (index < 0 || index >= int(std::size(kPapers))) {
            refreshPage();
            return;
        }
        const Paper &paper = kPapers[index];
        QSizeF size(paper.widthMm * kUnitsPerMm, paper.heightMm * kUnitsPerMm);
        const QSizeF current = m_document->pageSize();
        if (current.width() > current.height())
            size.transpose();
        applyPageSize(size);
    });
    for (QDoubleSpinBox *box : {m_pageWidth, m_pageHeight}) {
        connect(box, &QDoubleSpinBox::editingFinished, this, [this] {
            applyPageSize(QSizeF(units().fromDisplay(m_pageWidth->value()), units().fromDisplay(m_pageHeight->value())));
        });
    }
    auto orient = [this](bool landscape) {
        QSizeF size = m_document->pageSize();
        if ((size.width() > size.height()) != landscape)
            size.transpose();
        applyPageSize(size);
        refreshPage();
    };
    connect(m_portrait, &QToolButton::clicked, this, [orient] { orient(false); });
    connect(m_landscape, &QToolButton::clicked, this, [orient] { orient(true); });
    connect(m_unit, &QComboBox::activated, this,
            [this](int index) { Units::instance().setUnit(Unit(m_unit->itemData(index).toInt())); });
    // The document-unit value is kept on each box so switching units does not round it.
    connect(m_nudge, &QDoubleSpinBox::editingFinished, this, [this] {
        m_nudge->setProperty("units", units().fromDisplay(m_nudge->value()));
        emit nudgeDistanceChanged(m_nudge->property("units").toDouble());
    });
    for (QDoubleSpinBox *box : {m_duplicateX, m_duplicateY}) {
        connect(box, &QDoubleSpinBox::editingFinished, this, [this, box] {
            box->setProperty("units", units().fromDisplay(box->value()));
            emit duplicateOffsetChanged(
                QPointF(m_duplicateX->property("units").toDouble(), m_duplicateY->property("units").toDouble()));
        });
    }

    // Defaults matching the canvas: 1 mm nudge, 5 mm duplicate offset.
    m_nudge->setProperty("units", kUnitsPerMm);
    m_duplicateX->setProperty("units", 5 * kUnitsPerMm);
    m_duplicateY->setProperty("units", 5 * kUnitsPerMm);

    connect(m_document, &Document::changed, this, &PropertyBar::refresh);
    connect(&Units::instance(), &Units::unitChanged, this, &PropertyBar::updateUnits);
    updateUnits();
    updateMode();
}

QDoubleSpinBox *PropertyBar::addField(const QString &label, const QString &toolTip, double minimum,
                                      QList<QAction *> &group)
{
    auto *caption = new QLabel(label, this);
    caption->setContentsMargins(6, 0, 3, 0);
    group << addWidget(caption);

    auto *box = new QDoubleSpinBox(this);
    box->setRange(minimum, kMaxDisplay);
    box->setKeyboardTracking(false);
    box->setToolTip(toolTip);
    box->setMinimumWidth(90);
    box->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
    group << addWidget(box);
    return box;
}

QToolButton *PropertyBar::addButton(const QIcon &icon, const QString &toolTip, QList<QAction *> &group)
{
    auto *button = new QToolButton(this);
    button->setIcon(icon);
    button->setToolTip(toolTip);
    button->setAutoRaise(true);
    group << addWidget(button);
    return button;
}

void PropertyBar::setSelection(Shape *single, bool anySelected)
{
    m_shape = single;
    m_anySelected = anySelected || single;
    updateMode();
}

void PropertyBar::updateMode()
{
    for (QAction *action : std::as_const(m_pageActions))
        action->setVisible(!m_anySelected);
    for (QAction *action : std::as_const(m_objectActions))
        action->setVisible(m_anySelected);
    refresh();
}

// Suffix, decimals and values follow the display unit.
void PropertyBar::updateUnits()
{
    const QString suffix = QLatin1Char(' ') + units().suffix();
    for (QDoubleSpinBox *box : {m_pageWidth, m_pageHeight, m_nudge, m_duplicateX, m_duplicateY, m_x, m_y, m_width,
                                m_height, m_outline}) {
        box->setSuffix(suffix);
        box->setDecimals(units().decimals());
    }
    for (QDoubleSpinBox *box : {m_nudge, m_duplicateX, m_duplicateY})
        setValueQuietly(box, units().toDisplay(box->property("units").toDouble()));
    const QSignalBlocker blocker(m_unit);
    m_unit->setCurrentIndex(m_unit->findData(int(units().unit())));
    refresh();
}

void PropertyBar::refreshPage()
{
    const QSizeF size = m_document->pageSize();
    setValueQuietly(m_pageWidth, units().toDisplay(size.width()));
    setValueQuietly(m_pageHeight, units().toDisplay(size.height()));
    const bool landscape = size.width() > size.height();
    m_portrait->setChecked(!landscape);
    m_landscape->setChecked(landscape);

    int paper = int(std::size(kPapers)); // Custom
    const double shortSide = std::min(size.width(), size.height()) / kUnitsPerMm;
    const double longSide = std::max(size.width(), size.height()) / kUnitsPerMm;
    for (int i = 0; i < int(std::size(kPapers)); ++i) {
        if (std::abs(kPapers[i].widthMm - shortSide) < 0.5 && std::abs(kPapers[i].heightMm - longSide) < 0.5)
            paper = i;
    }
    const QSignalBlocker blocker(m_paper);
    m_paper->setCurrentIndex(paper);
}

void PropertyBar::refresh()
{
    if (!m_anySelected) {
        refreshPage();
        return;
    }
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
    m_outline->setEnabled(m_shape->kind() != ShapeKind::Group && m_shape->kind() != ShapeKind::Image);

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
    setValueQuietly(m_x, units().toDisplay(r.x()));
    setValueQuietly(m_y, units().toDisplay(r.y()));
    setValueQuietly(m_width, units().toDisplay(r.width()));
    setValueQuietly(m_height, units().toDisplay(r.height()));
    setValueQuietly(m_outline, m_shape->style().hasStroke() ? units().toDisplay(m_shape->style().strokeWidth) : 0.0);
    setValueQuietly(m_opacity, opacity * 100.0);
}

void PropertyBar::applyPageSize(const QSizeF &size)
{
    if (size.isEmpty() || size == m_document->pageSize()) {
        refreshPage();
        return;
    }
    m_undoStack->push(new SetPageSizeCommand(m_document, size));
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
    double x = m_x->value(), y = m_y->value(), w = m_width->value(), h = m_height->value();
    const double oldW = units().toDisplay(old.width()), oldH = units().toDisplay(old.height());
    if (!differs(x, units().toDisplay(old.x())) && !differs(y, units().toDisplay(old.y())) && !differs(w, oldW)
        && !differs(h, oldH))
        return;

    // With the ratio locked, the side that was edited drives the other one.
    if (m_lockRatio->isChecked()) {
        if (differs(w, oldW) && oldW > 0.0)
            h = oldH * w / oldW;
        else if (differs(h, oldH) && oldH > 0.0)
            w = oldW * h / oldH;
    }
    const QRectF bounds(units().fromDisplay(x), units().fromDisplay(y), units().fromDisplay(w), units().fromDisplay(h));
    m_undoStack->push(new SetBoundsCommand(m_document, m_shape, old, bounds));
}

void PropertyBar::applyOutlineWidth()
{
    if (!m_shape)
        return;
    Style style = m_shape->style();
    const double current = style.hasStroke() ? units().toDisplay(style.strokeWidth) : 0.0;
    if (!differs(m_outline->value(), current))
        return;

    style.strokeWidth = units().fromDisplay(m_outline->value());
    if (style.strokeWidth > 0.0 && style.stroke.alpha() == 0)
        style.stroke = Qt::black; // giving a width to "no outline" means the user wants one
    m_undoStack->push(new SetStyleCommand(m_document, m_shape, style));
}

} // namespace opendraw
