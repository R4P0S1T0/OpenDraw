#include "CanvasWidget.h"

#include "core/Blend.h"
#include "core/Commands.h"
#include "Icons.h"
#include "core/Document.h"
#include "core/Envelope.h"
#include "core/Generators.h"
#include "core/PathOps.h"
#include "core/Render.h"
#include "core/RichText.h"
#include "core/SvgIO.h"
#include "core/TextLayout.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QUndoStack>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace opendraw {

namespace {
constexpr double kMinZoom = 0.02;
constexpr double kMaxZoom = 64.0;
constexpr double kZoomStep = 1.25;
constexpr double kHitTolerancePx = 4.0;
constexpr double kNodeHitPx = 6.0;
constexpr double kMinCreateSizePx = 3.0;
constexpr double kMinMarqueePx = 3.0;
constexpr double kPageMarginPx = 24.0;
constexpr double kHandleSizePx = 7.0;
constexpr double kNodeSizePx = 7.0;
constexpr double kCloseDistancePx = 8.0;
constexpr double kFreehandTolerancePx = 1.5;
constexpr double kSnapPx = 6.0;

double distance(const QPointF &a, const QPointF &b)
{
    return std::hypot(a.x() - b.x(), a.y() - b.y());
}

QRectF squareAround(const QPointF &center, double size)
{
    return {center - QPointF(size / 2, size / 2), QSizeF(size, size)};
}
} // namespace

CanvasWidget::CanvasWidget(Document *document, QUndoStack *undoStack, QWidget *parent)
    : QWidget(parent)
    , m_document(document)
    , m_undoStack(undoStack)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);

    connect(m_document, &Document::changed, this, qOverload<>(&QWidget::update));
    connect(m_document, &Document::shapeRemoved, this, [this](Shape *shape) {
        if (shape == m_editText && !m_pendingText) {
            m_editText = nullptr;
            emit textEditingChanged();
        }
        if (!isSelected(shape))
            return;
        std::vector<Shape *> remaining = m_selection;
        std::erase(remaining, shape);
        setSelection(std::move(remaining));
    });
    connect(m_document, &Document::cleared, this, [this] {
        setSelection({});
        m_bezier = {};
    });
    connect(m_document, &Document::currentPageChanged, this, [this] {
        finishTextEdit();
        m_bezier = {};
        setSelection({});
    });
    // Shapes on a layer that was just hidden or locked can no longer stay selected.
    connect(m_document, &Document::structureChanged, this, [this] {
        const std::vector<Shape *> editable = m_document->editableShapes();
        std::vector<Shape *> kept;
        for (Shape *shape : m_selection) {
            if (std::find(editable.begin(), editable.end(), shape) != editable.end())
                kept.push_back(shape);
        }
        setSelection(std::move(kept));
    });
}

void CanvasWidget::setTool(Tool tool)
{
    if (tool == m_tool)
        return;
    finishTextEdit();
    if (m_tool == Tool::Bezier)
        finishCurve();
    m_tool = tool;
    m_toolBeforeSpace.reset();
    setActiveNode({});
    updateCursor();
    update();
    emit toolChanged(tool);
}

void CanvasWidget::setDefaultStyle(const Style &style)
{
    m_defaultStyle = style;
    emit defaultStyleChanged();
}

const Style &CanvasWidget::currentStyle() const
{
    for (Shape *shape : m_selection) {
        const Style *found = nullptr;
        shape->forEachLeaf([&found](Shape &leaf) {
            if (!found && leaf.kind() != ShapeKind::Image)
                found = &leaf.style();
        });
        if (found)
            return *found;
    }
    return m_defaultStyle;
}

void CanvasWidget::restyle(const QString &text, const std::function<void(Style &)> &change)
{
    if (m_selection.empty()) {
        Style style = m_defaultStyle;
        change(style);
        setDefaultStyle(style);
        return;
    }
    m_undoStack->beginMacro(text);
    for (Shape *shape : m_selection) {
        shape->forEachLeaf([&](Shape &leaf) {
            Style style = leaf.style();
            change(style);
            m_undoStack->push(new SetStyleCommand(m_document, &leaf, style));
        });
    }
    m_undoStack->endMacro();
}

void CanvasWidget::setGridSettings(const GridSettings &settings)
{
    m_grid = settings;
    update();
}

void CanvasWidget::setGuidesVisible(bool visible)
{
    m_guidesVisible = visible;
    update();
}

void CanvasWidget::setEyedropperColor(const std::optional<QColor> &color)
{
    m_eyedropperColor = color;
    updateCursor();
    if (color)
        emit eyedropperColorChanged(*color);
}

// ---------------------------------------------------------------------------------------------
// Guidelines

int CanvasWidget::guideAt(const QPointF &widgetPosition) const
{
    if (!m_guidesVisible)
        return -1;
    const QPointF doc = toDocument(widgetPosition);
    const double tolerance = kHitTolerancePx / m_zoom;
    const auto &guides = m_document->guides();
    for (int i = int(guides.size()) - 1; i >= 0; --i) {
        const double along = guides[i].orientation == Qt::Vertical ? doc.x() : doc.y();
        if (std::abs(along - guides[i].position) <= tolerance)
            return i;
    }
    return -1;
}

void CanvasWidget::beginGuideDrag(Qt::Orientation orientation, const QPointF &widgetPosition)
{
    finishTextEdit();
    m_guide = -1;
    m_guideBefore.reset();
    m_draggedGuide = Guide{orientation, 0.0};
    m_drag = Drag::Guide;
    updateGuideDrag(widgetPosition);
}

void CanvasWidget::updateGuideDrag(const QPointF &widgetPosition)
{
    if (!m_draggedGuide)
        return;
    QPointF doc = toDocument(widgetPosition);
    if (m_snap.grid || m_snap.objects || m_snap.page) {
        SnapSettings withoutGuides = m_snap;
        withoutGuides.guides = false;
        std::swap(m_snap, withoutGuides);
        doc = snapPoint(doc); // a guide does not snap to itself
        std::swap(m_snap, withoutGuides);
    }
    m_draggedGuide->position = m_draggedGuide->orientation == Qt::Vertical ? doc.x() : doc.y();
    emit cursorMoved(toDocument(widgetPosition));
    update();
}

// Dropping a guide back on the rulers (outside the drawing area) deletes it.
void CanvasWidget::endGuideDrag(const QPointF &widgetPosition)
{
    if (!m_draggedGuide)
        return;
    updateGuideDrag(widgetPosition);
    const bool inside = rect().contains(widgetPosition.toPoint());
    const Guide guide = *m_draggedGuide;
    if (m_guide < 0) {
        if (inside)
            m_undoStack->push(new GuideCommand(m_document, int(m_document->guides().size()), std::nullopt, guide));
    } else if (!inside) {
        m_undoStack->push(new GuideCommand(m_document, m_guide, m_guideBefore, std::nullopt));
    } else if (m_guideBefore && !(guide == *m_guideBefore)) {
        m_undoStack->push(new GuideCommand(m_document, m_guide, m_guideBefore, guide));
    }
    m_draggedGuide.reset();
    m_guideBefore.reset();
    m_guide = -1;
    m_drag = Drag::None;
    update();
}

// ---------------------------------------------------------------------------------------------
// Snapping

std::vector<double> CanvasWidget::snapTargets(Qt::Orientation axis) const
{
    const bool x = axis == Qt::Horizontal;
    std::vector<double> targets;
    if (m_snap.guides && m_guidesVisible) {
        for (const Guide &guide : m_document->guides()) {
            if ((guide.orientation == Qt::Vertical) == x)
                targets.push_back(guide.position);
        }
    }
    if (m_snap.page) {
        const double size = x ? m_document->pageSize().width() : m_document->pageSize().height();
        targets.insert(targets.end(), {0.0, size / 2.0, size});
    }
    if (m_snap.objects) {
        for (const Shape *shape : m_document->editableShapes()) {
            if (isSelected(shape))
                continue;
            const QRectF b = shape->bounds();
            if (x)
                targets.insert(targets.end(), {b.left(), b.center().x(), b.right()});
            else
                targets.insert(targets.end(), {b.top(), b.center().y(), b.bottom()});
        }
    }
    return targets;
}

namespace {
// Smallest correction (within `reach`) that puts one of `values` on a target or a grid line.
std::optional<double> nearestSnap(std::initializer_list<double> values, const std::vector<double> &targets,
                                  double gridSpacing, double reach)
{
    std::optional<double> best;
    auto consider = [&](double correction) {
        if (std::abs(correction) <= reach && (!best || std::abs(correction) < std::abs(*best)))
            best = correction;
    };
    for (double value : values) {
        for (double target : targets)
            consider(target - value);
        if (gridSpacing > 0.0)
            consider(std::round(value / gridSpacing) * gridSpacing - value);
    }
    return best;
}
} // namespace

QPointF CanvasWidget::snapPoint(const QPointF &point) const
{
    const double reach = kSnapPx / m_zoom;
    const double grid = m_snap.grid ? m_grid.spacing : 0.0;
    QPointF snapped = point;
    if (auto dx = nearestSnap({point.x()}, snapTargets(Qt::Horizontal), grid, reach))
        snapped.rx() += *dx;
    if (auto dy = nearestSnap({point.y()}, snapTargets(Qt::Vertical), grid, reach))
        snapped.ry() += *dy;
    return snapped;
}

QPointF CanvasWidget::snapBox(const QRectF &box) const
{
    const double reach = kSnapPx / m_zoom;
    const double grid = m_snap.grid ? m_grid.spacing : 0.0;
    QPointF offset;
    if (auto dx = nearestSnap({box.left(), box.center().x(), box.right()}, snapTargets(Qt::Horizontal), grid, reach))
        offset.rx() = *dx;
    if (auto dy = nearestSnap({box.top(), box.center().y(), box.bottom()}, snapTargets(Qt::Vertical), grid, reach))
        offset.ry() = *dy;
    return offset;
}

// ---------------------------------------------------------------------------------------------
// Object tools and the eyedropper

QPainterPath CanvasWidget::toolShapePreview(const QRectF &box) const
{
    QPainterPath path;
    switch (m_tool) {
    case Tool::Ellipse:
        path.addEllipse(box);
        break;
    case Tool::Rectangle: {
        const double r = std::min({m_toolSettings.cornerRadius, box.width() / 2, box.height() / 2});
        path.addRoundedRect(box, r, r);
        break;
    }
    default:
        if (auto shape = createToolShape(box))
            path = shape->path();
        break;
    }
    return path;
}

std::unique_ptr<Shape> CanvasWidget::createToolShape(const QRectF &box) const
{
    switch (m_tool) {
    case Tool::Rectangle: {
        auto shape = std::make_unique<Shape>(ShapeKind::Rectangle, box, m_defaultStyle);
        shape->setCornerRadius(m_toolSettings.cornerRadius);
        return shape;
    }
    case Tool::Ellipse:
        return std::make_unique<Shape>(ShapeKind::Ellipse, box, m_defaultStyle);
    case Tool::Polygon:
        return std::make_unique<Shape>(generators::polygon(box, m_toolSettings.polygonSides), m_defaultStyle);
    case Tool::Star:
        return std::make_unique<Shape>(
            generators::star(box, m_toolSettings.starPoints, m_toolSettings.starSharpness), m_defaultStyle);
    case Tool::Spiral:
        return std::make_unique<Shape>(generators::spiral(box, m_toolSettings.spiralRevolutions), curveStyle(false));
    case Tool::GraphPaper:
        return generators::graphPaper(box, m_toolSettings.graphColumns, m_toolSettings.graphRows, m_defaultStyle);
    default:
        return nullptr;
    }
}

// The colour on screen at a point, drawing only the page and the objects (no handles).
QColor CanvasWidget::colorAt(const QPointF &widgetPosition) const
{
    QImage pixel(1, 1, QImage::Format_ARGB32_Premultiplied);
    pixel.fill(palette().color(QPalette::Mid));
    QPainter painter(&pixel);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate(-widgetPosition.x(), -widgetPosition.y());
    painter.setTransform(viewTransform(), true);
    painter.fillRect(QRectF(QPointF(0, 0), m_document->pageSize()), Qt::white);
    render::paintPage(painter, *m_document->currentPage());
    painter.end();
    return pixel.pixelColor(0, 0);
}

// Clicking inside an object gives it the picked colour as fill; clicking its outline, as outline.
void CanvasWidget::applyEyedropper(const QPointF &documentPosition)
{
    if (!m_eyedropperColor)
        return;
    Shape *hit = m_document->shapeAt(documentPosition, kHitTolerancePx / m_zoom);
    if (!hit)
        return;
    const QColor color = *m_eyedropperColor;
    const bool outline = !hit->path().contains(documentPosition);
    m_undoStack->beginMacro(outline ? tr("Apply outline colour") : tr("Apply fill colour"));
    hit->forEachLeaf([&](Shape &leaf) {
        Style style = leaf.style();
        if (outline) {
            style.stroke = color;
            if (style.strokeWidth <= 0.0)
                style.strokeWidth = 1.0;
        } else {
            style.fill = color;
            style.gradient.reset();
            style.pattern.reset();
        }
        m_undoStack->push(new SetStyleCommand(m_document, &leaf, style));
    });
    m_undoStack->endMacro();
}

