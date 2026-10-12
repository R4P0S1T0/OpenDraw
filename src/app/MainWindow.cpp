#include "MainWindow.h"

#include "CanvasView.h"
#include "CanvasWidget.h"
#include "ColorPalette.h"
#include "ExportImageDialog.h"
#include "FillDialog.h"
#include "Icons.h"
#include "Language.h"
#include "ObjectsPanel.h"
#include "PageBar.h"
#include "PropertiesPanel.h"
#include "PropertyBar.h"
#include "Theme.h"
#include "Units.h"
#include "core/Render.h"
#include "core/CdrImport.h"
#include "core/Export.h"
#include "core/Commands.h"
#include "core/Document.h"
#include "core/SvgIO.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QTimer>
#include <QFileDialog>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QSettings>
#include <QFileInfo>
#include <QFontComboBox>
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

QString describeColor(const QColor &color)
{
    return color.alpha() == 0 ? QCoreApplication::translate("MainWindow", "none") : color.name().toUpper();
}

QString describeFill(const Style &style)
{
    if (style.pattern)
        return QCoreApplication::translate("MainWindow", "bitmap pattern");
    if (style.gradient) {
        return style.gradient->type == Gradient::Type::Radial
                   ? QCoreApplication::translate("MainWindow", "radial gradient")
                   : QCoreApplication::translate("MainWindow", "linear gradient");
    }
    return describeColor(style.fill);
}

const QString kSvgFilter = QStringLiteral("SVG (*.svg)");

// A swatch beside an icon, for the fill and outline indicators of the status bar.
QIcon indicatorIcon(const QIcon &glyph, const QBrush &swatch, bool none)
{
    const qreal dpr = qApp->devicePixelRatio();
    QPixmap pixmap(QSize(44, 20) * dpr);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    glyph.paint(&p, QRect(0, 0, 20, 20));
    const QRectF box(24.5, 2.5, 18, 15);
    p.setPen(QPen(QColor(0, 0, 0, 140), 1));
    p.setBrush(none ? QBrush(Qt::white) : swatch);
    p.drawRect(box);
    if (none) {
        p.setPen(QPen(QColor(0xd0, 0x30, 0x30), 1.5));
        p.drawLine(box.bottomLeft() + QPointF(2, -2), box.topRight() + QPointF(-2, 2));
    }
    return QIcon(pixmap);
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_document(new Document(this))
    , m_undoStack(new QUndoStack(this))
    , m_canvas(new CanvasWidget(m_document, m_undoStack, this))
{
    setDocumentMode(true);
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowTabbedDocks);

    // Centre: document tab, the canvas with rulers and scroll bars, the page navigator and the
    // document palette, top to bottom.
    auto *central = new QWidget(this);
    auto *centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    centralLayout->addWidget(createDocumentTabs());
    m_canvasView = new CanvasView(m_canvas, m_document, central);
    centralLayout->addWidget(m_canvasView, 1);
    m_pageBar = new PageBar(m_document, central);
    centralLayout->addWidget(m_pageBar);
    m_documentPalette = new ColorPalette(Qt::Horizontal, central);
    m_documentPalette->setToolTip(tr("Document palette: the colours used in this drawing"));
    centralLayout->addWidget(m_documentPalette);
    setCentralWidget(central);

    connect(m_pageBar, &PageBar::insertPageRequested, this, &MainWindow::insertPage);
    connect(m_pageBar, &PageBar::renamePageRequested, this, &MainWindow::renamePage);
    connect(m_pageBar, &PageBar::deletePageRequested, this, &MainWindow::deletePage);
    connect(m_documentPalette, &ColorPalette::fillChosen, this, &MainWindow::applyFill);
    connect(m_documentPalette, &ColorPalette::outlineChosen, this, &MainWindow::applyOutline);
    m_documentPaletteTimer = new QTimer(this);
    m_documentPaletteTimer->setSingleShot(true);
    m_documentPaletteTimer->setInterval(150);
    connect(m_documentPaletteTimer, &QTimer::timeout, this, &MainWindow::updateDocumentPalette);
    connect(m_document, &Document::changed, m_documentPaletteTimer, qOverload<>(&QTimer::start));
    connect(m_document, &Document::currentPageChanged, m_documentPaletteTimer, qOverload<>(&QTimer::start));

    createActions();

    m_propertyBar = new PropertyBar(m_document, m_undoStack, this);
    addToolBarBreak(Qt::TopToolBarArea);
    addToolBar(Qt::TopToolBarArea, m_propertyBar);
    addToolBar(Qt::TopToolBarArea, m_nodeBar);
    m_nodeBar->hide();
    createTextBar();
    m_propertyBar->setCanvas(m_canvas);
    connect(m_propertyBar, &PropertyBar::mirrorRequested, m_canvas, &CanvasWidget::mirrorSelection);
    connect(m_propertyBar, &PropertyBar::rotateRequested, m_canvas, &CanvasWidget::rotateSelection);
    connect(m_canvas, &CanvasWidget::transformModeChanged, this, &MainWindow::updateStyleIndicators);
    connect(m_propertyBar, &PropertyBar::nudgeDistanceChanged, m_canvas, &CanvasWidget::setNudgeDistance);
    connect(m_propertyBar, &PropertyBar::duplicateOffsetChanged, m_canvas, &CanvasWidget::setDuplicateOffset);
    connect(m_propertyBar, &PropertyBar::toolSettingsChanged, m_canvas, &CanvasWidget::setToolSettings);
    connect(m_propertyBar, &PropertyBar::eyedropperResetRequested, m_canvas,
            [this] { m_canvas->setEyedropperColor(std::nullopt); });
    connect(m_canvas, &CanvasWidget::eyedropperColorChanged, m_propertyBar, &PropertyBar::setEyedropperColor);
    connect(m_canvas, &CanvasWidget::toolChanged, m_propertyBar, &PropertyBar::setTool);
    m_propertyBar->setToolSettings(m_canvas->toolSettings());
    connect(m_canvas, &CanvasWidget::nodeSelectionChanged, this, &MainWindow::updateNodeActions);
    connect(m_document, &Document::changed, this, &MainWindow::updateNodeActions);
    updateNodeActions();

    // Dockers sit between the drawing and the colour palette, which hugs the window edge.
    auto *propertiesDock = new QDockWidget(tr("Properties"), this);
    propertiesDock->setObjectName(QStringLiteral("properties"));
    propertiesDock->setWidget(new PropertiesPanel(m_document, m_canvas, propertiesDock));
    addDockWidget(Qt::RightDockWidgetArea, propertiesDock);

    auto *layersDock = new QDockWidget(tr("Objects"), this);
    layersDock->setObjectName(QStringLiteral("objects"));
    layersDock->setWidget(new ObjectsPanel(m_document, m_undoStack, m_canvas, layersDock));
    addDockWidget(Qt::RightDockWidgetArea, layersDock);
    // Sizes only stick once the window is laid out.
    QTimer::singleShot(0, this, [this, layersDock, propertiesDock] {
        resizeDocks({layersDock}, {270}, Qt::Horizontal);
        resizeDocks({propertiesDock, layersDock}, {330, 400}, Qt::Vertical);
    });

    auto *palette = new ColorPalette(Qt::Vertical, this);
    m_paletteBar = new QToolBar(tr("Color Palette"), this);
    m_paletteBar->setObjectName(QStringLiteral("colorPalette"));
    m_paletteBar->setMovable(false);
    m_paletteBar->addWidget(palette);
    addToolBar(Qt::RightToolBarArea, m_paletteBar);
    connect(palette, &ColorPalette::fillChosen, this, &MainWindow::applyFill);
    connect(palette, &ColorPalette::outlineChosen, this, &MainWindow::applyOutline);

    // Window menu: dockers, toolbars and palettes can be hidden and brought back.
    QMenu *dockers = m_windowMenu->addMenu(tr("&Dockers"));
    dockers->addAction(propertiesDock->toggleViewAction());
    dockers->addAction(layersDock->toggleViewAction());
    QMenu *toolbars = m_windowMenu->addMenu(tr("&Toolbars"));
    for (QToolBar *bar : {m_standardBar, static_cast<QToolBar *>(m_propertyBar), m_toolbox}) {
        QAction *toggle = bar->toggleViewAction();
        toolbars->addAction(toggle);
    }
    QMenu *palettes = m_windowMenu->addMenu(tr("Color &Palettes"));
    QAction *defaultPalette = m_paletteBar->toggleViewAction();
    defaultPalette->setText(tr("Default Palette"));
    palettes->addAction(defaultPalette);
    QAction *documentPalette = palettes->addAction(tr("Document Palette"));
    documentPalette->setCheckable(true);
    documentPalette->setChecked(true);
    connect(documentPalette, &QAction::toggled, m_documentPalette, &QWidget::setVisible);

    createStatusBar();

    connect(m_canvas, &CanvasWidget::zoomChanged, this, [this](double zoom) {
        m_zoomBox->setEditText(QStringLiteral("%1%").arg(qRound(zoom * 100)));
    });
    connect(m_canvas, &CanvasWidget::cursorMoved, this, [this](const QPointF &pos) {
        const Units &units = Units::instance();
        m_positionLabel->setText(QStringLiteral("x %1  y %2 %3")
                                     .arg(QLocale().toString(units.toDisplay(pos.x()), 'f', units.decimals()),
                                          QLocale().toString(units.toDisplay(pos.y()), 'f', units.decimals()),
                                          units.suffix()));
    });
    connect(m_canvas, &CanvasWidget::selectionChanged, this, [this] {
        m_propertyBar->setSelection(m_canvas->selectedShape(), !m_canvas->selection().empty());
        updateStyleIndicators();
        const bool any = !m_canvas->selection().empty();
        m_cutAction->setEnabled(any);
        m_copyAction->setEnabled(any);
    });
    connect(m_canvas, &CanvasWidget::toolChanged, this, [this](CanvasWidget::Tool tool) {
        for (QAction *action : std::as_const(m_toolActions)) {
            if (action->data().toInt() == int(tool))
                action->setChecked(true);
        }
        m_nodeBar->setVisible(tool == CanvasWidget::Tool::Node);
        updateNodeActions();
        updateTextBar();
        updateStyleIndicators();
    });
    connect(m_document, &Document::changed, this, &MainWindow::updateStyleIndicators);
    connect(m_canvas, &CanvasWidget::eyedropperColorChanged, this, &MainWindow::updateStyleIndicators);
    connect(m_canvas, &CanvasWidget::defaultStyleChanged, this, &MainWindow::updateStyleIndicators);
    connect(&Units::instance(), &Units::unitChanged, this, &MainWindow::updateStyleIndicators);
    connect(m_undoStack, &QUndoStack::cleanChanged, this, &MainWindow::updateTitle);
    m_cutAction->setEnabled(false);
    m_copyAction->setEnabled(false);

    // Fit comfortably on the current screen, whatever its size and scaling.
    if (const QScreen *screen = this->screen()) {
        const QRect available = screen->availableGeometry();
        resize(available.size() * 0.85);
        move(available.center() - rect().center());
    }
    updateTitle();
    updateStyleIndicators();
    updateDocumentPalette();
}

