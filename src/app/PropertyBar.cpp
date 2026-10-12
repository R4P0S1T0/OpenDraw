#include "PropertyBar.h"

#include "Icons.h"
#include "PropertiesPanel.h"
#include "Units.h"
#include "core/Commands.h"
#include "core/Document.h"

#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QSignalBlocker>
#include <QSpinBox>
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
    m_cornerActions << addSeparator();
    m_cornerRadius = addField(tr("Corners"), tr("Corner radius of the rectangle"), 0.0, m_cornerActions);
    m_objectActions << m_cornerActions;
    m_objectActions << addSeparator();
    m_objectActions << addSeparator();
    m_angle = addField(tr("Angle"), tr("Rotation angle (degrees, clockwise)"), -360.0, m_objectActions);
    m_angle->setRange(-360.0, 360.0);
    m_angle->setDecimals(1);
    m_angle->setSuffix(QStringLiteral("°"));
    m_angle->setMinimumWidth(80);
    QToolButton *mirrorH = addButton(icon(Icon::MirrorHorizontal), tr("Mirror horizontally"), m_objectActions);
    QToolButton *mirrorV = addButton(icon(Icon::MirrorVertical), tr("Mirror vertically"), m_objectActions);
    m_objectActions << addSeparator();
    m_outline = addField(tr("Outline"), tr("Outline width (0 = no outline)"), 0.0, m_objectActions);
    m_startArrow = new QComboBox(this);
    m_endArrow = new QComboBox(this);
    PropertiesPanel::addArrowheads(m_startArrow, true);
    PropertiesPanel::addArrowheads(m_endArrow, false);
    m_lineActions << addWidget(m_startArrow) << addWidget(m_endArrow);
    m_objectActions << m_lineActions;
    for (QComboBox *box : {m_startArrow, m_endArrow}) {
        connect(box, &QComboBox::activated, this, [this, box](int index) {
            if (!m_canvas)
                return;
            const auto type = Arrowhead(box->itemData(index).toInt());
            const bool start = box == m_startArrow;
            m_canvas->restyle(tr("Arrowhead"), [type, start](Style &style) {
                (start ? style.startArrow : style.endArrow) = type;
            });
        });
    }
    m_objectActions << addSeparator();
    m_opacity = addField(tr("Opacity"), tr("Opacity of the whole object (100 = solid)"), 0.0, m_objectActions);
    m_opacity->setRange(0.0, 100.0);
    m_opacity->setDecimals(0);
    m_opacity->setSuffix(QStringLiteral(" %"));
    m_opacity->setMinimumWidth(70);

    // --- Tool options ---
    m_newCornerRadius = addField(tr("Corners"), tr("Corner radius of new rectangles"), 0.0, m_rectangleToolActions);
    m_polygonSides = addIntField(tr("Sides"), tr("Number of sides of new polygons"), 3, 500, m_polygonActions);
    m_starPoints = addIntField(tr("Points"), tr("Number of points of new stars"), 3, 500, m_starActions);
    m_starSharpness = addIntField(tr("Sharpness"), tr("How deep the notches between the points go"), 1, 99,
                                  m_starActions);
    m_spiralRevolutions = new QDoubleSpinBox(this);
    {
        auto *caption = new QLabel(tr("Revolutions"), this);
        caption->setContentsMargins(6, 0, 3, 0);
        m_spiralActions << addWidget(caption);
        m_spiralRevolutions->setRange(0.25, 100);
        m_spiralRevolutions->setDecimals(2);
        m_spiralRevolutions->setSingleStep(0.5);
        m_spiralRevolutions->setToolTip(tr("Turns of new spirals"));
        m_spiralActions << addWidget(m_spiralRevolutions);
    }
    m_graphColumns = addIntField(tr("Columns"), tr("Columns of new graph paper"), 1, 200, m_graphActions);
    m_graphRows = addIntField(tr("Rows"), tr("Rows of new graph paper"), 1, 200, m_graphActions);

    auto *eyedropperCaption = new QLabel(tr("Picked colour"), this);
    eyedropperCaption->setContentsMargins(6, 0, 3, 0);
    m_eyedropperActions << addWidget(eyedropperCaption);
    m_eyedropperSwatch = new QLabel(this);
    m_eyedropperSwatch->setFixedSize(40, 18);
    m_eyedropperSwatch->setFrameShape(QFrame::Box);
    m_eyedropperActions << addWidget(m_eyedropperSwatch);
    QToolButton *pickAgain = addButton(icon(Icon::Eyedropper), tr("Pick a colour (or Shift+click)"),
                                       m_eyedropperActions);
    auto *eyedropperHint = new QLabel(this);
    eyedropperHint->setContentsMargins(6, 0, 6, 0);
    m_eyedropperActions << addWidget(eyedropperHint);
    eyedropperHint->setText(tr("Click inside an object to fill it, on its outline to colour the outline"));
    connect(pickAgain, &QToolButton::clicked, this, &PropertyBar::eyedropperResetRequested);
    createEffectFields();

    for (QSpinBox *box : {m_polygonSides, m_starPoints, m_starSharpness, m_graphColumns, m_graphRows})
        connect(box, &QSpinBox::valueChanged, this, &PropertyBar::emitToolSettings);
    connect(m_spiralRevolutions, &QDoubleSpinBox::valueChanged, this, &PropertyBar::emitToolSettings);
    connect(m_newCornerRadius, &QDoubleSpinBox::valueChanged, this, &PropertyBar::emitToolSettings);
    connect(m_cornerRadius, &QDoubleSpinBox::editingFinished, this, [this] {
        if (!m_shape || m_shape->kind() != ShapeKind::Rectangle)
            return;
        const double radius = units().fromDisplay(m_cornerRadius->value());
        if (std::abs(radius - m_shape->cornerRadius()) > 1e-6)
            m_undoStack->push(new SetCornerRadiusCommand(m_document, m_shape, radius));
    });
    setToolSettings(m_toolSettings);

    for (QDoubleSpinBox *box : {m_x, m_y, m_width, m_height})
        connect(box, &QDoubleSpinBox::editingFinished, this, &PropertyBar::applyGeometry);
    connect(m_outline, &QDoubleSpinBox::editingFinished, this, &PropertyBar::applyOutlineWidth);
    connect(m_opacity, &QDoubleSpinBox::editingFinished, this, &PropertyBar::applyOpacity);
    connect(m_angle, &QDoubleSpinBox::editingFinished, this, [this] {
        const double delta = m_angle->value() - m_shownAngle;
        if (std::abs(delta) > 0.01)
            emit rotateRequested(delta);
    });
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

