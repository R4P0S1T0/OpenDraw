#include "core/Commands.h"
#include "core/Document.h"
#include "core/SvgIO.h"
#include "core/TextLayout.h"

#include <QBuffer>
#include <QUndoStack>
#include <QtTest>

using namespace opendraw;

namespace {

Shape makeText(const QString &text, TextAlign align = TextAlign::Left)
{
    TextData data{text, {}};
    data.format.align = align;
    return Shape(data, QPointF(100, 100), Style{QColor(Qt::black), QColor(Qt::transparent), 0.0});
}

} // namespace

class TextTests : public QObject {
    Q_OBJECT

private slots:
    void boundsGrowWithText();
    void linesStackDownwards();
    void alignmentAroundOrigin();
    void emptyTextStaysClickable();
    void caretMovesAlongTheLine();
    void scalingChangesFontSize();
    void svgRoundTrip();
    void importsTspansAndInheritedFont();
    void undoTextChange();
    void justifiedLinesFillTheFrame();
    void indentsMoveAndNarrowLines();
    void paragraphSpacing();
    void paragraphFormatSurvivesSvg();
    void textFollowsItsCurve();
    void textOnCurveSurvivesSvg();
};

void TextTests::boundsGrowWithText()
{
    const Shape short_ = makeText(QStringLiteral("Hi"));
    const Shape long_ = makeText(QStringLiteral("Hello, OpenDraw"));
    QVERIFY(long_.bounds().width() > short_.bounds().width() * 3);
    QVERIFY(qFuzzyCompare(short_.bounds().height(), long_.bounds().height()));
    QVERIFY(!long_.path().isEmpty());
    // The baseline passes through the origin, so the box straddles y = 100.
    QVERIFY(long_.bounds().top() < 100 && long_.bounds().bottom() > 100);
}

void TextTests::linesStackDownwards()
{
    const Shape one = makeText(QStringLiteral("One"));
    const Shape three = makeText(QStringLiteral("One\nTwo\nThree"));
    const double lineHeight = TextFormat{}.size * TextFormat{}.lineSpacing;
    QVERIFY(qAbs(three.bounds().height() - (one.bounds().height() + 2 * lineHeight)) < 0.01);
    QCOMPARE(textlayout::layout(three.textData(), three.textOrigin()).lines.size(), std::size_t(3));
}

void TextTests::alignmentAroundOrigin()
{
    const Shape left = makeText(QStringLiteral("Centre me"), TextAlign::Left);
    const Shape centre = makeText(QStringLiteral("Centre me"), TextAlign::Center);
    const Shape right = makeText(QStringLiteral("Centre me"), TextAlign::Right);
    QVERIFY(qAbs(left.bounds().left() - 100) < 0.01);
    QVERIFY(qAbs(centre.bounds().center().x() - 100) < 0.01);
    QVERIFY(qAbs(right.bounds().right() - 100) < 0.01);
}

void TextTests::emptyTextStaysClickable()
{
    const Shape empty = makeText(QString());
    QVERIFY(empty.bounds().width() > 0);
    QVERIFY(empty.contains(empty.bounds().center(), 0.5));
}

void TextTests::caretMovesAlongTheLine()
{
    const Shape text = makeText(QStringLiteral("abc\ndef"));
    const QLineF start = textlayout::caret(text.textData(), text.textOrigin(), 0);
    const QLineF afterB = textlayout::caret(text.textData(), text.textOrigin(), 2);
    const QLineF secondLine = textlayout::caret(text.textData(), text.textOrigin(), 4);
    QCOMPARE(start.x1(), 100.0);
    QVERIFY(afterB.x1() > start.x1());
    QCOMPARE(secondLine.x1(), 100.0);
    QVERIFY(secondLine.y1() > start.y1());
}

void TextTests::scalingChangesFontSize()
{
    Shape text = makeText(QStringLiteral("Scale"));
    const QRectF before = text.bounds();
    text.transform(QTransform::fromScale(2, 2));
    QCOMPARE(text.textData().format.size, TextFormat{}.size * 2);
    QVERIFY(qAbs(text.bounds().width() - before.width() * 2) < 0.5);
    text.moveBy({10, 0});
    QCOMPARE(text.textOrigin(), QPointF(210, 200));
}

