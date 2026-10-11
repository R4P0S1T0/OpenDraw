#pragma once

#include "core/Shape.h"

#include <QMainWindow>

#include <functional>

class QAction;
class QActionGroup;
class QComboBox;
class QDoubleSpinBox;
class QFontComboBox;
class QLabel;
class QMenu;
class QTabBar;
class QTimer;
class QToolBar;
class QToolButton;
class QUndoStack;

namespace opendraw {

class CanvasView;
class CanvasWidget;
class ColorPalette;
class Document;
class PageBar;
class PropertyBar;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    bool openFile(const QString &path);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void newDocument();
    void open();
    void import();
    bool save();
    bool saveAs();
    void exportDocument();
    void insertPage(int index);
    void deletePage();
    void renamePage();
    void chooseFillColor();
    void chooseOutlineColor();
    void applyFill(const QColor &color);
    void applyOutline(const QColor &color);
    void about();
    void updateTitle();
    void updateStyleIndicators();
    void updateNodeActions();
    void updateTextBar();
    void updateDocumentPalette();

private:
    void createActions();
    void createStandardBar();
    void createToolbox(QMenu *toolsMenu);
    void createSnapActions(QMenu *viewMenu);
    void createTextBar();
    void createStatusBar();
    QWidget *createDocumentTabs();
    const Style &displayedStyle() const;
    void restyleSelection(const QString &text, const std::function<void(Style &)> &change);
    bool maybeSave();
    bool writeFile(const QString &path);
    // `sourcePath` is the file the document came from; Save writes to `path` (empty after an
    // import, so a CorelDRAW file is never overwritten with SVG).
    void setFilePath(const QString &path, const QString &sourcePath);
    void setFilePath(const QString &path) { setFilePath(path, path); }
    QString documentName() const;
    QString toolHint() const;

    Document *m_document;
    QUndoStack *m_undoStack;
    CanvasWidget *m_canvas;
    CanvasView *m_canvasView = nullptr;
    PropertyBar *m_propertyBar = nullptr;
    PageBar *m_pageBar = nullptr;
    ColorPalette *m_documentPalette = nullptr;
    QTimer *m_documentPaletteTimer = nullptr;
    QString m_filePath;
    QString m_sourcePath;

    QAction *m_fillAction = nullptr;
    QAction *m_outlineAction = nullptr;
    QAction *m_cutAction = nullptr;
    QAction *m_copyAction = nullptr;
    QAction *m_pasteAction = nullptr;
    QAction *m_undoAction = nullptr;
    QAction *m_redoAction = nullptr;
    QAction *m_rulersAction = nullptr;
    QList<QAction *> m_toolActions;
    QToolBar *m_standardBar = nullptr;
    QToolBar *m_toolbox = nullptr;
    QToolBar *m_paletteBar = nullptr;
    QToolBar *m_nodeBar = nullptr;
    QList<QAction *> m_nodeActions; // need an active node
    QAction *m_joinAction = nullptr; // needs two end nodes

    QTabBar *m_documentTabs = nullptr;
    QAction *m_deletePageAction = nullptr;
    QMenu *m_viewMenu = nullptr;
    QMenu *m_windowMenu = nullptr;
    QComboBox *m_zoomBox = nullptr;

    QToolBar *m_textBar = nullptr;
    QFontComboBox *m_fontBox = nullptr;
    QDoubleSpinBox *m_fontSizeBox = nullptr;
    QAction *m_boldAction = nullptr;
    QAction *m_italicAction = nullptr;
    QList<QAction *> m_textAlignActions;
    bool m_updatingTextBar = false;
    QLabel *m_hintLabel = nullptr;
    QLabel *m_positionLabel = nullptr;
    QToolButton *m_fillIndicator = nullptr;
    QToolButton *m_outlineIndicator = nullptr;
};

} // namespace opendraw
