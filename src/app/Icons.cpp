#include "Icons.h"

#include <QApplication>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPolygonF>

#include <cmath>
#include <numbers>

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
    case Icon::Line:
        p.drawLine(QPointF(4, 20), QPointF(20, 4));
        p.setPen(Qt::NoPen);
        p.setBrush(accent);
        p.drawRect(QRectF(2, 18, 4, 4));
        p.drawRect(QRectF(18, 2, 4, 4));
        break;
    case Icon::Polygon: {
        QPolygonF pentagon;
        for (int i = 0; i < 5; ++i) {
            const double a = -std::numbers::pi / 2 + 2 * std::numbers::pi * i / 5;
            pentagon << QPointF(12 + 9 * std::cos(a), 13 + 9 * std::sin(a));
        }
        p.drawPolygon(pentagon);
        break;
    }
    case Icon::Star: {
        QPolygonF star;
        for (int i = 0; i < 10; ++i) {
            const double a = -std::numbers::pi / 2 + std::numbers::pi * i / 5;
            const double r = i % 2 ? 4.0 : 9.5;
            star << QPointF(12 + r * std::cos(a), 13 + r * std::sin(a));
        }
        p.drawPolygon(star);
        break;
    }
    case Icon::Spiral: {
        QPainterPath spiral;
        for (int i = 0; i <= 90; ++i) {
            const double t = i / 90.0;
            const double a = t * 3 * 2 * std::numbers::pi;
            const QPointF point(12 + 9 * t * std::cos(a), 12 + 9 * t * std::sin(a));
            if (i == 0)
                spiral.moveTo(point);
            else
                spiral.lineTo(point);
        }
        p.setPen(QPen(ink, 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(spiral);
        break;
    }
    case Icon::GraphPaper:
    case Icon::Grid:
        p.setPen(QPen(ink, id == Icon::Grid ? 1.0 : 1.3));
        for (double x : {4.0, 9.33, 14.67, 20.0})
            p.drawLine(QPointF(x, 4), QPointF(x, 20));
        for (double y : {4.0, 9.33, 14.67, 20.0})
            p.drawLine(QPointF(4, y), QPointF(20, y));
        break;
    case Icon::Eyedropper: {
        // Pipette: bulb at the top right, tip at the bottom left.
        p.save();
        p.translate(12, 12);
        p.rotate(45);
        p.drawRoundedRect(QRectF(-2.5, -10, 5, 6), 2, 2); // bulb
        p.drawLine(QPointF(-4.5, -4), QPointF(4.5, -4));   // collar
        p.drawPolygon(QPolygonF({QPointF(-2, -4), QPointF(2, -4), QPointF(1, 7), QPointF(0, 10), QPointF(-1, 7)}));
        p.restore();
        break;
    }
    case Icon::Guidelines:
        p.setPen(QPen(accent, 1.4, Qt::DashLine));
        p.drawLine(QPointF(8, 2), QPointF(8, 22));
        p.drawLine(QPointF(2, 15), QPointF(22, 15));
        break;
    case Icon::Snap: {
        // Magnet.
        QPainterPath magnet;
        magnet.moveTo(6, 4);
        magnet.lineTo(6, 13);
        magnet.arcTo(QRectF(6, 7, 12, 12), 180, 180);
        magnet.lineTo(18, 4);
        p.setPen(QPen(ink, 3.0, Qt::SolidLine, Qt::FlatCap));
        p.drawPath(magnet);
        p.setPen(QPen(accent, 3.0, Qt::SolidLine, Qt::FlatCap));
        p.drawLine(QPointF(6, 4), QPointF(6, 7));
        p.drawLine(QPointF(18, 4), QPointF(18, 7));
        break;
    }
    case Icon::CornerRadius: {
        QPainterPath corner;
        corner.moveTo(5, 20);
        corner.lineTo(5, 12);
        corner.arcTo(QRectF(5, 5, 14, 14), 180, -90);
        corner.lineTo(20, 5);
        p.drawPath(corner);
        break;
    }
    case Icon::Rotate:
    case Icon::RotateCounterclockwise: {
        if (id == Icon::RotateCounterclockwise) {
            p.translate(24, 0);
            p.scale(-1, 1);
        }
        // Three quarters of a circle with an arrow head, turning clockwise.
        QPainterPath arc;
        arc.arcMoveTo(QRectF(5, 5, 14, 14), 150);
        arc.arcTo(QRectF(5, 5, 14, 14), 150, -270);
        p.drawPath(arc);
        const QPointF tip = arc.pointAtPercent(1.0);
        p.setBrush(ink);
        polygon({tip + QPointF(-3.5, -2.5), tip + QPointF(3, -3.5), tip + QPointF(0.5, 3)});
        break;
    }
    case Icon::Eye:
    case Icon::EyeOff: {
        QPainterPath eye;
        eye.moveTo(2.5, 12);
        eye.quadTo(12, 3, 21.5, 12);
        eye.quadTo(12, 21, 2.5, 12);
        p.drawPath(eye);
        p.drawEllipse(QPointF(12, 12), 3, 3);
        if (id == Icon::EyeOff) {
            p.setPen(QPen(ink, 1.8, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(QPointF(4, 20), QPointF(20, 4));
        }
        break;
    }
    case Icon::Lock:
    case Icon::Unlock: {
        p.drawRoundedRect(QRectF(6, 11, 12, 9), 1.5, 1.5);
        QPainterPath shackle;
        shackle.moveTo(8.5, 11);
        shackle.lineTo(8.5, 8);
        shackle.arcTo(QRectF(8.5, 4, 7, 7), 180, -180);
        if (id == Icon::Lock)
            shackle.lineTo(15.5, 11);
        else
            shackle.lineTo(15.5, 8.5);
        p.drawPath(shackle);
        if (id == Icon::Lock) {
            p.setBrush(ink);
            p.drawEllipse(QPointF(12, 15.5), 1.2, 1.2);
        }
        break;
    }
    case Icon::Layer: {
        // Three stacked sheets.
        auto sheet = [&](double y) {
            polygon({{12, y}, {21, y + 4.5}, {12, y + 9}, {3, y + 4.5}});
        };
        p.setBrush(paper);
        sheet(10);
        sheet(7);
        p.setPen(QPen(accent, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        sheet(4);
        break;
    }
    case Icon::Page:
        polygon({{6, 3}, {14, 3}, {18, 7}, {18, 21}, {6, 21}});
        polyline({{14, 3}, {14, 7}, {18, 7}});
        break;
    case Icon::Group:
        p.drawRect(QRectF(4, 4, 9, 9));
        p.setBrush(paper);
        p.drawEllipse(QRectF(10, 10, 10, 10));
        p.setPen(QPen(accent, 1, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(2, 2, 20, 20));
        break;
    case Icon::Bitmap:
        p.drawRect(QRectF(3, 5, 18, 14));
        polyline({{3, 17}, {9, 11}, {13, 15}, {16, 12}, {21, 17}});
        p.setBrush(accent);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(16, 9), 1.8, 1.8);
        break;
    case Icon::FillNone:
        p.drawRect(QRectF(4, 4, 16, 16));
        p.setPen(QPen(QColor(0xd0, 0x30, 0x30), 1.8, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(5, 19), QPointF(19, 5));
        break;
    case Icon::FillUniform:
        p.setBrush(accent);
        p.drawRect(QRectF(4, 4, 16, 16));
        break;
    case Icon::FillLinear:
    case Icon::FillRadial: {
        QGradient *g = nullptr;
        QLinearGradient linear(4, 0, 20, 0);
        QRadialGradient radial(12, 12, 9);
        g = id == Icon::FillLinear ? static_cast<QGradient *>(&linear) : static_cast<QGradient *>(&radial);
        g->setColorAt(0, paper);
        g->setColorAt(1, accent);
        p.setBrush(*g);
        p.drawRect(QRectF(4, 4, 16, 16));
        break;
    }
    case Icon::Transparency:
        // Checkerboard showing through a half-solid square.
        p.setPen(Qt::NoPen);
        for (int y = 0; y < 4; ++y) {
            for (int x = 0; x < 4; ++x) {
                p.setBrush((x + y) % 2 ? ink : paper);
                p.drawRect(QRectF(4 + x * 4, 4 + y * 4, 4, 4));
            }
        }
        p.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 150));
        p.drawRect(QRectF(4, 4, 16, 8));
        p.setPen(line);
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(4, 4, 16, 16));
        break;
    case Icon::CapButt:
    case Icon::CapRound:
    case Icon::CapSquare: {
        // A thick line ending at the dashed mark, with the chosen end.
        QPen thick(accent, 8);
        thick.setCapStyle(id == Icon::CapButt ? Qt::FlatCap : id == Icon::CapRound ? Qt::RoundCap : Qt::SquareCap);
        p.setPen(thick);
        p.drawLine(QPointF(2, 12), QPointF(14, 12));
        p.setPen(QPen(ink, 1, Qt::DashLine));
        p.drawLine(QPointF(14, 4), QPointF(14, 20));
        break;
    }
    case Icon::JoinMiter:
    case Icon::JoinRound:
    case Icon::JoinBevel: {
        QPen thick(accent, 6);
        thick.setJoinStyle(id == Icon::JoinMiter ? Qt::MiterJoin : id == Icon::JoinRound ? Qt::RoundJoin : Qt::BevelJoin);
        thick.setCapStyle(Qt::FlatCap);
        p.setPen(thick);
        polyline({{4, 21}, {7, 7}, {21, 7}});
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
