#pragma once

#include <QColor>
#include <QList>
#include <QWidget>

namespace opendraw {

// A strip of colour swatches, vertical (the main palette at the right edge) or horizontal
// (the document palette). Left click picks a fill, right click an outline. The first swatch
// is "no colour". Scrolls with the arrows at its ends or the mouse wheel when it does not fit.
class ColorPalette : public QWidget {
    Q_OBJECT

public:
    explicit ColorPalette(Qt::Orientation orientation = Qt::Vertical, QWidget *parent = nullptr);

    // OpenDraw's default palette.
    static QList<QColor> defaultColors();
    // Replaces the swatches; "no colour" is always kept first.
    void setColors(const QList<QColor> &colors);
    const QList<QColor> &colors() const { return m_colors; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void fillChosen(const QColor &color);
    void outlineChosen(const QColor &color);

protected:
    bool event(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    bool overflows() const;
    int visibleCount() const;
    int maxOffset() const;
    void scrollBy(int swatches);
    QRect swatchRect(int index) const; // index into m_colors, accounting for the scroll offset
    QRect arrowRect(bool backwards) const;
    int swatchAt(const QPoint &pos) const;
    int along(const QSize &size) const;
    int along(const QPoint &point) const;

    Qt::Orientation m_orientation;
    QList<QColor> m_colors;
    int m_offset = 0;
};

} // namespace opendraw