// The undo stack and the document outlive this destructor (they are deleted with the other
// children afterwards); clearing the stack would otherwise call back into a half-destroyed window.
MainWindow::~MainWindow()
{
    m_undoStack->disconnect(this);
    m_document->disconnect(this);
    m_canvas->disconnect(this);
}

QWidget *MainWindow::createDocumentTabs()
{
    // One tab for the open drawing, as in other desktop editors; "+" starts a new drawing.
    auto *row = new QWidget(this);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(4, 2, 4, 0);
    layout->setSpacing(2);
    m_documentTabs = new QTabBar(row);
    m_documentTabs->setObjectName(QStringLiteral("documentTabs"));
    m_documentTabs->setDocumentMode(true);
    m_documentTabs->setDrawBase(false);
    m_documentTabs->setExpanding(false);
    m_documentTabs->addTab(QString());
    auto *add = new QToolButton(row);
    add->setText(QStringLiteral("+"));
    add->setAutoRaise(true);
    add->setToolTip(tr("New drawing (Ctrl+N)"));
    connect(add, &QToolButton::clicked, this, &MainWindow::newDocument);
    layout->addWidget(m_documentTabs);
    layout->addWidget(add);
    layout->addStretch();
    return row;
}

void MainWindow::createActions()
{
    // File
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    QAction *newAction = fileMenu->addAction(icon(Icon::New), tr("&New"), QKeySequence::New, this,
                                             &MainWindow::newDocument);
    QAction *openAction = fileMenu->addAction(icon(Icon::Open), tr("&Open…"), QKeySequence::Open, this,
                                              &MainWindow::open);
    fileMenu->addSeparator();
    QAction *saveAction = fileMenu->addAction(icon(Icon::Save), tr("&Save"), QKeySequence::Save, this,
                                              &MainWindow::save);
    fileMenu->addAction(tr("Save &As…"), QKeySequence::SaveAs, this, &MainWindow::saveAs);
    fileMenu->addSeparator();
    QAction *importAction = fileMenu->addAction(icon(Icon::Import), tr("&Import…"),
                                                QKeySequence(Qt::CTRL | Qt::Key_I), this, &MainWindow::import);
    QAction *exportAction = fileMenu->addAction(icon(Icon::Export), tr("&Export…"),
                                                QKeySequence(Qt::CTRL | Qt::Key_E), this,
                                                &MainWindow::exportDocument);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("&Quit"), QKeySequence::Quit, this, &QWidget::close);
    newAction->setToolTip(tr("New (Ctrl+N)"));
    openAction->setToolTip(tr("Open (Ctrl+O)"));
    saveAction->setToolTip(tr("Save (Ctrl+S)"));
    importAction->setToolTip(tr("Import a drawing into this page (Ctrl+I)"));
    exportAction->setToolTip(tr("Export as PDF or PNG (Ctrl+E)"));

    // Edit
    QMenu *editMenu = menuBar()->addMenu(tr("&Edit"));
    m_undoAction = m_undoStack->createUndoAction(this, tr("&Undo"));
    m_undoAction->setShortcut(QKeySequence::Undo);
    m_undoAction->setIcon(icon(Icon::Undo));
    m_redoAction = m_undoStack->createRedoAction(this, tr("&Redo"));
    m_redoAction->setShortcut(QKeySequence::Redo);
    m_redoAction->setIcon(icon(Icon::Redo));
    editMenu->addAction(m_undoAction);
    editMenu->addAction(m_redoAction);
    editMenu->addSeparator();
    m_cutAction = editMenu->addAction(icon(Icon::Cut), tr("Cu&t"), QKeySequence::Cut, m_canvas,
                                      &CanvasWidget::cutSelection);
    m_copyAction = editMenu->addAction(icon(Icon::Copy), tr("&Copy"), QKeySequence::Copy, m_canvas,
                                       &CanvasWidget::copySelection);
    m_pasteAction = editMenu->addAction(icon(Icon::Paste), tr("&Paste"), QKeySequence::Paste, m_canvas,
                                        &CanvasWidget::paste);
    editMenu->addAction(tr("D&uplicate"), QKeySequence(Qt::CTRL | Qt::Key_D), m_canvas,
                        &CanvasWidget::duplicateSelection);
    QAction *deleteAction = editMenu->addAction(tr("&Delete"), m_canvas, &CanvasWidget::deleteSelection);
    deleteAction->setShortcuts({QKeySequence::Delete, QKeySequence(Qt::Key_Backspace)});
    editMenu->addSeparator();
    editMenu->addAction(tr("Select &All"), QKeySequence::SelectAll, m_canvas, &CanvasWidget::selectAll);

    // View
    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    m_viewMenu = viewMenu;
    viewMenu->addAction(tr("Zoom &In"), QKeySequence::ZoomIn, m_canvas, &CanvasWidget::zoomIn);
    viewMenu->addAction(tr("Zoom &Out"), QKeySequence::ZoomOut, m_canvas, &CanvasWidget::zoomOut);
    viewMenu->addAction(tr("&Actual Size"), QKeySequence(Qt::CTRL | Qt::Key_1), m_canvas,
                        &CanvasWidget::zoomToActualSize);
    viewMenu->addAction(tr("Zoom to &Page"), QKeySequence(Qt::SHIFT | Qt::Key_F4), m_canvas, &CanvasWidget::fitPage);
    viewMenu->addAction(tr("Zoom to Page &Width"), m_canvas, &CanvasWidget::fitPageWidth);
    viewMenu->addAction(tr("Zoom to &Selected"), QKeySequence(Qt::SHIFT | Qt::Key_F2), m_canvas,
                        &CanvasWidget::zoomToSelection);
    viewMenu->addSeparator();
    m_rulersAction = viewMenu->addAction(icon(Icon::Rulers), tr("&Rulers"));
    m_rulersAction->setCheckable(true);
    m_rulersAction->setChecked(true);
    m_rulersAction->setToolTip(tr("Show rulers"));
    connect(m_rulersAction, &QAction::toggled, this, [this](bool on) { m_canvasView->setRulersVisible(on); });
    QMenu *schemeMenu = viewMenu->addMenu(tr("&Color Scheme"));
    auto *schemes = new QActionGroup(schemeMenu);
    for (const auto &[text, scheme] : {std::pair{tr("&Dark"), ColorScheme::Dark}, std::pair{tr("&Light"), ColorScheme::Light}}) {
        QAction *action = schemeMenu->addAction(text);
        action->setCheckable(true);
        action->setChecked(theme::stored() == scheme);
        schemes->addAction(action);
        connect(action, &QAction::triggered, this, [this, s = scheme] {
            theme::set(s);
            updateStyleIndicators();
        });
    }

    // Interface language; takes effect the next time OpenDraw starts.
    QMenu *languageMenu = viewMenu->addMenu(tr("La&nguage"));
    auto *languages = new QActionGroup(languageMenu);
    QStringList names{QString()};
    names << language::available();
    for (const QString &name : std::as_const(names)) {
        const QString text = name.isEmpty() ? tr("System Default")
                                            : QLocale(name).nativeLanguageName()
                                                  + (name.contains(u'_') ? QStringLiteral(" (%1)").arg(QLocale(name).nativeTerritoryName())
                                                                         : QString());
        QAction *action = languageMenu->addAction(text);
        action->setCheckable(true);
        action->setChecked(language::stored() == name);
        languages->addAction(action);
        connect(action, &QAction::triggered, this, [this, name] {
            if (name == language::stored())
                return;
            language::store(name);
            QMessageBox::information(this, tr("Language"), tr("Restart OpenDraw to use the new language."));
        });
    }

    // Layout: pages
    QMenu *layoutMenu = menuBar()->addMenu(tr("&Layout"));
    layoutMenu->addAction(tr("&Insert Page"), this, [this] { insertPage(m_document->currentPageIndex() + 1); });
    m_deletePageAction = layoutMenu->addAction(tr("&Delete Page"), this, &MainWindow::deletePage);
    layoutMenu->addAction(tr("&Rename Page…"), this, &MainWindow::renamePage);
    layoutMenu->addSeparator();
    layoutMenu->addAction(tr("&Next Page"), QKeySequence(Qt::Key_PageDown), this,
                          [this] { m_document->setCurrentPage(m_document->currentPageIndex() + 1); });
    layoutMenu->addAction(tr("&Previous Page"), QKeySequence(Qt::Key_PageUp), this,
                          [this] { m_document->setCurrentPage(m_document->currentPageIndex() - 1); });
    connect(m_document, &Document::structureChanged, this,
            [this] { m_deletePageAction->setEnabled(m_document->pageCount() > 1); });
    m_deletePageAction->setEnabled(m_document->pageCount() > 1);

    // Object
    QMenu *objectMenu = menuBar()->addMenu(tr("&Object"));
    objectMenu->addAction(tr("&Group"), QKeySequence(Qt::CTRL | Qt::Key_G), m_canvas, &CanvasWidget::groupSelection);
    objectMenu->addAction(tr("&Ungroup"), QKeySequence(Qt::CTRL | Qt::Key_U), m_canvas,
                          &CanvasWidget::ungroupSelection);
    objectMenu->addAction(tr("&Break Blend Apart"), QKeySequence(Qt::CTRL | Qt::Key_K), m_canvas,
                          &CanvasWidget::breakBlendApart);
    objectMenu->addAction(tr("&Remove Envelope"), m_canvas, &CanvasWidget::removeEnvelope);
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

    QMenu *transformMenu = objectMenu->addMenu(tr("&Transform"));
    transformMenu->addAction(icon(Icon::MirrorHorizontal), tr("Mirror &Horizontally"), m_canvas,
                             [this] { m_canvas->mirrorSelection(Qt::Horizontal); });
    transformMenu->addAction(icon(Icon::MirrorVertical), tr("Mirror &Vertically"), m_canvas,
                             [this] { m_canvas->mirrorSelection(Qt::Vertical); });
    transformMenu->addSeparator();
    transformMenu->addAction(icon(Icon::Rotate), tr("Rotate 90° &Clockwise"), m_canvas,
                             [this] { m_canvas->rotateSelection(90); });
    transformMenu->addAction(icon(Icon::RotateCounterclockwise), tr("Rotate 90° C&ounterclockwise"), m_canvas,
                             [this] { m_canvas->rotateSelection(-90); });
    transformMenu->addAction(tr("Rotate &180°"), m_canvas, [this] { m_canvas->rotateSelection(180); });

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

    // Text
    QMenu *textMenu = menuBar()->addMenu(tr("Te&xt"));
    textMenu->addAction(tr("&Paragraph Formatting"), QKeySequence(Qt::CTRL | Qt::Key_T), this, [this] {
        // The Properties docker, open at its Paragraph section.
        if (auto *panel = findChild<PropertiesPanel *>()) {
            panel->showSection(PropertiesPanel::Section::Paragraph);
            if (auto *dock = qobject_cast<QDockWidget *>(panel->parentWidget())) {
                dock->show();
                dock->raise();
            }
        }
    });
    textMenu->addAction(tr("&Convert to Paragraph/Artistic Text"), QKeySequence(Qt::CTRL | Qt::Key_F8), m_canvas,
                        &CanvasWidget::convertTextKind);

    // Tools
    QMenu *toolsMenu = menuBar()->addMenu(tr("&Tools"));
    createToolbox(toolsMenu);

    m_fillAction = new QAction(icon(Icon::FillBucket), tr("&Fill…"), this);
    m_fillAction->setToolTip(tr("Fill: colour or gradient (Shift+F11)"));
    m_fillAction->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F11));
    connect(m_fillAction, &QAction::triggered, this, &MainWindow::chooseFillColor);
    m_outlineAction = new QAction(icon(Icon::OutlinePen), tr("&Outline Color…"), this);
    m_outlineAction->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F12));
    connect(m_outlineAction, &QAction::triggered, this, &MainWindow::chooseOutlineColor);
    toolsMenu->addSeparator();
    toolsMenu->addAction(m_fillAction);
    toolsMenu->addAction(m_outlineAction);

    // Window
    m_windowMenu = menuBar()->addMenu(tr("&Window"));

    // Help
    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("&About OpenDraw"), this, &MainWindow::about);
    helpMenu->addAction(tr("About &Qt"), qApp, &QApplication::aboutQt);

    // Standard toolbar
    m_standardBar = addToolBar(tr("Standard"));
    m_standardBar->setObjectName(QStringLiteral("standard"));
    m_standardBar->setMovable(false);
    m_standardBar->setIconSize(QSize(20, 20));
    m_standardBar->addAction(newAction);
    m_standardBar->addAction(openAction);
    m_standardBar->addAction(saveAction);
    m_standardBar->addSeparator();
    m_standardBar->addAction(m_cutAction);
    m_standardBar->addAction(m_copyAction);
    m_standardBar->addAction(m_pasteAction);
    m_standardBar->addSeparator();
    m_standardBar->addAction(m_undoAction);
    m_standardBar->addAction(m_redoAction);
    m_standardBar->addSeparator();
    m_standardBar->addAction(importAction);
    m_standardBar->addAction(exportAction);
    m_standardBar->addSeparator();
    createStandardBar();
    m_standardBar->addSeparator();
    m_standardBar->addAction(m_rulersAction);
    createSnapActions(m_viewMenu);
}

