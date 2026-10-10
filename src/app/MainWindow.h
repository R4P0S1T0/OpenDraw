#pragma once

#include <QMainWindow>

class QAction;
class QActionGroup;
class QLabel;
class QUndoStack;

namespace opendraw {

class CanvasWidget;
class Document;

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
    void about();
    void updateTitle();

private:
    void createActions();
    bool maybeSave();
    bool writeFile(const QString &path);
    void setFilePath(const QString &path);
    void updateFillIcon();

    Document *m_document;
    QUndoStack *m_undoStack;
    CanvasWidget *m_canvas;
    QString m_filePath;

    QAction *m_fillAction = nullptr;
    QLabel *m_zoomLabel = nullptr;
    QLabel *m_positionLabel = nullptr;
};

} // namespace opendraw
