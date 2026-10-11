#pragma once

#include "CanvasWidget.h"
#include "ToolSettings.h"

#include <QPointF>
#include <QToolBar>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;
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
    // Shows the options of the active tool.
    void setTool(CanvasWidget::Tool tool);
    void setToolSettings(const ToolSettings &settings);
    void setEyedropperColor(const QColor &color);

signals:
    void mirrorRequested(Qt::Orientation orientation);
    void nudgeDistanceChanged(double documentUnits);
    void duplicateOffsetChanged(const QPointF &documentUnits);
    void toolSettingsChanged(const opendraw::ToolSettings &settings);
    // The eyedropper should pick a new colour.
    void eyedropperResetRequested();

private:
    QDoubleSpinBox *addField(const QString &label, const QString &toolTip, double minimum, QList<QAction *> &group);
    QSpinBox *addIntField(const QString &label, const QString &toolTip, int minimum, int maximum,
                          QList<QAction *> &group);
    void emitToolSettings();
    // Options of the active tool, or nullptr when it has none.
    QList<QAction *> *toolGroup();
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
    QList<QAction *> m_cornerActions; // rectangles only, part of the object actions
    QList<QAction *> m_rectangleToolActions;
    QList<QAction *> m_polygonActions;
    QList<QAction *> m_starActions;
    QList<QAction *> m_spiralActions;
    QList<QAction *> m_graphActions;
    QList<QAction *> m_eyedropperActions;
    CanvasWidget::Tool m_tool = CanvasWidget::Tool::Select;
    ToolSettings m_toolSettings;
    bool m_updatingToolFields = false;

    // Tools
    QDoubleSpinBox *m_newCornerRadius;
    QSpinBox *m_polygonSides;
    QSpinBox *m_starPoints;
    QSpinBox *m_starSharpness;
    QDoubleSpinBox *m_spiralRevolutions;
    QSpinBox *m_graphColumns;
    QSpinBox *m_graphRows;
    QLabel *m_eyedropperSwatch;
    QDoubleSpinBox *m_cornerRadius;

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