void CanvasWidget::paintGrid(QPainter &painter)
{
    if (!m_grid.visible || m_grid.spacing <= 0.0)
        return;
    double spacing = m_grid.spacing;
    while (spacing * m_zoom < 8.0)
        spacing *= 2.0; // too dense to see: show every other line
    const QRectF visible = visibleDocumentRect();
    QColor color = palette().color(QPalette::Highlight);
    color.setAlpha(45);
    QPen pen(color, 0);
    pen.setCosmetic(true);
    painter.setPen(pen);
    for (double x = std::floor(visible.left() / spacing) * spacing; x <= visible.right(); x += spacing)
        painter.drawLine(QPointF(x, visible.top()), QPointF(x, visible.bottom()));
    for (double y = std::floor(visible.top() / spacing) * spacing; y <= visible.bottom(); y += spacing)
        painter.drawLine(QPointF(visible.left(), y), QPointF(visible.right(), y));
}

void CanvasWidget::paintGuides(QPainter &painter)
{
    if (!m_guidesVisible && !m_draggedGuide)
        return;
    const QRectF visible = visibleDocumentRect();
    auto draw = [&](const Guide &guide, const QColor &color) {
        QPen pen(color, 1, Qt::DashLine);
        pen.setCosmetic(true);
        painter.setPen(pen);
        if (guide.orientation == Qt::Vertical)
            painter.drawLine(QPointF(guide.position, visible.top()), QPointF(guide.position, visible.bottom()));
        else
            painter.drawLine(QPointF(visible.left(), guide.position), QPointF(visible.right(), guide.position));
    };
    const auto &guides = m_document->guides();
    if (m_guidesVisible) {
        for (int i = 0; i < int(guides.size()); ++i) {
            if (i != m_guide || !m_draggedGuide)
                draw(guides[i], QColor(0x2a, 0x7f, 0xff));
        }
    }
    if (m_draggedGuide)
        draw(*m_draggedGuide, QColor(0xe5, 0x39, 0x35));
}

void CanvasWidget::setSelectedShape(Shape *shape)
{
    setSelection(shape ? std::vector<Shape *>{shape} : std::vector<Shape *>{});
}

void CanvasWidget::setSelection(std::vector<Shape *> shapes)
{
    if (shapes == m_selection)
        return;
    m_selection = std::move(shapes);
    setActiveNode({});
    m_rotationCenter.reset();
    setRotateMode(false);
    update();
    emit selectionChanged();
}

// ---------------------------------------------------------------------------------------------
// Rotating and skewing (second click on the selection)

void CanvasWidget::setRotateMode(bool rotating)
{
    rotating = rotating && !m_selection.empty();
    if (rotating == m_rotateMode)
        return;
    m_rotateMode = rotating;
    update();
    emit transformModeChanged(rotating);
}

QPointF CanvasWidget::rotationCenter() const
{
    return m_rotationCenter.value_or(selectionBounds().center());
}

bool CanvasWidget::rotationCenterAt(const QPointF &widgetPosition) const
{
    return m_rotateMode && m_tool == Tool::Select && !m_selection.empty()
           && distance(toWidget(rotationCenter()), widgetPosition) <= kNodeHitPx + 2;
}

void CanvasWidget::applyLiveTransform(const QTransform &t)
{
    const QTransform delta = m_liveTransform.inverted() * t;
    for (Shape *shape : m_selection)
        shape->transform(delta);
    m_liveTransform = t;
    if (!m_selection.empty())
        m_document->notifyShapeChanged(m_selection.front());
}

void CanvasWidget::commitLiveTransform(const QString &text)
{
    const QTransform t = m_liveTransform;
    m_liveTransform = QTransform();
    if (t.isIdentity() || m_selection.empty())
        return;
    m_undoStack->beginMacro(text);
    for (Shape *shape : m_selection)
        m_undoStack->push(new TransformShapeCommand(m_document, shape, t, text, true));
    m_undoStack->endMacro();
}

void CanvasWidget::rotateSelection(double degrees)
{
    if (m_selection.empty() || std::abs(degrees) < 1e-9)
        return;
    const QPointF c = rotationCenter();
    QTransform t = QTransform::fromTranslate(-c.x(), -c.y());
    t *= QTransform().rotate(degrees);
    t *= QTransform::fromTranslate(c.x(), c.y());
    const QString text = tr("Rotate");
    m_undoStack->beginMacro(text);
    for (Shape *shape : m_selection)
        m_undoStack->push(new TransformShapeCommand(m_document, shape, t, text));
    m_undoStack->endMacro();
}

// ---------------------------------------------------------------------------------------------
// Shadow and transparency tools

void CanvasWidget::captureEffectStyles()
{
    m_effectBefore.clear();
    for (Shape *shape : m_selection) {
        shape->forEachLeaf([this](Shape &leaf) { m_effectBefore.emplace_back(&leaf, leaf.style()); });
    }
}

QPointF CanvasWidget::fromFraction(const Shape &shape, const QPointF &fraction)
{
    const QRectF box = shape.localBounds();
    return shape.shapeTransform().map(
        QPointF(box.left() + fraction.x() * box.width(), box.top() + fraction.y() * box.height()));
}

QPointF CanvasWidget::toFraction(const Shape &shape, const QPointF &documentPosition)
{
    const QRectF box = shape.localBounds();
    const QPointF local = shape.shapeTransform().inverted().map(documentPosition);
    return QPointF(box.width() > 0 ? (local.x() - box.left()) / box.width() : 0.0,
                   box.height() > 0 ? (local.y() - box.top()) / box.height() : 0.0);
}

// Dragging with the shadow tool sets the shadow's offset; with the transparency tool, a
// gradient from solid (where the drag starts) to transparent (where it ends).
void CanvasWidget::applyEffectDrag(const QPointF &documentPosition, Qt::KeyboardModifiers modifiers)
{
    QPointF delta = documentPosition - m_dragStart;
    if (modifiers & Qt::ControlModifier) { // Ctrl: horizontal or vertical
        if (std::abs(delta.x()) >= std::abs(delta.y()))
            delta.ry() = 0;
        else
            delta.rx() = 0;
    }
    for (const auto &[leaf, before] : m_effectBefore) {
        Style style = before;
        if (m_tool == Tool::DropShadow) {
            DropShadow shadow = before.shadow.value_or(DropShadow{});
            shadow.offset = delta;
            style.shadow = shadow;
        } else if (m_tool == Tool::Extrude) {
            // The back of the extrusion follows the pointer; the vanishing point lies beyond it.
            Extrusion e = before.extrusion.value_or(Extrusion{});
            e.depth = std::clamp(e.depth, 0.05, 0.95);
            e.vanishingPoint = delta / e.depth;
            style.extrusion = e;
        } else if (m_tool == Tool::Contour) {
            // Out of the object: rings outside, as far as the pointer; into it: rings inside.
            Contour c = before.contour.value_or(Contour{});
            const bool inside = leaf->path().contains(m_dragStart + delta);
            if (!inside)
                c.direction = Contour::Direction::Outside;
            else if (c.direction == Contour::Direction::Outside)
                c.direction = Contour::Direction::Inside;
            const double extent = std::hypot(delta.x(), delta.y());
            const int rings = c.direction == Contour::Direction::ToCenter ? 1 : std::max(c.steps, 1);
            c.offset = std::max(extent / rings, 0.5);
            style.contour = c;
        } else {
            Gradient g = before.transparency.value_or(Gradient{});
            if (!before.transparency || g.stops.size() < 2) {
                g.stops = {{0.0, QColor(0, 0, 0, 255)}, {1.0, QColor(0, 0, 0, 0)}};
            }
            g.type = Gradient::Type::Linear;
            g.start = toFraction(*leaf, m_dragStart);
            g.end = toFraction(*leaf, m_dragStart + delta);
            style.transparency = g;
        }
        leaf->setStyle(style);
    }
    if (!m_selection.empty())
        m_document->notifyShapeChanged(m_selection.front());
}

void CanvasWidget::commitEffectDrag()
{
    if (m_effectBefore.empty())
        return;
    const QString text = m_tool == Tool::DropShadow ? tr("Drop shadow")
                         : m_tool == Tool::Contour  ? tr("Contour")
                         : m_tool == Tool::Extrude  ? tr("Extrude")
                                                    : tr("Transparency");
    m_undoStack->beginMacro(text);
    for (const auto &[leaf, before] : m_effectBefore) {
        const Style after = leaf->style();
        leaf->setStyle(before);
        m_undoStack->push(new SetStyleCommand(m_document, leaf, after));
    }
    m_undoStack->endMacro();
    m_effectBefore.clear();
}

Shape *CanvasWidget::transparencyShape() const
{
    Shape *single = selectedShape();
    return single && single->kind() != ShapeKind::Group && single->style().transparency ? single : nullptr;
}

std::array<QPointF, 2> CanvasWidget::transparencyHandles(const Shape &shape) const
{
    const Gradient &g = *shape.style().transparency;
    if (g.type == Gradient::Type::Radial) {
        const QRectF box = shape.localBounds();
        const double r = g.radius * std::max(box.width(), box.height());
        const QPointF centre = fromFraction(shape, g.center);
        return {centre, centre + shape.shapeTransform().map(QPointF(r, 0)) - shape.shapeTransform().map(QPointF(0, 0))};
    }
    return {fromFraction(shape, g.start), fromFraction(shape, g.end)};
}

int CanvasWidget::transparencyHandleAt(const QPointF &widgetPosition) const
{
    const Shape *shape = m_tool == Tool::Transparency ? transparencyShape() : nullptr;
    if (!shape)
        return -1;
    const auto handles = transparencyHandles(*shape);
    for (int i = 0; i < 2; ++i) {
        if (distance(toWidget(handles[i]), widgetPosition) <= kNodeHitPx + 2)
            return i;
    }
    return -1;
}

void CanvasWidget::paintEffectHandles(QPainter &painter)
{
    // A line from where the drag started, while dragging a shadow or a blend.
    if ((m_drag == Drag::Effect && m_tool != Tool::Transparency) || m_drag == Drag::Blend) {
        painter.setPen(QPen(palette().color(QPalette::Highlight), 1, Qt::DashLine));
        painter.drawLine(toWidget(m_dragStart), toWidget(m_cursor));
    }
    // Transparency: the gradient's two ends, solid (white) to transparent (black), and the line.
    if (const Shape *shape = transparencyShape(); shape && m_tool == Tool::Transparency) {
        const auto handles = transparencyHandles(*shape);
        painter.setPen(QPen(palette().color(QPalette::Highlight), 1.5));
        painter.drawLine(toWidget(handles[0]), toWidget(handles[1]));
        painter.setPen(QPen(QColor(0x20, 0x20, 0x20), 1));
        painter.setBrush(Qt::white);
        painter.drawRect(squareAround(toWidget(handles[0]), 9));
        painter.setBrush(Qt::black);
        painter.drawRect(squareAround(toWidget(handles[1]), 9));
        painter.setBrush(Qt::NoBrush);
    }
}

Shape *CanvasWidget::envelopeShape() const
{
    Shape *single = selectedShape();
    return m_tool == Tool::Envelope && single && Shape::canHaveEnvelope(single->kind()) ? single : nullptr;
}

// Maps box fractions of a shape's own box onto the page.
static QTransform envelopeFrame(const Shape &shape)
{
    const QRectF box = shape.localBounds();
    return QTransform(box.width(), 0, 0, box.height(), box.left(), box.top()) * shape.shapeTransform();
}

std::array<QPointF, 12> CanvasWidget::envelopeHandles(const Shape &shape) const
{
    const Envelope e = shape.envelope().value_or(Envelope::identity());
    const QTransform frame = envelopeFrame(shape);
    std::array<QPointF, 12> handles;
    for (int i = 0; i < 12; ++i)
        handles[i] = frame.map(e.points[i]);
    return handles;
}

int CanvasWidget::envelopeHandleAt(const QPointF &widgetPosition) const
{
    const Shape *shape = envelopeShape();
    if (!shape)
        return -1;
    const auto handles = envelopeHandles(*shape);
    // Corners first: they sit on top of their handles when the edges are straight.
    for (int pass = 0; pass < 2; ++pass) {
        for (int i = 0; i < 12; ++i) {
            if (Envelope::isCorner(i) == (pass == 0) && distance(toWidget(handles[i]), widgetPosition) <= kNodeHitPx + 2)
                return i;
        }
    }
    return -1;
}

void CanvasWidget::dragEnvelopeHandle(const QPointF &documentPosition)
{
    Shape *shape = envelopeShape();
    if (!shape || m_envelopeHandle < 0)
        return;
    Envelope e = m_envelopeBefore.value_or(Envelope::identity());
    const QPointF unit = envelopeFrame(*shape).inverted().map(documentPosition);
    const int i = m_envelopeHandle;
    if (Envelope::isCorner(i)) {
        // A corner takes its two handles along.
        const QPointF delta = unit - e.points[i];
        for (int k : {i, (i + 1) % 12, (i + 11) % 12})
            e.points[k] += delta;
    } else {
        e.points[i] = unit;
    }
    shape->setEnvelope(e);
    m_document->notifyShapeChanged(shape);
}

void CanvasWidget::removeEnvelope()
{
    std::vector<Shape *> bent;
    for (Shape *shape : m_selection) {
        shape->forEachLeaf([&bent](Shape &leaf) {
            if (leaf.envelope())
                bent.push_back(&leaf);
        });
    }
    if (bent.empty())
        return;
    m_undoStack->beginMacro(tr("Remove envelope"));
    for (Shape *shape : bent)
        m_undoStack->push(new SetEnvelopeCommand(m_document, shape, std::nullopt));
    m_undoStack->endMacro();
}

