#include "core/Commands.h"
#include "core/Document.h"
#include "core/Generators.h"
#include "core/PathOps.h"
#include "core/Render.h"
#include "core/RichText.h"
#include "core/TextLayout.h"

#include <QPen>
#include "core/SvgIO.h"

#include <QBuffer>
#include <QUndoStack>
#include <QtTest>

using namespace opendraw;

namespace {

bool near(double a, double b, double tolerance = 1e-6)
{
    return std::abs(a - b) <= tolerance;
}

QByteArray writeSvg(const Document &doc)
{
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    svg::write(doc, &buffer);
    return data;
}

std::unique_ptr<Document> readSvg(QByteArray data)
{
    QBuffer buffer(&data);
    buffer.open(QIODevice::ReadOnly);
    auto doc = std::make_unique<Document>();
    svg::read(&buffer, *doc);
    return doc;
}

} // namespace

class ToolTests : public QObject {
    Q_OBJECT

private slots:
    void polygonHasItsCornersOnTheBox();
    void starAlternatesTipsAndNotches();
    void spiralStaysInsideItsBox();
    void graphPaperIsAGroupOfCells();
    void roundedRectangles();
    void guidelinesUndoAndSvg();
    void readsInkscapeGuides();
    void rotatedShapesStayEditable();
    void rotatedTextAndSvg();
    void objectNamesAreSaved();
    void outlineStyles();
    void paragraphTextWraps();
    void paragraphTextSvg();
    void richTextEditing();
    void richTextLayoutAndSvg();
};

void ToolTests::richTextEditing()
{
    TextFormat format;
    format.size = 20;
    TextData data{QStringLiteral("Hello world"), format};
    CharFormat bold;
    bold.bold = true;
    richtext::apply(data, 6, 11, [&](CharFormat &c) { c.bold = true; });
    QCOMPARE(data.runs.size(), std::size_t(1));
    QCOMPARE(data.runs.front().start, 6);
    QCOMPARE(data.runs.front().length, 5);
    QVERIFY(richtext::effectiveFormat(data, 7).bold);
    QVERIFY(!richtext::effectiveFormat(data, 2).bold);

    // Typing continues the format of the letter before; deleting shifts the runs.
    richtext::insert(data, 11, QStringLiteral("!!"));
    QCOMPARE(data.runs.front().length, 7);
    richtext::insert(data, 0, QStringLiteral(">"));
    QCOMPARE(data.runs.front().start, 7);
    richtext::remove(data, 0, 3);
    QCOMPARE(data.text, QStringLiteral("llo world!!"));
    QCOMPARE(data.runs.front().start, 4);

    // Overlapping changes merge; setting the whole text drops what repeats it.
    richtext::apply(data, 0, 6, [](CharFormat &c) { c.size = 30; });
    QCOMPARE(richtext::effectiveFormat(data, 5).size, 30.0);
    QVERIFY(richtext::effectiveFormat(data, 5).bold);
    richtext::applyToAll(data, bold);
    QVERIFY(data.format.bold);
    for (const TextRun &run : data.runs)
        QVERIFY(!run.format.bold);
    richtext::apply(data, 0, int(data.text.size()), [](CharFormat &c) { c.size = 20; });
    QVERIFY(data.runs.empty()); // everything back to the text's own format
}

