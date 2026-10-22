#include "core/markdown.h"

#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextTable>
#include <QtTest>

#include <functional>

using namespace ocapi;

namespace {

QFont testFont() {
    QFont font(QStringLiteral("DejaVu Sans"));
    font.setPointSize(10);
    return font;
}

// Returns the plain text of the rendered document with block boundaries as
// newlines.
QString renderedText(const QString &markdown) {
    const auto doc = MarkdownRenderer::render(markdown, 400, testFont());
    return doc->toPlainText();
}

bool hasFormat(const QTextDocument &doc, const std::function<bool(const QTextCharFormat &)> &pred) {
    for (QTextBlock block = doc.begin(); block != doc.end(); block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            if (pred(it.fragment().charFormat()))
                return true;
        }
    }
    return false;
}

} // namespace

class TestMarkdown : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void plainTextPassesThrough();
    void boldIsRendered();
    void italicIsRendered();
    void strikethroughIsRendered();
    void inlineCodeIsMonospaceAndLiteral();
    void unbalancedBacktickStaysLiteral();
    void linksAreAnchored();
    void headingsAreBoldAndLarger();
    void headingWithoutSpaceIsNotAHeading();
    void fencedCodeBlockIsMonospace();
    void unterminatedFenceRendersContent();
    void unorderedListItemsGetBullets();
    void orderedListItemsKeepNumbers();
    void blockquoteIsItalic();
    void horizontalRuleRendersLine();
    void paragraphsJoinConsecutiveLines();
    void emptyInputRendersEmpty();
    void cacheReturnsSameDocument();
    void tableRendersAsTable();
    void tableKeepsCellText();
    void tableSupportsAlignmentMarkers();
    void pipeTextWithoutDelimiterIsNotATable();
    void clearCacheForcesReRender();
};

void TestMarkdown::initTestCase() {
    MarkdownRenderer::clearCache();
}

void TestMarkdown::plainTextPassesThrough() {
    QCOMPARE(renderedText(QStringLiteral("Hello world")), QStringLiteral("Hello world"));
}

void TestMarkdown::boldIsRendered() {
    const auto doc = MarkdownRenderer::render(QStringLiteral("a **bold** b"), 400, testFont());
    QCOMPARE(doc->toPlainText(), QStringLiteral("a bold b"));
    QVERIFY(
        hasFormat(*doc, [](const QTextCharFormat &f) { return f.fontWeight() == QFont::Bold; }));
}

void TestMarkdown::italicIsRendered() {
    const auto doc = MarkdownRenderer::render(QStringLiteral("a *italic* b"), 400, testFont());
    QCOMPARE(doc->toPlainText(), QStringLiteral("a italic b"));
    QVERIFY(hasFormat(*doc, [](const QTextCharFormat &f) { return f.fontItalic(); }));
}

void TestMarkdown::strikethroughIsRendered() {
    const auto doc = MarkdownRenderer::render(QStringLiteral("a ~~gone~~ b"), 400, testFont());
    QCOMPARE(doc->toPlainText(), QStringLiteral("a gone b"));
    QVERIFY(hasFormat(*doc, [](const QTextCharFormat &f) { return f.fontStrikeOut(); }));
}

void TestMarkdown::inlineCodeIsMonospaceAndLiteral() {
    const auto doc =
        MarkdownRenderer::render(QStringLiteral("use `**not bold**` here"), 400, testFont());
    QCOMPARE(doc->toPlainText(), QStringLiteral("use **not bold** here"));
    QVERIFY(hasFormat(*doc, [](const QTextCharFormat &f) {
        if (!f.hasProperty(QTextFormat::FontFamilies))
            return false;
        const QStringList families = f.property(QTextFormat::FontFamilies).toStringList();
        return !families.isEmpty() && families.first() == QLatin1String("monospace") &&
               f.fontWeight() != QFont::Bold;
    }));
}

void TestMarkdown::unbalancedBacktickStaysLiteral() {
    QCOMPARE(renderedText(QStringLiteral("a ` b")), QStringLiteral("a ` b"));
}

void TestMarkdown::linksAreAnchored() {
    const auto doc =
        MarkdownRenderer::render(QStringLiteral("see [Qt](https://qt.io) now"), 400, testFont());
    QCOMPARE(doc->toPlainText(), QStringLiteral("see Qt now"));
    QVERIFY(hasFormat(*doc, [](const QTextCharFormat &f) {
        return f.isAnchor() && f.anchorHref() == QLatin1String("https://qt.io");
    }));
}

void TestMarkdown::headingsAreBoldAndLarger() {
    const auto doc = MarkdownRenderer::render(QStringLiteral("# Title"), 400, testFont());
    QCOMPARE(doc->toPlainText(), QStringLiteral("Title"));
    QVERIFY(hasFormat(*doc, [](const QTextCharFormat &f) {
        return f.fontWeight() == QFont::Bold && f.fontPointSize() > 10.0;
    }));
}

void TestMarkdown::headingWithoutSpaceIsNotAHeading() {
    QCOMPARE(renderedText(QStringLiteral("#hashtag")), QStringLiteral("#hashtag"));
}

