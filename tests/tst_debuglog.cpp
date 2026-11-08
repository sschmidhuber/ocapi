
#include "core/debuglog.h"

#include <QtTest>

using namespace ocapi;

class TestDebugLog : public QObject {
    Q_OBJECT

private slots:
    void redactsAuthorizationHeader();
    void redactsApiKeyHeaders();
    void redactsJsonApiKeyFields();
    void redactsUrlKeyParameter();
    void redactLeavesOrdinaryTextAlone();
    void clipKeepsShortText();
    void clipMarksTruncationWithTotalLength();
    void redactWorksOnByteArrays();
};

void TestDebugLog::redactsAuthorizationHeader() {
    const QString out = debugSanitize::redact(QStringLiteral("Authorization: Bearer sk-secret123"));
    QVERIFY(!out.contains(QStringLiteral("sk-secret123")));
    QVERIFY(out.contains(QStringLiteral("Authorization: Bearer <redacted>")));
}

void TestDebugLog::redactsApiKeyHeaders() {
    const QString out = debugSanitize::redact(QStringLiteral("x-api-key: abcdef123456\n"
                                                             "xi-api-key: xyz987"));
    QVERIFY(!out.contains(QStringLiteral("abcdef123456")));
    QVERIFY(!out.contains(QStringLiteral("xyz987")));
    QVERIFY(out.contains(QStringLiteral("x-api-key: <redacted>")));
    QVERIFY(out.contains(QStringLiteral("xi-api-key: <redacted>")));
}

void TestDebugLog::redactsJsonApiKeyFields() {
    const QString out = debugSanitize::redact(
        QStringLiteral(R"({"api_key": "sk-abc", "apiKey": "sk-def", "model": "m"})"));
    QVERIFY(!out.contains(QStringLiteral("sk-abc")));
    QVERIFY(!out.contains(QStringLiteral("sk-def")));
    QVERIFY(out.contains(QStringLiteral("\"api_key\": \"<redacted>\"")));
    QVERIFY(out.contains(QStringLiteral("\"apiKey\": \"<redacted>\"")));
    QVERIFY(out.contains(QStringLiteral("\"model\": \"m\"")));
}

void TestDebugLog::redactsUrlKeyParameter() {
    const QString out = debugSanitize::redact(QStringLiteral(
        "https://generativelanguage.googleapis.com/v1/models?key=AIzaSecret&alt=json"));
    QVERIFY(!out.contains(QStringLiteral("AIzaSecret")));
    QVERIFY(out.contains(QStringLiteral("key=<redacted>")));
    QVERIFY(out.contains(QStringLiteral("alt=json")));
}

void TestDebugLog::redactLeavesOrdinaryTextAlone() {
    const QString text = QStringLiteral("POST /v1/chat/completions with 3 messages");
    QCOMPARE(debugSanitize::redact(text), text);
}

void TestDebugLog::clipKeepsShortText() {
    const QString text = QStringLiteral("short log line");
    QCOMPARE(debugSanitize::clip(text, 1000), text);
}

void TestDebugLog::clipMarksTruncationWithTotalLength() {
    const QString text = QString(500, QChar('x'));
    const QString out = debugSanitize::clip(text, 100);
    QCOMPARE(out.size(), 100 + QStringLiteral("…[truncated, 500 chars total]").size());
    QVERIFY(out.startsWith(QString(100, QChar('x'))));
    QVERIFY(out.contains(QStringLiteral("truncated, 500 chars total")));
}

void TestDebugLog::redactWorksOnByteArrays() {
    const QByteArray out = debugSanitize::redact(QByteArray("Authorization: Bearer tok123"));
    QVERIFY(!out.contains("tok123"));
    QVERIFY(out.contains("<redacted>"));
    QCOMPARE(debugSanitize::clip(QByteArray(50, 'a'), 1000), QByteArray(50, 'a'));
}

QTEST_GUILESS_MAIN(TestDebugLog)
#include "tst_debuglog.moc"
