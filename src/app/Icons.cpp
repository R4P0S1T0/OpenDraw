#include "Icons.h"

#include <QApplication>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>

namespace opendraw {

namespace {

const QColor kAccent(0x3d, 0x8e, 0xe6);

// Draws one icon on the 24 × 24 grid with the given ink.
void draw(QPainter &p, Icon id, const QColor &ink, const QColor &accent, const QColor &paper)
{
    const QPen line(ink, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(line);
    p.setBrush(Qt::NoBrush);
    auto polygon = [&p](std::initializer_list<QPointF> points) {
        p.drawPolygon(std::data(points), int(points.size()));
    };
    auto polyline = [&p](std::initializer_list<QPointF> points) {
        p.drawPolyline(std::data(points), int(points.size()));
    };
    auto arrowHead = [&](QPointF tip, QPointF a, QPointF b) { polyline({a, tip, b}); };

    switch (id) {
    case Icon::New:
        polygon({{6, 3}, {14, 3}, {18, 7}, {18, 21}, {6, 21}});
        polyline({{14, 3}, {14, 7}, {18, 7}});
        p.setPen(QPen(accent, 1.8, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(12, 11), QPointF(12, 17));
        p.drawLine(QPointF(9, 14), QPointF(15, 14));
        break;
    case Icon::Open:
        polyline({{3, 19}, {3, 5}, {9, 5}, {11, 7}, {18, 7}, {18, 10}});
        polygon({{3, 19}, {6, 10}, {21, 10}, {18, 19}});
        break;
    case Icon::Save:
        polygon({{4, 4}, {17, 4}, {20, 7}, {20, 20}, {4, 20}});
        p.drawRect(QRectF(8, 4, 8, 5));
        p.drawRect(QRectF(7, 13, 10, 7));
        break;
    case Icon::Cut:
        p.drawEllipse(QPointF(7, 17), 3, 3);
        p.drawEllipse(QPointF(17, 17), 3, 3);
        p.drawLine(QPointF(9, 15), QPointF(17, 3));
        p.drawLine(QPointF(15, 15), QPointF(7, 3));
        break;
    case Icon::Copy:
        p.drawRoundedRect(QRectF(8, 3, 11, 13), 1.5, 1.5);
        p.setBrush(paper);
        p.drawRoundedRect(QRectF(5, 8, 11, 13), 1.5, 1.5);
        break;
    case Icon::Paste:
        p.drawRoundedRect(QRectF(5, 5, 14, 16), 1.5, 1.5);
        p.setBrush(ink);
        p.drawRoundedRect(QRectF(9, 3, 6, 4), 1, 1);
        p.setBrush(Qt::NoBrush);
        p.drawLine(QPointF(9, 12), QPointF(15, 12));
        p.drawLine(QPointF(9, 16), QPointF(15, 16));
        break;
    case Icon::Undo:
    case Icon::Redo: {
        if (id == Icon::Redo) {
            p.translate(24, 0);
            p.scale(-1, 1);
        }
        QPainterPath path;
        path.moveTo(5, 10);
        path.lineTo(14, 10);
        path.cubicTo(21, 10, 21, 20, 14, 20);
        path.lineTo(10, 20);
        p.drawPath(path);
        arrowHead({5, 10}, {9, 6}, {9, 14});
        break;
    }
    case Icon::Import:
    case Icon::Export:
        polyline({{4, 14}, {4, 20}, {20, 20}, {20, 14}});
        p.setPen(QPen(accent, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        if (id == Icon::Import) {
            p.drawLine(QPointF(12, 3), QPointF(12, 15));
            arrowHead({12, 15}, {8, 11}, {16, 11});
        } else {
            p.drawLine(QPointF(12, 15), QPointF(12, 3));
            arrowHead({12, 3}, {8, 7}, {16, 7});
        }
        break;
    case Icon::ExportPdf: {
        polygon({{6, 3}, {14, 3}, {18, 7}, {18, 21}, {6, 21}});
        polyline({{14, 3}, {14, 7}, {18, 7}});
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0xd8, 0x3b, 0x3b));
        p.drawRoundedRect(QRectF(3, 11, 15, 7), 1, 1);
        QFont font = QApplication::font();
        font.setPixelSize(6);
        font.setBold(true);
        p.setFont(font);
        p.setPen(Qt::white);
        p.drawText(QRectF(3, 11, 15, 7), Qt::AlignCenter, QStringLiteral("PDF"));
        break;
    }
    case Icon::Rulers:
        polygon({{3, 3}, {21, 3}, {21, 8}, {8, 8}, {8, 21}, {3, 21}});
        p.setPen(QPen(ink, 1.2, Qt::SolidLine, Qt::RoundCap));
        for (double x : {11.0, 14.0, 17.0})
            p.drawLine(QPointF(x, 3), QPointF(x, x == 14.0 ? 6.5 : 5.5));
        for (double y : {11.0, 14.0, 17.0})
            p.drawLine(QPointF(3, y), QPointF(y == 14.0 ? 6.5 : 5.5, y));
        break;
    case Icon::Pick:
        p.setBrush(ink);
        polygon({{6, 3}, {6, 19}, {10, 15}, {13, 21}, {15.5, 20}, {12.5, 14}, {18, 14}});
        break;
    case Icon::Shape: {
        QPainterPath curve;
        curve.moveTo(3, 20);
        curve.cubicTo(6, 9, 13, 22, 21, 6);
        p.setPen(QPen(ink, 1.2));
        p.drawPath(curve);
        p.setPen(line);
        p.setBrush(paper);
        polygon({{9, 4}, {9, 15}, {11.8, 12.3}, {14, 16.5}, {15.6, 15.7}, {13.4, 11.6}, {17, 11.6}});
        p.setBrush(accent);
        p.setPen(Qt::NoPen);
        p.drawRect(QRectF(18.5, 3.5, 4, 4));
        break;
    }
    case Icon::Freehand: {
        QPainterPath squiggle;
        squiggle.moveTo(3, 17);
        squiggle.cubicTo(6, 5, 10, 5, 10.5, 12);
        squiggle.cubicTo(11, 19, 16, 19, 20, 7);
        p.drawPath(squiggle);
        break;
    }
    case Icon::Bezier: {
        QPainterPath curve;
        curve.moveTo(4, 19);
        curve.cubicTo(4, 6, 20, 18, 20, 5);
        p.drawPath(curve);
        p.setPen(QPen(accent, 1.1));
        p.drawLine(QPointF(20, 5), QPointF(15, 2));
        p.drawLine(QPointF(20, 5), QPointF(23, 10));
        p.setPen(Qt::NoPen);
        p.setBrush(ink);
        p.drawRect(QRectF(2, 17, 4, 4));
        p.drawRect(QRectF(18, 3, 4, 4));
        break;
    }
    case Icon::Zoom:
        p.drawEllipse(QPointF(10, 10), 6, 6);
        p.setPen(QPen(ink, 2.6, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(14.5, 14.5), QPointF(20, 20));
        break;
    case Icon::Pan:
        p.drawLine(QPointF(12, 3), QPointF(12, 21));
        p.drawLine(QPointF(3, 12), QPointF(21, 12));
        arrowHead({12, 3}, {9.5, 5.5}, {14.5, 5.5});
        arrowHead({12, 21}, {9.5, 18.5}, {14.5, 18.5});
        arrowHead({3, 12}, {5.5, 9.5}, {5.5, 14.5});
        arrowHead({21, 12}, {18.5, 9.5}, {18.5, 14.5});
        break;
    case Icon::Rectangle:
        p.drawRect(QRectF(4, 6, 16, 12));
        break;
    case Icon::Ellipse:
        p.drawEllipse(QRectF(3, 6, 18, 12));
        break;
    case Icon::Text:
        p.setPen(QPen(ink, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        polyline({{5, 20}, {12, 4}, {19, 20}});
        p.drawLine(QPointF(8, 14), QPointF(16, 14));
        break;
    case Icon::FillBucket: {
        p.save();
        p.translate(11, 11);
        p.rotate(45);
        p.drawRoundedRect(QRectF(-5.5, -5.5, 11, 11), 1, 1);
        p.restore();
        p.drawLine(QPointF(4, 11), QPointF(18, 11));
        QPainterPath drop;
        drop.moveTo(20, 13);
        drop.cubicTo(18, 16, 18, 19, 20, 19);
        drop.cubicTo(22, 19, 22, 16, 20, 13);
        p.setBrush(accent);
        p.setPen(Qt::NoPen);
        p.drawPath(drop);
        break;
    }
    case Icon::OutlinePen:
        polygon({{12, 3}, {18, 10}, {14.5, 21}, {9.5, 21}, {6, 10}});
        p.drawLine(QPointF(12, 3), QPointF(12, 10));
        p.drawEllipse(QPointF(12, 12), 1.6, 1.6);
        break;
    case Icon::Portrait:
        p.drawRect(QRectF(7, 3, 10, 18));
        break;
    case Icon::Landscape:
        p.drawRect(QRectF(3, 7, 18, 10));
        break;
    case Icon::FirstPage:
    case Icon::PreviousPage:
    case Icon::NextPage:
    case Icon::LastPage:
        if (id == Icon::NextPage || id == Icon::LastPage) {
            p.translate(24, 0);
            p.scale(-1, 1);
        }
        p.setBrush(ink);
        if (id == Icon::FirstPage || id == Icon::LastPage) {
            p.drawLine(QPointF(7, 7), QPointF(7, 17));
            polygon({{17, 7}, {10, 12}, {17, 17}});
        } else {
            polygon({{15, 7}, {8, 12}, {15, 17}});
        }
        break;
    case Icon::AddPage:
        p.drawEllipse(QPointF(12, 12), 8, 8);
        p.drawLine(QPointF(12, 8), QPointF(12, 16));
        p.drawLine(QPointF(8, 12), QPointF(16, 12));
        break;
    case Icon::LockRatio: {
        p.drawRoundedRect(QRectF(6, 11, 12, 9), 1.5, 1.5);
        QPainterPath shackle;
        shackle.moveTo(8.5, 11);
        shackle.lineTo(8.5, 8);
        shackle.arcTo(QRectF(8.5, 4, 7, 7), 180, -180);
        shackle.lineTo(15.5, 11);
        p.drawPath(shackle);
        break;
    }
    case Icon::MirrorHorizontal:
    case Icon::MirrorVertical:
        if (id == Icon::MirrorVertical) {
            p.translate(12, 12);
            p.rotate(90);
            p.translate(-12, -12);
        }
        p.setPen(QPen(ink, 1.2, Qt::DashLine));
        p.drawLine(QPointF(12, 3), QPointF(12, 21));
        p.setPen(line);
        p.setBrush(ink);
        polygon({{10, 6}, {10, 18}, {3, 18}});
        p.setBrush(Qt::NoBrush);
        polygon({{14, 6}, {14, 18}, {21, 18}});
        break;
    case Icon::AlignLeft:
    case Icon::AlignCenter:
    case Icon::AlignRight: {
        const double longLine = 16, shortLine = 10;
        for (int i = 0; i < 4; ++i) {
            const double length = i % 2 ? shortLine : longLine;
            const double y = 6 + i * 4;
            double x = 4;
            if (id == Icon::AlignCenter)
                x = 12 - length / 2;
            else if (id == Icon::AlignRight)
                x = 20 - length;
            p.drawLine(QPointF(x, y), QPointF(x + length, y));
        }
        break;
    }
    case Icon::ScrollUp:
    case Icon::ScrollDown:
    case Icon::ScrollLeft:
    case Icon::ScrollRight:
        p.translate(12, 12);
        p.rotate(id == Icon::ScrollUp ? 0 : id == Icon::ScrollRight ? 90 : id == Icon::ScrollDown ? 180 : 270);
        p.translate(-12, -12);
        polyline({{7, 15}, {12, 9}, {17, 15}});
        break;
    }
}

class PaintedIconEngine : public QIconEngine {
public:
    explicit PaintedIconEngine(Icon id)
        : m_id(id)
    {
    }

    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State) override
    {
        const QPalette palette = QApplication::palette();
        QColor ink = palette.color(QPalette::WindowText);
        QColor accent = kAccent;
        if (mode == QIcon::Disabled) {
            ink = palette.color(QPalette::Disabled, QPalette::WindowText);
            accent = ink;
        } else if (mode == QIcon::Selected) {
            ink = palette.color(QPalette::HighlightedText);
        }
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->translate(rect.topLeft());
        painter->scale(rect.width() / 24.0, rect.height() / 24.0);
        draw(*painter, m_id, ink, accent, palette.color(QPalette::Window));
        painter->restore();
    }

    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    {
        return scaledPixmap(size, mode, state, 1.0);
    }

    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State state, qreal scale) override
    {
        QPixmap pixmap(size * scale);
        pixmap.fill(Qt::transparent);
        {
            QPainter painter(&pixmap);
            paint(&painter, QRect(QPoint(0, 0), pixmap.size()), mode, state);
        }
        pixmap.setDevicePixelRatio(scale);
        return pixmap;
    }

    QIconEngine *clone() const override { return new PaintedIconEngine(m_id); }
    QString key() const override { return QStringLiteral("opendraw-painted"); }

private:
    Icon m_id;
};

} // namespace

QIcon icon(Icon id)
{
    return QIcon(new PaintedIconEngine(id));
}

} // namespace opendraw