void PropertyBar::createEffectFields()
{
    // Shadow tool
    m_shadowX = addField(tr("Shadow X"), tr("Horizontal offset of the shadow"), -kMaxDisplay, m_shadowActions);
    m_shadowY = addField(tr("Y"), tr("Vertical offset of the shadow"), -kMaxDisplay, m_shadowActions);
    m_shadowBlur = addField(tr("Feather"), tr("How soft the shadow is"), 0.0, m_shadowActions);
    m_shadowOpacity = addIntField(tr("Opacity"), tr("How dark the shadow is"), 0, 100, m_shadowActions);
    m_shadowOpacity->setSuffix(QStringLiteral(" %"));
    m_shadowColor = addButton(QIcon(), tr("Shadow colour"), m_shadowActions);
    QToolButton *noShadow = addButton(icon(Icon::FillNone), tr("Remove the shadow"), m_shadowActions);
    for (QDoubleSpinBox *box : {m_shadowX, m_shadowY}) {
        connect(box, &QDoubleSpinBox::editingFinished, this, [this] {
            const QPointF offset(units().fromDisplay(m_shadowX->value()), units().fromDisplay(m_shadowY->value()));
            applyShadow([offset](DropShadow &s) { s.offset = offset; });
        });
    }
    connect(m_shadowBlur, &QDoubleSpinBox::editingFinished, this, [this] {
        const double blur = units().fromDisplay(m_shadowBlur->value());
        applyShadow([blur](DropShadow &s) { s.blur = blur; });
    });
    connect(m_shadowOpacity, &QSpinBox::editingFinished, this, [this] {
        const int percent = m_shadowOpacity->value();
        applyShadow([percent](DropShadow &s) { s.color.setAlphaF(percent / 100.0); });
    });
    connect(m_shadowColor, &QToolButton::clicked, this, [this] {
        const auto current = m_canvas ? m_canvas->currentStyle().shadow : std::nullopt;
        const QColor color = QColorDialog::getColor(current ? current->color : QColor(Qt::black), this, tr("Shadow colour"));
        if (color.isValid())
            applyShadow([color](DropShadow &s) {
                const double alpha = s.color.alphaF();
                s.color = color;
                s.color.setAlphaF(alpha);
            });
    });
    connect(noShadow, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->restyle(tr("Remove shadow"), [](Style &s) { s.shadow.reset(); });
        refreshEffects();
    });

    // Transparency tool
    auto *typeLabel = new QLabel(tr("Transparency"), this);
    typeLabel->setContentsMargins(6, 0, 3, 0);
    m_transparencyActions << addWidget(typeLabel);
    m_transparencyType = new QComboBox(this);
    m_transparencyType->addItems({tr("None"), tr("Uniform"), tr("Linear"), tr("Radial")});
    m_transparencyType->setToolTip(tr("Kind of transparency"));
    m_transparencyActions << addWidget(m_transparencyType);
    m_transparencyStart = addIntField(tr("Start"), tr("Transparency where the gradient starts (or of the whole object)"), 0, 100,
                                      m_transparencyActions);
    m_transparencyEnd = addIntField(tr("End"), tr("Transparency where the gradient ends"), 0, 100, m_transparencyActions);
    m_transparencyStart->setSuffix(QStringLiteral(" %"));
    m_transparencyEnd->setSuffix(QStringLiteral(" %"));
    connect(m_transparencyType, &QComboBox::activated, this, &PropertyBar::applyTransparency);
    connect(m_transparencyStart, &QSpinBox::editingFinished, this, &PropertyBar::applyTransparency);
    connect(m_transparencyEnd, &QSpinBox::editingFinished, this, &PropertyBar::applyTransparency);
}

