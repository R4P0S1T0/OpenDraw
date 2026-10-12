#include "PropertiesPanel.h"

#include "CanvasWidget.h"
#include "ColorPalette.h"
#include "Icons.h"
#include "Units.h"
#include "core/Document.h"
#include "core/Render.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

namespace opendraw {

namespace {

enum FillType { NoFill, Uniform, Linear, Radial, Pattern };

FillType fillTypeOf(const Style &style)
{
    if (style.pattern)
        return Pattern;
    if (style.gradient)
        return style.gradient->type == Gradient::Type::Radial ? Radial : Linear;
    return style.fill.alpha() > 0 ? Uniform : NoFill;
}

QIcon swatch(const QBrush &brush, bool none)
{
    QPixmap pixmap(36, 18);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setPen(QPen(QColor(0, 0, 0, 140), 1));
    p.setBrush(none ? QBrush(Qt::white) : brush);
    p.drawRect(QRectF(0.5, 0.5, 35, 17));
    if (none) {
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor(0xd0, 0x30, 0x30), 1.5));
        p.drawLine(QPointF(3, 15), QPointF(33, 3));
    }
    return QIcon(pixmap);
}

// A short line drawn with a dash pattern, for the outline style list.
QIcon dashIcon(const std::vector<double> &dashes, const QColor &ink)
{
    QPixmap pixmap(96, 14);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    Style style;
    style.stroke = ink;
    style.strokeWidth = 2.5;
    style.dashes = dashes;
    p.setPen(render::outlinePen(style));
    p.drawLine(QPointF(3, 7), QPointF(93, 7));
    return QIcon(pixmap);
}

// A short line with an arrowhead at one end, for the arrowhead lists.
QIcon arrowIcon(Arrowhead type, bool atStart, const QColor &ink)
{
    QPixmap pixmap(64, 16);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    const double width = 1.5;
    const QPointF from(atStart ? 50 : 6, 8), to(atStart ? 18 : 46, 8);
    p.setPen(QPen(ink, width, Qt::SolidLine, Qt::FlatCap));
    p.drawLine(from, to);
    p.setPen(Qt::NoPen);
    p.setBrush(ink);
    p.drawPath(render::arrowheadPath(type, to, to - from, width));
    return QIcon(pixmap);
}

QToolButton *iconButton(Icon id, const QString &toolTip, QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setIcon(icon(id));
    button->setIconSize(QSize(22, 22));
    button->setToolTip(toolTip);
    button->setCheckable(true);
    button->setAutoRaise(true);
    return button;
}

QLabel *caption(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    QFont bold = label->font();
    bold.setBold(true);
    label->setFont(bold);
    return label;
}

} // namespace

const std::vector<std::vector<double>> &PropertiesPanel::dashPresets()
{
    static const std::vector<std::vector<double>> presets = {
        {}, {4, 2}, {1, 2}, {4, 2, 1, 2}, {4, 2, 1, 2, 1, 2}, {8, 3},
    };
    return presets;
}

void PropertiesPanel::addArrowheads(QComboBox *box, bool atStart)
{
    const std::pair<Arrowhead, QString> kinds[] = {
        {Arrowhead::None, tr("None")},         {Arrowhead::Triangle, tr("Triangle")},
        {Arrowhead::Stealth, tr("Notched")},   {Arrowhead::Open, tr("Open")},
        {Arrowhead::Circle, tr("Circle")},     {Arrowhead::Square, tr("Square")},
        {Arrowhead::Diamond, tr("Diamond")},   {Arrowhead::Bar, tr("Bar")},
    };
    box->setIconSize(QSize(64, 16));
    const QColor ink = box->palette().color(QPalette::WindowText);
    for (const auto &[type, name] : kinds) {
        box->addItem(arrowIcon(type, atStart, ink), QString(), int(type));
        box->setItemData(box->count() - 1, name, Qt::ToolTipRole);
    }
    box->setToolTip(atStart ? tr("Start arrowhead") : tr("End arrowhead"));
}