void ToolTests::richTextLayoutAndSvg()
{
    TextFormat format;
    format.size = 20;
    TextData data{QStringLiteral("Big\nsmall red"), format};
    richtext::apply(data, 0, 3, [](CharFormat &c) { c.size = 60; });
    richtext::apply(data, 10, 13, [](CharFormat &c) { c.color = QColor(Qt::red); });
    const auto layout = textlayout::layout(data, QPointF(0, 100));
    QCOMPARE(layout.lines.size(), std::size_t(2));
    QVERIFY(layout.lines[0].ascent > layout.lines[1].ascent * 2); // the big line is taller
    QCOMPARE(layout.lines[1].pieces.size(), std::size_t(2));
    QCOMPARE(layout.lines[1].pieces[1].color, std::optional<QColor>(QColor(Qt::red)));
    QVERIFY(textlayout::selectionRects(data, QPointF(0, 100), 1, 6).size() == 2);
    QCOMPARE(textlayout::coloredOutline(data, QPointF(0, 100)).size(), std::size_t(2));
    // The caret goes where the mouse points.
    const QLineF caret = textlayout::caret(data, QPointF(0, 100), 6);
    QCOMPARE(textlayout::indexAt(data, QPointF(0, 100), caret.center()), 6);

    Document doc;
    Style style;
    style.fill = Qt::black;
    style.stroke = Qt::transparent;
    doc.insertShape(-1, std::make_unique<Shape>(data, QPointF(0, 100), style));
    const QByteArray svgData = writeSvg(doc);
    QVERIFY(svgData.contains("font-size=\"60\""));
    auto copy = readSvg(svgData);
    const TextData &read = copy->shapes().front()->textData();
    QCOMPARE(read.text, data.text);
    QCOMPARE(read.format.size, 20.0); // not the first letter's 60: the text says 20
    QCOMPARE(read.runs, data.runs);
    QCOMPARE(read.format.lineSpacing, data.format.lineSpacing);

    // Spans formatted by another program (fonts on the spans only, as the CorelDRAW import).
    auto other = readSvg(QByteArrayLiteral(
        R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="300" height="300">
                <text x="10" y="50"><tspan font-family="Arial" font-size="14" fill="#373435">Title</tspan><tspan font-family="Arial" font-size="12" fill="#373435"> body</tspan></text>
              </svg>)svg"));
    const Shape &text = *other->shapes().front();
    QCOMPARE(text.textData().format.size, 14.0);
    QCOMPARE(text.style().fill, QColor(0x37, 0x34, 0x35));
    QCOMPARE(text.textData().runs.size(), std::size_t(1)); // only " body" differs (its size)
    QCOMPARE(*text.textData().runs.front().format.size, 12.0);
    QVERIFY(!text.textData().runs.front().format.color);
}

void ToolTests::paragraphTextWraps()
{
    TextFormat format;
    format.size = 20;
    const QString words = QStringLiteral("one two three four five six seven eight nine ten");
    TextData data{words + QStringLiteral("\nthe end"), format, QSizeF(120, 1000)};
    const QPointF origin(10, 20);
    const auto layout = textlayout::layout(data, origin);
    QVERIFY(layout.lines.size() >= 4); // wrapped, plus the second paragraph
    QCOMPARE(layout.bounds, QRectF(origin, data.frame));
    for (const auto &line : layout.lines) {
        QVERIFY(line.width <= 120 + 1e-6);
        QVERIFY(!line.text.startsWith(QLatin1Char(' ')) && !line.text.endsWith(QLatin1Char(' ')));
        QVERIFY(!line.hidden);
    }
    // The first paragraph's lines put back together give the original words.
    QString rebuilt;
    for (const auto &line : layout.lines) {
        if (line.start >= words.size())
            break;
        rebuilt += data.text.mid(line.start, std::min(line.next, int(words.size())) - line.start);
    }
    QCOMPARE(rebuilt, words);
    QVERIFY(layout.lines.front().softBreak);
    QVERIFY(!layout.lines.back().softBreak);
    QCOMPARE(layout.lines.back().text, QStringLiteral("the end"));
    // First baseline one ascent below the top of the frame.
    QVERIFY(std::abs(layout.lines.front().baselineStart.y() - (20 + layout.ascent)) < 1e-9);

    // The caret follows the wrap.
    const int inSecondLine = layout.lines[1].start + 1;
    const QLineF caret = textlayout::caret(data, origin, inSecondLine);
    QVERIFY(std::abs(caret.p2().y() - (layout.lines[1].baselineStart.y() + layout.descent)) < 1e-9);

    // A word too long for the frame is broken inside; centred lines sit in the middle.
    TextData narrow{QStringLiteral("Incomprehensibilities"), format, QSizeF(60, 1000)};
    narrow.format.align = TextAlign::Center;
    const auto broken = textlayout::layout(narrow, origin);
    QVERIFY(broken.lines.size() >= 3);
    const auto &mid = broken.lines[1];
    QVERIFY(std::abs((mid.baselineStart.x() + mid.width / 2) - (origin.x() + 30)) < 1e-6);

    // Lines below the frame are hidden.
    TextData shortFrame{words, format, QSizeF(120, 30)};
    const auto clipped = textlayout::layout(shortFrame, origin);
    QVERIFY(clipped.overflow);
    QVERIFY(!clipped.lines.front().hidden);
    QVERIFY(clipped.lines.back().hidden);

    // Resizing the frame reflows; scaling scales.
    Shape text(data, origin, Style{});
    const std::size_t lines = textlayout::layout(text.textData(), text.textOrigin()).lines.size();
    text.setBounds(QRectF(0, 0, 400, 300));
    QCOMPARE(text.bounds(), QRectF(0, 0, 400, 300));
    QCOMPARE(text.textData().format.size, 20.0);
    QVERIFY(textlayout::layout(text.textData(), text.textOrigin()).lines.size() < lines);
    text.transform(QTransform::fromScale(2, 2));
    QCOMPARE(text.bounds(), QRectF(0, 0, 800, 600));
    QCOMPARE(text.textData().format.size, 40.0);
}

void ToolTests::paragraphTextSvg()
{
    TextFormat format;
    format.size = 20;
    format.align = TextAlign::Right;
    const QString content = QStringLiteral("Lorem ipsum  dolor sit amet, consectetur adipiscing\n\nelit sed do");
    Document doc;
    doc.insertShape(-1, std::make_unique<Shape>(TextData{content, format, QSizeF(150, 400)}, QPointF(30, 40), Style{}));
    const QByteArray data = writeSvg(doc);
    QVERIFY(data.contains("opendraw:frame=\"30 40 150 400\""));
    QVERIFY(data.contains("opendraw:soft=\"true\""));
    auto copy = readSvg(data);
    const Shape &read = *copy->shapes().front();
    QCOMPARE(read.textData().text, content);
    QCOMPARE(read.textData().frame, QSizeF(150, 400));
    QCOMPARE(read.textOrigin(), QPointF(30, 40));
    QCOMPARE(read.textData().format.align, TextAlign::Right);

    // As the CorelDRAW import marks it up: a frame and paragraph marks.
    auto imported = readSvg(QStringLiteral(
        R"svg(<svg xmlns="http://www.w3.org/2000/svg" xmlns:opendraw="https://github.com/R4P0S1T0/OpenDraw/ns/1"
                   width="200" height="200">
                <text x="0" y="190" opendraw:frame="10 20 100 150" font-size="12">
                  <tspan>First paragraph</tspan><tspan>%1</tspan><tspan>Second</tspan>
                </text>
              </svg>)svg").arg(QChar(0xE000)).toUtf8());
    const Shape &paragraph = *imported->shapes().front();
    QCOMPARE(paragraph.textData().text, QStringLiteral("First paragraph\nSecond"));
    QCOMPARE(paragraph.textData().frame, QSizeF(100, 150));
    QCOMPARE(paragraph.textOrigin(), QPointF(10, 20));
}

// Dashes (relative to the outline width), ends and corners survive SVG, and the renderer uses them.
void ToolTests::outlineStyles()
{
    Style style;
    style.strokeWidth = 2;
    style.dashes = {4, 2};
    style.cap = LineCap::Round;
    style.join = LineJoin::Bevel;
    const QPen pen = render::outlinePen(style);
    QCOMPARE(pen.capStyle(), Qt::RoundCap);
    QCOMPARE(pen.joinStyle(), Qt::BevelJoin);
    QCOMPARE(pen.dashPattern(), QList<qreal>({4, 2}));

    Document doc;
    doc.insertShape(-1, std::make_unique<Shape>(generators::line({0, 0}, {100, 0}), style));
    const QByteArray data = writeSvg(doc);
    QVERIFY(data.contains("stroke-dasharray=\"8 4\"")); // absolute lengths in the file
    QVERIFY(data.contains("stroke-linecap=\"round\""));
    QVERIFY(data.contains("stroke-linejoin=\"bevel\""));
    auto copy = readSvg(data);
    const Style &read = copy->shapes().front()->style();
    QCOMPARE(read.dashes, std::vector<double>({4, 2}));
    QCOMPARE(read.cap, LineCap::Round);
    QCOMPARE(read.join, LineJoin::Bevel);

    // From other programs: inherited from a group, odd-length patterns kept as they are.
    auto other = readSvg(QByteArrayLiteral(
        R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="100" height="100">
                <g stroke="black" stroke-width="3" stroke-dasharray="3,6,9" stroke-linecap="square">
                  <line x1="0" y1="0" x2="50" y2="0"/>
                </g>
                <line x1="0" y1="10" x2="50" y2="10" stroke="black" stroke-dasharray="none"/>
              </svg>)svg"));
    const Style &dashed = other->shapes()[0]->children().front()->style();
    QCOMPARE(dashed.dashes, std::vector<double>({1, 2, 3}));
    QCOMPARE(dashed.cap, LineCap::Square);
    QVERIFY(other->shapes()[1]->style().dashes.empty());
    QCOMPARE(render::outlinePen(dashed).dashPattern().size(), 6); // repeated to an even length
}

// Names given in the Objects docker are stored as Inkscape labels.
void ToolTests::objectNamesAreSaved()
{
    Document doc;
    Shape *rect = doc.insertShape(-1, std::make_unique<Shape>(ShapeKind::Rectangle, QRectF(0, 0, 10, 10)));
    std::vector<std::unique_ptr<Shape>> members;
    members.push_back(std::make_unique<Shape>(ShapeKind::Ellipse, QRectF(20, 0, 10, 10)));
    Shape *group = doc.insertShape(-1, std::make_unique<Shape>(std::move(members)));
    QUndoStack undo;
    undo.push(new RenameShapeCommand(&doc, rect, QStringLiteral("Logo")));
    group->setName(QStringLiteral("Ícones"));
    group->children().front()->setName(QStringLiteral("Círculo"));

    const QByteArray data = writeSvg(doc);
    QVERIFY(data.contains("inkscape:label=\"Logo\""));
    auto copy = readSvg(data);
    QCOMPARE(copy->shapes()[0]->name(), QStringLiteral("Logo"));
    QCOMPARE(copy->shapes()[1]->name(), QStringLiteral("Ícones"));
    QCOMPARE(copy->shapes()[1]->children().front()->name(), QStringLiteral("Círculo"));
    undo.undo();
    QVERIFY(rect->name().isEmpty());
}

namespace {
QTransform rotationAround(const QPointF &c, double degrees)
{
    return QTransform::fromTranslate(-c.x(), -c.y()) * QTransform().rotate(degrees) * QTransform::fromTranslate(c.x(), c.y());
}

bool sameRect(const QRectF &a, const QRectF &b)
{
    return near(a.x(), b.x(), 1e-6) && near(a.y(), b.y(), 1e-6) && near(a.width(), b.width(), 1e-6)
           && near(a.height(), b.height(), 1e-6);
}
} // namespace

void ToolTests::rotatedShapesStayEditable()
{
    Style style;
    style.strokeWidth = 2;
    Shape rect(ShapeKind::Rectangle, QRectF(0, 0, 100, 50), style);
    rect.setCornerRadius(5);
    rect.transform(rotationAround({50, 25}, 90));
    QCOMPARE(rect.kind(), ShapeKind::Rectangle);
    QVERIFY(near(rect.rotation(), 90));
    QVERIFY(sameRect(rect.bounds(), QRectF(25, -25, 50, 100)));
    QVERIFY(sameRect(rect.localBounds(), QRectF(0, 0, 100, 50)));
    QCOMPARE(rect.cornerRadius(), 5.0);

    // Moving and scaling a turned rectangle keep it turned.
    rect.moveBy({10, 0});
    QVERIFY(sameRect(rect.bounds(), QRectF(35, -25, 50, 100)));
    rect.setBounds(QRectF(35, -25, 50, 200));
    QVERIFY(sameRect(rect.bounds(), QRectF(35, -25, 50, 200)));

    // Turned back upright, it is an ordinary rectangle again.
    Shape ellipse(ShapeKind::Ellipse, QRectF(0, 0, 40, 20));
    ellipse.transform(rotationAround({20, 10}, 30));
    QVERIFY(!ellipse.shapeTransform().isIdentity());
    ellipse.transform(rotationAround({20, 10}, -30));
    QVERIFY(ellipse.shapeTransform().isIdentity());
    QVERIFY(sameRect(ellipse.bounds(), QRectF(0, 0, 40, 20)));

    // Rotated rectangles are saved as rectangles with a transform, outline width unchanged.
    Document doc;
    auto owned = std::make_unique<Shape>(ShapeKind::Rectangle, QRectF(0, 0, 100, 50), style);
    owned->transform(rotationAround({50, 25}, 30) * QTransform::fromScale(2, 2));
    doc.insertShape(-1, std::move(owned));
    const QByteArray data = writeSvg(doc);
    QVERIFY(data.contains("non-scaling-stroke"));
    auto copy = readSvg(data);
    const Shape &read = *copy->shapes().front();
    QCOMPARE(read.kind(), ShapeKind::Rectangle);
    QVERIFY(near(read.rotation(), 30, 1e-6));
    QVERIFY(near(read.style().strokeWidth, 2));
    QVERIFY(sameRect(read.bounds(), doc.shapes().front()->bounds()));
}

void ToolTests::rotatedTextAndSvg()
{
    TextFormat format;
    format.size = 20;
    Shape text(TextData{QStringLiteral("Hello"), format}, QPointF(10, 50), Style{});
    const QRectF upright = text.bounds();
    text.transform(rotationAround(upright.center(), 90));
    QVERIFY(near(text.rotation(), 90));
    QVERIFY(near(text.bounds().width(), upright.height(), 1e-3));
    QVERIFY(near(text.textData().format.size, 20)); // the font keeps its size
    QVERIFY(text.contains(text.bounds().center(), 0));

    Document doc;
    auto owned = std::make_unique<Shape>(TextData{QStringLiteral("Hello"), format}, QPointF(10, 50), Style{});
    owned->transform(rotationAround(upright.center(), 45));
    doc.insertShape(-1, std::move(owned));
    auto copy = readSvg(writeSvg(doc));
    const Shape &read = *copy->shapes().front();
    QCOMPARE(read.kind(), ShapeKind::Text);
    QVERIFY(near(read.rotation(), 45, 1e-6));
    QVERIFY(near(read.textData().format.size, 20));
    QVERIFY(sameRect(read.bounds(), doc.shapes().front()->bounds()));
}

void ToolTests::polygonHasItsCornersOnTheBox()
{
    const QRectF box(10, 20, 100, 100);
    const PathData hexagon = generators::polygon(box, 6);
    QCOMPARE(hexagon.size(), std::size_t(1));
    QVERIFY(hexagon.front().closed);
    QCOMPARE(hexagon.front().nodes.size(), std::size_t(6));
    // First corner at the top centre; all corners on the circle.
    QVERIFY(near(hexagon.front().nodes.front().point.x(), 60));
    QVERIFY(near(hexagon.front().nodes.front().point.y(), 20));
    for (const PathNode &node : hexagon.front().nodes)
        QVERIFY(near(std::hypot(node.point.x() - 60, node.point.y() - 70), 50));
    QCOMPARE(generators::polygon(box, 1).front().nodes.size(), std::size_t(3)); // at least a triangle
}

void ToolTests::starAlternatesTipsAndNotches()
{
    const PathData star = generators::star(QRectF(0, 0, 100, 100), 5, 50);
    const auto &nodes = star.front().nodes;
    QCOMPARE(nodes.size(), std::size_t(10));
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const double r = std::hypot(nodes[i].point.x() - 50, nodes[i].point.y() - 50);
        QVERIFY(near(r, i % 2 ? 25.0 : 50.0));
    }
}

void ToolTests::spiralStaysInsideItsBox()
{
    const QRectF box(0, 0, 200, 100);
    const PathData spiral = generators::spiral(box, 3);
    QVERIFY(!spiral.front().closed);
    QVERIFY(spiral.front().nodes.size() >= 4);
    const QRectF bounds = pathops::toPainterPath(spiral).boundingRect();
    QVERIFY(box.adjusted(-1, -1, 1, 1).contains(bounds));
    QVERIFY(bounds.width() > 150 && bounds.height() > 75); // and fills most of it
}

void ToolTests::graphPaperIsAGroupOfCells()
{
    auto grid = generators::graphPaper(QRectF(0, 0, 40, 30), 4, 3, Style{});
    QCOMPARE(grid->kind(), ShapeKind::Group);
    QCOMPARE(grid->children().size(), std::size_t(12));
    QCOMPARE(grid->children().back()->bounds(), QRectF(30, 20, 10, 10));
    QCOMPARE(grid->bounds(), QRectF(0, 0, 40, 30));
}

void ToolTests::roundedRectangles()
{
    Document doc;
    Shape *rect = doc.insertShape(-1, std::make_unique<Shape>(ShapeKind::Rectangle, QRectF(0, 0, 100, 50)));
    QUndoStack undo;
    undo.push(new SetCornerRadiusCommand(&doc, rect, 10));
    QCOMPARE(rect->cornerRadius(), 10.0);
    QVERIFY(!rect->path().contains(QPointF(1, 1)));  // the corner is cut off
    QVERIFY(rect->path().contains(QPointF(50, 25)));

    // Saved as rx/ry and read back as an editable rounded rectangle.
    auto copy = readSvg(writeSvg(doc));
    const Shape &read = *copy->shapes().front();
    QCOMPARE(read.kind(), ShapeKind::Rectangle);
    QVERIFY(near(read.cornerRadius(), 10));

    // Scaling scales the corners with the shorter side's factor.
    rect->transform(QTransform::fromScale(2, 3));
    QVERIFY(near(rect->cornerRadius(), 20));
    undo.undo();
    QCOMPARE(rect->cornerRadius(), 0.0);
}

void ToolTests::guidelinesUndoAndSvg()
{
    Document doc;
    QUndoStack undo;
    undo.push(new GuideCommand(&doc, 0, std::nullopt, Guide{Qt::Vertical, 30}));
    undo.push(new GuideCommand(&doc, 1, std::nullopt, Guide{Qt::Horizontal, 40}));
    QCOMPARE(doc.guides().size(), std::size_t(2));
    undo.push(new GuideCommand(&doc, 0, doc.guides()[0], Guide{Qt::Vertical, 35}));
    QCOMPARE(doc.guides()[0].position, 35.0);
    undo.undo();
    QCOMPARE(doc.guides()[0].position, 30.0);

    const QByteArray data = writeSvg(doc);
    QVERIFY(data.contains("sodipodi:guide"));
    auto copy = readSvg(data);
    QCOMPARE(copy->guides().size(), std::size_t(2));
    QCOMPARE(copy->guides()[0].orientation, Qt::Vertical);
    QVERIFY(near(copy->guides()[0].position, 30));
    QCOMPARE(copy->guides()[1].orientation, Qt::Horizontal);
    QVERIFY(near(copy->guides()[1].position, 40));

    undo.undo();
    undo.undo();
    QVERIFY(doc.guides().empty());
    doc.insertGuide(-1, Guide{});
    doc.clear();
    QVERIFY(doc.guides().empty());
}

// Inkscape measures guide positions upwards from the bottom of the page.
void ToolTests::readsInkscapeGuides()
{
    auto doc = readSvg(QByteArrayLiteral(
        R"svg(<svg xmlns="http://www.w3.org/2000/svg" xmlns:sodipodi="http://sodipodi.sourceforge.net/DTD/sodipodi-0.dtd"
                   width="200" height="100" viewBox="0 0 200 100">
                <sodipodi:namedview>
                  <sodipodi:guide position="0,75" orientation="0,1"/>
                  <sodipodi:guide position="120,0" orientation="-1,0"/>
                  <sodipodi:guide position="10,10" orientation="0.7,0.7"/>
                </sodipodi:namedview>
              </svg>)svg"));
    QCOMPARE(doc->guides().size(), std::size_t(2)); // the slanted one is not supported
    QCOMPARE(doc->guides()[0].orientation, Qt::Horizontal);
    QVERIFY(near(doc->guides()[0].position, 25));
    QCOMPARE(doc->guides()[1].orientation, Qt::Vertical);
    QVERIFY(near(doc->guides()[1].position, 120));
}

QTEST_MAIN(ToolTests)
#include "test_tools.moc"
