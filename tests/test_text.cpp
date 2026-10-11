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

QTEST_MAIN(TextTests)
#include "test_text.moc"
