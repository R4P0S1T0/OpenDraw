#pragma once

#include <QWidget>

class QScrollBar;

namespace opendraw {

class CanvasWidget;
class Document;

// Ruler along the top or left edge of the canvas, in the display unit, with a marker
// following the cursor.
class Ruler : public QWidget {
    Q_OBJECT

public:
    Ruler(Qt::Orientation orientation, CanvasWidget *canvas, QWidget *parent = nullptr);

    QSize sizeHint() const override;
    void setCursorPosition(double documentPosition);

protected:
    void paintEvent(QPaintEvent *event) override;
    // Dragging from a ruler pulls out a guideline.
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QPointF onCanvas(const QMouseEvent *event) const;

    Qt::Orientation m_orientation;
    CanvasWidget *m_canvas;
    double m_cursor = qQNaN();
    bool m_dragging = false;
};

// The canvas framed by rulers and scroll bars.
class CanvasView : public QWidget {
    Q_OBJECT

public:
    CanvasView(CanvasWidget *canvas, Document *document, QWidget *parent = nullptr);

    bool rulersVisible() const;
    void setRulersVisible(bool visible);
    // Follows another open drawing.
    void setDocument(Document *document);

private:
    void syncScrollBars();
    void scrolled();

    CanvasWidget *m_canvas;
    Document *m_document;
    Ruler *m_horizontalRuler;
    Ruler *m_verticalRuler;
    QWidget *m_corner;
    QScrollBar *m_horizontalBar;
    QScrollBar *m_verticalBar;
    bool m_syncing = false;
};

} // namespace opendraw