PropertiesPanel::PropertiesPanel(Document *document, CanvasWidget *canvas, QWidget *parent)
    : QWidget(parent)
    , m_document(document)
    , m_canvas(canvas)
{
    // Section buttons along the top, as tabs with icons.
    m_sections = new QButtonGroup(this);
    auto *tabs = new QHBoxLayout;
    tabs->setContentsMargins(0, 0, 0, 0);
    const std::tuple<Icon, QString, Section> sections[] = {
        {Icon::OutlinePen, tr("Outline"), Section::Outline},
        {Icon::FillBucket, tr("Fill"), Section::Fill},
        {Icon::Transparency, tr("Transparency"), Section::Transparency},
        {Icon::AlignJustify, tr("Paragraph"), Section::Paragraph},
    };
    for (const auto &[iconId, text, section] : sections) {
        QToolButton *button = iconButton(iconId, text, this);
        button->setIconSize(QSize(24, 24));
        m_sections->addButton(button, int(section));
        tabs->addWidget(button);
    }
    tabs->addStretch();

    m_pages = new QStackedWidget(this);
    m_pages->addWidget(createOutlinePage());
    m_pages->addWidget(createFillPage());
    m_pages->addWidget(createTransparencyPage());
    m_pages->addWidget(createParagraphPage());

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->addLayout(tabs);
    layout->addWidget(m_pages);
    layout->addStretch();

    connect(m_sections, &QButtonGroup::idClicked, this, [this](int id) { showSection(Section(id)); });
    showSection(Section::Fill);

    connect(m_canvas, &CanvasWidget::selectionChanged, this, &PropertiesPanel::refresh);
    connect(m_canvas, &CanvasWidget::defaultStyleChanged, this, &PropertiesPanel::refresh);
    connect(m_document, &Document::changed, this, &PropertiesPanel::refresh);
    connect(m_canvas, &CanvasWidget::textEditingChanged, this, &PropertiesPanel::refresh);
    connect(&Units::instance(), &Units::unitChanged, this, &PropertiesPanel::updateUnits);
    updateUnits();
    refresh();
}

void PropertiesPanel::setDocument(Document *document)
{
    if (document == m_document)
        return;
    m_document->disconnect(this);
    m_document = document;
    connect(m_document, &Document::changed, this, &PropertiesPanel::refresh);
    refresh();
}

void PropertiesPanel::showSection(Section section)
{
    m_pages->setCurrentIndex(int(section));
    m_sections->button(int(section))->setChecked(true);
}

QToolButton *PropertiesPanel::colorButton(const QString &toolTip)
{
    auto *button = new QToolButton(this);
    button->setIconSize(QSize(36, 18));
    button->setToolTip(toolTip);
    return button;
}

void PropertiesPanel::apply(const QString &text, const std::function<void(Style &)> &change)
{
    if (m_updating)
        return;
    m_canvas->restyle(text, change);
    refresh();
}