// Zoom levels box on the standard toolbar.
void MainWindow::createStandardBar()
{
    m_zoomBox = new QComboBox(this);
    m_zoomBox->setEditable(true);
    m_zoomBox->setInsertPolicy(QComboBox::NoInsert);
    m_zoomBox->setMinimumWidth(110);
    m_zoomBox->setToolTip(tr("Zoom level"));
    const std::pair<QString, std::function<void()>> fits[] = {
        {tr("To Page"), [this] { m_canvas->fitPage(); }},
        {tr("To Width"), [this] { m_canvas->fitPageWidth(); }},
        {tr("To Height"), [this] { m_canvas->fitPageHeight(); }},
        {tr("To Selected"), [this] { m_canvas->zoomToSelection(); }},
    };
    for (const auto &fit : fits)
        m_zoomBox->addItem(fit.first);
    for (int percent : {10, 25, 50, 75, 100, 200, 400, 800, 1600})
        m_zoomBox->addItem(QStringLiteral("%1%").arg(percent), percent);
    std::vector<std::function<void()>> actions;
    for (const auto &fit : fits)
        actions.push_back(fit.second);
    connect(m_zoomBox, &QComboBox::activated, this, [this, actions](int index) {
        if (index < int(actions.size()))
            actions[index]();
        else
            m_canvas->setZoom(m_zoomBox->itemData(index).toInt() / 100.0);
        m_zoomBox->setEditText(QStringLiteral("%1%").arg(qRound(m_canvas->zoom() * 100)));
        m_canvas->setFocus();
    });
    connect(m_zoomBox->lineEdit(), &QLineEdit::returnPressed, this, [this] {
        QString text = m_zoomBox->lineEdit()->text();
        text.remove(QLatin1Char('%'));
        bool ok = false;
        const double percent = text.trimmed().toDouble(&ok);
        if (ok && percent > 0)
            m_canvas->setZoom(percent / 100.0);
        m_zoomBox->setEditText(QStringLiteral("%1%").arg(qRound(m_canvas->zoom() * 100)));
        m_canvas->setFocus();
    });
    m_standardBar->addWidget(m_zoomBox);
}

