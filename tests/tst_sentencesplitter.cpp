#include "core/sentencesplitter.h"

#include <QtTest>

using namespace ocapi;

class TestSentenceSplitter : public QObject {
    Q_OBJECT

private slots:
    void singleSentenceEmittedAtOnce();
    void partialSentenceStaysBuffered();
    void incrementalFeeding();
    void newlineEndsSentence();
    void abbreviationsDoNotSplit();
    void decimalsAndFileNamesDoNotSplit();
    void questionAndExclamationMarks();
    void flushReturnsRemainder();
    void codeSpansAreStripped();
    void codeBlocksAreStrippedAcrossFeeds();
    void markdownIsStripped();
    void linksKeepTextDropUrl();
    void htmlTagsAreStripped();
    void stripMarkupStandalone();
    void emptyInputEmitsNothing();
    void resetClearsBuffer();
};

void TestSentenceSplitter::singleSentenceEmittedAtOnce() {
    SentenceSplitter splitter;
    // A terminator at the end of the buffer is only emitted while flushing
    // (it could still grow into an abbreviation or ellipsis).
    QVERIFY(splitter.feed(QStringLiteral("Hello world.")).isEmpty());
    QCOMPARE(splitter.flush(), QStringLiteral("Hello world."));
    QVERIFY(splitter.flush().isEmpty());
}

void TestSentenceSplitter::partialSentenceStaysBuffered() {
    SentenceSplitter splitter;
    QVERIFY(splitter.feed(QStringLiteral("Hello wor")).isEmpty());
    QCOMPARE(splitter.flush(), QStringLiteral("Hello wor"));
}

void TestSentenceSplitter::incrementalFeeding() {
    SentenceSplitter splitter;
    QCOMPARE(splitter.feed(QStringLiteral("First sentence. Sec")),
             QStringList{QStringLiteral("First sentence.")});
    QCOMPARE(splitter.feed(QStringLiteral("ond sentence. ")),
             QStringList{QStringLiteral("Second sentence.")});
    QVERIFY(splitter.feed(QStringLiteral("Third sent")).isEmpty());
    QCOMPARE(splitter.flush(), QStringLiteral("Third sent"));
}

void TestSentenceSplitter::newlineEndsSentence() {
    SentenceSplitter splitter;
    QCOMPARE(splitter.feed(QStringLiteral("Line one\nLine two")),
             QStringList{QStringLiteral("Line one")});
    QCOMPARE(splitter.flush(), QStringLiteral("Line two"));
}

void TestSentenceSplitter::abbreviationsDoNotSplit() {
    SentenceSplitter splitter;
    QCOMPARE(splitter.feed(QStringLiteral("Dr. Smith went home. Next.")),
             QStringList{QStringLiteral("Dr. Smith went home.")});
    QCOMPARE(splitter.flush(), QStringLiteral("Next."));
    SentenceSplitter splitter2;
    QCOMPARE(splitter2.feed(QStringLiteral("He likes etc. a lot. Done.")),
             QStringList{QStringLiteral("He likes etc. a lot.")});
    QCOMPARE(splitter2.flush(), QStringLiteral("Done."));
}

void TestSentenceSplitter::decimalsAndFileNamesDoNotSplit() {
    SentenceSplitter splitter;
    QCOMPARE(splitter.feed(QStringLiteral("It costs 3.5 euros. Yes.")),
             QStringList{QStringLiteral("It costs 3.5 euros.")});
    QCOMPARE(splitter.flush(), QStringLiteral("Yes."));
    SentenceSplitter splitter2;
    QCOMPARE(splitter2.feed(QStringLiteral("Open file.txt now. OK.")),
             QStringList{QStringLiteral("Open file.txt now.")});
    QCOMPARE(splitter2.flush(), QStringLiteral("OK."));
}

void TestSentenceSplitter::questionAndExclamationMarks() {
    SentenceSplitter splitter;
    QCOMPARE(splitter.feed(QStringLiteral("Really? Yes! No.")),
             (QStringList{QStringLiteral("Really?"), QStringLiteral("Yes!")}));
    QCOMPARE(splitter.flush(), QStringLiteral("No."));
}

