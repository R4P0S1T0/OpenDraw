#pragma once

#include <QToolBar>

class QDoubleSpinBox;
class QUndoStack;

namespace opendraw {

class Document;
class Shape;

// Position, size and outline width of the selected shape, editable in millimetres.
class PropertyBar : public QToolBar {
    Q_OBJECT

public:
    PropertyBar(Document *document, QUndoStack *undoStack, QWidget *parent = nullptr);

    void setShape(Shape *shape);

private:
    QDoubleSpinBox *addField(const QString &label, const QString &toolTip, double minimum);
    void refresh();
    void applyGeometry();
    void applyOutlineWidth();
    void applyOpacity();

    Document *m_document;
    QUndoStack *m_undoStack;
    Shape *m_shape = nullptr;

    QDoubleSpinBox *m_x;
    QDoubleSpinBox *m_y;
    QDoubleSpinBox *m_width;
    QDoubleSpinBox *m_height;
    QDoubleSpinBox *m_outline;
    QDoubleSpinBox *m_opacity;
};

} // namespace opendraw