// Toolbox on the left, with flyouts grouping related tools (hold the button or click its
// corner arrow to open one).
void MainWindow::createToolbox(QMenu *toolsMenu)
{
    m_toolbox = new QToolBar(tr("Toolbox"), this);
    m_toolbox->setObjectName(QStringLiteral("tools"));
    m_toolbox->setMovable(false);
    m_toolbox->setIconSize(QSize(22, 22));
    addToolBar(Qt::LeftToolBarArea, m_toolbox);

    auto *tools = new QActionGroup(this);
    struct ToolDef {
        CanvasWidget::Tool tool;
        Icon iconId;
        QString name;
        QKeySequence shortcut;
    };
    auto makeAction = [&](const ToolDef &def) {
        QAction *action = tools->addAction(icon(def.iconId), def.name);
        action->setCheckable(true);
        action->setData(int(def.tool));
        action->setShortcut(def.shortcut);
        action->setToolTip(def.shortcut.isEmpty()
                               ? def.name
                               : QStringLiteral("%1 (%2)").arg(def.name, def.shortcut.toString(QKeySequence::NativeText)));
        action->setChecked(def.tool == CanvasWidget::Tool::Select);
        connect(action, &QAction::triggered, m_canvas, [this, tool = def.tool] { m_canvas->setTool(tool); });
        toolsMenu->addAction(action);
        m_toolActions << action;
        return action;
    };
    auto addFlyout = [&](std::initializer_list<ToolDef> defs) {
        auto *button = new QToolButton(m_toolbox);
        auto *menu = new QMenu(button);
        QAction *first = nullptr;
        for (const ToolDef &def : defs) {
            QAction *action = makeAction(def);
            menu->addAction(action);
            if (!first)
                first = action;
            // The flyout button shows the tool last picked from it.
            connect(action, &QAction::triggered, button, [button, action] { button->setDefaultAction(action); });
        }
        button->setDefaultAction(first);
        button->setMenu(menu);
        button->setPopupMode(QToolButton::DelayedPopup);
        button->setIconSize(m_toolbox->iconSize());
        m_toolbox->addWidget(button);
    };

    m_toolbox->addAction(makeAction({CanvasWidget::Tool::Select, Icon::Pick, tr("Pick"), QKeySequence(Qt::Key_V)}));
    m_toolbox->addAction(
        makeAction({CanvasWidget::Tool::Node, Icon::Shape, tr("Shape (edit nodes)"), QKeySequence(Qt::Key_F10)}));
    addFlyout({{CanvasWidget::Tool::Zoom, Icon::Zoom, tr("Zoom"), QKeySequence(Qt::Key_Z)},
               {CanvasWidget::Tool::Pan, Icon::Pan, tr("Pan"), QKeySequence(Qt::Key_H)}});
    m_toolbox->addSeparator();
    addFlyout({{CanvasWidget::Tool::Freehand, Icon::Freehand, tr("Freehand"), QKeySequence(Qt::Key_F5)},
               {CanvasWidget::Tool::Line, Icon::Line, tr("2-Point Line"), QKeySequence()},
               {CanvasWidget::Tool::Bezier, Icon::Bezier, tr("Bézier"), QKeySequence(Qt::SHIFT | Qt::Key_B)}});
    m_toolbox->addSeparator();
    m_toolbox->addAction(
        makeAction({CanvasWidget::Tool::Rectangle, Icon::Rectangle, tr("Rectangle"), QKeySequence(Qt::Key_F6)}));
    m_toolbox->addAction(
        makeAction({CanvasWidget::Tool::Ellipse, Icon::Ellipse, tr("Ellipse"), QKeySequence(Qt::Key_F7)}));
    addFlyout({{CanvasWidget::Tool::Polygon, Icon::Polygon, tr("Polygon"), QKeySequence(Qt::Key_Y)},
               {CanvasWidget::Tool::Star, Icon::Star, tr("Star"), QKeySequence()},
               {CanvasWidget::Tool::Spiral, Icon::Spiral, tr("Spiral"), QKeySequence(Qt::Key_A)},
               {CanvasWidget::Tool::GraphPaper, Icon::GraphPaper, tr("Graph Paper"), QKeySequence(Qt::Key_D)}});
    m_toolbox->addSeparator();
    m_toolbox->addAction(makeAction({CanvasWidget::Tool::Text, Icon::Text, tr("Text"), QKeySequence(Qt::Key_F8)}));
    m_toolbox->addSeparator();
    addFlyout({{CanvasWidget::Tool::DropShadow, Icon::Shadow, tr("Drop Shadow"), QKeySequence()},
               {CanvasWidget::Tool::Blend, Icon::Blend, tr("Blend"), QKeySequence()},
               {CanvasWidget::Tool::Contour, Icon::Contour, tr("Contour"), QKeySequence(Qt::CTRL | Qt::Key_F9)},
               {CanvasWidget::Tool::Envelope, Icon::Envelope, tr("Envelope"), QKeySequence()},
               {CanvasWidget::Tool::Extrude, Icon::Extrude, tr("Extrude"), QKeySequence()}});
    m_toolbox->addAction(makeAction({CanvasWidget::Tool::Transparency, Icon::Transparency, tr("Transparency"), QKeySequence()}));
    m_toolbox->addAction(
        makeAction({CanvasWidget::Tool::Eyedropper, Icon::Eyedropper, tr("Color Eyedropper"), QKeySequence(Qt::Key_I)}));
}