QWidget *PropertiesPanel::createFillPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 6, 0, 0);
    layout->addWidget(caption(tr("Fill"), page));

    m_fillTypes = new QButtonGroup(this);
    auto *types = new QHBoxLayout;
    const std::tuple<Icon, QString, FillType> fillTypes[] = {
        {Icon::FillNone, tr("No fill"), NoFill},
        {Icon::FillUniform, tr("Uniform fill"), Uniform},
        {Icon::FillLinear, tr("Linear gradient"), Linear},
        {Icon::FillRadial, tr("Radial gradient"), Radial},
    };
    for (const auto &[iconId, text, type] : fillTypes) {
        QToolButton *button = iconButton(iconId, text, page);
        m_fillTypes->addButton(button, type);
        types->addWidget(button);
    }
    types->addStretch();
    layout->addLayout(types);

    auto *form = new QFormLayout;
    m_fillColor = colorButton(tr("Fill colour"));
    m_fillEndColor = colorButton(tr("End colour of the gradient"));
    m_fillAngle = new QSpinBox(page);
    m_fillAngle->setRange(0, 359);
    m_fillAngle->setWrapping(true);
    m_fillAngle->setSuffix(QStringLiteral("°"));
    m_fillAngle->setKeyboardTracking(false);
    m_fillEndLabel = new QLabel(tr("End colour:"), page);
    m_fillAngleLabel = new QLabel(tr("Angle:"), page);
    form->addRow(tr("Colour:"), m_fillColor);
    form->addRow(m_fillEndLabel, m_fillEndColor);
    form->addRow(m_fillAngleLabel, m_fillAngle);
    layout->addLayout(form);
    m_patternNote = new QLabel(tr("Bitmap pattern (pick another fill type to replace it)"), page);
    m_patternNote->setWordWrap(true);
    layout->addWidget(m_patternNote);

    m_fillPalette = new ColorPalette(Qt::Horizontal, page);
    m_fillPalette->setColors(ColorPalette::defaultColors());
    m_fillPalette->setToolTip(tr("Click a colour for a uniform fill"));
    layout->addWidget(m_fillPalette);

    connect(m_fillTypes, &QButtonGroup::idClicked, this, &PropertiesPanel::setFillType);
    connect(m_fillColor, &QToolButton::clicked, this, [this] { chooseFillColor(false); });
    connect(m_fillEndColor, &QToolButton::clicked, this, [this] { chooseFillColor(true); });
    connect(m_fillAngle, &QSpinBox::valueChanged, this, [this](int angle) {
        apply(tr("Gradient angle"), [angle](Style &style) {
            if (style.gradient && style.gradient->type == Gradient::Type::Linear) {
                Gradient g = Gradient::linear(Qt::black, Qt::black, angle);
                style.gradient->start = g.start;
                style.gradient->end = g.end;
            }
        });
    });
    connect(m_fillPalette, &ColorPalette::fillChosen, this, [this](const QColor &color) {
        apply(tr("Fill"), [color](Style &style) {
            style.fill = color;
            style.gradient.reset();
            style.pattern.reset();
        });
    });
    return page;
}

void PropertiesPanel::setFillType(int type)
{
    const Style current = m_canvas->currentStyle();
    const QColor first = fillTypeOf(current) == Linear || fillTypeOf(current) == Radial
                             ? current.gradient->stops.front().color
                             : (current.fill.alpha() > 0 ? current.fill : QColor(Qt::black));
    const QColor second = current.gradient && current.gradient->stops.size() > 1 ? current.gradient->stops.back().color
                                                                                  : QColor(Qt::white);
    const int angle = m_fillAngle->value();
    apply(tr("Fill"), [type, first, second, angle](Style &style) {
        style.pattern.reset();
        switch (type) {
        case NoFill:
            style.gradient.reset();
            style.fill = Qt::transparent;
            break;
        case Uniform:
            style.gradient.reset();
            style.fill = first;
            break;
        case Linear:
            style.gradient = Gradient::linear(first, second, angle);
            break;
        case Radial:
            style.gradient = Gradient::radial(first, second);
            break;
        }
    });
}

void PropertiesPanel::chooseFillColor(bool second)
{
    const Style current = m_canvas->currentStyle();
    QColor initial = current.fill;
    if (current.gradient && !current.gradient->stops.empty())
        initial = second ? current.gradient->stops.back().color : current.gradient->stops.front().color;
    const QColor color = QColorDialog::getColor(initial.alpha() > 0 ? initial : QColor(Qt::black), this,
                                                second ? tr("End colour") : tr("Fill colour"),
                                                QColorDialog::ShowAlphaChannel);
    if (!color.isValid())
        return;
    apply(tr("Fill"), [color, second](Style &style) {
        style.pattern.reset();
        if (style.gradient && !style.gradient->stops.empty()) {
            (second ? style.gradient->stops.back() : style.gradient->stops.front()).color = color;
        } else {
            style.fill = color;
        }
    });
}

