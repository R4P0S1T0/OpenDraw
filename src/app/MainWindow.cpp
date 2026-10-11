#include "MainWindow.h"

#include "CanvasWidget.h"
#include "ColorPalette.h"
#include "PropertyBar.h"
#include "core/Commands.h"
#include "core/Document.h"
#include "core/SvgIO.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QColorDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QSaveFile>
#include <QScreen>
#include <QStatusBar>
#include <QToolBar>
#include <QUndoStack>

namespace opendraw {

namespace {

// Simple drawn icons so the first release needs no image assets.
QIcon toolIcon(CanvasWidget::Tool tool, const QColor &ink)
{
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(ink, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    switch (tool) {
    case CanvasWidget::Tool::Select: {
        const QPointF arrow[] = {{9, 5}, {9, 25}, {14, 20}, {18, 28}, {21, 27}, {17, 19}, {24, 19}};
        p.setBrush(ink);
        p.drawPolygon(arrow, 7);
        break;
    }
    case CanvasWidget::Tool::Node: {
        // Hollow arrow touching a node on a curve.
        QPainterPath curve;
        curve.moveTo(4, 27);
        curve.cubicTo(8, 12, 18, 30, 28, 8);
        p.setPen(QPen(ink, 1.6));
        p.drawPath(curve);
        const QPointF arrow[] = {{12, 6}, {12, 20}, {15.5, 16.5}, {18.5, 22}, {20.5, 21}, {17.5, 15.5}, {22, 15.5}};
        p.setPen(QPen(ink, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::white);
        p.drawPolygon(arrow, 7);
        p.setBrush(ink);
        p.drawRect(QRectF(25, 5, 5, 5));
        break;
    }
    case CanvasWidget::Tool::Freehand: {
        QPainterPath squiggle;
        squiggle.moveTo(4, 22);
        squiggle.cubicTo(8, 6, 13, 6, 14, 16);
        squiggle.cubicTo(15, 26, 22, 26, 27, 10);
        p.setBrush(Qt::NoBrush);
        p.drawPath(squiggle);
        break;
    }
    case CanvasWidget::Tool::Bezier: {
        QPainterPath curve;
        curve.moveTo(5, 25);
        curve.cubicTo(5, 8, 27, 24, 27, 7);
        p.setPen(QPen(ink, 1.8));
        p.setBrush(Qt::NoBrush);
        p.drawPath(curve);
        p.setPen(QPen(ink, 1));
        p.drawLine(QPointF(27, 7), QPointF(20, 3));
        p.drawLine(QPointF(27, 7), QPointF(30, 13));
        p.setBrush(ink);
        p.drawRect(QRectF(2.5, 22.5, 5, 5));
        p.drawRect(QRectF(24.5, 4.5, 5, 5));
        break;
    }
    case CanvasWidget::Tool::Rectangle:
        p.drawRect(QRectF(6, 9, 20, 14));
        break;
    case CanvasWidget::Tool::Ellipse:
        p.drawEllipse(QRectF(5, 8, 22, 16));
        break;
    }
    return QIcon(pixmap);
}

QIcon swatchIcon(const QColor &color, bool outline)
{
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF box(5.5, 5.5, 21, 21);
    if (outline) {
        p.setPen(QPen(color.alpha() > 0 ? color : QColor(0, 0, 0, 120), color.alpha() > 0 ? 4 : 1));
        p.setBrush(Qt::NoBrush);
    } else {
        p.setPen(QPen(QColor(0, 0, 0, 120), 1));
        p.setBrush(color.alpha() > 0 ? QBrush(color) : QBrush(Qt::NoBrush));
    }
    p.drawRoundedRect(box, 3, 3);
    if (color.alpha() == 0) {
        p.setPen(QPen(Qt::red, 2));
        p.drawLine(QPointF(7, 25), QPointF(25, 7));
    }
    return QIcon(pixmap);
}

QString describeColor(const QColor &color)
{
    return color.alpha() == 0 ? QCoreApplication::translate("MainWindow", "none") : color.name().toUpper();
}

const QString kSvgFilter = QStringLiteral("SVG (*.svg)");

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_document(new Document(this))
    , m_undoStack(new QUndoStack(this))
    , m_canvas(new CanvasWidget(m_document, m_undoStack, this))
{
    setCentralWidget(m_canvas);
    createActions();

    m_propertyBar = new PropertyBar(m_document, m_undoStack, this);
    addToolBar(Qt::TopToolBarArea, m_propertyBar);
    addToolBar(Qt::TopToolBarArea, m_nodeBar);
    m_nodeBar->hide();
    connect(m_canvas, &CanvasWidget::nodeSelectionChanged, this, &MainWindow::updateNodeActions);
    connect(m_document, &Document::changed, this, &MainWindow::updateNodeActions);
    updateNodeActions();

    auto *palette = new ColorPalette(this);
    QToolBar *paletteBar = new QToolBar(tr("Color Palette"), this);
    paletteBar->setObjectName(QStringLiteral("colorPalette"));
    paletteBar->setMovable(false);
    paletteBar->addWidget(palette);
    addToolBar(Qt::RightToolBarArea, paletteBar);
    connect(palette, &ColorPalette::fillChosen, this, &MainWindow::applyFill);
    connect(palette, &ColorPalette::outlineChosen, this, &MainWindow::applyOutline);

    m_styleLabel = new QLabel(this);
    m_zoomLabel = new QLabel(this);
    m_positionLabel = new QLabel(this);
    statusBar()->addWidget(m_styleLabel);
    statusBar()->addPermanentWidget(m_positionLabel);
    statusBar()->addPermanentWidget(m_zoomLabel);

    connect(m_canvas, &CanvasWidget::zoomChanged, this, [this](double zoom) {
        m_zoomLabel->setText(tr("Zoom %1%").arg(qRound(zoom * 100)));
    });
    connect(m_canvas, &CanvasWidget::cursorMoved, this, [this](const QPointF &pos) {
        m_positionLabel->setText(tr("x %1 mm   y %2 mm")
                                     .arg(pos.x() / kUnitsPerMm, 0, 'f', 1)
                                     .arg(pos.y() / kUnitsPerMm, 0, 'f', 1));
    });
    connect(m_canvas, &CanvasWidget::selectionChanged, this, [this] {
        m_propertyBar->setShape(m_canvas->selectedShape());
        updateStyleIndicators();
    });
    connect(m_document, &Document::changed, this, &MainWindow::updateStyleIndicators);
    connect(m_undoStack, &QUndoStack::cleanChanged, this, &MainWindow::updateTitle);

    // Fit comfortably on the current screen, whatever its size and scaling.
    if (const QScreen *screen = this->screen()) {
        const QRect available = screen->availableGeometry();
        resize(available.size() * 0.85);
        move(available.center() - rect().center());
    }
    updateTitle();
    updateStyleIndicators();
}

void MainWindow::createActions()
{
    const QColor ink = palette().color(QPalette::WindowText);

    // File
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(tr("&New"), QKeySequence::New, this, &MainWindow::newDocument);
    fileMenu->addAction(tr("&Open…"), QKeySequence::Open, this, &MainWindow::open);
    fileMenu->addAction(tr("&Save"), QKeySequence::Save, this, &MainWindow::save);
    fileMenu->addAction(tr("Save &As…"), QKeySequence::SaveAs, this, &MainWindow::saveAs);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("&Quit"), QKeySequence::Quit, this, &QWidget::close);

    // Edit
    QMenu *editMenu = menuBar()->addMenu(tr("&Edit"));
    QAction *undo = m_undoStack->createUndoAction(this, tr("&Undo"));
    undo->setShortcut(QKeySequence::Undo);
    QAction *redo = m_undoStack->createRedoAction(this, tr("&Redo"));
    redo->setShortcut(QKeySequence::Redo);
    editMenu->addAction(undo);
    editMenu->addAction(redo);
    editMenu->addSeparator();
    QAction *deleteAction = editMenu->addAction(tr("&Delete"), m_canvas, &CanvasWidget::deleteSelection);
    deleteAction->setShortcuts({QKeySequence::Delete, QKeySequence(Qt::Key_Backspace)});
    editMenu->addAction(tr("Select &All"), QKeySequence::SelectAll, m_canvas, &CanvasWidget::selectAll);

    // View
    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(tr("Zoom &In"), QKeySequence::ZoomIn, m_canvas, &CanvasWidget::zoomIn);
    viewMenu->addAction(tr("Zoom &Out"), QKeySequence::ZoomOut, m_canvas, &CanvasWidget::zoomOut);
    viewMenu->addAction(tr("&Actual Size"), QKeySequence(Qt::CTRL | Qt::Key_1), m_canvas,
                        &CanvasWidget::zoomToActualSize);
    viewMenu->addAction(tr("&Fit Page"), QKeySequence(Qt::SHIFT | Qt::Key_F4), m_canvas, &CanvasWidget::fitPage);

    // Object
    QMenu *objectMenu = menuBar()->addMenu(tr("&Object"));
    objectMenu->addAction(tr("&Group"), QKeySequence(Qt::CTRL | Qt::Key_G), m_canvas, &CanvasWidget::groupSelection);
    objectMenu->addAction(tr("&Ungroup"), QKeySequence(Qt::CTRL | Qt::Key_U), m_canvas,
                          &CanvasWidget::ungroupSelection);
    objectMenu->addSeparator();

    QMenu *orderMenu = objectMenu->addMenu(tr("&Order"));
    orderMenu->addAction(tr("To &Front"), QKeySequence(Qt::SHIFT | Qt::Key_PageUp), m_canvas,
                         &CanvasWidget::orderToFront);
    orderMenu->addAction(tr("To &Back"), QKeySequence(Qt::SHIFT | Qt::Key_PageDown), m_canvas,
                         &CanvasWidget::orderToBack);
    orderMenu->addAction(tr("&Forward One"), QKeySequence(Qt::CTRL | Qt::Key_PageUp), m_canvas,
                         &CanvasWidget::orderForwardOne);
    orderMenu->addAction(tr("Back &One"), QKeySequence(Qt::CTRL | Qt::Key_PageDown), m_canvas,
                         &CanvasWidget::orderBackOne);

    // Single-letter shortcuts as in other desktop vector editors. With one object selected,
    // it is aligned to the page; with several, to the selection's bounds.
    QMenu *alignMenu = objectMenu->addMenu(tr("&Align and Distribute"));
    struct AlignCommand {
        QString text;
        Qt::Key key;
        arrange::Alignment alignment;
    };
    const AlignCommand alignCommands[] = {
        {tr("Align &Left"), Qt::Key_L, arrange::Alignment::Left},
        {tr("Align &Right"), Qt::Key_R, arrange::Alignment::Right},
        {tr("Align &Top"), Qt::Key_T, arrange::Alignment::Top},
        {tr("Align &Bottom"), Qt::Key_B, arrange::Alignment::Bottom},
        {tr("Align Centers &Horizontally"), Qt::Key_E, arrange::Alignment::CentersHorizontally},
        {tr("Align Centers &Vertically"), Qt::Key_C, arrange::Alignment::CentersVertically},
    };
    for (const AlignCommand &command : alignCommands) {
        alignMenu->addAction(command.text, QKeySequence(command.key), m_canvas,
                             [this, a = command.alignment] { m_canvas->align(a); });
    }
    alignMenu->addAction(tr("Center to &Page"), QKeySequence(Qt::Key_P), m_canvas, &CanvasWidget::centerOnPage);
    alignMenu->addSeparator();
    alignMenu->addAction(tr("Distribute Hori&zontally"), m_canvas,
                         [this] { m_canvas->distribute(Qt::Horizontal); });
    alignMenu->addAction(tr("Distribute V&ertically"), m_canvas, [this] { m_canvas->distribute(Qt::Vertical); });

    objectMenu->addSeparator();
    objectMenu->addAction(tr("Convert to &Curves"), QKeySequence(Qt::CTRL | Qt::Key_Q), m_canvas,
                          &CanvasWidget::convertSelectionToCurves);

    // Node commands: in Object > Nodes and on a bar shown while the shape tool is active.
    QMenu *nodesMenu = objectMenu->addMenu(tr("&Nodes"));
    m_nodeBar = new QToolBar(tr("Nodes"), this);
    m_nodeBar->setObjectName(QStringLiteral("nodes"));
    m_nodeBar->setMovable(false);
    m_nodeBar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    struct NodeCommand {
        QString text;
        QString tip;
        void (CanvasWidget::*slot)();
        bool join;
    };
    const NodeCommand nodeCommands[] = {
        {tr("Smooth"), tr("Make the node smooth: its handles stay in line"), &CanvasWidget::makeNodeSmooth, false},
        {tr("Corner"), tr("Make the node a corner: its handles move freely"), &CanvasWidget::makeNodeCorner, false},
        {tr("To Line"), tr("Straighten the segment after the node"), &CanvasWidget::segmentToLine, false},
        {tr("To Curve"), tr("Give the segment after the node handles so it can bend"), &CanvasWidget::segmentToCurve,
         false},
        {tr("Break"), tr("Break the curve at the node"), &CanvasWidget::breakNode, false},
        {tr("Join"), tr("Join two end nodes (Shift+click the second node)"), &CanvasWidget::joinNodes, true},
    };
    for (const NodeCommand &command : nodeCommands) {
        QAction *action = new QAction(command.text, this);
        action->setToolTip(command.tip);
        action->setStatusTip(command.tip);
        connect(action, &QAction::triggered, m_canvas, command.slot);
        m_nodeBar->addAction(action);
        nodesMenu->addAction(action);
        if (command.join)
            m_joinAction = action;
        else
            m_nodeActions << action;
    }

    // Tools
    QMenu *toolsMenu = menuBar()->addMenu(tr("&Tools"));
    QToolBar *toolBar = addToolBar(tr("Tools"));
    toolBar->setObjectName(QStringLiteral("tools"));
    addToolBar(Qt::LeftToolBarArea, toolBar);
    toolBar->setMovable(false);

    auto *tools = new QActionGroup(this);
    struct ToolDef {
        CanvasWidget::Tool tool;
        QString name;
        QKeySequence shortcut;
    };
    const ToolDef defs[] = {
        {CanvasWidget::Tool::Select, tr("Select"), QKeySequence(Qt::Key_V)},
        {CanvasWidget::Tool::Node, tr("Shape (edit nodes)"), QKeySequence(Qt::Key_F10)},
        {CanvasWidget::Tool::Freehand, tr("Freehand"), QKeySequence(Qt::Key_F5)},
        {CanvasWidget::Tool::Bezier, tr("Bézier"), QKeySequence(Qt::SHIFT | Qt::Key_B)},
        {CanvasWidget::Tool::Rectangle, tr("Rectangle"), QKeySequence(Qt::Key_F6)},
        {CanvasWidget::Tool::Ellipse, tr("Ellipse"), QKeySequence(Qt::Key_F7)},
    };
    for (const ToolDef &def : defs) {
        QAction *action = tools->addAction(toolIcon(def.tool, ink), def.name);
        action->setCheckable(true);
        action->setShortcut(def.shortcut);
        action->setToolTip(QStringLiteral("%1 (%2)").arg(def.name, def.shortcut.toString(QKeySequence::NativeText)));
        action->setChecked(def.tool == CanvasWidget::Tool::Select);
        connect(action, &QAction::triggered, m_canvas, [this, tool = def.tool] {
            m_canvas->setTool(tool);
            m_nodeBar->setVisible(tool == CanvasWidget::Tool::Node);
            updateNodeActions();
        });
        toolBar->addAction(action);
        toolsMenu->addAction(action);
    }

    toolBar->addSeparator();
    m_fillAction = toolBar->addAction(tr("Fill Color…"), this, &MainWindow::chooseFillColor);
    m_fillAction->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F11));
    m_outlineAction = toolBar->addAction(tr("Outline Color…"), this, &MainWindow::chooseOutlineColor);
    m_outlineAction->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F12));
    toolsMenu->addSeparator();
    toolsMenu->addAction(m_fillAction);
    toolsMenu->addAction(m_outlineAction);

    // Help
    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("&About OpenDraw"), this, &MainWindow::about);
    helpMenu->addAction(tr("About &Qt"), qApp, &QApplication::aboutQt);
}