// Grid, guidelines and snapping: View menu and the "Snap To" button on the standard toolbar.
void MainWindow::createSnapActions(QMenu *viewMenu)
{
    QSettings settings;
    SnapSettings snap;
    snap.grid = settings.value(QStringLiteral("snap/grid"), snap.grid).toBool();
    snap.guides = settings.value(QStringLiteral("snap/guides"), snap.guides).toBool();
    snap.objects = settings.value(QStringLiteral("snap/objects"), snap.objects).toBool();
    snap.page = settings.value(QStringLiteral("snap/page"), snap.page).toBool();
    m_canvas->setSnapSettings(snap);
    GridSettings grid;
    grid.visible = settings.value(QStringLiteral("view/grid"), grid.visible).toBool();
    grid.spacing = settings.value(QStringLiteral("view/gridSpacing"), grid.spacing).toDouble();
    m_canvas->setGridSettings(grid);
    m_canvas->setGuidesVisible(settings.value(QStringLiteral("view/guides"), true).toBool());

    viewMenu->addSeparator();
    QAction *showGrid = viewMenu->addAction(icon(Icon::Grid), tr("&Grid"));
    showGrid->setCheckable(true);
    showGrid->setChecked(grid.visible);
    connect(showGrid, &QAction::toggled, this, [this](bool on) {
        GridSettings g = m_canvas->gridSettings();
        g.visible = on;
        m_canvas->setGridSettings(g);
        QSettings().setValue(QStringLiteral("view/grid"), on);
    });
    viewMenu->addAction(tr("Grid &Spacing…"), this, [this] {
        const Units &units = Units::instance();
        bool ok = false;
        const double value = QInputDialog::getDouble(this, tr("Grid Spacing"), tr("Distance between grid lines (%1):").arg(units.suffix()),
                                                      units.toDisplay(m_canvas->gridSettings().spacing), 0.001, 100000.0,
                                                      units.decimals(), &ok);
        if (!ok)
            return;
        GridSettings g = m_canvas->gridSettings();
        g.spacing = units.fromDisplay(value);
        m_canvas->setGridSettings(g);
        QSettings().setValue(QStringLiteral("view/gridSpacing"), g.spacing);
    });
    QAction *showGuides = viewMenu->addAction(icon(Icon::Guidelines), tr("G&uidelines"));
    showGuides->setCheckable(true);
    showGuides->setChecked(m_canvas->guidesVisible());
    connect(showGuides, &QAction::toggled, this, [this](bool on) {
        m_canvas->setGuidesVisible(on);
        QSettings().setValue(QStringLiteral("view/guides"), on);
    });
    viewMenu->addAction(tr("&Delete All Guidelines"), this, [this] {
        const int count = int(m_document->guides().size());
        if (count == 0)
            return;
        m_undoStack->beginMacro(tr("Delete all guidelines"));
        for (int i = count - 1; i >= 0; --i)
            m_undoStack->push(new GuideCommand(m_document, i, m_document->guides()[i], std::nullopt));
        m_undoStack->endMacro();
    });

    QMenu *snapMenu = viewMenu->addMenu(icon(Icon::Snap), tr("S&nap To"));
    struct SnapOption {
        QString text;
        QString key;
        bool SnapSettings::*flag;
    };
    const SnapOption options[] = {
        {tr("&Grid"), QStringLiteral("snap/grid"), &SnapSettings::grid},
        {tr("G&uidelines"), QStringLiteral("snap/guides"), &SnapSettings::guides},
        {tr("&Objects"), QStringLiteral("snap/objects"), &SnapSettings::objects},
        {tr("&Page"), QStringLiteral("snap/page"), &SnapSettings::page},
    };
    for (const SnapOption &option : options) {
        QAction *action = snapMenu->addAction(option.text);
        action->setCheckable(true);
        action->setChecked(snap.*option.flag);
        connect(action, &QAction::toggled, this, [this, option](bool on) {
            SnapSettings s = m_canvas->snapSettings();
            s.*option.flag = on;
            m_canvas->setSnapSettings(s);
            QSettings().setValue(option.key, on);
        });
    }

    auto *snapButton = new QToolButton(this);
    snapButton->setIcon(icon(Icon::Snap));
    snapButton->setText(tr("Snap To"));
    snapButton->setToolTip(tr("Snap to grid, guidelines, objects or page"));
    snapButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    snapButton->setPopupMode(QToolButton::InstantPopup);
    snapButton->setMenu(snapMenu);
    m_standardBar->addWidget(snapButton);
    m_standardBar->addAction(showGrid);
    m_standardBar->addAction(showGuides);
}

