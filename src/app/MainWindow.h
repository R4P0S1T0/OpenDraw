#pragma once

#include "core/Shape.h"

#include <QMainWindow>

#include <functional>

class QAction;
class QActionGroup;
class QDoubleSpinBox;
class QFontComboBox;
class QLabel;
class QToolBar;
class QUndoStack;

namespace opendraw {

class CanvasWidget;
class Document;
class PropertyBar;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    bool openFile(const QString &path);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void newDocument();
    void open();
    bool save();
    bool saveAs();
    void exportDocument();
    void chooseFillColor();
    void chooseOutlineColor();
    void applyFill(const QColor &color);
    void applyOutline(const QColor &color);
    void about();
    void updateTitle();
    void updateStyleIndicators();
    void updateNodeActions();
    void updateTextBar();

private:
    void createActions();
    void createTextBar();
    const Style &displayedStyle() const;
    void restyleSelection(const QString &text, const std::function<void(Style &)> &change);
    bool maybeSave();
    bool writeFile(const QString &path);
    void setFilePath(const QString &path);

    Document *m_document;
    QUndoStack *m_undoStack;
    CanvasWidget *m_canvas;
    PropertyBar *m_propertyBar = nullptr;
    QString m_filePath;

    QAction *m_fillAction = nullptr;
    QAction *m_outlineAction = nullptr;
    QToolBar *m_nodeBar = nullptr;
    QList<QAction *> m_nodeActions; // need an active node
    QAction *m_joinAction = nullptr; // needs two end nodes

    QToolBar *m_textBar = nullptr;
    QFontComboBox *m_fontBox = nullptr;
    QDoubleSpinBox *m_fontSizeBox = nullptr;
    QAction *m_boldAction = nullptr;
    QAction *m_italicAction = nullptr;
    QList<QAction *> m_textAlignActions;
    bool m_updatingTextBar = false;
    QLabel *m_styleLabel = nullptr;
    QLabel *m_zoomLabel = nullptr;
    QLabel *m_positionLabel = nullptr;
};

} // namespace opendraw