void TestSentenceSplitter::flushReturnsRemainder() {
    SentenceSplitter splitter;
    QCOMPARE(splitter.feed(QStringLiteral("Done. Partial")), QStringList{QStringLiteral("Done.")});
    QCOMPARE(splitter.flush(), QStringLiteral("Partial"));
    QVERIFY(splitter.flush().isEmpty());
}

void TestSentenceSplitter::codeSpansAreStripped() {
    SentenceSplitter splitter;
    const auto sentences = splitter.feed(QStringLiteral("Use `rm -rf` carefully. Done."));
    QCOMPARE(sentences, (QStringList{QStringLiteral("Use  carefully.")}));
    QCOMPARE(splitter.flush(), QStringLiteral("Done."));
}

void TestSentenceSplitter::codeBlocksAreStrippedAcrossFeeds() {
    SentenceSplitter splitter;
    QVERIFY(splitter.feed(QStringLiteral("Before.\n```\nsecret code\n"))
                .contains(QStringLiteral("Before.")));
    QVERIFY(splitter.feed(QStringLiteral("more code\n")).isEmpty());
    QVERIFY(splitter.feed(QStringLiteral("```\nAfter.")).isEmpty());
    QCOMPARE(splitter.flush(), QStringLiteral("After."));
}

void TestSentenceSplitter::markdownIsStripped() {
    SentenceSplitter splitter;
    const auto sentences = splitter.feed(QStringLiteral("## Title\n**Bold** and _italic_."));
    QCOMPARE(sentences, QStringList{QStringLiteral("Title")});
    QCOMPARE(splitter.flush(), QStringLiteral("Bold and italic."));
}

void TestSentenceSplitter::linksKeepTextDropUrl() {
    SentenceSplitter splitter;
    QVERIFY(splitter.feed(QStringLiteral("See [the docs](https://example.com) now.")).isEmpty());
    QCOMPARE(splitter.flush(), QStringLiteral("See the docs now."));
}

void TestSentenceSplitter::htmlTagsAreStripped() {
    SentenceSplitter splitter;
    QCOMPARE(splitter.feed(QStringLiteral("<p>Hello<br>world.</p>")),
             QStringList{QStringLiteral("Hello")});
    QCOMPARE(splitter.flush(), QStringLiteral("world."));
}

void TestSentenceSplitter::stripMarkupStandalone() {
    QCOMPARE(SentenceSplitter::stripMarkup(QStringLiteral("**bold** text")),
             QStringLiteral("bold text"));
    QCOMPARE(SentenceSplitter::stripMarkup(QStringLiteral("a `code` b")), QStringLiteral("a  b"));
    QCOMPARE(SentenceSplitter::stripMarkup(QStringLiteral("> quoted")), QStringLiteral(" quoted"));
    QCOMPARE(SentenceSplitter::stripMarkup(QStringLiteral("[x](http://y)")), QStringLiteral("x"));
    QVERIFY(SentenceSplitter::stripMarkup(QStringLiteral("```\ncode\n```")).isEmpty());
}

void TestSentenceSplitter::emptyInputEmitsNothing() {
    SentenceSplitter splitter;
    QVERIFY(splitter.feed(QString()).isEmpty());
    QVERIFY(splitter.feed(QStringLiteral("   ")).isEmpty());
}

void TestSentenceSplitter::resetClearsBuffer() {
    SentenceSplitter splitter;
    splitter.feed(QStringLiteral("Partial sentence"));
    splitter.reset();
    QVERIFY(splitter.flush().isEmpty());
    QCOMPARE(splitter.feed(QStringLiteral("New. Rest")), QStringList{QStringLiteral("New.")});
    QCOMPARE(splitter.flush(), QStringLiteral("Rest"));
}

QTEST_GUILESS_MAIN(TestSentenceSplitter)
#include "tst_sentencesplitter.moc"