void CanvasWidget::paintEnvelope(QPainter &painter)
{
    const Shape *shape = envelopeShape();
    if (!shape)
        return;
    const Envelope e = shape->envelope().value_or(Envelope::identity());
    const QTransform toView = envelopeFrame(*shape) * viewTransform();
    const QColor highlight = palette().color(QPalette::Highlight);
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(highlight, 1.2, Qt::DashLine));
    painter.drawPath(toView.map(envelope::outline(e)));
    std::array<QPointF, 12> handles;
    for (int i = 0; i < 12; ++i)
        handles[i] = toView.map(e.points[i]);
    painter.setPen(QPen(highlight, 1));
    for (int i = 0; i < 12; i += 3) {
        painter.drawLine(handles[i], handles[(i + 1) % 12]);
        painter.drawLine(handles[i], handles[(i + 11) % 12]);
    }
    painter.setPen(QPen(QColor(0x20, 0x20, 0x20), 1));
    for (int i = 0; i < 12; ++i) {
        if (Envelope::isCorner(i)) {
            painter.setBrush(Qt::white);
            painter.drawRect(squareAround(handles[i], 8));
        } else {
            painter.setBrush(highlight);
            painter.drawEllipse(handles[i], 3.5, 3.5);
        }
    }
    painter.restore();
}

void CanvasWidget::paintTransformHandles(QPainter &painter, const QRectF &box)
{
    const QColor highlight = palette().color(QPalette::Highlight);
    painter.setRenderHint(QPainter::Antialiasing);
    const QPen pen(highlight, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    auto arrowHead = [&painter](const QPointF &tip, double angle) {
        const double a = angle * std::numbers::pi / 180.0;
        const QPointF back(std::cos(a), std::sin(a));
        const QPointF side(-back.y(), back.x());
        painter.drawLine(tip, tip - back * 4 + side * 3);
        painter.drawLine(tip, tip - back * 4 - side * 3);
    };
    // Corners: curved double arrows (rotate).
    const QPointF corners[] = {box.topLeft(), box.topRight(), box.bottomRight(), box.bottomLeft()};
    const double startAngles[] = {90, 0, 270, 180}; // arc bulging away from the box
    for (int i = 0; i < 4; ++i) {
        const QRectF arc = squareAround(corners[i], 14);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        QPainterPath path;
        path.arcMoveTo(arc, startAngles[i]);
        path.arcTo(arc, startAngles[i], 90);
        painter.drawPath(path);
        const QPointF a = path.pointAtPercent(0), b = path.pointAtPercent(1);
        arrowHead(a, std::atan2(a.y() - b.y(), a.x() - b.x()) * 180.0 / std::numbers::pi);
        arrowHead(b, std::atan2(b.y() - a.y(), b.x() - a.x()) * 180.0 / std::numbers::pi);
    }
    // Edges: straight double arrows along the edge (skew).
    const QPointF top(box.center().x(), box.top()), bottom(box.center().x(), box.bottom());
    const QPointF left(box.left(), box.center().y()), right(box.right(), box.center().y());
    for (const QPointF &p : {top, bottom}) {
        painter.drawLine(p - QPointF(7, 0), p + QPointF(7, 0));
        arrowHead(p - QPointF(7, 0), 180);
        arrowHead(p + QPointF(7, 0), 0);
    }
    for (const QPointF &p : {left, right}) {
        painter.drawLine(p - QPointF(0, 7), p + QPointF(0, 7));
        arrowHead(p - QPointF(0, 7), 270);
        arrowHead(p + QPointF(0, 7), 90);
    }
    // Rotation centre: a circle with a dot.
    const QPointF c = toWidget(rotationCenter());
    painter.setBrush(Qt::white);
    painter.drawEllipse(c, 5, 5);
    painter.setBrush(highlight);
    painter.drawEllipse(c, 1.5, 1.5);
}

bool CanvasWidget::isSelected(const Shape *shape) const
{
    return std::find(m_selection.begin(), m_selection.end(), shape) != m_selection.end();
}

QRectF CanvasWidget::selectionBounds() const
{
    std::vector<QRectF> boxes;
    for (const Shape *shape : m_selection)
        boxes.push_back(shape->bounds());
    return arrange::unite(boxes);
}

// ---------------------------------------------------------------------------------------------
// View

QTransform CanvasWidget::viewTransform() const
{
    return QTransform::fromTranslate(m_pan.x(), m_pan.y()).scale(m_zoom, m_zoom);
}

QPointF CanvasWidget::toDocument(const QPointF &widgetPosition) const
{
    return (widgetPosition - m_pan) / m_zoom;
}

QPointF CanvasWidget::toWidget(const QPointF &documentPosition) const
{
    return documentPosition * m_zoom + m_pan;
}

QRectF CanvasWidget::visibleDocumentRect() const
{
    return QRectF(toDocument(QPointF(0, 0)), toDocument(QPointF(width(), height())));
}

void CanvasWidget::viewUpdated(bool zoomChanged)
{
    m_fitOnResize = false; // fitPage() sets it again
    update();
    if (zoomChanged)
        emit this->zoomChanged(m_zoom);
    emit viewChanged();
}

void CanvasWidget::setPan(const QPointF &pan)
{
    if (pan == m_pan)
        return;
    m_pan = pan;
    viewUpdated(false);
}

void CanvasWidget::zoomAround(const QPointF &widgetPosition, double factor)
{
    const QPointF anchor = toDocument(widgetPosition);
    m_zoom = std::clamp(m_zoom * factor, kMinZoom, kMaxZoom);
    m_pan = widgetPosition - anchor * m_zoom;
    viewUpdated(true);
}

void CanvasWidget::setZoom(double zoom)
{
    zoomAround(rect().center(), zoom / m_zoom);
}

void CanvasWidget::zoomToRect(const QRectF &documentRect)
{
    const QRectF target = documentRect.normalized();
    if (target.isEmpty())
        return;
    const double availableWidth = std::max(width() - 2.0 * kPageMarginPx, 1.0);
    const double availableHeight = std::max(height() - 2.0 * kPageMarginPx, 1.0);
    m_zoom = std::clamp(std::min(availableWidth / target.width(), availableHeight / target.height()), kMinZoom,
                        kMaxZoom);
    m_pan = QPointF(width() / 2.0, height() / 2.0) - target.center() * m_zoom;
    viewUpdated(true);
}

void CanvasWidget::fitPageWidth()
{
    const QSizeF page = m_document->pageSize();
    m_zoom = std::clamp(std::max(width() - 2.0 * kPageMarginPx, 1.0) / page.width(), kMinZoom, kMaxZoom);
    m_pan = QPointF((width() - page.width() * m_zoom) / 2.0, kPageMarginPx);
    viewUpdated(true);
}

void CanvasWidget::fitPageHeight()
{
    const QSizeF page = m_document->pageSize();
    m_zoom = std::clamp(std::max(height() - 2.0 * kPageMarginPx, 1.0) / page.height(), kMinZoom, kMaxZoom);
    m_pan = QPointF((width() - page.width() * m_zoom) / 2.0, (height() - page.height() * m_zoom) / 2.0);
    viewUpdated(true);
}

void CanvasWidget::zoomToSelection()
{
    if (m_selection.empty())
        return;
    zoomToRect(selectionBounds());
}

void CanvasWidget::zoomIn()
{
    zoomAround(rect().center(), kZoomStep);
}

void CanvasWidget::zoomOut()
{
    zoomAround(rect().center(), 1.0 / kZoomStep);
}

void CanvasWidget::zoomToActualSize()
{
    zoomAround(rect().center(), 1.0 / m_zoom);
}

void CanvasWidget::fitPage()
{
    const QSizeF page = m_document->pageSize();
    const double availableWidth = std::max(width() - 2.0 * kPageMarginPx, 1.0);
    const double availableHeight = std::max(height() - 2.0 * kPageMarginPx, 1.0);
    m_zoom = std::clamp(std::min(availableWidth / page.width(), availableHeight / page.height()),
                        kMinZoom, kMaxZoom);
    m_pan = QPointF((width() - page.width() * m_zoom) / 2.0, (height() - page.height() * m_zoom) / 2.0);
    viewUpdated(true);
    m_fitOnResize = true;
}

void CanvasWidget::updateCursor()
{
    // Cursor drawn from one of OpenDraw's icons, with its hot spot on the 24 Ã— 24 grid.
    auto iconCursor = [](Icon id, QPoint hotSpot) {
        const QPixmap pixmap = icon(id).pixmap(QSize(24, 24));
        return QCursor(pixmap, hotSpot.x(), hotSpot.y());
    };
    if (m_spaceHeld || m_tool == Tool::Pan)
        setCursor(Qt::OpenHandCursor);
    else if (m_tool == Tool::Zoom)
        setCursor(iconCursor(Icon::Zoom, {10, 10}));
    else if (m_tool == Tool::Eyedropper)
        setCursor(m_eyedropperColor ? iconCursor(Icon::FillBucket, {20, 19}) : iconCursor(Icon::Eyedropper, {3, 21}));
    else if (m_tool == Tool::Select || m_tool == Tool::Node)
        unsetCursor();
    else if (m_tool == Tool::Text)
        setCursor(Qt::IBeamCursor);
    else
        setCursor(Qt::CrossCursor);
}

// ---------------------------------------------------------------------------------------------
// Text tool

std::pair<int, int> CanvasWidget::textSelection() const
{
    if (!m_editText || m_textAnchor < 0 || m_textAnchor == m_textCursor)
        return {m_textCursor, m_textCursor};
    return {std::min(m_textAnchor, m_textCursor), std::max(m_textAnchor, m_textCursor)};
}

void CanvasWidget::setTextCursor(int position, bool extendSelection)
{
    if (!m_editText)
        return;
    position = std::clamp(position, 0, int(m_editText->textData().text.size()));
    if (extendSelection) {
        if (m_textAnchor < 0)
            m_textAnchor = m_textCursor;
    } else {
        m_textAnchor = -1;
    }
    m_textCursor = position;
    update();
    emit textEditingChanged();
}

TextFormat CanvasWidget::currentTextFormat() const
{
    if (m_editText) {
        // The format typing continues: the selected part's start, or the letter before the caret.
        const TextData &data = m_editText->textData();
        const auto [start, end] = textSelection();
        const int index = start < end ? start : std::max(m_textCursor - 1, 0);
        return data.text.isEmpty() ? data.format : richtext::effectiveFormat(data, index);
    }
    if (const Shape *shape = selectedShape(); shape && shape->kind() == ShapeKind::Text)
        return richtext::effectiveFormat(shape->textData(), 0);
    return m_defaultTextFormat;
}

void CanvasWidget::setTextFormat(const TextFormat &format)
{
    const TextFormat current = currentTextFormat();
    CharFormat change;
    if (format.family != current.family)
        change.family = format.family;
    if (std::abs(format.size - current.size) > 1e-9)
        change.size = format.size;
    if (format.bold != current.bold)
        change.bold = format.bold;
    if (format.italic != current.italic)
        change.italic = format.italic;
    if (!change.isEmpty())
        applyCharFormat(change);

    // Alignment, spacing and indents belong to whole texts.
    if (format.sameParagraphFormat(current))
        return;
    auto paragraphFormat = [&format](TextData &data) { data.format.setParagraphFormat(format); };
    if (m_editText) {
        TextData data = m_editText->textData();
        paragraphFormat(data);
        setEditedData(std::move(data), m_textCursor);
        m_defaultTextFormat.setParagraphFormat(format);
        return;
    }
    std::vector<Shape *> texts;
    for (Shape *shape : m_selection) {
        shape->forEachLeaf([&texts](Shape &leaf) {
            if (leaf.kind() == ShapeKind::Text)
                texts.push_back(&leaf);
        });
    }
    if (texts.empty()) {
        m_defaultTextFormat.setParagraphFormat(format);
        return;
    }
    m_undoStack->beginMacro(tr("Text format"));
    for (Shape *text : texts) {
        TextData data = text->textData();
        paragraphFormat(data);
        if (data != text->textData())
            m_undoStack->push(new SetTextCommand(m_document, text, text->textData(), data, tr("Text format")));
    }
    m_undoStack->endMacro();
}

void CanvasWidget::applyCharFormat(const CharFormat &change)
{
    auto merge = [&change](CharFormat &c) {
        if (change.family)
            c.family = change.family;
        if (change.size)
            c.size = change.size;
        if (change.bold)
            c.bold = change.bold;
        if (change.italic)
            c.italic = change.italic;
        if (change.color)
            c.color = change.color;
    };
    if (m_editText) {
        TextData data = m_editText->textData();
        const auto [start, end] = textSelection();
        if (start < end)
            richtext::apply(data, start, end, merge);
        else
            richtext::applyToAll(data, change);
        setEditedData(std::move(data), m_textCursor);
        return;
    }
    std::vector<Shape *> texts;
    for (Shape *shape : m_selection) {
        shape->forEachLeaf([&texts](Shape &leaf) {
            if (leaf.kind() == ShapeKind::Text)
                texts.push_back(&leaf);
        });
    }
    if (texts.empty()) {
        // The format new text starts with.
        if (change.family)
            m_defaultTextFormat.family = *change.family;
        if (change.size)
            m_defaultTextFormat.size = *change.size;
        if (change.bold)
            m_defaultTextFormat.bold = *change.bold;
        if (change.italic)
            m_defaultTextFormat.italic = *change.italic;
        return;
    }
    m_undoStack->beginMacro(tr("Text format"));
    for (Shape *text : texts) {
        TextData data = text->textData();
        richtext::applyToAll(data, change);
        if (data != text->textData())
            m_undoStack->push(new SetTextCommand(m_document, text, text->textData(), data, tr("Text format")));
    }
    m_undoStack->endMacro();
}

void CanvasWidget::beginTextEdit(Shape *text)
{
    finishTextEdit();
    setSelectedShape(text);
    m_editText = text;
    m_textBeforeEdit = text->textData();
    m_textCursor = int(text->textData().text.size());
    m_textAnchor = -1;
    update();
    emit textEditingChanged();
}

void CanvasWidget::beginNewText(const QPointF &origin, const QSizeF &frame)
{
    finishTextEdit();
    setSelection({});
    const Style style{QColor(Qt::black), QColor(Qt::transparent), 0.0};
    m_pendingText = std::make_unique<Shape>(TextData{QString(), m_defaultTextFormat, frame}, origin, style);
    m_editText = m_pendingText.get();
    m_textBeforeEdit = {};
    m_textCursor = 0;
    m_textAnchor = -1;
    update();
    emit textEditingChanged();
}

void CanvasWidget::finishTextEdit()
{
    if (!m_editText)
        return;
    Shape *text = m_editText;
    m_editText = nullptr;
    m_textAnchor = -1;

    if (m_pendingText) {
        std::unique_ptr<Shape> pending = std::move(m_pendingText);
        if (!pending->textData().text.trimmed().isEmpty())
            addNewShape(std::move(pending));
    } else {
        const TextData now = text->textData();
        if (now.text.trimmed().isEmpty()) {
            // Typing everything away deletes the text; undo brings back the original.
            text->setTextData(m_textBeforeEdit);
            m_undoStack->push(new RemoveShapeCommand(m_document, text));
        } else if (now != m_textBeforeEdit) {
            m_undoStack->push(new SetTextCommand(m_document, text, m_textBeforeEdit, now, tr("Edit text")));
        }
    }
    update();
    emit textEditingChanged();
}

void CanvasWidget::setEditedData(TextData data, int cursor)
{
    m_textCursor = std::clamp(cursor, 0, int(data.text.size()));
    if (m_textAnchor > int(data.text.size()))
        m_textAnchor = -1;
    m_editText->setTextData(std::move(data));
    if (m_pendingText)
        update();
    else
        m_document->notifyShapeChanged(m_editText);
    emit textEditingChanged();
}

void CanvasWidget::replaceTextSelection(const QString &inserted)
{
    TextData data = m_editText->textData();
    const auto [start, end] = textSelection();
    // Typing over a selected part takes on its format.
    const CharFormat replaced = start < end ? richtext::formatAt(data, start) : CharFormat();
    richtext::remove(data, start, end);
    richtext::insert(data, start, inserted);
    if (start < end)
        richtext::apply(data, start, start + int(inserted.size()), [&replaced](CharFormat &c) { c = replaced; });
    m_textAnchor = -1;
    setEditedData(std::move(data), start + int(inserted.size()));
}

int CanvasWidget::textIndexAt(const QPointF &widgetPosition) const
{
    const QPointF local = m_editText->shapeTransform().inverted().map(toDocument(widgetPosition));
    return textlayout::indexAt(m_editText->textData(), m_editText->textOrigin(), local);
}

bool CanvasWidget::isTextEditingKey(const QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Backspace:
    case Qt::Key_Delete:
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Up:
    case Qt::Key_Down:
    case Qt::Key_Home:
    case Qt::Key_End:
    case Qt::Key_Escape:
        return true;
    default:
        break;
    }
    if (event->matches(QKeySequence::SelectAll) || event->matches(QKeySequence::Bold)
        || event->matches(QKeySequence::Italic))
        return true;
    if (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))
        return false;
    const QString text = event->text();
    return !text.isEmpty() && text.at(0).isPrint();
}

