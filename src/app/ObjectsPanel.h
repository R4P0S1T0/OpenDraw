#pragma once

#include <QHash>
#include <QSet>
#include <QTreeWidget>
#include <QWidget>

class QTimer;
class QToolButton;
class QUndoStack;

namespace opendraw {

class CanvasWidget;
class Document;
class Shape;
struct Layer;
struct Page;

// Tree that reports drops instead of moving its items; the panel turns them into commands.
class ObjectsTree : public QTreeWidget {
    Q_OBJECT

public:
    using QTreeWidget::QTreeWidget;

    // Where dragged items were dropped relative to the target item.
    enum class DropPosition { On, Above, Below };
    Q_ENUM(DropPosition)

signals:
    void itemsDropped(const QList<QTreeWidgetItem *> &items, QTreeWidgetItem *target,
                      opendraw::ObjectsTree::DropPosition position);

protected:
    void dropEvent(QDropEvent *event) override;
};

// The Objects docker: every page, its layers (topmost first) and their objects (topmost
// first, groups expandable). Selecting objects here selects them on the drawing and the
// other way round; layers show and toggle visibility and lock; pages, layers and objects
// can be renamed; objects are dragged to restack them or move them to another layer.
class ObjectsPanel : public QWidget {
    Q_OBJECT

public:
    ObjectsPanel(Document *document, QUndoStack *undoStack, CanvasWidget *canvas, QWidget *parent = nullptr);

    // Rebuilds the tree now (it also rebuilds itself shortly after any change).
    void refresh();

    // How an object is listed: its name, or what it is.
    static QString describe(const Shape &shape);

private:
    enum class Kind { Page, Layer, Shape };

    static Kind kindOf(const QTreeWidgetItem *item);
    static void *pointerOf(const QTreeWidgetItem *item);
    Shape *topShapeOf(const QTreeWidgetItem *item) const;
    QTreeWidgetItem *addShapeItem(QTreeWidgetItem *parent, Shape *shape, Shape *top);
    void syncSelectionFromCanvas();
    void selectionChangedInTree();
    void onItemChanged(QTreeWidgetItem *item, int column);
    void onItemClicked(QTreeWidgetItem *item, int column);
    void onDrop(const QList<QTreeWidgetItem *> &items, QTreeWidgetItem *target, ObjectsTree::DropPosition position);
    void showContextMenu(const QPoint &position);
    void deleteSelected();
    void moveUp(bool up);
    void updateButtons();

    Document *m_document;
    QUndoStack *m_undoStack;
    CanvasWidget *m_canvas;
    ObjectsTree *m_tree;
    QTimer *m_refreshTimer;
    QToolButton *m_add;
    QToolButton *m_remove;
    QToolButton *m_up;
    QToolButton *m_down;
    QToolButton *m_moveHere;
    QHash<const Shape *, QTreeWidgetItem *> m_shapeItems; // top-level shapes and group members
    QSet<const void *> m_collapsed;                       // pages and layers the user closed
    QSet<const void *> m_expanded;                        // groups the user opened
    bool m_updating = false;
};

} // namespace opendraw
