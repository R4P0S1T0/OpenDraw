#pragma once

#include <QColor>
#include <QList>
#include <QWidget>

namespace opendraw {

// Vertical strip of colour swatches. Left click picks a fill, right click an outline.
// The first swatch is "no colour".
class ColorPalette : public QWidget {
    Q_OBJECT

public:
    explicit ColorPalette(QWidget *parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void fillChosen(const QColor &color);
    void outlineChosen(const QColor &color);

protected:
    bool event(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    QRect swatchRect(int index) const;
    int swatchAt(const QPoint &pos) const;

    QList<QColor> m_colors;
};

} // namespace opendraw