void PropertyBar::setCanvas(CanvasWidget *canvas)
{
    m_canvas = canvas;
    connect(canvas, &CanvasWidget::selectionChanged, this, &PropertyBar::refreshEffects);
    refreshEffects();
}

void PropertyBar::applyShadow(const std::function<void(DropShadow &)> &change)
{
    if (!m_canvas || m_updatingEffects)
        return;
    m_canvas->restyle(tr("Drop shadow"), [&change](Style &style) {
        DropShadow shadow = style.shadow.value_or(DropShadow{});
        change(shadow);
        style.shadow = shadow;
    });
    refreshEffects();
}

void PropertyBar::applyTransparency()
{
    if (!m_canvas || m_updatingEffects)
        return;
    const int type = m_transparencyType->currentIndex();
    const int startAlpha = qRound(255 * (1.0 - m_transparencyStart->value() / 100.0));
    const int endAlpha = qRound(255 * (1.0 - m_transparencyEnd->value() / 100.0));
    m_canvas->restyle(tr("Transparency"), [=](Style &style) {
        switch (type) {
        case 0: // none
            style.transparency.reset();
            style.opacity = 1.0;
            break;
        case 1: // uniform
            style.transparency.reset();
            style.opacity = startAlpha / 255.0;
            break;
        default: {
            const bool radial = type == 3;
            Gradient g = style.transparency.value_or(Gradient{});
            if (!style.transparency || (g.type == Gradient::Type::Radial) != radial) {
                g = radial ? Gradient::radial(Qt::black, Qt::black) : Gradient::linear(Qt::black, Qt::black, 0);
            }
            if (g.stops.size() < 2)
                g.stops = {{0.0, Qt::black}, {1.0, Qt::black}};
            g.stops.front().color.setAlpha(startAlpha);
            g.stops.back().color.setAlpha(endAlpha);
            style.transparency = g;
            style.opacity = 1.0;
            break;
        }
        }
    });
    refreshEffects();
}