QWidget *PropertiesPanel::createOutlinePage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 6, 0, 0);
    layout->addWidget(caption(tr("Outline"), page));

    auto *form = new QFormLayout;
    m_outlineColor = colorButton(tr("Outline colour"));
    auto *noOutline = new QToolButton(page);
    noOutline->setIcon(icon(Icon::FillNone));
    noOutline->setToolTip(tr("No outline"));
    noOutline->setAutoRaise(true);
    auto *colorRow = new QHBoxLayout;
    colorRow->addWidget(m_outlineColor);
    colorRow->addWidget(noOutline);
    colorRow->addStretch();
    form->addRow(tr("Colour:"), colorRow);

    m_outlineWidth = new QDoubleSpinBox(page);
    m_outlineWidth->setRange(0.0, 100000.0);
    m_outlineWidth->setKeyboardTracking(false);
    m_outlineWidth->setToolTip(tr("Outline width (0 = no outline)"));
    form->addRow(tr("Width:"), m_outlineWidth);

    m_dashes = new QComboBox(page);
    m_dashes->setIconSize(QSize(96, 14));
    const QColor ink = palette().color(QPalette::WindowText);
    for (std::size_t i = 0; i < dashPresets().size(); ++i)
        m_dashes->addItem(dashIcon(dashPresets()[i], ink), QString(), int(i));
    m_dashes->addItem(tr("Custom"), -1);
    m_dashes->setToolTip(tr("Line style"));
    form->addRow(tr("Style:"), m_dashes);

    auto addChoice = [page](QButtonGroup *group, QHBoxLayout *row, Icon id, const QString &tip, int value) {
        QToolButton *button = iconButton(id, tip, page);
        group->addButton(button, value);
        row->addWidget(button);
    };
    m_caps = new QButtonGroup(this);
    auto *capRow = new QHBoxLayout;
    addChoice(m_caps, capRow, Icon::CapButt, tr("Flat ends"), int(LineCap::Butt));
    addChoice(m_caps, capRow, Icon::CapRound, tr("Round ends"), int(LineCap::Round));
    addChoice(m_caps, capRow, Icon::CapSquare, tr("Square ends"), int(LineCap::Square));
    capRow->addStretch();
    form->addRow(tr("Ends:"), capRow);
    m_joins = new QButtonGroup(this);
    auto *joinRow = new QHBoxLayout;
    addChoice(m_joins, joinRow, Icon::JoinMiter, tr("Sharp corners"), int(LineJoin::Miter));
    addChoice(m_joins, joinRow, Icon::JoinRound, tr("Round corners"), int(LineJoin::Round));
    addChoice(m_joins, joinRow, Icon::JoinBevel, tr("Bevelled corners"), int(LineJoin::Bevel));
    joinRow->addStretch();
    form->addRow(tr("Corners:"), joinRow);

    m_startArrow = new QComboBox(page);
    m_endArrow = new QComboBox(page);
    addArrowheads(m_startArrow, true);
    addArrowheads(m_endArrow, false);
    auto *arrowRow = new QHBoxLayout;
    arrowRow->addWidget(m_startArrow);
    arrowRow->addWidget(m_endArrow);
    arrowRow->addStretch();
    form->addRow(tr("Arrows:"), arrowRow);
    layout->addLayout(form);

    m_strokeBehind = new QCheckBox(tr("Behind fill"), page);
    m_strokeBehind->setToolTip(tr("Draw the outline under the fill, which hides its inner half"));
    m_scaleStroke = new QCheckBox(tr("Scale with object"), page);
    m_scaleStroke->setToolTip(tr("Resizing the object resizes its outline too"));
    layout->addWidget(m_strokeBehind);
    layout->addWidget(m_scaleStroke);

    m_outlinePalette = new ColorPalette(Qt::Horizontal, page);
    m_outlinePalette->setColors(ColorPalette::defaultColors());
    m_outlinePalette->setToolTip(tr("Click a colour for the outline"));
    layout->addWidget(m_outlinePalette);

    auto setColor = [this](const QColor &color) {
        apply(tr("Outline"), [color](Style &style) {
            style.stroke = color;
            if (color.alpha() > 0 && style.strokeWidth <= 0.0)
                style.strokeWidth = 1.0;
        });
    };
    connect(m_outlineColor, &QToolButton::clicked, this, [this, setColor] {
        const Style current = m_canvas->currentStyle();
        const QColor color = QColorDialog::getColor(current.stroke.alpha() > 0 ? current.stroke : QColor(Qt::black),
                                                    this, tr("Outline colour"), QColorDialog::ShowAlphaChannel);
        if (color.isValid())
            setColor(color);
    });
    connect(noOutline, &QToolButton::clicked, this, [setColor] { setColor(Qt::transparent); });
    connect(m_outlinePalette, &ColorPalette::fillChosen, this, setColor);
    connect(m_outlinePalette, &ColorPalette::outlineChosen, this, setColor);
    connect(m_outlineWidth, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        const double width = Units::instance().fromDisplay(value);
        apply(tr("Outline width"), [width](Style &style) {
            style.strokeWidth = width;
            if (width > 0.0 && style.stroke.alpha() == 0)
                style.stroke = Qt::black; // giving a width to "no outline" means the user wants one
        });
    });
    connect(m_dashes, &QComboBox::activated, this, [this](int index) {
        const int preset = m_dashes->itemData(index).toInt();
        if (preset < 0)
            return;
        const std::vector<double> dashes = dashPresets()[preset];
        apply(tr("Line style"), [dashes](Style &style) { style.dashes = dashes; });
    });
    connect(m_caps, &QButtonGroup::idClicked, this, [this](int id) {
        apply(tr("Line ends"), [id](Style &style) { style.cap = LineCap(id); });
    });
    connect(m_joins, &QButtonGroup::idClicked, this, [this](int id) {
        apply(tr("Line corners"), [id](Style &style) { style.join = LineJoin(id); });
    });
    connect(m_startArrow, &QComboBox::activated, this, [this](int index) {
        const auto type = Arrowhead(m_startArrow->itemData(index).toInt());
        apply(tr("Arrowhead"), [type](Style &style) { style.startArrow = type; });
    });
    connect(m_endArrow, &QComboBox::activated, this, [this](int index) {
        const auto type = Arrowhead(m_endArrow->itemData(index).toInt());
        apply(tr("Arrowhead"), [type](Style &style) { style.endArrow = type; });
    });
    connect(m_strokeBehind, &QCheckBox::toggled, this, [this](bool on) {
        apply(tr("Outline behind fill"), [on](Style &style) { style.strokeBehind = on; });
    });
    connect(m_scaleStroke, &QCheckBox::toggled, this, [this](bool on) {
        apply(tr("Scale outline with object"), [on](Style &style) { style.scaleStroke = on; });
    });
    return page;
}

