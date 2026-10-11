#pragma once

#include <QPointF>
#include <QToolBar>

class QComboBox;
class QDoubleSpinBox;
class QToolButton;
class QUndoStack;

namespace opendraw {

class Document;
class Shape;

// Context-sensitive bar under the toolbars. With nothing selected it shows the page (size,
// orientation), the unit, and the nudge and duplicate distances; with a selection, the
// object's position, size, mirroring, outline width and opacity. Values are in the display unit.
class PropertyBar : public QToolBar {
    Q_OBJECT

public:
    PropertyBar(Document *document, QUndoStack *undoStack, QWidget *parent = nullptr);

    // `single` is the selected shape when exactly one is selected; `anySelected` tells
    // whether there is a selection at all.
    void setSelection(Shape *single, bool anySelected);
    void setShape(Shape *shape) { setSelection(shape, shape != nullptr); }

signals:
    void mirrorRequested(Qt::Orientation orientation);
    void nudgeDistanceChanged(double documentUnits);
    void duplicateOffsetChanged(const QPointF &documentUnits);

private:
    QDoubleSpinBox *addField(const QString &label, const QString &toolTip, double minimum, QList<QAction *> &group);
    QToolButton *addButton(const QIcon &icon, const QString &toolTip, QList<QAction *> &group);
    void updateMode();
    void refresh();
    void refreshPage();
    void updateUnits();
    void applyGeometry();
    void applyOutlineWidth();
    void applyOpacity();
    void applyPageSize(const QSizeF &size);

    Document *m_document;
    QUndoStack *m_undoStack;
    Shape *m_shape = nullptr;
    bool m_anySelected = false;

    QList<QAction *> m_pageActions;
    QList<QAction *> m_objectActions;

    // Page
    QComboBox *m_paper;
    QDoubleSpinBox *m_pageWidth;
    QDoubleSpinBox *m_pageHeight;
    QToolButton *m_portrait;
    QToolButton *m_landscape;
    QComboBox *m_unit;
    QDoubleSpinBox *m_nudge;
    QDoubleSpinBox *m_duplicateX;
    QDoubleSpinBox *m_duplicateY;

    // Object
    QDoubleSpinBox *m_x;
    QDoubleSpinBox *m_y;
    QDoubleSpinBox *m_width;
    QDoubleSpinBox *m_height;
    QToolButton *m_lockRatio;
    QDoubleSpinBox *m_outline;
    QDoubleSpinBox *m_opacity;
};

} // namespace opendraw