bool CanvasWidget::handleTextKey(QKeyEvent *event)
{
    const TextData &data = m_editText->textData();
    const int size = int(data.text.size());
    const int cursor = m_textCursor;
    const bool extend = event->modifiers() & Qt::ShiftModifier;
    const auto [start, end] = textSelection();

    if (event->matches(QKeySequence::SelectAll)) {
        m_textAnchor = 0;
        m_textCursor = size;
        update();
        emit textEditingChanged();
        return true;
    }
    if (event->matches(QKeySequence::Bold) || event->matches(QKeySequence::Italic)) {
        const TextFormat format = currentTextFormat();
        CharFormat change;
        if (event->matches(QKeySequence::Bold))
            change.bold = !format.bold;
        else
            change.italic = !format.italic;
        applyCharFormat(change);
        return true;
    }

    // Visual lines, for Home, End, Up and Down in wrapped text.
    const textlayout::Layout layout = textlayout::layout(data, m_editText->textOrigin());
    const std::size_t lineIndex = textlayout::lineOf(layout, cursor);
    const textlayout::Line &line = layout.lines[lineIndex];
    auto moveTo = [this, extend, start, end](int position, bool collapseTo) {
        // Without Shift, Left/Right on a selection just go to its edge.
        if (!extend && start < end && collapseTo)
            position = position < m_textCursor ? start : end;
        setTextCursor(position, extend);
    };

    switch (event->key()) {
    case Qt::Key_Escape:
        finishTextEdit();
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        replaceTextSelection(QStringLiteral("\n"));
        return true;
    case Qt::Key_Backspace:
        if (start < end) {
            replaceTextSelection(QString());
        } else if (cursor > 0) {
            TextData edited = data;
            richtext::remove(edited, cursor - 1, cursor);
            setEditedData(std::move(edited), cursor - 1);
        }
        return true;
    case Qt::Key_Delete:
        if (start < end) {
            replaceTextSelection(QString());
        } else if (cursor < size) {
            TextData edited = data;
            richtext::remove(edited, cursor, cursor + 1);
            setEditedData(std::move(edited), cursor);
        }
        return true;
    case Qt::Key_Left:
        moveTo(std::max(0, cursor - 1), true);
        return true;
    case Qt::Key_Right:
        moveTo(std::min(size, cursor + 1), true);
        return true;
    case Qt::Key_Home:
        moveTo(line.start, false);
        return true;
    case Qt::Key_End:
        moveTo(line.start + int(line.text.size()), false);
        return true;
    case Qt::Key_Up:
    case Qt::Key_Down: {
        const bool up = event->key() == Qt::Key_Up;
        if ((up && lineIndex == 0) || (!up && lineIndex + 1 >= layout.lines.size())) {
            moveTo(up ? 0 : size, false);
            return true;
        }
        const textlayout::Line &target = layout.lines[up ? lineIndex - 1 : lineIndex + 1];
        const double x = textlayout::caret(data, m_editText->textOrigin(), cursor).x1();
        moveTo(textlayout::indexAt(data, m_editText->textOrigin(), QPointF(x, target.baselineStart.y())), false);
        return true;
    }
    default:
        break;
    }
    if (!isTextEditingKey(event))
        return false;
    replaceTextSelection(event->text());
    return true;
}

bool CanvasWidget::event(QEvent *event)
{
    // While typing, keys that edit text must not trigger the window's shortcuts (L aligns,
    // Delete removes the object...). Other shortcuts end the editing and then run.
    if (event->type() == QEvent::ShortcutOverride && m_editText) {
        auto *key = static_cast<QKeyEvent *>(event);
        // Shift, Ctrl and the like alone are no shortcut (Shift selects and types capitals).
        switch (key->key()) {
        case Qt::Key_Shift:
        case Qt::Key_Control:
        case Qt::Key_Alt:
        case Qt::Key_AltGr:
        case Qt::Key_Meta:
        case Qt::Key_CapsLock:
            return QWidget::event(event);
        default:
            break;
        }
        if (isTextEditingKey(key)) {
            event->accept();
            return true;
        }
        finishTextEdit();
    }
    return QWidget::event(event);
}

void CanvasWidget::paintTextEdit(QPainter &painter)
{
    if (!m_editText)
        return;
    const QColor highlight = palette().color(QPalette::Highlight);
    painter.setPen(QPen(highlight, 1, Qt::DashLine));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(viewTransform().mapRect(m_editText->bounds()).adjusted(-3, -3, 3, 3));

    // The selected part, then the caret; both turn with rotated text.
    const auto [selectionStart, selectionEnd] = textSelection();
    QColor selectionColor = highlight;
    selectionColor.setAlpha(90);
    painter.setPen(Qt::NoPen);
    painter.setBrush(selectionColor);
    for (const QRectF &r : textlayout::selectionRects(m_editText->textData(), m_editText->textOrigin(), selectionStart,
                                                      selectionEnd))
        painter.drawPolygon(viewTransform().map(m_editText->shapeTransform().map(QPolygonF(r))));
    painter.setBrush(Qt::NoBrush);
    const QLineF caret = m_editText->shapeTransform().map(
        textlayout::caret(m_editText->textData(), m_editText->textOrigin(), m_textCursor));
    painter.setPen(QPen(Qt::black, 1.5));
    painter.drawLine(toWidget(caret.p1()), toWidget(caret.p2()));
}

// ---------------------------------------------------------------------------------------------
// Editing actions

Style CanvasWidget::curveStyle(bool closed) const
{
    Style style = m_defaultStyle;
    if (!closed)
        style.fill = Qt::transparent;
    if (!style.hasStroke()) {
        style.stroke = Qt::black;
        style.strokeWidth = std::max(style.strokeWidth, 1.0);
    }
    return style;
}

void CanvasWidget::addNewShape(std::unique_ptr<Shape> shape)
{
    auto *command = new AddShapeCommand(m_document, std::move(shape));
    Shape *created = command->shape();
    m_undoStack->push(command);
    setSelectedShape(created);
}

void CanvasWidget::selectAll()
{
    setSelection(m_document->editableShapes());
}

void CanvasWidget::deleteSelection()
{
    if (!m_bezier.nodes.empty()) {
        m_bezier.nodes.pop_back();
        update();
        return;
    }
    if (m_tool == Tool::Node && activeNodeIsValid()) {
        removeActiveNode();
        return;
    }
    if (m_selection.empty())
        return;
    const std::vector<Shape *> shapes = m_selection;
    m_undoStack->beginMacro(tr("Delete"));
    for (Shape *shape : shapes)
        m_undoStack->push(new RemoveShapeCommand(m_document, shape));
    m_undoStack->endMacro();
}

void CanvasWidget::convertSelectionToCurves()
{
    Shape *shape = selectedShape();
    const bool plainCurve = shape && shape->kind() == ShapeKind::Path && !shape->envelope()
                            && shape->shapeTransform().isIdentity();
    if (!shape || plainCurve || shape->kind() == ShapeKind::Group || shape->kind() == ShapeKind::Image)
        return;
    auto curve = std::make_unique<Shape>(pathops::fromPainterPath(shape->path()), shape->style());
    auto *command = new ReplaceShapeCommand(m_document, shape, std::move(curve), tr("Convert to curves"));
    Shape *created = command->newShape();
    m_undoStack->push(command);
    setSelectedShape(created);
}

void CanvasWidget::convertTextKind()
{
    finishTextEdit();
    std::vector<Shape *> texts;
    for (Shape *shape : m_selection) {
        if (shape->kind() == ShapeKind::Text)
            texts.push_back(shape);
    }
    if (texts.empty())
        return;
    std::vector<Shape *> converted;
    m_undoStack->beginMacro(tr("Convert text"));
    for (Shape *shape : texts) {
        const TextData &data = shape->textData();
        const textlayout::Layout layout = textlayout::layout(data, shape->textOrigin());
        TextData result = data;
        QPointF origin;
        if (data.isParagraph()) {
            // The lines stay as they are now, each ending in a line break.
            QString text;
            for (std::size_t i = 0; i < layout.lines.size(); ++i) {
                if (i > 0)
                    text += QLatin1Char('\n');
                text += layout.lines[i].text;
            }
            result.text = text;
            result.frame = QSizeF();
            const QRectF frame = shape->localBounds();
            double x = frame.left();
            if (data.format.align == TextAlign::Center)
                x = frame.center().x();
            else if (data.format.align == TextAlign::Right)
                x = frame.right();
            origin = QPointF(x, layout.lines.front().baselineStart.y());
        } else {
            // A frame just around the text (a little wider, so it does not wrap).
            const QRectF box = layout.bounds;
            result.frame = QSizeF(box.width() + data.format.size * 0.1, box.height() + data.format.size * 0.1);
            origin = box.topLeft();
        }
        auto replacement = std::make_unique<Shape>(result, origin, shape->style());
        replacement->setShapeTransform(shape->shapeTransform());
        replacement->setName(shape->name());
        auto *command = new ReplaceShapeCommand(m_document, shape, std::move(replacement), tr("Convert text"));
        converted.push_back(command->newShape());
        m_undoStack->push(command);
    }
    m_undoStack->endMacro();
    setSelection(std::move(converted));
}

void CanvasWidget::finishCurve()
{
    SubPath curve = std::move(m_bezier);
    m_bezier = {};
    if (curve.nodes.size() >= 2) {
        const bool closed = curve.closed;
        addNewShape(std::make_unique<Shape>(PathData{std::move(curve)}, curveStyle(closed)));
    }
    update();
}

// ---------------------------------------------------------------------------------------------
// Arrange

void CanvasWidget::groupSelection()
{
    if (m_selection.size() < 2)
        return;
    auto *command = new GroupCommand(m_document, m_selection);
    Shape *group = command->group();
    m_undoStack->push(command);
    setSelectedShape(group);
}

void CanvasWidget::blendShapes(Shape *from, Shape *to)
{
    auto *command = new GroupCommand(m_document, {from, to});
    command->setText(tr("Blend"));
    Shape *group = command->group();
    group->setBlendSteps(m_toolSettings.blendSteps);
    m_undoStack->push(command);
    setSelectedShape(group);
}