void PropertyBar::refreshEffects()
{
    if (!m_canvas)
        return;
    m_updatingEffects = true;
    const Style &style = m_canvas->currentStyle();
    const DropShadow shadow = style.shadow.value_or(DropShadow{});
    setValueQuietly(m_shadowX, units().toDisplay(shadow.offset.x()));
    setValueQuietly(m_shadowY, units().toDisplay(shadow.offset.y()));
    setValueQuietly(m_shadowBlur, units().toDisplay(shadow.blur));
    {
        const QSignalBlocker blocker(m_shadowOpacity);
        m_shadowOpacity->setValue(qRound(shadow.color.alphaF() * 100));
    }
    QPixmap swatch(28, 16);
    swatch.fill(QColor(shadow.color.red(), shadow.color.green(), shadow.color.blue()));
    m_shadowColor->setIcon(QIcon(swatch));

    int type = 0;
    int start = 0, end = 0;
    if (style.transparency && style.transparency->stops.size() >= 2) {
        type = style.transparency->type == Gradient::Type::Radial ? 3 : 2;
        start = qRound(100 - style.transparency->stops.front().color.alphaF() * 100);
        end = qRound(100 - style.transparency->stops.back().color.alphaF() * 100);
    } else if (style.opacity < 1.0) {
        type = 1;
        start = qRound((1.0 - style.opacity) * 100);
    } else {
        end = 100; // what a new gradient starts with
    }
    const QSignalBlocker b1(m_transparencyType), b2(m_transparencyStart), b3(m_transparencyEnd);
    m_transparencyType->setCurrentIndex(type);
    m_transparencyStart->setValue(start);
    m_transparencyEnd->setValue(end);
    m_transparencyEnd->setEnabled(type >= 2);
    m_updatingEffects = false;
}

QSpinBox *PropertyBar::addIntField(const QString &label, const QString &toolTip, int minimum, int maximum,
                                   QList<QAction *> &group)
{
    auto *caption = new QLabel(label, this);
    caption->setContentsMargins(6, 0, 3, 0);
    group << addWidget(caption);
    auto *box = new QSpinBox(this);
    box->setRange(minimum, maximum);
    box->setToolTip(toolTip);
    box->setMinimumWidth(60);
    group << addWidget(box);
    return box;
}

void PropertyBar::setToolSettings(const ToolSettings &settings)
{
    m_toolSettings = settings;
    m_updatingToolFields = true;
    m_polygonSides->setValue(settings.polygonSides);
    m_starPoints->setValue(settings.starPoints);
    m_starSharpness->setValue(int(std::lround(settings.starSharpness)));
    m_spiralRevolutions->setValue(settings.spiralRevolutions);
    m_graphColumns->setValue(settings.graphColumns);
    m_graphRows->setValue(settings.graphRows);
    m_newCornerRadius->setValue(units().toDisplay(settings.cornerRadius));
    m_updatingToolFields = false;
}

void PropertyBar::emitToolSettings()
{
    if (m_updatingToolFields)
        return;
    m_toolSettings.polygonSides = m_polygonSides->value();
    m_toolSettings.starPoints = m_starPoints->value();
    m_toolSettings.starSharpness = m_starSharpness->value();
    m_toolSettings.spiralRevolutions = m_spiralRevolutions->value();
    m_toolSettings.graphColumns = m_graphColumns->value();
    m_toolSettings.graphRows = m_graphRows->value();
    m_toolSettings.cornerRadius = units().fromDisplay(m_newCornerRadius->value());
    emit toolSettingsChanged(m_toolSettings);
}

void PropertyBar::setEyedropperColor(const QColor &color)
{
    m_eyedropperSwatch->setStyleSheet(QStringLiteral("background: %1;").arg(color.name()));
    m_eyedropperSwatch->setToolTip(color.name().toUpper());
}