void MainWindow::createStatusBar()
{
    m_hintLabel = new QLabel(this);
    m_hintLabel->setContentsMargins(6, 0, 6, 0);
    m_hintLabel->setMinimumWidth(100);
    m_positionLabel = new QLabel(this);
    m_positionLabel->setContentsMargins(8, 0, 8, 0);
    auto indicator = [this](QAction *action) {
        auto *button = new QToolButton(this);
        button->setAutoRaise(true);
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setIconSize(QSize(44, 20));
        button->setToolTip(action->toolTip());
        connect(button, &QToolButton::clicked, action, &QAction::trigger);
        return button;
    };
    m_fillIndicator = indicator(m_fillAction);
    m_outlineIndicator = indicator(m_outlineAction);
    m_outlineIndicator->setToolTip(tr("Outline colour (Shift+F12)"));
    statusBar()->addWidget(m_hintLabel, 1);
    statusBar()->addPermanentWidget(m_positionLabel);
    statusBar()->addPermanentWidget(m_fillIndicator);
    statusBar()->addPermanentWidget(m_outlineIndicator);
}

// The colours in use on the current page, for the document palette.
void MainWindow::updateDocumentPalette()
{
    QList<QColor> colors;
    auto add = [&colors](QColor color) {
        if (color.alpha() == 0 || colors.size() >= 64)
            return;
        color.setAlpha(255);
        if (!colors.contains(color))
            colors.append(color);
    };
    for (const auto &layer : m_document->currentPage()->layers) {
        for (const auto &shape : layer->shapes) {
            shape->forEachLeaf([&add](Shape &leaf) {
                const Style &style = leaf.style();
                if (style.gradient) {
                    for (const GradientStop &stop : style.gradient->stops)
                        add(stop.color);
                } else if (!style.pattern && leaf.kind() != ShapeKind::Image) {
                    add(style.fill);
                }
                if (style.hasStroke())
                    add(style.stroke);
            });
        }
    }
    m_documentPalette->setColors(colors);
}

QString MainWindow::documentName() const
{
    return m_sourcePath.isEmpty() ? tr("Untitled") : QFileInfo(m_sourcePath).fileName();
}

QString MainWindow::toolHint() const
{
    switch (m_canvas->tool()) {
    case CanvasWidget::Tool::Select:
        return tr("Click an object to select it; Shift+click adds to the selection; drag on empty space to select "
                  "objects inside a rectangle");
    case CanvasWidget::Tool::Node:
        return tr("Click a curve to edit its nodes; double-click a segment to add a node, a node to delete it");
    case CanvasWidget::Tool::Zoom:
        return tr("Click to zoom in; Shift+click or right-click to zoom out; drag to zoom to an area");
    case CanvasWidget::Tool::Pan:
        return tr("Drag to move the view");
    case CanvasWidget::Tool::Freehand:
        return tr("Drag to draw; end near the start to close the shape");
    case CanvasWidget::Tool::Bezier:
        return tr("Click for a corner node, drag for a smooth node; click the first node to close, Enter to finish");
    case CanvasWidget::Tool::Line:
        return tr("Drag to draw a straight line; hold Ctrl for 15° steps");
    case CanvasWidget::Tool::Rectangle:
        return tr("Drag to draw a rectangle; Ctrl for a square, Shift to draw from the centre");
    case CanvasWidget::Tool::Ellipse:
        return tr("Drag to draw an ellipse; Ctrl for a circle, Shift to draw from the centre");
    case CanvasWidget::Tool::Polygon:
        return tr("Drag to draw a polygon; Ctrl keeps it regular, Shift draws from the centre");
    case CanvasWidget::Tool::Star:
        return tr("Drag to draw a star; Ctrl keeps it regular, Shift draws from the centre");
    case CanvasWidget::Tool::Spiral:
        return tr("Drag to draw a spiral; Ctrl keeps it round, Shift draws from the centre");
    case CanvasWidget::Tool::GraphPaper:
        return tr("Drag to draw a grid of rectangles; Ctrl keeps it square");
    case CanvasWidget::Tool::Text:
        return tr("Click for artistic text, or drag a frame for paragraph text, then type; Esc finishes");
    case CanvasWidget::Tool::DropShadow:
        return tr("Drag from an object to give it a shadow; the property bar sets its offset, softness, opacity and colour");
    case CanvasWidget::Tool::Transparency:
        return tr("Drag across an object: solid where you start, transparent where you end; drag the squares to adjust");
    case CanvasWidget::Tool::Blend:
        return tr("Drag from one object to another to blend them; click a blend to change its steps on the property bar");
    case CanvasWidget::Tool::Extrude:
        return tr("Drag from an object to give it depth: the back follows the pointer; the property bar sets the depth, "
                  "vanishing point and colour");
    case CanvasWidget::Tool::Envelope:
        return tr("Click an object, then drag the corners and round handles of its envelope to bend it");
    case CanvasWidget::Tool::Contour:
        return tr("Drag from an object outwards for rings around it, or inwards for rings inside it; the property bar "
                  "sets their number, spacing and colours");
    case CanvasWidget::Tool::Eyedropper:
        return m_canvas->eyedropperColor()
                   ? tr("Click inside an object to fill it, on its outline to colour the outline; Shift+click or Esc "
                        "picks another colour")
                   : tr("Click anywhere to pick up a colour");
    }
    return {};
}

