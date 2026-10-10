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
    connect(m_canvas, &CanvasWidget::selectionChanged, this, [this](Shape *shape) {
        m_propertyBar->setShape(shape);
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

    // View
    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(tr("Zoom &In"), QKeySequence::ZoomIn, m_canvas, &CanvasWidget::zoomIn);
    viewMenu->addAction(tr("Zoom &Out"), QKeySequence::ZoomOut, m_canvas, &CanvasWidget::zoomOut);
    viewMenu->addAction(tr("&Actual Size"), QKeySequence(Qt::CTRL | Qt::Key_1), m_canvas,
                        &CanvasWidget::zoomToActualSize);
    viewMenu->addAction(tr("&Fit Page"), QKeySequence(Qt::SHIFT | Qt::Key_F4), m_canvas, &CanvasWidget::fitPage);

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
        {CanvasWidget::Tool::Rectangle, tr("Rectangle"), QKeySequence(Qt::Key_F6)},
        {CanvasWidget::Tool::Ellipse, tr("Ellipse"), QKeySequence(Qt::Key_F7)},
    };
    for (const ToolDef &def : defs) {
        QAction *action = tools->addAction(toolIcon(def.tool, ink), def.name);
        action->setCheckable(true);
        action->setShortcut(def.shortcut);
        action->setToolTip(QStringLiteral("%1 (%2)").arg(def.name, def.shortcut.toString(QKeySequence::NativeText)));
        action->setChecked(def.tool == CanvasWidget::Tool::Select);
        connect(action, &QAction::triggered, m_canvas, [this, tool = def.tool] { m_canvas->setTool(tool); });
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
                kinds << QStringLiteral("%1 × <%2>").arg(it.value()).arg(it.key());
            details << tr("%n object(s) this version cannot edit yet were left out: %1.", nullptr,
                          report.skippedCount())
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

void MainWindow::chooseFillColor()
{
    Shape *selected = m_canvas->selectedShape();
    const QColor initial = selected ? selected->style().fill : m_canvas->defaultStyle().fill;
    const QColor color = QColorDialog::getColor(initial, this, tr("Fill Color"), QColorDialog::ShowAlphaChannel);
    if (color.isValid())
        applyFill(color);
}

void MainWindow::chooseOutlineColor()
{
    Shape *selected = m_canvas->selectedShape();
    const QColor initial = selected ? selected->style().stroke : m_canvas->defaultStyle().stroke;
    const QColor color = QColorDialog::getColor(initial, this, tr("Outline Color"), QColorDialog::ShowAlphaChannel);
    if (color.isValid())
        applyOutline(color);
}

// With a selection, colours apply to it; without one, they become the default for new shapes.
void MainWindow::applyFill(const QColor &color)
{
    if (Shape *selected = m_canvas->selectedShape()) {
        Style style = selected->style();
        style.fill = color;
        m_undoStack->push(new SetStyleCommand(m_document, selected, style));
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
    if (Shape *selected = m_canvas->selectedShape()) {
        Style style = selected->style();
        style.stroke = color;
        if (color.alpha() > 0 && style.strokeWidth <= 0.0)
            style.strokeWidth = 1.0;
        m_undoStack->push(new SetStyleCommand(m_document, selected, style));
    } else {
        Style defaults = m_canvas->defaultStyle();
        defaults.stroke = color;
        m_canvas->setDefaultStyle(defaults);
        statusBar()->showMessage(tr("Default outline for new shapes: %1").arg(describeColor(color)), 3000);
    }
    updateStyleIndicators();
}

void MainWindow::updateStyleIndicators()
{
    Shape *selected = m_canvas->selectedShape();
    const Style &style = selected ? selected->style() : m_canvas->defaultStyle();
    m_fillAction->setIcon(swatchIcon(style.fill, false));
    m_outlineAction->setIcon(swatchIcon(style.hasStroke() ? style.stroke : QColor(Qt::transparent), true));

    const QString outline = style.hasStroke()
                                ? tr("%1, %2 mm").arg(describeColor(style.stroke))
                                      .arg(style.strokeWidth / kUnitsPerMm, 0, 'f', 2)
                                : tr("none");
    const QString prefix = selected ? (selected->kind() == ShapeKind::Rectangle ? tr("Rectangle") : tr("Ellipse"))
                                    : tr("New shapes");
    m_styleLabel->setText(tr("%1 — Fill: %2   Outline: %3").arg(prefix, describeColor(style.fill), outline));
}

void MainWindow::about()
{
    QMessageBox::about(this, tr("About OpenDraw"),
                       tr("<h3>OpenDraw %1</h3>"
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