QWidget *PropertiesPanel::createTransparencyPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 6, 0, 0);
    layout->addWidget(caption(tr("Transparency"), page));
    auto *row = new QHBoxLayout;
    m_transparencySlider = new QSlider(Qt::Horizontal, page);
    m_transparencySlider->setRange(0, 100);
    m_transparency = new QSpinBox(page);
    m_transparency->setRange(0, 100);
    m_transparency->setSuffix(QStringLiteral(" %"));
    m_transparency->setKeyboardTracking(false);
    row->addWidget(m_transparencySlider, 1);
    row->addWidget(m_transparency);
    layout->addLayout(row);
    auto *hint = new QLabel(tr("0 % is solid, 100 % invisible. Applies to the whole object, fill and outline."), page);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto set = [this](int percent) {
        apply(tr("Transparency"), [percent](Style &style) { style.opacity = 1.0 - percent / 100.0; });
    };
    // The slider applies when released, so dragging it is one undoable step.
    connect(m_transparencySlider, &QSlider::valueChanged, m_transparency, [this](int v) {
        const QSignalBlocker blocker(m_transparency);
        m_transparency->setValue(v);
    });
    connect(m_transparencySlider, &QSlider::sliderReleased, this, [this, set] { set(m_transparencySlider->value()); });
    connect(m_transparencySlider, &QSlider::actionTriggered, this, [this, set](int action) {
        if (action != QAbstractSlider::SliderMove)
            QMetaObject::invokeMethod(this, [this, set] { set(m_transparencySlider->value()); }, Qt::QueuedConnection);
    });
    connect(m_transparency, &QSpinBox::valueChanged, this, set);
    return page;
}