void MainWindow::import()
{
    QStringList filters{kSvgFilter};
    if (cdr::isAvailable()) {
        filters.prepend(tr("All supported files (*.svg *.cdr *.cdt)"));
        filters << tr("CorelDRAW (*.cdr *.cdt)");
    }
    const QString path = QFileDialog::getOpenFileName(this, tr("Import"), QString(), filters.join(QStringLiteral(";;")));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Import"), tr("Could not open %1:\n%2").arg(path, file.errorString()));
        return;
    }
    const QString suffix = QFileInfo(path).suffix().toLower();
    std::vector<std::unique_ptr<Shape>> shapes;
    if (suffix == u"cdr" || suffix == u"cdt") {
        Document imported;
        QString error;
        if (!cdr::read(file.readAll(), imported, &error)) {
            QMessageBox::warning(this, tr("Import"), tr("Could not read %1:\n%2").arg(path, error));
            return;
        }
        auto pages = imported.releasePages();
        for (auto &layer : pages.front()->layers) {
            for (auto &shape : layer->shapes)
                shapes.push_back(std::move(shape));
        }
    } else {
        shapes = svg::readShapes(file.readAll());
    }
    if (shapes.empty()) {
        QMessageBox::information(this, tr("Import"), tr("%1 contains nothing OpenDraw can import.").arg(path));
        return;
    }
    m_canvas->addShapes(std::move(shapes), QPointF(), tr("Import"));
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
    QStringList filters{kSvgFilter};
    if (cdr::isAvailable()) {
        filters.prepend(tr("All supported files (*.svg *.cdr *.cdt)"));
        filters << tr("CorelDRAW (*.cdr *.cdt)");
    }
    const QString path = QFileDialog::getOpenFileName(this, tr("Open"), QString(), filters.join(QStringLiteral(";;")));
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
    const QString suffix = QFileInfo(path).suffix().toLower();
    const bool corel = suffix == u"cdr" || suffix == u"cdt";
    QString error;
    svg::ReadReport report;
    const bool ok = corel ? cdr::read(file.readAll(), *m_document, &error, &report)
                          : svg::read(&file, *m_document, &error, &report);
    if (!ok) {
        QMessageBox::warning(this, tr("Open"), tr("Could not read %1:\n%2").arg(path, error));
        return false;
    }
    m_undoStack->clear();
    // An imported file is not overwritten: Save asks where to put the SVG.
    setFilePath(corel ? QString() : path, path);
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
            if (!corel)
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
    QString suggested = m_filePath;
    if (suggested.isEmpty()) {
        const QFileInfo source(m_sourcePath);
        suggested = m_sourcePath.isEmpty() ? tr("Untitled.svg")
                                           : source.dir().filePath(source.completeBaseName() + QStringLiteral(".svg"));
    }
    QString path = QFileDialog::getSaveFileName(this, tr("Save As"), suggested, kSvgFilter);
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

void MainWindow::insertPage(int index)
{
    m_canvas->finishTextEdit();
    m_undoStack->push(new AddPageCommand(m_document, index));
}

void MainWindow::deletePage()
{
    if (m_document->pageCount() < 2)
        return;
    m_canvas->finishTextEdit();
    m_undoStack->push(new RemovePageCommand(m_document, m_document->currentPage()));
}

void MainWindow::renamePage()
{
    Page *page = m_document->currentPage();
    bool ok = false;
    const QString name =
        QInputDialog::getText(this, tr("Rename Page"), tr("Page name:"), QLineEdit::Normal, page->name, &ok).trimmed();
    if (ok && !name.isEmpty() && name != page->name)
        m_undoStack->push(new RenamePageCommand(m_document, page, name));
}

void MainWindow::exportDocument()
{
    m_canvas->finishTextEdit();
    const QString pdfFilter = tr("PDF document (*.pdf)");
    const QString pngFilter = tr("PNG image (*.png)");

    QSettings settings;
    QString filter = settings.value(QStringLiteral("export/filter"), pdfFilter).toString();
    const QString base = m_sourcePath.isEmpty() ? tr("Untitled") : QFileInfo(m_sourcePath).completeBaseName();
    const QString dir = settings.value(QStringLiteral("export/dir"), QFileInfo(m_sourcePath).absolutePath()).toString();
    const QString suggested = QDir(dir).filePath(base + (filter == pngFilter ? QStringLiteral(".png")
                                                                               : QStringLiteral(".pdf")));
    QString path = QFileDialog::getSaveFileName(this, tr("Export"), suggested, pdfFilter + QStringLiteral(";;") + pngFilter,
                                                &filter);
    if (path.isEmpty())
        return;

    const bool png = filter == pngFilter || path.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive);
    if (QFileInfo(path).suffix().isEmpty())
        path += png ? QStringLiteral(".png") : QStringLiteral(".pdf");
    settings.setValue(QStringLiteral("export/filter"), png ? pngFilter : pdfFilter);
    settings.setValue(QStringLiteral("export/dir"), QFileInfo(path).absolutePath());

    bool ok = false;
    QString error;
    if (png) {
        std::vector<const Shape *> selection(m_canvas->selection().begin(), m_canvas->selection().end());
        ExportImageDialog dialog(*m_document, std::move(selection), this);
        if (dialog.exec() != QDialog::Accepted)
            return;
        const QImage image = exporter::renderImage(*m_document, dialog.options());
        ok = !image.isNull() && image.save(path, "PNG");
        if (!ok)
            error = image.isNull() ? tr("The image is too large or there is not enough memory.")
                                   : tr("The file could not be written.");
    } else {
        ok = exporter::writePdf(*m_document, path, &error);
    }

    if (ok)
        statusBar()->showMessage(tr("Exported %1").arg(QFileInfo(path).fileName()), 4000);
    else
        QMessageBox::warning(this, tr("Export"), tr("Could not export %1:\n%2").arg(path, error));
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
    return m_canvas->currentStyle();
}

