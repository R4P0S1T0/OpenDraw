#pragma once

#include <QWidget>

class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;
class QUndoStack;

namespace opendraw {

class CanvasWidget;
class Document;
struct Layer;

// Layers of the current page, topmost first: visibility, lock, name and the active layer.
class LayersPanel : public QWidget {
    Q_OBJECT

public:
    LayersPanel(Document *document, QUndoStack *undoStack, CanvasWidget *canvas, QWidget *parent = nullptr);

    void refresh();

private:
    Layer *layerFor(const QTreeWidgetItem *item) const;
    void onItemChanged(QTreeWidgetItem *item, int column);
    void onCurrentItemChanged(QTreeWidgetItem *current);
    void moveActiveLayer(int step);

    Document *m_document;
    QUndoStack *m_undoStack;
    CanvasWidget *m_canvas;
    QTreeWidget *m_tree;
    QToolButton *m_add;
    QToolButton *m_remove;
    QToolButton *m_up;
    QToolButton *m_down;
    QToolButton *m_moveHere;
    bool m_updating = false;
};

} // namespace opendraw