void CanvasWidget::setBlendSteps(int steps)
{
    std::vector<Shape *> blends;
    for (Shape *shape : m_selection) {
        if (shape->isBlend() && shape->blendSteps() != steps)
            blends.push_back(shape);
    }
    if (blends.empty())
        return;
    m_undoStack->beginMacro(tr("Blend steps"));
    for (Shape *blend : blends)
        m_undoStack->push(new SetBlendStepsCommand(m_document, blend, steps));
    m_undoStack->endMacro();
}

void CanvasWidget::breakBlendApart()
{
    std::vector<Shape *> blends;
    for (Shape *shape : m_selection) {
        if (shape->isBlend())
            blends.push_back(shape);
    }
    if (blends.empty())
        return;
    // Start, steps (as a group) and end become separate objects.
    std::vector<Shape *> released;
    m_undoStack->beginMacro(tr("Break blend apart"));
    for (Shape *blend : blends) {
        m_undoStack->push(new BreakBlendCommand(m_document, blend));
        auto *ungroup = new UngroupCommand(m_document, blend);
        released.insert(released.end(), ungroup->children().begin(), ungroup->children().end());
        m_undoStack->push(ungroup);
    }
    m_undoStack->endMacro();
    setSelection(std::move(released));
}

void CanvasWidget::ungroupSelection()
{
    std::vector<Shape *> groups;
    for (Shape *shape : m_selection) {
        if (shape->kind() == ShapeKind::Group)
            groups.push_back(shape);
    }
    if (groups.empty())
        return;
    std::vector<Shape *> released;
    m_undoStack->beginMacro(tr("Ungroup"));
    for (Shape *group : groups) {
        auto *command = new UngroupCommand(m_document, group);
        released.insert(released.end(), command->children().begin(), command->children().end());
        m_undoStack->push(command);
    }
    m_undoStack->endMacro();
    setSelection(std::move(released));
}

// Stacking order is per layer: each layer holding selected shapes is reordered on its own.
void CanvasWidget::reorder(const std::function<std::vector<Shape *>(std::vector<Shape *>)> &rearrange,
                           const QString &text)
{
    std::vector<std::pair<Layer *, std::vector<Shape *>>> changes;
    for (const auto &layer : m_document->currentPage()->layers) {
        const std::vector<Shape *> current = m_document->order(layer.get());
        if (std::none_of(current.begin(), current.end(), [this](Shape *s) { return isSelected(s); }))
            continue;
        std::vector<Shape *> rearranged = rearrange(current);
        if (rearranged != current)
            changes.emplace_back(layer.get(), std::move(rearranged));
    }
    if (changes.empty())
        return;
    m_undoStack->beginMacro(text);
    for (auto &[layer, order] : changes)
        m_undoStack->push(new ReorderCommand(m_document, layer, std::move(order), text));
    m_undoStack->endMacro();
}

void CanvasWidget::orderToFront()
{
    reorder(
        [this](const std::vector<Shape *> &current) {
            std::vector<Shape *> order;
            std::vector<Shape *> selected;
            for (Shape *shape : current)
                (isSelected(shape) ? selected : order).push_back(shape);
            order.insert(order.end(), selected.begin(), selected.end());
            return order;
        },
        tr("To front"));
}

void CanvasWidget::orderToBack()
{
    reorder(
        [this](const std::vector<Shape *> &current) {
            std::vector<Shape *> order;
            std::vector<Shape *> others;
            for (Shape *shape : current)
                (isSelected(shape) ? order : others).push_back(shape);
            order.insert(order.end(), others.begin(), others.end());
            return order;
        },
        tr("To back"));
}

void CanvasWidget::orderForwardOne()
{
    reorder(
        [this](std::vector<Shape *> order) {
            // From the top down, so a selected shape never jumps over another selected one.
            for (int i = int(order.size()) - 2; i >= 0; --i) {
                if (isSelected(order[i]) && !isSelected(order[i + 1]))
                    std::swap(order[i], order[i + 1]);
            }
            return order;
        },
        tr("Forward one"));
}

void CanvasWidget::orderBackOne()
{
    reorder(
        [this](std::vector<Shape *> order) {
            for (int i = 1; i < int(order.size()); ++i) {
                if (isSelected(order[i]) && !isSelected(order[i - 1]))
                    std::swap(order[i], order[i - 1]);
            }
            return order;
        },
        tr("Back one"));
}

void CanvasWidget::moveSelectionToLayer(Layer *layer)
{
    std::vector<Shape *> moving;
    for (Shape *shape : m_selection) {
        if (m_document->layerOf(shape) != layer)
            moving.push_back(shape);
    }
    if (moving.empty())
        return;
    // Keep their relative stacking: move them bottom to top.
    std::vector<Shape *> ordered;
    for (const auto &l : m_document->currentPage()->layers) {
        for (Shape *shape : m_document->order(l.get())) {
            if (std::find(moving.begin(), moving.end(), shape) != moving.end())
                ordered.push_back(shape);
        }
    }
    m_undoStack->beginMacro(tr("Move to layer"));
    for (Shape *shape : ordered)
        m_undoStack->push(new MoveToLayerCommand(m_document, shape, layer));
    m_undoStack->endMacro();
}

void CanvasWidget::moveSelection(const std::vector<QPointF> &offsets, const QString &text)
{
    if (std::all_of(offsets.begin(), offsets.end(), [](const QPointF &p) { return p.isNull(); }))
        return;
    m_undoStack->beginMacro(text);
    for (std::size_t i = 0; i < m_selection.size(); ++i) {
        if (!offsets[i].isNull())
            m_undoStack->push(new MoveShapeCommand(m_document, m_selection[i], offsets[i], false));
    }
    m_undoStack->endMacro();
}

void CanvasWidget::align(arrange::Alignment alignment)
{
    if (m_selection.empty())
        return;
    std::vector<QRectF> boxes;
    for (const Shape *shape : m_selection)
        boxes.push_back(shape->bounds());
    const QRectF reference = m_selection.size() == 1 ? QRectF(QPointF(0, 0), m_document->pageSize())
                                                     : arrange::unite(boxes);
    moveSelection(arrange::alignOffsets(boxes, alignment, reference), tr("Align"));
}

void CanvasWidget::centerOnPage()
{
    if (m_selection.empty())
        return;
    // The selection moves as a block, keeping its layout.
    const QRectF page(QPointF(0, 0), m_document->pageSize());
    const QPointF offset = page.center() - selectionBounds().center();
    moveSelection(std::vector<QPointF>(m_selection.size(), offset), tr("Center on page"));
}

void CanvasWidget::distribute(Qt::Orientation orientation)
{
    std::vector<QRectF> boxes;
    for (const Shape *shape : m_selection)
        boxes.push_back(shape->bounds());
    moveSelection(arrange::distributeOffsets(boxes, orientation), tr("Distribute"));
}

void CanvasWidget::nudgeSelection(const QPointF &offset)
{
    moveSelection(std::vector<QPointF>(m_selection.size(), offset), tr("Nudge"));
}

void CanvasWidget::mirrorSelection(Qt::Orientation orientation)
{
    if (m_selection.empty())
        return;
    // The selection flips as a block around its centre.
    const QPointF c = selectionBounds().center();
    QTransform t = QTransform::fromTranslate(c.x(), c.y());
    t.scale(orientation == Qt::Horizontal ? -1 : 1, orientation == Qt::Vertical ? -1 : 1);
    t.translate(-c.x(), -c.y());
    const QString text = orientation == Qt::Horizontal ? tr("Mirror horizontally") : tr("Mirror vertically");
    m_undoStack->beginMacro(text);
    for (Shape *shape : m_selection)
        m_undoStack->push(new TransformShapeCommand(m_document, shape, t, text));
    m_undoStack->endMacro();
}

// ---------------------------------------------------------------------------------------------
// Clipboard. Shapes travel as SVG, so they can also be pasted into other programs.

namespace {
const QString kSvgMime = QStringLiteral("image/svg+xml");
}

void CanvasWidget::copySelection()
{
    finishTextEdit();
    if (m_selection.empty())
        return;
    const std::vector<const Shape *> shapes(m_selection.begin(), m_selection.end());
    auto *mime = new QMimeData;
    mime->setData(kSvgMime, svg::writeShapes(shapes, m_document->pageSize()));
    QGuiApplication::clipboard()->setMimeData(mime);
}

void CanvasWidget::cutSelection()
{
    copySelection();
    if (m_selection.empty())
        return;
    const std::vector<Shape *> shapes = m_selection;
    m_undoStack->beginMacro(tr("Cut"));
    for (Shape *shape : shapes)
        m_undoStack->push(new RemoveShapeCommand(m_document, shape));
    m_undoStack->endMacro();
}

void CanvasWidget::paste()
{
    finishTextEdit();
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    if (!mime)
        return;
    QByteArray data = mime->data(kSvgMime);
    if (data.isEmpty() && mime->hasText() && mime->text().trimmed().startsWith(u'<'))
        data = mime->text().toUtf8(); // SVG markup copied as text
    if (data.isEmpty())
        return;
    addShapes(svg::readShapes(data), QPointF(), tr("Paste"));
}

void CanvasWidget::duplicateSelection()
{
    finishTextEdit();
    if (m_selection.empty())
        return;
    const std::vector<const Shape *> shapes(m_selection.begin(), m_selection.end());
    addShapes(svg::readShapes(svg::writeShapes(shapes, m_document->pageSize())), m_duplicateOffset,
              tr("Duplicate"));
}

void CanvasWidget::addShapes(std::vector<std::unique_ptr<Shape>> shapes, const QPointF &offset, const QString &text)
{
    if (shapes.empty() || !m_document->activeLayer()->isEditable())
        return;
    std::vector<Shape *> added;
    m_undoStack->beginMacro(text);
    for (auto &shape : shapes) {
        if (!offset.isNull())
            shape->moveBy(offset);
        auto *command = new AddShapeCommand(m_document, std::move(shape));
        added.push_back(command->shape());
        m_undoStack->push(command);
    }
    m_undoStack->endMacro();
    setSelection(std::move(added));
}

// ---------------------------------------------------------------------------------------------
// Shape tool (node editing)

Shape *CanvasWidget::selectedPath() const
{
    // A curve bent by an envelope is edited through the envelope (or converted to curves first).
    Shape *shape = selectedShape();
    return shape && shape->kind() == ShapeKind::Path && !shape->envelope() && shape->shapeTransform().isIdentity()
               ? shape
               : nullptr;
}

bool CanvasWidget::isValidNode(const NodeRef &ref) const
{
    const Shape *path = selectedPath();
    if (!path || !ref.isValid())
        return false;
    const PathData &data = path->pathData();
    return ref.subpath < int(data.size()) && ref.node < int(data[ref.subpath].nodes.size());
}

bool CanvasWidget::activeNodeIsValid() const
{
    return isValidNode(m_activeNode);
}

bool CanvasWidget::hasNodePairToJoin() const
{
    return activeNodeIsValid() && isValidNode(m_secondaryNode);
}

void CanvasWidget::setActiveNode(const NodeRef &node)
{
    setActiveNode(node, NodeRef());
}

void CanvasWidget::setActiveNode(const NodeRef &node, const NodeRef &secondary)
{
    m_activeNode = node;
    m_secondaryNode = secondary;
    update();
    emit nodeSelectionChanged();
}

CanvasWidget::NodeRef CanvasWidget::nodeAt(const QPointF &widgetPosition) const
{
    const Shape *path = selectedPath();
    if (!path)
        return {};
    NodeRef best;
    double bestDistance = kNodeHitPx;
    const PathData &data = path->pathData();
    for (int s = 0; s < int(data.size()); ++s) {
        for (int n = 0; n < int(data[s].nodes.size()); ++n) {
            const double d = distance(toWidget(data[s].nodes[n].point), widgetPosition);
            if (d <= bestDistance) {
                bestDistance = d;
                best = {s, n};
            }
        }
    }
    return best;
}

std::optional<CanvasWidget::NodeHandle> CanvasWidget::nodeHandleAt(const QPointF &widgetPosition) const
{
    if (!activeNodeIsValid())
        return std::nullopt;
    const PathNode &node = selectedPath()->pathData()[m_activeNode.subpath].nodes[m_activeNode.node];
    if (node.out != node.point && distance(toWidget(node.out), widgetPosition) <= kNodeHitPx)
        return NodeHandle::Out;
    if (node.in != node.point && distance(toWidget(node.in), widgetPosition) <= kNodeHitPx)
        return NodeHandle::In;
    return std::nullopt;
}

void CanvasWidget::commitPathEdit(const PathData &before, const QString &text)
{
    Shape *path = selectedPath();
    if (path && path->pathData() != before)
        m_undoStack->push(new SetPathCommand(m_document, path, before, path->pathData(), text));
}

void CanvasWidget::removeActiveNode()
{
    Shape *path = selectedPath();
    PathData data = path->pathData();
    pathops::removeNode(data, m_activeNode.subpath, m_activeNode.node);
    setActiveNode({});
    if (data.empty()) {
        m_undoStack->push(new RemoveShapeCommand(m_document, path));
        return;
    }
    m_undoStack->push(new SetPathCommand(m_document, path, path->pathData(), data, tr("Delete node")));
}

template<typename Edit>
bool CanvasWidget::editPath(const QString &text, Edit edit)
{
    Shape *path = selectedPath();
    if (!path || !activeNodeIsValid())
        return false;
    PathData data = path->pathData();
    if (!edit(data))
        return false;
    m_undoStack->push(new SetPathCommand(m_document, path, path->pathData(), std::move(data), text));
    return true;
}