void MainWindow::newDocument()
{
    if (!maybeSave())
        return;
    m_document->clear();
    m_undoStack->clear();
    setFilePath(QString());
    m_canvas->fitPage();
}

void MainWindow::open()
{
    if (!maybeSave())
        return;
    const QString path = QFileDialog::getOpenFileName(this, tr("Open"), QString(), kSvgFilter);
    if (!path.isEmpty())
        openFile(path);
}

bool MainWindow::openFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Open"), tr("Could not open %1:\n%2").arg(path, file.errorString()));
        return false;
    }
    QString error;
    svg::ReadReport report;
    if (!svg::read(&file, *m_document, &error, &report)) {
        QMessageBox::warning(this, tr("Open"), tr("Could not read %1:\n%2").arg(path, error));
        return false;
    }
    m_undoStack->clear();
    setFilePath(path);
    m_canvas->fitPage();

    // Never refuse a file: open what we understand and say what was left out.
    if (report.isNewerFormat() || report.skippedCount() > 0) {
        QStringList details;
        if (report.isNewerFormat())
            details << tr("This file was saved by a newer OpenDraw (format %1; this version reads format %2).")
                           .arg(report.formatVersion)
                           .arg(svg::kFormatVersion);
        if (report.skippedCount() > 0) {
            QStringList kinds;
            for (auto it = report.skippedElements.cbegin(); it != report.skippedElements.cend(); ++it)
                kinds << QStringLiteral("%1 × %2").arg(it.value()).arg(it.key());
            details << tr("%n item(s) this version does not support yet were left out or simplified: %1.",
                          nullptr, report.skippedCount())
                           .arg(kinds.join(QStringLiteral(", ")));
            details << tr("Saving over the original file would lose them. Use Save As to keep both.");
        }
        QMessageBox::information(this, tr("Opened with limitations"), details.join(QStringLiteral("\n\n")));
    }
    return true;
}