QWidget *PropertiesPanel::createParagraphPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 6, 0, 0);
    layout->addWidget(caption(tr("Paragraph"), page));

    m_aligns = new QButtonGroup(this);
    auto *alignRow = new QHBoxLayout;
    const std::tuple<Icon, QString, TextAlign> aligns[] = {
        {Icon::AlignLeft, tr("Align left"), TextAlign::Left},
        {Icon::AlignCenter, tr("Center"), TextAlign::Center},
        {Icon::AlignRight, tr("Align right"), TextAlign::Right},
        {Icon::AlignJustify, tr("Justify (paragraph text)"), TextAlign::Justify},
    };
    for (const auto &[iconId, text, align] : aligns) {
        QToolButton *button = iconButton(iconId, text, page);
        m_aligns->addButton(button, int(align));
        alignRow->addWidget(button);
    }
    alignRow->addStretch();
    layout->addLayout(alignRow);

    auto *form = new QFormLayout;
    auto percentBox = [page](double maximum, const QString &tip) {
        auto *box = new QSpinBox(page);
        box->setRange(0, int(maximum));
        box->setSuffix(QStringLiteral(" %"));
        box->setKeyboardTracking(false);
        box->setToolTip(tip);
        return box;
    };
    m_lineSpacing = percentBox(1000, tr("Distance between lines, in percent of the font size"));
    m_lineSpacing->setMinimum(10);
    m_spaceBefore = percentBox(1000, tr("Extra space above each paragraph, in percent of the font size"));
    m_spaceAfter = percentBox(1000, tr("Extra space below each paragraph, in percent of the font size"));
    form->addRow(tr("Line spacing:"), m_lineSpacing);
    form->addRow(tr("Before paragraph:"), m_spaceBefore);
    form->addRow(tr("After paragraph:"), m_spaceAfter);
    auto lengthBox = [page](const QString &tip) {
        auto *box = new QDoubleSpinBox(page);
        box->setRange(-100000.0, 100000.0);
        box->setKeyboardTracking(false);
        box->setToolTip(tip);
        return box;
    };
    m_firstIndent = lengthBox(tr("Where the first line of each paragraph starts, from the left of the frame"));
    m_leftIndent = lengthBox(tr("Where the other lines start, from the left of the frame"));
    m_rightIndent = lengthBox(tr("Space kept free at the right of the frame"));
    form->addRow(tr("First line indent:"), m_firstIndent);
    form->addRow(tr("Left indent:"), m_leftIndent);
    form->addRow(tr("Right indent:"), m_rightIndent);
    layout->addLayout(form);
    auto *hint = new QLabel(tr("Indents and justification apply to paragraph text (a frame drawn with the Text tool)."), page);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    connect(m_aligns, &QButtonGroup::idClicked, this,
            [this](int id) { applyParagraph([id](TextFormat &f) { f.align = TextAlign(id); }); });
    connect(m_lineSpacing, &QSpinBox::valueChanged, this,
            [this](int v) { applyParagraph([v](TextFormat &f) { f.lineSpacing = v / 100.0; }); });
    connect(m_spaceBefore, &QSpinBox::valueChanged, this,
            [this](int v) { applyParagraph([v](TextFormat &f) { f.spaceBefore = v / 100.0; }); });
    connect(m_spaceAfter, &QSpinBox::valueChanged, this,
            [this](int v) { applyParagraph([v](TextFormat &f) { f.spaceAfter = v / 100.0; }); });
    const std::pair<QDoubleSpinBox *, double TextFormat::*> indents[] = {
        {m_firstIndent, &TextFormat::firstIndent},
        {m_leftIndent, &TextFormat::leftIndent},
        {m_rightIndent, &TextFormat::rightIndent},
    };
    for (const auto &[box, field] : indents) {
        connect(box, &QDoubleSpinBox::valueChanged, this, [this, field = field](double value) {
            const double length = Units::instance().fromDisplay(value);
            applyParagraph([field, length](TextFormat &f) { f.*field = length; });
        });
    }
    return page;
}