void CanvasWidget::makeNodeSmooth()
{
    const NodeRef n = m_activeNode;
    editPath(tr("Make node smooth"),
             [n](PathData &d) { return pathops::setNodeType(d, n.subpath, n.node, NodeType::Smooth); });
}

void CanvasWidget::makeNodeCorner()
{
    const NodeRef n = m_activeNode;
    editPath(tr("Make node corner"),
             [n](PathData &d) { return pathops::setNodeType(d, n.subpath, n.node, NodeType::Corner); });
}

void CanvasWidget::segmentToLine()
{
    const NodeRef n = m_activeNode;
    editPath(tr("To line"), [n](PathData &d) {
        const auto segment = pathops::segmentAtNode(d, n.subpath, n.node);
        return segment && pathops::segmentToLine(d, n.subpath, *segment);
    });
}

void CanvasWidget::segmentToCurve()
{
    const NodeRef n = m_activeNode;
    editPath(tr("To curve"), [n](PathData &d) {
        const auto segment = pathops::segmentAtNode(d, n.subpath, n.node);
        return segment && pathops::segmentToCurve(d, n.subpath, *segment);
    });
}

void CanvasWidget::breakNode()
{
    const NodeRef n = m_activeNode;
    if (editPath(tr("Break node"), [n](PathData &d) { return pathops::breakAtNode(d, n.subpath, n.node); }))
        setActiveNode({});
}

void CanvasWidget::joinNodes()
{
    if (!hasNodePairToJoin())
        return;
    const NodeRef a = m_activeNode;
    const NodeRef b = m_secondaryNode;
    if (editPath(tr("Join nodes"),
                 [a, b](PathData &d) { return pathops::joinNodes(d, a.subpath, a.node, b.subpath, b.node); }))
        setActiveNode({});
}

// ---------------------------------------------------------------------------------------------
// Select tool resize handles

std::array<QRectF, 8> CanvasWidget::resizeHandleRects() const
{
    std::array<QRectF, 8> rects;
    if (m_selection.empty())
        return rects;
    const QRectF box = viewTransform().mapRect(selectionBounds()).adjusted(-3, -3, 3, 3);
    const QPointF centers[] = {box.topLeft(),     QPointF(box.center().x(), box.top()),
                               box.topRight(),    QPointF(box.right(), box.center().y()),
                               box.bottomRight(), QPointF(box.center().x(), box.bottom()),
                               box.bottomLeft(),  QPointF(box.left(), box.center().y())};
    for (int i = 0; i < 8; ++i)
        rects[i] = squareAround(centers[i], kHandleSizePx);
    return rects;
}

int CanvasWidget::resizeHandleAt(const QPointF &widgetPosition) const
{
    if (m_selection.empty() || m_tool != Tool::Select)
        return -1;
    const auto rects = resizeHandleRects();
    for (int i = 0; i < 8; ++i) {
        if (rects[i].adjusted(-2, -2, 2, 2).contains(widgetPosition))
            return i;
    }
    return -1;
}

void CanvasWidget::setHoverCursor(const QPointF &widgetPosition)
{
    static const Qt::CursorShape shapes[] = {Qt::SizeFDiagCursor, Qt::SizeVerCursor, Qt::SizeBDiagCursor,
                                             Qt::SizeHorCursor,   Qt::SizeFDiagCursor, Qt::SizeVerCursor,
                                             Qt::SizeBDiagCursor, Qt::SizeHorCursor};
    const int handle = m_spaceHeld ? -1 : resizeHandleAt(widgetPosition);
    if (m_rotateMode && !m_spaceHeld && rotationCenterAt(widgetPosition))
        setCursor(Qt::SizeAllCursor);
    else if (m_rotateMode && handle >= 0)
        setCursor(handle % 2 == 0 ? QCursor(icon(Icon::Rotate).pixmap(QSize(24, 24)), 12, 12)
                                  : QCursor(handle == 1 || handle == 5 ? Qt::SplitHCursor : Qt::SplitVCursor));
    else if (handle >= 0)
        setCursor(shapes[handle]);
    else if (m_tool == Tool::Node && !m_spaceHeld && (nodeAt(widgetPosition).isValid() || nodeHandleAt(widgetPosition)))
        setCursor(Qt::SizeAllCursor);
    else if (const int guide = m_tool == Tool::Select && !m_spaceHeld ? guideAt(widgetPosition) : -1; guide >= 0)
        setCursor(m_document->guides()[guide].orientation == Qt::Vertical ? Qt::SplitHCursor : Qt::SplitVCursor);
    else
        updateCursor();
}

// ---------------------------------------------------------------------------------------------
// Painting

void CanvasWidget::paintShape(QPainter &painter, const Shape &shape)
{
    render::paintShape(painter, shape);
}

void CanvasWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), palette().color(QPalette::Mid));

    painter.setTransform(viewTransform());

    const QRectF page(QPointF(0, 0), m_document->pageSize());
    const double onePixel = 1.0 / m_zoom;
    painter.fillRect(page.translated(4 * onePixel, 4 * onePixel), QColor(0, 0, 0, 50));
    painter.fillRect(page, Qt::white);
    painter.setPen(QPen(QColor(0, 0, 0, 90), 0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(page);

    for (const auto &layer : m_document->currentPage()->layers) {
        if (!layer->visible)
            continue;
        for (const auto &shape : layer->shapes)
            paintShape(painter, *shape);
    }
    if (m_pendingText)
        paintShape(painter, *m_pendingText);
    paintGrid(painter);
    paintGuides(painter);

    // Frames of selected (or edited) paragraph text; red when text does not fit.
    auto paintFrame = [&](const Shape *shape) {
        if (shape->kind() != ShapeKind::Text || !shape->textData().isParagraph())
            return;
        const bool overflow = textlayout::layout(shape->textData(), shape->textOrigin()).overflow;
        QPen pen(overflow ? QColor(0xe5, 0x39, 0x35) : palette().color(QPalette::Highlight), overflow ? 1.5 : 1,
                 Qt::DashLine);
        pen.setCosmetic(true);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPolygon(shape->shapeTransform().map(QPolygonF(shape->localBounds())));
    };
    for (const Shape *shape : m_selection)
        paintFrame(shape);
    if (m_editText && !isSelected(m_editText))
        paintFrame(m_editText);

    QPen preview(palette().color(QPalette::Highlight), 0, Qt::DashLine);
    preview.setCosmetic(true);
    if (m_drag == Drag::TextFrame && !m_creating.isEmpty()) {
        painter.setPen(preview);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(m_creating);
    }
    if (m_drag == Drag::Create) {
        painter.setPen(preview);
        painter.setBrush(Qt::NoBrush);
        if (m_tool == Tool::Line)
            painter.drawLine(m_dragStart, m_lineEnd);
        else if (!m_creating.isEmpty())
            painter.drawPath(toolShapePreview(m_creating));
    }
    if (m_drag == Drag::Freehand && m_freehand.size() > 1) {
        const Style style = curveStyle(false);
        painter.setPen(QPen(style.stroke, style.strokeWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPolyline(m_freehand.data(), int(m_freehand.size()));
    }

    painter.resetTransform();
    if (m_editText)
        paintTextEdit(painter);
    else if (envelopeShape())
        paintEnvelope(painter); // instead of the selection handles
    else
        paintSelection(painter);
    paintBezierPreview(painter);
    paintEffectHandles(painter);

    if (m_drag == Drag::Marquee || m_drag == Drag::ZoomBox) {
        QColor fill = palette().color(QPalette::Highlight);
        fill.setAlpha(m_drag == Drag::Marquee ? 40 : 0);
        painter.setPen(QPen(palette().color(QPalette::Highlight), 1, Qt::DashLine));
        painter.setBrush(fill);
        painter.drawRect(viewTransform().mapRect(m_drag == Drag::Marquee ? m_marquee : m_zoomBox.normalized()));
    }
}

void CanvasWidget::paintSelection(QPainter &painter)
{
    if (m_selection.empty())
        return;
    const QColor highlight = palette().color(QPalette::Highlight);

    if (m_tool == Tool::Node) {
        Shape *single = selectedShape();
        if (single && single->kind() == ShapeKind::Path) {
            paintNodes(painter, *single);
        } else {
            // Only a single curve has editable nodes; rectangles and ellipses need "Convert to curves".
            painter.setPen(QPen(highlight, 1, Qt::DotLine));
            painter.setBrush(Qt::NoBrush);
            for (const Shape *shape : m_selection)
                painter.drawRect(viewTransform().mapRect(shape->bounds()).adjusted(-3, -3, 3, 3));
        }
        return;
    }

    // With several objects, show each one's box faintly and the combined box with handles.
    if (m_selection.size() > 1) {
        QColor faint = highlight;
        faint.setAlpha(120);
        painter.setPen(QPen(faint, 1, Qt::DotLine));
        painter.setBrush(Qt::NoBrush);
        for (const Shape *shape : m_selection)
            painter.drawRect(viewTransform().mapRect(shape->bounds()));
    }

    const QRectF box = viewTransform().mapRect(selectionBounds()).adjusted(-3, -3, 3, 3);
    painter.setPen(QPen(highlight, 1, Qt::DashLine));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(box);
    if (m_tool == Tool::Select && m_rotateMode) {
        paintTransformHandles(painter, box);
    } else if (m_tool == Tool::Select) {
        painter.setPen(QPen(highlight, 1));
        painter.setBrush(Qt::white);
        for (const QRectF &handle : resizeHandleRects())
            painter.drawRect(handle);
    }
}

void CanvasWidget::paintNodes(QPainter &painter, const Shape &shape)
{
    const QColor highlight = palette().color(QPalette::Highlight);

    // Outline of the path on top, so it is visible even without a stroke.
    QPen outline(highlight, 1);
    outline.setCosmetic(true);
    painter.setPen(outline);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(viewTransform().map(shape.path()));

    const PathData &data = shape.pathData();
    if (activeNodeIsValid()) {
        const PathNode &node = data[m_activeNode.subpath].nodes[m_activeNode.node];
        painter.setPen(QPen(highlight, 1));
        for (const QPointF &handle : {node.in, node.out}) {
            if (handle == node.point)
                continue;
            painter.drawLine(toWidget(node.point), toWidget(handle));
            painter.setBrush(highlight);
            painter.drawEllipse(toWidget(handle), 3.5, 3.5);
            painter.setBrush(Qt::NoBrush);
        }
    }

    for (int s = 0; s < int(data.size()); ++s) {
        for (int n = 0; n < int(data[s].nodes.size()); ++n) {
            const bool active = m_activeNode.subpath == s && m_activeNode.node == n;
            const bool secondary = m_secondaryNode.subpath == s && m_secondaryNode.node == n;
            const PathNode &node = data[s].nodes[n];
            painter.setPen(QPen(highlight, 1));
            QColor fill = Qt::white;
            if (active)
                fill = highlight;
            else if (secondary)
                fill = highlight.lighter(160);
            painter.setBrush(fill);
            const QRectF r = squareAround(toWidget(node.point), kNodeSizePx);
            if (node.type == NodeType::Smooth)
                painter.drawEllipse(r);
            else
                painter.drawRect(r);
        }
    }
}

void CanvasWidget::paintBezierPreview(QPainter &painter)
{
    if (m_bezier.nodes.empty())
        return;
    const QColor highlight = palette().color(QPalette::Highlight);

    // The curve so far, plus the segment that would be added at the mouse position.
    PathData preview{m_bezier};
    if (m_drag != Drag::BezierHandle)
        preview.front().nodes.push_back(PathNode::corner(m_cursor));
    QPen pen(curveStyle(false).stroke, 1);
    pen.setCosmetic(true);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(viewTransform().map(pathops::toPainterPath(preview)));

    const PathNode &last = m_bezier.nodes.back();
    painter.setPen(QPen(highlight, 1));
    for (const QPointF &handle : {last.in, last.out}) {
        if (handle == last.point)
            continue;
        painter.drawLine(toWidget(last.point), toWidget(handle));
        painter.setBrush(highlight);
        painter.drawEllipse(toWidget(handle), 3.5, 3.5);
        painter.setBrush(Qt::NoBrush);
    }
    for (std::size_t i = 0; i < m_bezier.nodes.size(); ++i) {
        painter.setBrush(i == 0 ? QBrush(highlight) : QBrush(Qt::white));
        painter.drawRect(squareAround(toWidget(m_bezier.nodes[i].point), kNodeSizePx));
    }
}

void CanvasWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    // Until the user zooms or scrolls, the page stays fitted as the window changes size.
    if ((m_needsInitialFit || m_fitOnResize) && width() > 0 && height() > 0) {
        m_needsInitialFit = false;
        fitPage();
    }
    emit viewChanged();
}

// ---------------------------------------------------------------------------------------------
// Mouse

void CanvasWidget::mousePressEvent(QMouseEvent *event)
{
    const QPointF pos = event->position();
    const QPointF docPos = toDocument(pos);
    const bool shift = event->modifiers() & Qt::ShiftModifier;

    if (event->button() == Qt::MiddleButton
        || (event->button() == Qt::LeftButton && (m_spaceHeld || m_tool == Tool::Pan))) {
        m_drag = Drag::Pan;
        m_panLast = pos;
        m_spacePanned = m_spaceHeld;
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (m_tool == Tool::Zoom && event->button() == Qt::RightButton) {
        zoomAround(pos, 1.0 / kZoomStep);
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;

    if (m_editText) {
        // Clicking inside the text being edited places the caret (Shift: extends the selected
        // part) and dragging selects; clicking anywhere else ends the editing.
        if (m_editText->contains(docPos, kHitTolerancePx / m_zoom)) {
            const int index = textIndexAt(pos);
            if (shift) {
                setTextCursor(index, true);
            } else {
                setTextCursor(index);
                m_textAnchor = index;
            }
            m_drag = Drag::TextSelect;
            return;
        }
        finishTextEdit();
    }

    m_dragStart = m_dragLast = docPos;
    const double tolerance = kHitTolerancePx / m_zoom;

    switch (m_tool) {
    case Tool::Zoom:
        m_drag = Drag::ZoomBox;
        m_zoomBox = QRectF(docPos, docPos);
        break;

    case Tool::Pan:
        break; // handled above

    case Tool::Text: {
        Shape *hit = m_document->shapeAt(docPos, tolerance);
        if (hit && hit->kind() == ShapeKind::Text) {
            beginTextEdit(hit);
        } else {
            // A click starts artistic text, a drag draws a paragraph frame (on release).
            m_drag = Drag::TextFrame;
            m_dragStart = m_dragLast = snapPoint(docPos);
            m_creating = QRectF(m_dragStart, m_dragStart);
        }
        break;
    }

    case Tool::Select: {
        if (m_rotateMode && !shift) {
            if (rotationCenterAt(pos)) {
                m_drag = Drag::RotationCenter;
                break;
            }
            if (const int handle = resizeHandleAt(pos); handle >= 0) {
                // Corners rotate, edge midpoints skew.
                m_drag = handle % 2 == 0 ? Drag::Rotate : Drag::Skew;
                m_resizeHandle = handle;
                m_transformBox = selectionBounds();
                m_transformCenter = rotationCenter();
                m_liveTransform = QTransform();
                break;
            }
        }
        if (const int handle = resizeHandleAt(pos); handle >= 0 && !shift) {
            m_drag = Drag::Resize;
            m_resizeHandle = handle;
            m_resizeOriginal = selectionBounds();
            m_resizeOriginals.clear();
            for (const Shape *shape : m_selection)
                m_resizeOriginals.push_back(shape->bounds());
            break;
        }
        if (const int guide = guideAt(pos); guide >= 0) {
            // Drag a guideline; dropping it on a ruler deletes it.
            m_guide = guide;
            m_guideBefore = m_document->guides()[guide];
            m_draggedGuide = m_guideBefore;
            m_drag = Drag::Guide;
            break;
        }
        Shape *hit = m_document->shapeAt(docPos, tolerance);
        if (!hit) {
            // Empty space: start a rubber band. Shift keeps the current selection.
            m_drag = Drag::Marquee;
            m_marquee = QRectF(docPos, docPos);
            m_marqueeAdds = shift;
            break;
        }
        if (shift) {
            std::vector<Shape *> selection = m_selection;
            if (isSelected(hit))
                std::erase(selection, hit);
            else
                selection.push_back(hit);
            setSelection(std::move(selection));
            break;
        }
        m_pressedSelected = isSelected(hit);
        if (!m_pressedSelected)
            setSelectedShape(hit);
        m_drag = Drag::Move; // moves everything selected
        m_moveOriginal = selectionBounds();
        break;
    }

    case Tool::Node:
        if (const auto handle = nodeHandleAt(pos)) {
            m_drag = Drag::NodeHandle;
            m_draggedHandle = *handle;
            m_pathBeforeEdit = selectedPath()->pathData();
        } else if (const NodeRef node = nodeAt(pos); node.isValid()) {
            if (shift) {
                // Shift+click picks a second node (for joining) and keeps the first one active.
                if (activeNodeIsValid() && !(node.subpath == m_activeNode.subpath && node.node == m_activeNode.node))
                    setActiveNode(m_activeNode, node);
                else
                    setActiveNode(node);
                break;
            }
            setActiveNode(node);
            m_drag = Drag::NodeMove;
            m_pathBeforeEdit = selectedPath()->pathData();
        } else if (selectedPath() && pathops::nearestSegment(selectedPath()->pathData(), docPos, tolerance)) {
            setActiveNode({}); // clicked the outline: keep the curve selected
        } else {
            setSelectedShape(m_document->shapeAt(docPos, tolerance));
        }
        break;

    case Tool::Bezier:
        if (m_bezier.nodes.size() >= 2
            && distance(toWidget(m_bezier.nodes.front().point), pos) <= kCloseDistancePx) {
            m_bezier.closed = true;
            finishCurve();
            return;
        }
        m_bezier.nodes.push_back(PathNode::corner(snapPoint(docPos)));
        m_drag = Drag::BezierHandle;
        update();
        break;

    case Tool::Freehand:
        m_freehand = {docPos};
        m_drag = Drag::Freehand;
        break;

    case Tool::Line:
    case Tool::Rectangle:
    case Tool::Ellipse:
    case Tool::Polygon:
    case Tool::Star:
    case Tool::Spiral:
    case Tool::GraphPaper:
        m_drag = Drag::Create;
        m_dragStart = m_dragLast = snapPoint(docPos);
        m_creating = QRectF(m_dragStart, m_dragStart);
        m_lineEnd = m_dragStart;
        break;

    case Tool::DropShadow:
    case Tool::Contour:
    case Tool::Extrude:
    case Tool::Transparency: {
        if (const int handle = transparencyHandleAt(pos); handle >= 0) {
            m_effectHandle = handle;
            captureEffectStyles();
            m_drag = Drag::EffectHandle;
            break;
        }
        Shape *hit = m_document->shapeAt(docPos, tolerance);
        if (!hit) {
            setSelection({});
            break;
        }
        if (!isSelected(hit))
            setSelectedShape(hit);
        captureEffectStyles();
        m_drag = Drag::Effect;
        break;
    }

    case Tool::Envelope: {
        // Drag the envelope's corners and handles of the selected object; a click selects.
        if (const int handle = envelopeHandleAt(pos); handle >= 0) {
            m_envelopeHandle = handle;
            m_envelopeBefore = envelopeShape()->envelope();
            m_drag = Drag::EnvelopeHandle;
            break;
        }
        Shape *hit = m_document->shapeAt(docPos, tolerance);
        if (!hit)
            setSelection({});
        else
            setSelectedShape(hit);
        update();
        break;
    }

    case Tool::Blend: {
        // Drag from one object to another to blend them; a click selects (a blend, to change it).
        Shape *hit = m_document->shapeAt(docPos, tolerance);
        if (!hit) {
            setSelection({});
            break;
        }
        setSelectedShape(hit);
        if (blend::canBlend(*hit)) {
            m_blendStart = hit;
            m_dragStart = docPos;
            m_drag = Drag::Blend;
        }
        break;
    }

    case Tool::Eyedropper:
        // The first click picks up a colour; later clicks apply it. Shift+click picks again.
        if (!m_eyedropperColor || shift)
            setEyedropperColor(colorAt(pos));
        else
            applyEyedropper(docPos);
        break;
    }
}

void CanvasWidget::mouseMoveEvent(QMouseEvent *event)
{
    const QPointF pos = event->position();
    const QPointF docPos = toDocument(pos);
    m_cursor = docPos;
    emit cursorMoved(docPos);

    switch (m_drag) {
    case Drag::None:
        setHoverCursor(pos);
        if (!m_bezier.nodes.empty())
            update();
        break;

    case Drag::Pan:
        m_pan += pos - m_panLast;
        m_panLast = pos;
        viewUpdated(false);
        break;

    case Drag::ZoomBox:
        m_zoomBox.setBottomRight(docPos);
        update();
        break;

    case Drag::Move: {
        // Ctrl keeps the move horizontal or vertical; the moved box snaps as a whole.
        const bool constrain = event->modifiers() & Qt::ControlModifier;
        QPointF total = docPos - m_dragStart;
        if (constrain) {
            if (std::abs(total.x()) >= std::abs(total.y()))
                total.ry() = 0;
            else
                total.rx() = 0;
        }
        QPointF snap = snapBox(m_moveOriginal.translated(total));
        if (constrain) {
            if (total.y() == 0)
                snap.ry() = 0;
            else
                snap.rx() = 0;
        }
        total += snap;
        const QPointF delta = m_dragStart + total - m_dragLast;
        for (Shape *shape : m_selection)
            shape->moveBy(delta);
        m_dragLast = m_dragStart + total;
        if (!m_selection.empty())
            m_document->notifyShapeChanged(m_selection.front());
        break;
    }

    case Drag::Guide:
        updateGuideDrag(pos);
        break;

    case Drag::Rotate: {
        const QPointF c = m_transformCenter;
        const double from = std::atan2(m_dragStart.y() - c.y(), m_dragStart.x() - c.x());
        const double to = std::atan2(docPos.y() - c.y(), docPos.x() - c.x());
        double degrees = (to - from) * 180.0 / std::numbers::pi;
        if (event->modifiers() & Qt::ControlModifier)
            degrees = std::round(degrees / 15.0) * 15.0; // Ctrl: 15Â° steps
        applyLiveTransform(QTransform::fromTranslate(-c.x(), -c.y()) * QTransform().rotate(degrees)
                           * QTransform::fromTranslate(c.x(), c.y()));
        break;
    }

    case Drag::Skew: {
        // The dragged edge slides along itself; the rotation centre stays put.
        const QPointF c = m_transformCenter;
        const QPointF d = docPos - m_dragStart;
        QTransform shear;
        if (m_resizeHandle == 1 || m_resizeHandle == 5) {
            const double edge = m_resizeHandle == 1 ? m_transformBox.top() : m_transformBox.bottom();
            if (std::abs(edge - c.y()) > 1e-9)
                shear = QTransform(1, 0, d.x() / (edge - c.y()), 1, 0, 0);
        } else {
            const double edge = m_resizeHandle == 3 ? m_transformBox.right() : m_transformBox.left();
            if (std::abs(edge - c.x()) > 1e-9)
                shear = QTransform(1, d.y() / (edge - c.x()), 0, 1, 0, 0);
        }
        applyLiveTransform(QTransform::fromTranslate(-c.x(), -c.y()) * shear * QTransform::fromTranslate(c.x(), c.y()));
        break;
    }

    case Drag::RotationCenter:
        m_rotationCenter = snapPoint(docPos);
        update();
        break;

    case Drag::TextFrame:
        m_creating = QRectF(m_dragStart, snapPoint(docPos)).normalized();
        update();
        break;

    case Drag::TextSelect:
        if (m_editText) {
            m_textCursor = textIndexAt(pos);
            update();
            emit textEditingChanged();
        }
        break;

    case Drag::Effect:
        applyEffectDrag(docPos, event->modifiers());
        break;

    case Drag::Blend:
        update(); // the line towards the pointer
        break;

    case Drag::EnvelopeHandle:
        dragEnvelopeHandle(snapPoint(docPos));
        break;

    case Drag::EffectHandle:
        if (!m_effectBefore.empty()) {
            auto &[leaf, before] = m_effectBefore.front();
            Style style = before;
            Gradient g = *before.transparency;
            const QPointF fraction = toFraction(*leaf, snapPoint(docPos));
            if (g.type == Gradient::Type::Radial) {
                if (m_effectHandle == 0) {
                    g.center = fraction;
                } else {
                    const QRectF box = leaf->localBounds();
                    const QPointF local = leaf->shapeTransform().inverted().map(docPos);
                    const QPointF centre(box.left() + g.center.x() * box.width(), box.top() + g.center.y() * box.height());
                    g.radius = distance(local, centre) / std::max({box.width(), box.height(), 1e-9});
                }
            } else if (m_effectHandle == 0) {
                g.start = fraction;
            } else {
                g.end = fraction;
            }
            style.transparency = g;
            leaf->setStyle(style);
            m_document->notifyShapeChanged(leaf);
        }
        break;

    case Drag::Resize: {
        // Handles 0-7 go clockwise from top-left; each moves the edges it sits on.
        const int h = m_resizeHandle;
        const QPointF to = snapPoint(docPos);
        double left = m_resizeOriginal.left(), right = m_resizeOriginal.right();
        double top = m_resizeOriginal.top(), bottom = m_resizeOriginal.bottom();
        if (h == 0 || h == 6 || h == 7)
            left = to.x();
        if (h == 2 || h == 3 || h == 4)
            right = to.x();
        if (h == 0 || h == 1 || h == 2)
            top = to.y();
        if (h == 4 || h == 5 || h == 6)
            bottom = to.y();
        QRectF bounds = QRectF(QPointF(left, top), QPointF(right, bottom)).normalized();
        const double minimum = 1.0 / m_zoom;
        bounds.setWidth(std::max(bounds.width(), minimum));
        bounds.setHeight(std::max(bounds.height(), minimum));

        // Every selected shape keeps its place inside the combined box.
        const QRectF &from = m_resizeOriginal;
        const double sx = from.width() > 0 ? bounds.width() / from.width() : 1.0;
        const double sy = from.height() > 0 ? bounds.height() / from.height() : 1.0;
        QTransform t;
        t.translate(bounds.left(), bounds.top());
        t.scale(sx, sy);
        t.translate(-from.left(), -from.top());
        for (std::size_t i = 0; i < m_selection.size(); ++i)
            m_selection[i]->setBounds(t.mapRect(m_resizeOriginals[i]));
        if (!m_selection.empty())
            m_document->notifyShapeChanged(m_selection.front());
        break;
    }

    case Drag::Marquee:
        m_marquee = QRectF(m_dragStart, docPos).normalized();
        update();
        break;

    case Drag::Create: {
        // As in other desktop editors: Ctrl constrains (square, circle, regular shape, line
        // angles in 15Â° steps), Shift draws from the centre.
        const bool constrain = event->modifiers() & Qt::ControlModifier;
        const bool fromCenter = event->modifiers() & Qt::ShiftModifier;
        const QPointF end = snapPoint(docPos);
        QPointF d = end - m_dragStart;
        if (m_tool == Tool::Line) {
            if (constrain && !d.isNull()) {
                const double step = std::numbers::pi / 12.0;
                const double angle = std::round(std::atan2(d.y(), d.x()) / step) * step;
                const double length = std::hypot(d.x(), d.y());
                d = QPointF(std::cos(angle), std::sin(angle)) * length;
            }
            m_lineEnd = m_dragStart + d;
            update();
            break;
        }
        if (constrain) {
            const double side = std::max(std::abs(d.x()), std::abs(d.y()));
            d = QPointF(std::copysign(side, d.x()), std::copysign(side, d.y()));
        }
        m_creating = (fromCenter ? QRectF(m_dragStart - d, m_dragStart + d) : QRectF(m_dragStart, m_dragStart + d))
                         .normalized();
        update();
        break;
    }

    case Drag::NodeMove: {
        Shape *path = selectedPath();
        PathData data = path->pathData();
        PathNode &node = data[m_activeNode.subpath].nodes[m_activeNode.node];
        const QPointF to = snapPoint(docPos);
        const QPointF delta = to - m_dragLast;
        node.point += delta;
        node.in += delta;
        node.out += delta;
        m_dragLast = to;
        path->setPathData(std::move(data));
        m_document->notifyShapeChanged(path);
        break;
    }

    case Drag::NodeHandle: {
        Shape *path = selectedPath();
        PathData data = path->pathData();
        PathNode &node = data[m_activeNode.subpath].nodes[m_activeNode.node];
        QPointF &moved = m_draggedHandle == NodeHandle::Out ? node.out : node.in;
        QPointF &opposite = m_draggedHandle == NodeHandle::Out ? node.in : node.out;
        moved = docPos;
        if (event->modifiers() & Qt::AltModifier) {
            node.type = NodeType::Corner; // Alt breaks the symmetry
        } else if (node.type == NodeType::Smooth) {
            // Keep the opposite handle on the same line, with its own length.
            const double handleLength = distance(moved, node.point);
            const double oppositeLength = distance(opposite, node.point);
            if (handleLength > 0.0 && oppositeLength > 0.0)
                opposite = node.point - (moved - node.point) * (oppositeLength / handleLength);
        }
        path->setPathData(std::move(data));
        m_document->notifyShapeChanged(path);
        break;
    }

    case Drag::BezierHandle: {
        // Dragging while placing a node pulls out symmetric handles: a smooth node.
        PathNode &node = m_bezier.nodes.back();
        if (distance(toWidget(node.point), pos) > 2.0) {
            node.out = docPos;
            node.in = 2 * node.point - docPos;
            node.type = NodeType::Smooth;
        } else {
            node = PathNode::corner(node.point);
        }
        update();
        break;
    }

    case Drag::Freehand:
        if (distance(toWidget(m_freehand.back()), pos) >= 1.0) {
            m_freehand.push_back(docPos);
            update();
        }
        break;
    }
}

void CanvasWidget::mouseReleaseEvent(QMouseEvent *event)
{
    const Drag drag = m_drag;
    m_drag = Drag::None;

    switch (drag) {
    case Drag::None:
    case Drag::BezierHandle:
        break;

    case Drag::Pan:
        updateCursor();
        break;

    case Drag::ZoomBox: {
        // A click zooms in (Shift: out) around the cursor; a drag zooms to the rectangle.
        const QRectF box = m_zoomBox.normalized();
        m_zoomBox = QRectF();
        if (box.width() * m_zoom < kMinMarqueePx && box.height() * m_zoom < kMinMarqueePx) {
            const bool out = event->modifiers() & Qt::ShiftModifier;
            zoomAround(event->position(), out ? 1.0 / kZoomStep : kZoomStep);
        } else {
            zoomToRect(box);
        }
        break;
    }

    case Drag::Move: {
        const QPointF total = m_dragLast - m_dragStart;
        if (!total.isNull()) {
            m_undoStack->beginMacro(tr("Move"));
            for (Shape *shape : m_selection)
                m_undoStack->push(new MoveShapeCommand(m_document, shape, total, true));
            m_undoStack->endMacro();
        } else if (m_pressedSelected && !(event->modifiers() & Qt::ShiftModifier)) {
            // Clicking the selection again switches between resize and rotate/skew handles.
            setRotateMode(!m_rotateMode);
        }
        m_pressedSelected = false;
        break;
    }

    case Drag::Rotate:
        commitLiveTransform(tr("Rotate"));
        break;

    case Drag::Skew:
        commitLiveTransform(tr("Skew"));
        break;

    case Drag::RotationCenter:
        break;

    case Drag::TextSelect:
        if (m_textAnchor == m_textCursor)
            m_textAnchor = -1;
        break;

    case Drag::Effect:
        // A click (no drag) only selects.
        if (distance(toWidget(m_dragStart), event->position()) < kMinCreateSizePx) {
            for (const auto &[leaf, before] : m_effectBefore)
                leaf->setStyle(before);
            m_effectBefore.clear();
            update();
        } else {
            commitEffectDrag();
        }
        break;

    case Drag::EffectHandle:
        commitEffectDrag();
        m_effectHandle = -1;
        break;

    case Drag::EnvelopeHandle:
        if (Shape *shape = envelopeShape()) {
            const std::optional<Envelope> after = shape->envelope();
            shape->setEnvelope(m_envelopeBefore);
            if (after != m_envelopeBefore)
                m_undoStack->push(new SetEnvelopeCommand(m_document, shape, after));
        }
        m_envelopeHandle = -1;
        break;

    case Drag::Blend: {
        Shape *from = m_blendStart;
        m_blendStart = nullptr;
        Shape *to = m_document->shapeAt(toDocument(event->position()), kHitTolerancePx / m_zoom);
        if (from && to && to != from && blend::canBlend(*to))
            blendShapes(from, to);
        update();
        break;
    }

    case Drag::TextFrame: {
        const QRectF frame = m_creating;
        m_creating = QRectF();
        if (frame.width() * m_zoom >= 8 && frame.height() * m_zoom >= 8) {
            beginNewText(frame.topLeft(), frame.size());
        } else {
            // The click marks the top-left of the text; the first baseline sits one ascent below.
            const double ascent = m_defaultTextFormat.size * 0.8;
            beginNewText(m_dragStart + QPointF(0, ascent));
        }
        break;
    }

    case Drag::Resize: {
        bool changed = false;
        for (std::size_t i = 0; i < m_selection.size(); ++i)
            changed = changed || m_selection[i]->bounds() != m_resizeOriginals[i];
        if (changed) {
            m_undoStack->beginMacro(tr("Resize"));
            for (std::size_t i = 0; i < m_selection.size(); ++i)
                m_undoStack->push(new SetBoundsCommand(m_document, m_selection[i], m_resizeOriginals[i],
                                                       m_selection[i]->bounds()));
            m_undoStack->endMacro();
        }
        m_resizeHandle = -1;
        setHoverCursor(event->position());
        break;
    }

    case Drag::Marquee: {
        // Objects lying completely inside the rubber band are selected.
        std::vector<Shape *> selection = m_marqueeAdds ? m_selection : std::vector<Shape *>{};
        const QRectF band = m_marquee;
        m_marquee = QRectF();
        if (band.width() * m_zoom >= kMinMarqueePx || band.height() * m_zoom >= kMinMarqueePx) {
            for (Shape *shape : m_document->editableShapes()) {
                if (band.contains(shape->bounds()) && std::find(selection.begin(), selection.end(), shape) == selection.end())
                    selection.push_back(shape);
            }
        }
        setSelection(std::move(selection));
        update();
        break;
    }

    case Drag::Create: {
        const QRectF bounds = m_creating;
        m_creating = QRectF();
        if (m_tool == Tool::Line) {
            if (distance(toWidget(m_dragStart), toWidget(m_lineEnd)) >= kMinCreateSizePx)
                addNewShape(std::make_unique<Shape>(generators::line(m_dragStart, m_lineEnd), curveStyle(false)));
        } else if (bounds.width() * m_zoom >= kMinCreateSizePx && bounds.height() * m_zoom >= kMinCreateSizePx) {
            if (auto shape = createToolShape(bounds))
                addNewShape(std::move(shape));
        }
        update();
        break;
    }

    case Drag::Guide:
        m_drag = Drag::Guide; // endGuideDrag finishes it
        endGuideDrag(event->position());
        setHoverCursor(event->position());
        break;

    case Drag::NodeMove:
        commitPathEdit(m_pathBeforeEdit, tr("Move node"));
        break;

    case Drag::NodeHandle:
        commitPathEdit(m_pathBeforeEdit, tr("Adjust curve"));
        break;

    case Drag::Freehand: {
        std::vector<QPointF> points = std::move(m_freehand);
        m_freehand.clear();
        const bool closed = points.size() > 2
                            && distance(toWidget(points.front()), toWidget(points.back())) <= kCloseDistancePx;
        SubPath curve = pathops::fitFreehand(points, kFreehandTolerancePx / m_zoom, closed);
        if (curve.nodes.size() >= 2) {
            const bool isClosed = curve.closed;
            addNewShape(std::make_unique<Shape>(PathData{std::move(curve)}, curveStyle(isClosed)));
        }
        update();
        break;
    }
    }
    QWidget::mouseReleaseEvent(event);
}

void CanvasWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;

    if (m_tool == Tool::Bezier) {
        finishCurve();
        return;
    }

    // Double-click in the text being edited selects a word.
    if (m_editText && m_editText->contains(toDocument(event->position()), kHitTolerancePx / m_zoom)) {
        const QString &text = m_editText->textData().text;
        const int index = textIndexAt(event->position());
        int from = index, to = index;
        while (from > 0 && text[from - 1].isLetterOrNumber())
            --from;
        while (to < text.size() && text[to].isLetterOrNumber())
            ++to;
        m_textAnchor = from;
        setTextCursor(to, true);
        m_textAnchor = from;
        update();
        return;
    }

    if (m_tool == Tool::Select) {
        // Double-click a text to type into it.
        Shape *hit = m_document->shapeAt(toDocument(event->position()), kHitTolerancePx / m_zoom);
        if (hit && hit->kind() == ShapeKind::Text)
            beginTextEdit(hit);
        return;
    }

    if (m_tool == Tool::Node && selectedPath()) {
        // Double-click a node to delete it, or a segment to add a node there.
        if (const NodeRef node = nodeAt(event->position()); node.isValid()) {
            setActiveNode(node);
            removeActiveNode();
            return;
        }
        Shape *path = selectedPath();
        const auto hit = pathops::nearestSegment(path->pathData(), toDocument(event->position()),
                                                 kHitTolerancePx / m_zoom);
        if (hit) {
            PathData data = path->pathData();
            const int index = pathops::insertNode(data, hit->subpath, hit->segment, hit->t);
            m_undoStack->push(new SetPathCommand(m_document, path, path->pathData(), data, tr("Add node")));
            setActiveNode({hit->subpath, index});
        }
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void CanvasWidget::wheelEvent(QWheelEvent *event)
{
    const int delta = event->angleDelta().y();
    if (delta == 0)
        return;
    zoomAround(event->position(), std::pow(1.0015, delta));
    event->accept();
}

// ---------------------------------------------------------------------------------------------
// Keyboard

void CanvasWidget::keyPressEvent(QKeyEvent *event)
{
    if (m_editText && handleTextKey(event))
        return;
    if (event->key() == Qt::Key_Space) {
        if (!event->isAutoRepeat()) {
            m_spaceHeld = true;
            m_spacePanned = false;
            updateCursor();
        }
        return;
    }
    if (m_tool == Tool::Eyedropper && m_eyedropperColor && event->key() == Qt::Key_Escape) {
        setEyedropperColor(std::nullopt); // back to picking a colour
        return;
    }
    if (!m_bezier.nodes.empty()
        && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter || event->key() == Qt::Key_Escape)) {
        finishCurve();
        return;
    }
    if (!m_selection.empty() && m_drag == Drag::None) {
        const double step = m_nudge * (event->modifiers() & Qt::ShiftModifier ? 10.0 : 1.0);
        QPointF offset;
        switch (event->key()) {
        case Qt::Key_Left:
            offset = {-step, 0};
            break;
        case Qt::Key_Right:
            offset = {step, 0};
            break;
        case Qt::Key_Up:
            offset = {0, -step};
            break;
        case Qt::Key_Down:
            offset = {0, step};
            break;
        default:
            break;
        }
        if (!offset.isNull()) {
            nudgeSelection(offset);
            return;
        }
    }
    QWidget::keyPressEvent(event);
}

void CanvasWidget::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        // Only a Space pressed outside text editing counts (typing a space must not switch tools).
        const bool wasHeld = m_spaceHeld;
        m_spaceHeld = false;
        // A tap (no panning) switches to the pick tool, and the next tap back.
        if (wasHeld && !m_spacePanned && m_drag == Drag::None && !m_editText) {
            if (m_tool != Tool::Select) {
                const Tool previous = m_tool;
                setTool(Tool::Select);
                m_toolBeforeSpace = previous;
            } else if (m_toolBeforeSpace) {
                const Tool previous = *m_toolBeforeSpace;
                m_toolBeforeSpace.reset();
                setTool(previous);
            }
        }
        updateCursor();
        return;
    }
    QWidget::keyReleaseEvent(event);
}

} // namespace opendraw