void TextTests::svgRoundTrip()
{
    Document original;
    TextData data{QStringLiteral("First line\n  indented & <special>"), {}};
    data.format.family = QStringLiteral("Courier New");
    data.format.size = 20;
    data.format.bold = true;
    data.format.italic = true;
    data.format.align = TextAlign::Center;
    original.insertShape(-1, std::make_unique<Shape>(data, QPointF(50, 60), Style{QColor(255, 0, 0), {}, 0}));

    QBuffer buffer;
    QVERIFY(buffer.open(QIODevice::ReadWrite));
    QVERIFY(svg::write(original, &buffer));
    buffer.seek(0);

    Document loaded;
    QVERIFY(svg::read(&buffer, loaded));
    QCOMPARE(loaded.count(), 1);
    const Shape &text = *loaded.shapes()[0];
    QCOMPARE(text.kind(), ShapeKind::Text);
    QCOMPARE(text.textData(), data);
    QCOMPARE(text.textOrigin(), QPointF(50, 60));
    QCOMPARE(text.style().fill, QColor(255, 0, 0));
}

void TextTests::importsTspansAndInheritedFont()
{
    QByteArray data = R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 400 400">
        <g font-family="'Times New Roman', serif" font-size="12pt" transform="translate(10 0)">
            <text x="20" y="30" text-anchor="end">
                <tspan x="20" y="30">Hello</tspan>
                <tspan x="20" dy="20">   wide    world   </tspan>
            </text>
        </g>
    </svg>)svg";
    QBuffer buffer(&data);
    QVERIFY(buffer.open(QIODevice::ReadOnly));

    Document doc;
    QVERIFY(svg::read(&buffer, doc));
    const Shape &text = *doc.shapes()[0]->children()[0];
    QCOMPARE(text.kind(), ShapeKind::Text);
    QCOMPARE(text.textData().text, QStringLiteral("Hello\nwide world"));
    QCOMPARE(text.textData().format.family, QStringLiteral("Times New Roman"));
    QCOMPARE(text.textData().format.size, 16.0); // 12 pt
    QCOMPARE(text.textData().format.align, TextAlign::Right);
    QCOMPARE(text.textOrigin(), QPointF(30, 30));
}

void TextTests::undoTextChange()
{
    Document doc;
    QUndoStack stack;
    Shape *text = doc.insertShape(-1, std::make_unique<Shape>(TextData{QStringLiteral("old"), {}}, QPointF(), Style{}));
    TextData changed = text->textData();
    changed.text = QStringLiteral("new");
    changed.format.bold = true;
    stack.push(new SetTextCommand(&doc, text, text->textData(), changed, QStringLiteral("Edit text")));
    QCOMPARE(text->textData().text, QStringLiteral("new"));
    stack.undo();
    QCOMPARE(text->textData().text, QStringLiteral("old"));
    QVERIFY(!text->textData().format.bold);
}

namespace {
const QString kLongText = QStringLiteral("The quick brown fox jumps over the lazy dog and keeps running far "
                                         "across the wide green field until the evening comes.");

TextData paragraphText(const QString &text, double width = 300)
{
    TextData data{text, {}};
    data.format.size = 20;
    data.frame = QSizeF(width, 400);
    return data;
}
} // namespace

void TextTests::justifiedLinesFillTheFrame()
{
    TextData data = paragraphText(kLongText + QStringLiteral("\nShort."));
    data.format.align = TextAlign::Justify;
    const QPointF origin(10, 10);
    const textlayout::Layout l = textlayout::layout(data, origin);
    QVERIFY(l.lines.size() >= 3);
    int justified = 0;
    for (const textlayout::Line &line : l.lines) {
        if (line.softBreak) {
            // Wrapped lines reach both edges of the frame.
            QVERIFY(qAbs(line.baselineStart.x() - 10) < 1e-6);
            QVERIFY(qAbs(line.width - 300) < 0.5);
            QVERIFY(line.wordSpacing > 0.0);
            // The caret at the end of the line is at the right edge, and the last word's
            // letters end there too.
            const QLineF caret = textlayout::caret(data, origin, line.start + int(line.text.size()));
            QVERIFY(qAbs(caret.x1() - 310) < 0.5);
            const textlayout::Piece &last = line.pieces.back();
            QVERIFY(qAbs(last.x + last.width - 310) < 0.5);
            ++justified;
        } else {
            QCOMPARE(line.wordSpacing, 0.0); // ends of paragraphs stay as they are
            QVERIFY(line.width < 300);
        }
    }
    QVERIFY(justified >= 2);
    // Clicking a letter of a justified line finds that letter.
    const textlayout::Line &first = l.lines.front();
    const int target = first.start + int(first.text.size()) - 2;
    const QLineF at = textlayout::caret(data, origin, target);
    QCOMPARE(textlayout::indexAt(data, origin, QPointF(at.x1() + 1, at.y1() + 5)), target);

    // Artistic text is never stretched.
    TextData artistic{kLongText, {}};
    artistic.format.align = TextAlign::Justify;
    QCOMPARE(textlayout::layout(artistic, origin).lines.front().wordSpacing, 0.0);
}