bool MainWindow::save()
{
    return m_filePath.isEmpty() ? saveAs() : writeFile(m_filePath);
}

bool MainWindow::saveAs()
{
    QString path = QFileDialog::getSaveFileName(this, tr("Save As"),
                                                m_filePath.isEmpty() ? tr("Untitled.svg") : m_filePath,
                                                kSvgFilter);
    if (path.isEmpty())
        return false;
    if (QFileInfo(path).suffix().isEmpty())
        path += QStringLiteral(".svg");
    return writeFile(path);
}

bool MainWindow::writeFile(const QString &path)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || !svg::write(*m_document, &file) || !file.commit()) {
        QMessageBox::warning(this, tr("Save"), tr("Could not save %1:\n%2").arg(path, file.errorString()));
        return false;
    }
    m_undoStack->setClean();
    setFilePath(path);
    statusBar()->showMessage(tr("Saved %1").arg(QFileInfo(path).fileName()), 3000);
    return true;
}

bool MainWindow::maybeSave()
{
    if (m_undoStack->isClean())
        return true;
    const auto answer = QMessageBox::warning(this, tr("OpenDraw"),
                                             tr("The drawing has unsaved changes. Save them?"),
                                             QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (answer == QMessageBox::Save)
        return save();
    return answer == QMessageBox::Discard;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (maybeSave())
        event->accept();
    else
        event->ignore();
}

// The style shown for the selection: its first plain shape (groups have no style of their own).
const Style &MainWindow::displayedStyle() const
{
    for (Shape *shape : m_canvas->selection()) {
        const Style *found = nullptr;
        shape->forEachLeaf([&found](Shape &leaf) {
            if (!found)
                found = &leaf.style();
        });
        if (found)
            return *found;
    }
    return m_canvas->defaultStyle();
}

void MainWindow::chooseFillColor()
{
    const QColor color =
        QColorDialog::getColor(displayedStyle().fill, this, tr("Fill Color"), QColorDialog::ShowAlphaChannel);
    if (color.isValid())
        applyFill(color);
}

void MainWindow::chooseOutlineColor()
{
    const QColor color =
        QColorDialog::getColor(displayedStyle().stroke, this, tr("Outline Color"), QColorDialog::ShowAlphaChannel);
    if (color.isValid())
        applyOutline(color);
}

void MainWindow::restyleSelection(const QString &text, const std::function<void(Style &)> &change)
{
    m_undoStack->beginMacro(text);
    for (Shape *shape : m_canvas->selection()) {
        shape->forEachLeaf([&](Shape &leaf) {
            Style style = leaf.style();
            change(style);
            m_undoStack->push(new SetStyleCommand(m_document, &leaf, style));
        });
    }
    m_undoStack->endMacro();
}

// With a selection, colours apply to every object in it (including inside groups);
// without one, they become the default for new shapes.
void MainWindow::applyFill(const QColor &color)
{
    if (!m_canvas->selection().empty()) {
        restyleSelection(tr("Fill"), [&color](Style &style) { style.fill = color; });
    } else {
        Style defaults = m_canvas->defaultStyle();
        defaults.fill = color;
        m_canvas->setDefaultStyle(defaults);
        statusBar()->showMessage(tr("Default fill for new shapes: %1").arg(describeColor(color)), 3000);
    }
    updateStyleIndicators();
}

void MainWindow::applyOutline(const QColor &color)
{
    if (!m_canvas->selection().empty()) {
        restyleSelection(tr("Outline"), [&color](Style &style) {
            style.stroke = color;
            if (color.alpha() > 0 && style.strokeWidth <= 0.0)
                style.strokeWidth = 1.0;
        });
    } else {
        Style defaults = m_canvas->defaultStyle();
        defaults.stroke = color;
        m_canvas->setDefaultStyle(defaults);
        statusBar()->showMessage(tr("Default outline for new shapes: %1").arg(describeColor(color)), 3000);
    }
    updateStyleIndicators();
}

void MainWindow::updateNodeActions()
{
    const bool hasNode = m_canvas->hasActiveNode();
    for (QAction *action : std::as_const(m_nodeActions))
        action->setEnabled(hasNode);
    m_joinAction->setEnabled(m_canvas->hasNodePairToJoin());
}

void MainWindow::updateStyleIndicators()
{
    Shape *selected = m_canvas->selectedShape();
    const Style &style = displayedStyle();
    m_fillAction->setIcon(swatchIcon(style.fill, false));
    m_outlineAction->setIcon(swatchIcon(style.hasStroke() ? style.stroke : QColor(Qt::transparent), true));

    const QString outline = style.hasStroke()
                                ? tr("%1, %2 mm").arg(describeColor(style.stroke))
                                      .arg(style.strokeWidth / kUnitsPerMm, 0, 'f', 2)
                                : tr("none");
    QString prefix = tr("New shapes");
    if (m_canvas->selection().size() > 1)
        prefix = tr("%n objects selected", nullptr, int(m_canvas->selection().size()));
    if (selected) {
        switch (selected->kind()) {
        case ShapeKind::Rectangle:
            prefix = tr("Rectangle");
            break;
        case ShapeKind::Ellipse:
            prefix = tr("Ellipse");
            break;
        case ShapeKind::Path: {
            int nodes = 0;
            for (const SubPath &sp : selected->pathData())
                nodes += int(sp.nodes.size());
            prefix = tr("Curve, %n node(s)", nullptr, nodes);
            break;
        }
        case ShapeKind::Group:
            prefix = tr("Group of %n object(s)", nullptr, int(selected->children().size()));
            break;
        }
    }
    m_styleLabel->setText(tr("%1 — Fill: %2   Outline: %3").arg(prefix, describeColor(style.fill), outline));
}

void MainWindow::about()
{
    QMessageBox::about(this, tr("About OpenDraw"),
                       tr("<p><img src=\":/opendraw-icon-256.png\" width=\"96\" height=\"96\"></p>"
                          "<h3>OpenDraw %1</h3>"
                          "<p>A free, open-source, cross-platform vector graphics editor.</p>"
                          "<p>Licensed under the GNU General Public License v3.0 or later.</p>"
                          "<p><a href=\"https://github.com/R4P0S1T0/OpenDraw\">github.com/R4P0S1T0/OpenDraw</a></p>")
                           .arg(QApplication::applicationVersion()));
}

void MainWindow::setFilePath(const QString &path)
{
    m_filePath = path;
    updateTitle();
}

void MainWindow::updateTitle()
{
    const QString name = m_filePath.isEmpty() ? tr("Untitled") : QFileInfo(m_filePath).fileName();
    setWindowTitle(QStringLiteral("%1[*] — OpenDraw").arg(name));
    setWindowModified(!m_undoStack->isClean());
}

} // namespace opendraw
