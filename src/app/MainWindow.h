#pragma once

#include <QMainWindow>

class QAction;
class QActionGroup;
class QLabel;
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
    void chooseFillColor();
    void chooseOutlineColor();
    void applyFill(const QColor &color);
    void applyOutline(const QColor &color);
    void about();
    void updateTitle();
    void updateStyleIndicators();

private:
    void createActions();
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
    QLabel *m_styleLabel = nullptr;
    QLabel *m_zoomLabel = nullptr;
    QLabel *m_positionLabel = nullptr;
};

} // namespace opendraw