void TextTests::indentsMoveAndNarrowLines()
{
    TextData plain = paragraphText(kLongText);
    TextData indented = plain;
    indented.format.firstIndent = 40;
    indented.format.leftIndent = 15;
    indented.format.rightIndent = 60;
    const textlayout::Layout a = textlayout::layout(plain, QPointF(0, 0));
    const textlayout::Layout b = textlayout::layout(indented, QPointF(0, 0));
    QVERIFY(b.lines.size() > a.lines.size()); // less room, more lines
    QCOMPARE(b.lines[0].baselineStart.x(), 40.0);
    QCOMPARE(b.lines[1].baselineStart.x(), 15.0);
    for (const textlayout::Line &line : b.lines)
        QVERIFY(line.baselineStart.x() + line.width <= 300 - 60 + 0.5);

    // Right-aligned lines end at the right indent.
    indented.format.align = TextAlign::Right;
    const textlayout::Layout c = textlayout::layout(indented, QPointF(0, 0));
    QVERIFY(qAbs(c.lines[1].baselineStart.x() + c.lines[1].width - 240) < 1e-6);

    // Scaling the text scales its indents.
    Shape shape(indented, QPointF(0, 0), Style{});
    shape.transform(QTransform::fromScale(2, 2));
    QCOMPARE(shape.textData().format.firstIndent, 80.0);
    QCOMPARE(shape.textData().format.rightIndent, 120.0);
}

void TextTests::paragraphSpacing()
{
    for (bool paragraph : {false, true}) {
        TextData data = paragraphText(QStringLiteral("One\nTwo"));
        if (!paragraph)
            data.frame = QSizeF();
        const textlayout::Layout plain = textlayout::layout(data, QPointF(0, 0));
        data.format.spaceBefore = 0.5;
        data.format.spaceAfter = 0.25;
        const textlayout::Layout spaced = textlayout::layout(data, QPointF(0, 0));
        const double gapPlain = plain.lines[1].baselineStart.y() - plain.lines[0].baselineStart.y();
        const double gapSpaced = spaced.lines[1].baselineStart.y() - spaced.lines[0].baselineStart.y();
        QVERIFY(qAbs(gapSpaced - gapPlain - 0.75 * 20) < 1e-6);
        // Nothing above the first paragraph.
        QCOMPARE(spaced.lines[0].baselineStart.y(), plain.lines[0].baselineStart.y());
    }
    // Wrapped lines inside a paragraph keep the normal line spacing.
    TextData data = paragraphText(kLongText);
    data.format.spaceBefore = 2.0;
    const textlayout::Layout l = textlayout::layout(data, QPointF(0, 0));
    QVERIFY(qAbs(l.lines[1].baselineStart.y() - l.lines[0].baselineStart.y() - 20 * data.format.lineSpacing) < 1e-6);
}

void TextTests::paragraphFormatSurvivesSvg()
{
    Document original;
    TextData data = paragraphText(kLongText + QStringLiteral("\nSecond paragraph."));
    data.format.align = TextAlign::Justify;
    data.format.spaceBefore = 0.3;
    data.format.spaceAfter = 0.6;
    data.format.firstIndent = 25;
    data.format.leftIndent = 5;
    data.format.rightIndent = 12.5;
    original.insertShape(-1, std::make_unique<Shape>(data, QPointF(30, 40), Style{QColor(Qt::black), {}, 0}));

    QBuffer buffer;
    QVERIFY(buffer.open(QIODevice::ReadWrite));
    QVERIFY(svg::write(original, &buffer));
    QVERIFY(buffer.data().contains("word-spacing="));
    buffer.seek(0);
    Document loaded;
    QVERIFY(svg::read(&buffer, loaded));
    const TextData &read = loaded.shapes()[0]->textData();
    QCOMPARE(read.text, data.text);
    QVERIFY(read.format.sameParagraphFormat(data.format));
}

namespace {
TextData onLine(const QString &text, const QPointF &from, const QPointF &to, double offset = 0.0)
{
    TextData data{text, {}};
    data.format.size = 20;
    SubPath line;
    line.nodes = {PathNode::corner(from), PathNode::corner(to)};
    data.path = TextPath{{line}, offset};
    return data;
}
} // namespace