void PropertyBar::setTool(CanvasWidget::Tool tool)
{
    m_tool = tool;
    updateMode();
}

QList<QAction *> *PropertyBar::toolGroup()
{
    switch (m_tool) {
    case CanvasWidget::Tool::Rectangle:
        return &m_rectangleToolActions;
    case CanvasWidget::Tool::Polygon:
        return &m_polygonActions;
    case CanvasWidget::Tool::Star:
        return &m_starActions;
    case CanvasWidget::Tool::Spiral:
        return &m_spiralActions;
    case CanvasWidget::Tool::GraphPaper:
        return &m_graphActions;
    case CanvasWidget::Tool::Eyedropper:
        return &m_eyedropperActions;
    case CanvasWidget::Tool::DropShadow:
        return &m_shadowActions;
    case CanvasWidget::Tool::Transparency:
        return &m_transparencyActions;
    default:
        return nullptr;
    }
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

// With a selection, the object's properties; otherwise the active tool's options, or the
// page. The eyedropper's options show whatever is selected.
void PropertyBar::updateMode()
{
    QList<QAction *> *tool = toolGroup();
    // These tools act on the selection, so their options show with it.
    const bool always = tool == &m_eyedropperActions || tool == &m_shadowActions || tool == &m_transparencyActions;
    const bool showTool = tool && (always || !m_anySelected);
    const bool showObject = m_anySelected && !always;
    const bool showPage = !m_anySelected && !showTool;
    for (QList<QAction *> *group : {&m_rectangleToolActions, &m_polygonActions, &m_starActions, &m_spiralActions,
                                    &m_graphActions, &m_eyedropperActions, &m_shadowActions, &m_transparencyActions}) {
        for (QAction *action : std::as_const(*group))
            action->setVisible(showTool && group == tool);
    }
    for (QAction *action : std::as_const(m_pageActions))
        action->setVisible(showPage);
    for (QAction *action : std::as_const(m_objectActions))
        action->setVisible(showObject);
    const bool rectangle = m_shape && m_shape->kind() == ShapeKind::Rectangle;
    for (QAction *action : std::as_const(m_cornerActions))
        action->setVisible(showObject && rectangle);
    const bool curve = m_shape && m_shape->kind() == ShapeKind::Path;
    for (QAction *action : std::as_const(m_lineActions))
        action->setVisible(showObject && curve);
    refresh();
}

// Suffix, decimals and values follow the display unit.
void PropertyBar::updateUnits()
{
    const QString suffix = QLatin1Char(' ') + units().suffix();
    for (QDoubleSpinBox *box : {m_pageWidth, m_pageHeight, m_nudge, m_duplicateX, m_duplicateY, m_x, m_y, m_width,
                                m_height, m_outline, m_cornerRadius, m_newCornerRadius, m_shadowX, m_shadowY, m_shadowBlur}) {
        box->setSuffix(suffix);
        box->setDecimals(units().decimals());
    }
    m_updatingToolFields = true;
    m_newCornerRadius->setValue(units().toDisplay(m_toolSettings.cornerRadius));
    m_updatingToolFields = false;
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
    refreshEffects();
    if (!m_anySelected) {
        refreshPage();
        return;
    }
    const bool enabled = m_shape != nullptr;
    for (QDoubleSpinBox *box : {m_x, m_y, m_width, m_height, m_outline, m_opacity, m_angle}) {
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
    m_shownAngle = m_shape->rotation();
    setValueQuietly(m_angle, m_shownAngle);
    if (m_shape->kind() == ShapeKind::Rectangle)
        setValueQuietly(m_cornerRadius, units().toDisplay(m_shape->cornerRadius()));
    const QSignalBlocker b1(m_startArrow), b2(m_endArrow);
    m_startArrow->setCurrentIndex(m_startArrow->findData(int(m_shape->style().startArrow)));
    m_endArrow->setCurrentIndex(m_endArrow->findData(int(m_shape->style().endArrow)));
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