void PropertiesPanel::applyParagraph(const std::function<void(TextFormat &)> &change)
{
    if (m_updating)
        return;
    TextFormat format = m_canvas->currentTextFormat();
    change(format);
    m_canvas->setTextFormat(format);
    refresh();
}

void PropertiesPanel::updateUnits()
{
    const Units &units = Units::instance();
    for (QDoubleSpinBox *box : {m_outlineWidth, m_firstIndent, m_leftIndent, m_rightIndent}) {
        box->setSuffix(QLatin1Char(' ') + units.suffix());
        box->setDecimals(units.decimals());
        box->setSingleStep(units.unit() == Unit::Inches || units.unit() == Unit::Centimeters ? 0.01 : 0.1);
    }
    refresh();
}

void PropertiesPanel::refresh()
{
    if (m_updating)
        return;
    m_updating = true;
    const Style &style = m_canvas->currentStyle();

    // Fill
    const FillType type = fillTypeOf(style);
    if (QAbstractButton *button = m_fillTypes->button(type)) {
        button->setChecked(true);
    } else {
        m_fillTypes->setExclusive(false);
        for (QAbstractButton *b : m_fillTypes->buttons())
            b->setChecked(false);
        m_fillTypes->setExclusive(true);
    }
    const bool gradient = type == Linear || type == Radial;
    m_fillColor->setIcon(swatch(gradient ? QBrush(style.gradient->stops.front().color) : QBrush(style.fill),
                                type == NoFill || type == Pattern));
    m_fillColor->setEnabled(type != Pattern);
    m_fillEndColor->setVisible(gradient);
    m_fillEndLabel->setVisible(gradient);
    if (gradient)
        m_fillEndColor->setIcon(swatch(QBrush(style.gradient->stops.back().color), false));
    m_fillAngle->setVisible(type == Linear);
    m_fillAngleLabel->setVisible(type == Linear);
    if (type == Linear)
        m_fillAngle->setValue(int(std::lround(style.gradient->angle())) % 360);
    m_patternNote->setVisible(type == Pattern);

    // Outline
    m_outlineColor->setIcon(swatch(QBrush(style.stroke), !style.hasStroke()));
    m_outlineWidth->setValue(style.hasStroke() ? Units::instance().toDisplay(style.strokeWidth) : 0.0);
    int dashIndex = m_dashes->findData(-1);
    for (std::size_t i = 0; i < dashPresets().size(); ++i) {
        if (dashPresets()[i] == style.dashes)
            dashIndex = m_dashes->findData(int(i));
    }
    m_dashes->setCurrentIndex(dashIndex);
    m_caps->button(int(style.cap))->setChecked(true);
    m_joins->button(int(style.join))->setChecked(true);
    m_startArrow->setCurrentIndex(m_startArrow->findData(int(style.startArrow)));
    m_endArrow->setCurrentIndex(m_endArrow->findData(int(style.endArrow)));
    m_strokeBehind->setChecked(style.strokeBehind);
    m_scaleStroke->setChecked(style.scaleStroke);

    // Transparency
    const int percent = int(std::lround((1.0 - style.opacity) * 100.0));
    m_transparencySlider->setValue(percent);
    m_transparency->setValue(percent);

    // Paragraph
    const TextFormat text = m_canvas->currentTextFormat();
    m_aligns->button(int(text.align))->setChecked(true);
    m_lineSpacing->setValue(int(std::lround(text.lineSpacing * 100.0)));
    m_spaceBefore->setValue(int(std::lround(text.spaceBefore * 100.0)));
    m_spaceAfter->setValue(int(std::lround(text.spaceAfter * 100.0)));
    const Units &units = Units::instance();
    m_firstIndent->setValue(units.toDisplay(text.firstIndent));
    m_leftIndent->setValue(units.toDisplay(text.leftIndent));
    m_rightIndent->setValue(units.toDisplay(text.rightIndent));
    m_updating = false;
}

} // namespace opendraw