void TextTests::textFollowsItsCurve()
{
    // Along a horizontal line: like a straight text whose baseline is the line.
    const TextData flat = onLine(QStringLiteral("Wave"), {0, 100}, {300, 100}, 10);
    const QRectF glyphs = textlayout::outline(flat, {}).boundingRect();
    QVERIFY(glyphs.left() >= 10 && glyphs.left() < 14);
    QVERIFY(glyphs.top() < 90 && glyphs.bottom() <= 101);
    QVERIFY(textlayout::layout(flat, {}).bounds.contains(glyphs));
    // Down a vertical line: the letters turn, so the text is tall and narrow.
    const TextData down = onLine(QStringLiteral("Wave"), {200, 0}, {200, 300});
    const QRectF turned = textlayout::outline(down, {}).boundingRect();
    QVERIFY(turned.height() > 2 * turned.width());
    QVERIFY(turned.left() >= 199 && turned.right() < 230); // turned clockwise: the tops face right
    // The caret runs along the curve, across it.
    const QLineF start = textlayout::caret(down, {}, 0);
    QVERIFY(std::abs(start.p1().y() - start.p2().y()) < 1e-6); // horizontal: across a vertical line
    QVERIFY(std::abs(start.p2().y()) < 1e-6);
    const QLineF third = textlayout::caret(down, {}, 2);
    QVERIFY(third.p1().y() > 10);
    QCOMPARE(textlayout::indexAt(down, {}, (third.p1() + third.p2()) / 2 + QPointF(0, 1)), 2);
    QCOMPARE(textlayout::selectionRects(down, {}, 0, 2).size(), std::size_t(2));
    // Centred: the text sits around the middle of the curve.
    TextData centred = flat;
    centred.path->offset = 0;
    centred.format.align = TextAlign::Center;
    QVERIFY(std::abs(textlayout::outline(centred, {}).boundingRect().center().x() - 150) < 3);
    // Moving and scaling the text moves its curve.
    Shape shape(flat, QPointF(0, 100), Style{});
    const QRectF before = shape.bounds();
    shape.transform(QTransform::fromTranslate(0, 50));
    QVERIFY(std::abs(shape.bounds().top() - (before.top() + 50)) < 1e-6);
    shape.transform(QTransform::fromScale(2, 2));
    QCOMPARE(shape.textData().path->offset, 20.0);
}

void TextTests::textOnCurveSurvivesSvg()
{
    Document doc;
    SubPath arc;
    arc.nodes = {PathNode::corner({0, 200}), PathNode{{300, 200}, {200, 0}, {300, 200}}};
    arc.nodes[0].out = {100, 0};
    TextData data{QStringLiteral("Round and round"), {}};
    data.path = TextPath{{arc}, 12.5};
    doc.insertShape(-1, std::make_unique<Shape>(data, QPointF(0, 200), Style{QColor(Qt::black), {}, 0}));
    QBuffer buffer;
    QVERIFY(buffer.open(QIODevice::ReadWrite));
    QVERIFY(svg::write(doc, &buffer));
    QVERIFY(buffer.data().contains("<textPath"));
    buffer.seek(0);
    Document copy;
    QVERIFY(svg::read(&buffer, copy));
    QCOMPARE(copy.count(), 1);
    const TextData &read = copy.shapes()[0]->textData();
    QCOMPARE(read.text, data.text);
    QVERIFY(read.path);
    QCOMPARE(read.path->offset, 12.5);
    QCOMPARE(read.path->curve.front().nodes.back().point, QPointF(300, 200));

    // Another program's <textPath>, centred on the middle of its path.
    QByteArray other(R"svg(<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink">
        <defs><path id="p" d="M 0 50 L 200 50"/></defs>
        <text font-size="10" text-anchor="middle"><textPath xlink:href="#p" startOffset="50%">Hello</textPath></text>
        </svg>)svg");
    QBuffer in(&other);
    in.open(QIODevice::ReadOnly);
    Document foreign;
    QVERIFY(svg::read(&in, foreign));
    QCOMPARE(foreign.count(), 1);
    const TextData &hello = foreign.shapes()[0]->textData();
    QCOMPARE(hello.text, QStringLiteral("Hello"));
    QVERIFY(hello.path);
    QVERIFY(std::abs(hello.path->offset) < 1e-6);
    QCOMPARE(hello.format.align, TextAlign::Center);
}

QTEST_MAIN(TextTests)
#include "test_text.moc"