void MainWindow::chooseFillColor()
{
    FillDialog dialog(displayedStyle(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    if (!m_canvas->selection().empty()) {
        restyleSelection(tr("Fill"), [&dialog](Style &style) { style = dialog.applyTo(style); });
    } else {
        m_canvas->setDefaultStyle(dialog.applyTo(m_canvas->defaultStyle()));
        statusBar()->showMessage(tr("Default fill for new shapes changed"), 3000);
    }
    updateStyleIndicators();
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
    m_canvas->restyle(text, change);
}

// With a selection, colours apply to every object in it (including inside groups);
// without one, they become the default for new shapes.
void MainWindow::applyFill(const QColor &color)
{
    // While typing, a colour picked for a selected part of the text colours just those letters.
    if (m_canvas->editedText() && m_canvas->hasTextSelection() && color.alpha() > 0) {
        CharFormat change;
        change.color = color;
        m_canvas->applyCharFormat(change);
        m_canvas->setFocus(); // keep typing
        return;
    }
    if (!m_canvas->selection().empty()) {
        restyleSelection(tr("Fill"), [&color](Style &style) {
            style.fill = color;
            style.gradient.reset(); // a palette colour replaces a gradient or pattern
            style.pattern.reset();
        });
    } else {
        Style defaults = m_canvas->defaultStyle();
        defaults.fill = color;
        defaults.gradient.reset();
        defaults.pattern.reset();
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

void MainWindow::createTextBar()
{
    m_textBar = new QToolBar(tr("Text"), this);
    m_textBar->setObjectName(QStringLiteral("text"));
    m_textBar->setMovable(false);

    m_fontBox = new QFontComboBox(this);
    m_fontBox->setToolTip(tr("Font"));
    m_textBar->addWidget(m_fontBox);

    m_fontSizeBox = new QDoubleSpinBox(this);
    m_fontSizeBox->setRange(1.0, 2000.0);
    m_fontSizeBox->setDecimals(1);
    m_fontSizeBox->setSuffix(tr(" pt"));
    m_fontSizeBox->setKeyboardTracking(false);
    m_fontSizeBox->setToolTip(tr("Font size"));
    m_textBar->addWidget(m_fontSizeBox);
    m_textBar->addSeparator();

    auto addToggle = [this](const QString &text, const QString &tip) {
        QAction *action = m_textBar->addAction(text);
        action->setCheckable(true);
        action->setToolTip(tip);
        return action;
    };
    m_boldAction = addToggle(tr("B"), tr("Bold"));
    QFont boldFont = font();
    boldFont.setBold(true);
    m_boldAction->setFont(boldFont);
    m_italicAction = addToggle(tr("I"), tr("Italic"));
    QFont italicFont = font();
    italicFont.setItalic(true);
    m_italicAction->setFont(italicFont);
    m_textBar->addSeparator();

    auto *alignGroup = new QActionGroup(this);
    const std::tuple<QString, TextAlign, Icon> aligns[] = {{tr("Left"), TextAlign::Left, Icon::AlignLeft},
                                                           {tr("Center"), TextAlign::Center, Icon::AlignCenter},
                                                           {tr("Right"), TextAlign::Right, Icon::AlignRight},
                                                           {tr("Justify"), TextAlign::Justify, Icon::AlignJustify}};
    for (const auto &[text, align, iconId] : aligns) {
        QAction *action = addToggle(text, tr("Align text: %1").arg(text));
        action->setIcon(icon(iconId));
        action->setData(int(align));
        alignGroup->addAction(action);
        m_textAlignActions << action;
    }

    // Any change sends the whole format; the canvas applies it to the right target.
    auto apply = [this] {
        if (m_updatingTextBar)
            return;
        TextFormat format = m_canvas->currentTextFormat();
        format.family = m_fontBox->currentFont().family();
        format.size = m_fontSizeBox->value() * 96.0 / 72.0;
        format.bold = m_boldAction->isChecked();
        format.italic = m_italicAction->isChecked();
        for (QAction *action : std::as_const(m_textAlignActions)) {
            if (action->isChecked())
                format.align = TextAlign(action->data().toInt());
        }
        m_canvas->setTextFormat(format);
        if (m_canvas->editedText())
            m_canvas->setFocus();
    };
    connect(m_fontBox, &QFontComboBox::currentFontChanged, this, apply);
    connect(m_fontSizeBox, &QDoubleSpinBox::valueChanged, this, apply);
    connect(m_boldAction, &QAction::triggered, this, apply);
    connect(m_italicAction, &QAction::triggered, this, apply);
    connect(alignGroup, &QActionGroup::triggered, this, apply);

    addToolBar(Qt::TopToolBarArea, m_textBar);
    m_textBar->hide();
    connect(m_canvas, &CanvasWidget::textEditingChanged, this, &MainWindow::updateTextBar);
    connect(m_canvas, &CanvasWidget::selectionChanged, this, &MainWindow::updateTextBar);
    connect(m_document, &Document::changed, this, &MainWindow::updateTextBar);
}

// Shown with the text tool, while typing, or when a text is selected; always shows the
// format that a change would start from.
void MainWindow::updateTextBar()
{
    const Shape *selected = m_canvas->selectedShape();
    const bool visible = m_canvas->tool() == CanvasWidget::Tool::Text || m_canvas->editedText()
                         || (selected && selected->kind() == ShapeKind::Text);
    m_textBar->setVisible(visible);
    if (!visible)
        return;

    const TextFormat format = m_canvas->currentTextFormat();
    m_updatingTextBar = true;
    m_fontBox->setCurrentFont(QFont(format.family));
    m_fontSizeBox->setValue(format.size * 72.0 / 96.0);
    m_boldAction->setChecked(format.bold);
    m_italicAction->setChecked(format.italic);
    for (QAction *action : std::as_const(m_textAlignActions))
        action->setChecked(TextAlign(action->data().toInt()) == format.align);
    m_updatingTextBar = false;
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

    // Status bar indicators: fill and outline of the selection, or of new shapes.
    const QRectF swatchBox(24.5, 2.5, 18, 15);
    m_fillIndicator->setIcon(indicatorIcon(icon(Icon::FillBucket), render::fillBrush(style, swatchBox), !style.hasFill()));
    m_fillIndicator->setText(style.hasFill() ? describeFill(style) : tr("None"));
    m_outlineIndicator->setIcon(indicatorIcon(icon(Icon::OutlinePen), QBrush(style.stroke), !style.hasStroke()));
    const Units &units = Units::instance();
    m_outlineIndicator->setText(style.hasStroke() ? QStringLiteral("%1  %2 %3")
                                                        .arg(describeColor(style.stroke))
                                                        .arg(QLocale().toString(units.toDisplay(style.strokeWidth), 'f',
                                                                                units.decimals()))
                                                        .arg(units.suffix())
                                                  : tr("None"));

    if (m_canvas->selection().empty()) {
        m_hintLabel->setText(toolHint());
        return;
    }
    QString prefix = tr("%n objects selected", nullptr, int(m_canvas->selection().size()));
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
            prefix = selected->isBlend() ? tr("Blend of %n step(s)", nullptr, selected->blendSteps())
                                         : tr("Group of %n object(s)", nullptr, int(selected->children().size()));
            break;
        case ShapeKind::Text: {
            const TextFormat &f = selected->textData().format;
            prefix = tr("Text, %1 %2 pt").arg(f.family, QLocale().toString(f.size * 72.0 / 96.0, 'f', 1));
            break;
        }
        case ShapeKind::Image:
            prefix = tr("Bitmap, %1 × %2 pixels").arg(selected->image().width()).arg(selected->image().height());
            break;
        }
    }
    QString text = prefix;
    if (const Layer *layer = m_document->layerOf(m_canvas->selection().front()); layer && selected)
        text = tr("%1 on %2").arg(prefix, layer->name);
    if (selected && style.opacity < 1.0)
        text += tr("   Opacity: %1%").arg(qRound(style.opacity * 100));
    if (m_canvas->rotateMode() && m_canvas->tool() == CanvasWidget::Tool::Select)
        text += QStringLiteral(" — ")
                + tr("Drag a corner arrow to rotate (Ctrl: 15° steps), an edge arrow to skew, the centre to move "
                     "it; click the selection again to resize");
    m_hintLabel->setText(text);
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

void MainWindow::setFilePath(const QString &path, const QString &sourcePath)
{
    m_filePath = path;
    m_sourcePath = sourcePath;
    updateTitle();
}

void MainWindow::updateTitle()
{
    const QString name = documentName();
    setWindowTitle(QStringLiteral("%1[*] — OpenDraw").arg(name));
    setWindowModified(!m_undoStack->isClean());
    m_documentTabs->setTabText(0, m_undoStack->isClean() ? name : name + QLatin1Char('*'));
    m_documentTabs->setTabToolTip(0, m_sourcePath.isEmpty() ? name : QDir::toNativeSeparators(m_sourcePath));
}

} // namespace opendraw