void TestMarkdown::fencedCodeBlockIsMonospace() {
    const QString md = QStringLiteral("before\n```cpp\nint x = 1;\n```\nafter");
    const auto doc = MarkdownRenderer::render(md, 400, testFont());
    QVERIFY(doc->toPlainText().contains(QStringLiteral("int x = 1;")));
    QVERIFY(!doc->toPlainText().contains(QStringLiteral("```")));
    QVERIFY(hasFormat(*doc, [](const QTextCharFormat &f) {
        if (!f.hasProperty(QTextFormat::FontFamilies))
            return false;
        const QStringList families = f.property(QTextFormat::FontFamilies).toStringList();
        return !families.isEmpty() && families.first() == QLatin1String("monospace");
    }));
}

void TestMarkdown::unterminatedFenceRendersContent() {
    const QString md = QStringLiteral("```\ncode line");
    const auto doc = MarkdownRenderer::render(md, 400, testFont());
    QVERIFY(doc->toPlainText().contains(QStringLiteral("code line")));
}

void TestMarkdown::unorderedListItemsGetBullets() {
    const QString text = renderedText(QStringLiteral("- one\n- two"));
    QVERIFY(text.contains(QStringLiteral("\u2022 one")));
    QVERIFY(text.contains(QStringLiteral("\u2022 two")));
}

void TestMarkdown::orderedListItemsKeepNumbers() {
    const QString text = renderedText(QStringLiteral("1. first\n2. second"));
    QVERIFY(text.contains(QStringLiteral("1. first")));
    QVERIFY(text.contains(QStringLiteral("2. second")));
}

void TestMarkdown::blockquoteIsItalic() {
    const auto doc = MarkdownRenderer::render(QStringLiteral("> wise words"), 400, testFont());
    QCOMPARE(doc->toPlainText(), QStringLiteral("wise words"));
    QVERIFY(hasFormat(*doc, [](const QTextCharFormat &f) { return f.fontItalic(); }));
}

void TestMarkdown::horizontalRuleRendersLine() {
    const auto doc = MarkdownRenderer::render(QStringLiteral("above\n---\nbelow"), 400, testFont());
    const QString text = doc->toPlainText();
    QVERIFY(text.contains(QChar(0x2500)));
    QVERIFY(text.contains(QStringLiteral("above")));
    QVERIFY(text.contains(QStringLiteral("below")));
}

void TestMarkdown::paragraphsJoinConsecutiveLines() {
    QCOMPARE(renderedText(QStringLiteral("line one\nline two")),
             QStringLiteral("line one line two"));
}

void TestMarkdown::emptyInputRendersEmpty() {
    const auto doc = MarkdownRenderer::render(QString(), 400, testFont());
    QVERIFY(doc->isEmpty());
}

void TestMarkdown::cacheReturnsSameDocument() {
    MarkdownRenderer::clearCache();
    const QString md = QStringLiteral("cached **content**");
    const auto first = MarkdownRenderer::render(md, 400, testFont());
    const auto second = MarkdownRenderer::render(md, 400, testFont());
    QCOMPARE(first->toPlainText(), second->toPlainText());
    QCOMPARE(first->toPlainText(), QStringLiteral("cached content"));
}

void TestMarkdown::clearCacheForcesReRender() {
    MarkdownRenderer::clearCache();
    const QString md = QStringLiteral("some text");
    const auto first = MarkdownRenderer::render(md, 400, testFont());
    MarkdownRenderer::clearCache();
    const auto second = MarkdownRenderer::render(md, 400, testFont());
    QCOMPARE(first->toPlainText(), second->toPlainText());
}

void TestMarkdown::tableRendersAsTable() {
    const QString md = QStringLiteral("| a | b |\n| --- | --- |\n| 1 | 2 |");
    const auto doc = MarkdownRenderer::render(md, 400, testFont());
    QTextCursor cursor(doc.get());
    QVERIFY(cursor.currentTable() == nullptr); // not a table at the very start
    cursor.movePosition(QTextCursor::NextBlock);
    QVERIFY(cursor.currentTable() != nullptr);
    QCOMPARE(cursor.currentTable()->rows(), 2);
    QCOMPARE(cursor.currentTable()->columns(), 2);
}

void TestMarkdown::tableKeepsCellText() {
    const QString md = QStringLiteral("| Name | Size |\n| --- | --- |\n| ocapi | small |");
    const auto doc = MarkdownRenderer::render(md, 400, testFont());
    const QString text = doc->toPlainText();
    QVERIFY(text.contains(QStringLiteral("Name")));
    QVERIFY(text.contains(QStringLiteral("Size")));
    QVERIFY(text.contains(QStringLiteral("ocapi")));
    QVERIFY(text.contains(QStringLiteral("small")));
}

void TestMarkdown::tableSupportsAlignmentMarkers() {
    const QString md = QStringLiteral("| left | right |\n| :--- | ---: |\n| x | y |");
    const auto doc = MarkdownRenderer::render(md, 400, testFont());
    QVERIFY(doc->toPlainText().contains(QStringLiteral("left")));
    QVERIFY(doc->toPlainText().contains(QStringLiteral("right")));
}

void TestMarkdown::pipeTextWithoutDelimiterIsNotATable() {
    const QString md = QStringLiteral("a | b\n1 | 2");
    const auto doc = MarkdownRenderer::render(md, 400, testFont());
    QCOMPARE(doc->toPlainText(), QStringLiteral("a | b 1 | 2"));
}

QTEST_GUILESS_MAIN(TestMarkdown)
#include "tst_markdown.moc"
