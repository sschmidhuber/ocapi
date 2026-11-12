#include "backends/geminibackend.h"
#include "core/backendconfig.h"
#include "core/chatsession.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>

using namespace okapi;

namespace {

// Minimal fake Gemini server: records the raw request line and replies with
// a scripted Server-Sent Events stream (or a scripted HTTP error).
class FakeGeminiServer : public QObject {
    Q_OBJECT

public:
    explicit FakeGeminiServer(QObject *parent = nullptr) : QObject(parent) {
        connect(&m_server, &QTcpServer::newConnection, this, [this]() {
            auto *socket = m_server.nextPendingConnection();
            connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
            connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
                m_requests[socket].append(socket->readAll());
                respondIfComplete(socket);
            });
        });
    }

    [[nodiscard]] bool start() {
        return m_server.isListening() || m_server.listen(QHostAddress::LocalHost, 0);
    }

    [[nodiscard]] quint16 port() const { return m_server.serverPort(); }

    void setStreamEvents(const QStringList &events) { m_events = events; }
    void setErrorResponse(int status, const QByteArray &jsonBody) {
        m_errorStatus = status;
        m_errorBody = jsonBody;
    }
    // Respond with a fixed status/content-type/body instead of the SSE stream.
    void setRawResponse(int status, const QByteArray &contentType, const QByteArray &body) {
        m_rawStatus = status;
        m_rawContentType = contentType;
        m_rawBody = body;
    }

    // Request target (path + query) of the most recently completed request.
    [[nodiscard]] QString lastRequestTarget() const { return m_lastTarget; }

private:
    void respondIfComplete(QTcpSocket *socket) {
        QByteArray &buffer = m_requests[socket];
        const int headerEnd = static_cast<int>(buffer.indexOf("\r\n\r\n"));
        if (headerEnd < 0)
            return;
        const int contentLength = [this, &buffer, headerEnd]() {
            const QString headers = QString::fromLatin1(buffer.left(headerEnd)).toLower();
            const int index = static_cast<int>(headers.indexOf(QLatin1String("content-length:")));
            if (index < 0)
                return -1;
            return headers.mid(index + 16).split(QLatin1String("\r\n")).first().toInt();
        }();
        if (contentLength < 0 || buffer.size() < headerEnd + 4 + contentLength)
            return;

        const QString requestLine = QString::fromLatin1(buffer.left(buffer.indexOf("\r\n")));
        m_lastTarget = requestLine.split(QLatin1Char(' ')).value(1);
        // Consume the parsed request so a keep-alive connection does not
        // re-report the previous request (see tst_openaiapibackend).
        buffer.remove(0, headerEnd + 4 + contentLength);

        QByteArray body;
        QByteArray statusLine;
        QByteArray contentType;
        if (m_rawStatus != 0) {
            statusLine = "HTTP/1.1 " + QByteArray::number(m_rawStatus) + " Raw\r\n";
            contentType = m_rawContentType;
            body = m_rawBody;
        } else if (m_errorStatus >= 400) {
            statusLine = "HTTP/1.1 " + QByteArray::number(m_errorStatus) + " Error\r\n";
            contentType = "application/json";
            body = m_errorBody;
        } else {
            statusLine = "HTTP/1.1 200 OK\r\n";
            contentType = "text/event-stream";
            for (const QString &event : m_events)
                body += "data: " + event.toUtf8() + "\n\n";
        }
        QByteArray response;
        response += statusLine;
        response += "Content-Type: " + contentType + "\r\n";
        response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
        response += "\r\n";
        response += body;
        socket->write(response);
        socket->disconnectFromHost();
    }

    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_requests;
    QStringList m_events;
    int m_errorStatus = 0;
    QByteArray m_errorBody;
    int m_rawStatus = 0;
    QByteArray m_rawContentType;
    QByteArray m_rawBody;
    QString m_lastTarget;
};

BackendConfig geminiConfig(quint16 port) {
    BackendConfig config;
    config.name = QStringLiteral("fake-gemini");
    config.kind = BackendKind::Gemini;
    config.baseUrl = QStringLiteral("http://127.0.0.1:%1/v1beta").arg(port);
    config.apiKey = QStringLiteral("secret-key");
    config.enabled = true;
    return config;
}

QString geminiEvent(const QString &text, const QString &finishReason = {}) {
    QJsonObject candidate{
        {QStringLiteral("content"),
         QJsonObject{{QStringLiteral("role"), QStringLiteral("model")},
                     {QStringLiteral("parts"), QJsonArray{QJsonObject{
                                                   {QStringLiteral("text"), text},
                                               }}}}},
    };
    if (!finishReason.isEmpty())
        candidate.insert(QStringLiteral("finishReason"), finishReason);
    return QString::fromUtf8(
        QJsonDocument(QJsonObject{{QStringLiteral("candidates"), QJsonArray{candidate}}})
            .toJson(QJsonDocument::Compact));
}

} // namespace

class TestGeminiBackend : public QObject {
    Q_OBJECT

private slots:
    void sendMessageRequestsSseAndStreamsText();
    void httpErrorIsReportedAsFailure();
    void nonSseResponseStillFinishes();

private:
    FakeGeminiServer m_server;
};

// Regression test: the streaming request must carry alt=sse (otherwise the
// API answers with one non-streamed JSON body and the chat silently ends
// empty) AND the API key. QUrl::setQuery replaces the whole query, so adding
// the key after building "...?alt=sse" used to drop alt=sse.
void TestGeminiBackend::sendMessageRequestsSseAndStreamsText() {
    QVERIFY(m_server.start());
    m_server.setStreamEvents({geminiEvent(QStringLiteral("Hello ")),
                              geminiEvent(QStringLiteral("world."), QStringLiteral("STOP"))});

    GeminiBackend backend(geminiConfig(m_server.port()));
    ChatSession session;
    session.appendMessage(Role::User, QStringLiteral("hi"));
    QSignalSpy tokenSpy(&backend, &IBackend::tokenReceived);
    QSignalSpy finishedSpy(&backend, &IBackend::finished);
    backend.sendMessage(&session, QStringLiteral("gemini-test"));

    QVERIFY(tokenSpy.wait(10000));
    QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() >= 1, 10000);
    QCOMPARE(tokenSpy.last().at(1).toString(), QStringLiteral("Hello world."));

    const QString target = m_server.lastRequestTarget();
    QVERIFY2(target.contains(QStringLiteral("/models/gemini-test:streamGenerateContent")),
             qPrintable(target));
    QVERIFY2(target.contains(QStringLiteral("alt=sse")), qPrintable(target));
    QVERIFY2(target.contains(QStringLiteral("key=secret-key")), qPrintable(target));
}

void TestGeminiBackend::httpErrorIsReportedAsFailure() {
    QVERIFY(m_server.start());
    m_server.setErrorResponse(400, R"({"error":{"message":"API key not valid"}})");

    GeminiBackend backend(geminiConfig(m_server.port()));
    ChatSession session;
    session.appendMessage(Role::User, QStringLiteral("hi"));
    QSignalSpy failedSpy(&backend, &IBackend::failed);
    QSignalSpy finishedSpy(&backend, &IBackend::finished);
    backend.sendMessage(&session, QStringLiteral("gemini-test"));

    QVERIFY(failedSpy.wait(10000));
    QCOMPARE(failedSpy.last().at(1).toString(), QStringLiteral("API key not valid"));
    QCOMPARE(failedSpy.last().at(2).toInt(), 400);
    // A failed request must not also report finished(): the UI's finish
    // handler would clear the error again.
    QTest::qWait(100);
    QCOMPARE(finishedSpy.count(), 0);
}

// A 200 response that is not SSE (e.g. when alt=sse was dropped from the
// request URL) must not hang the turn forever: the stream ends and no tokens
// are emitted.
void TestGeminiBackend::nonSseResponseStillFinishes() {
    QVERIFY(m_server.start());
    m_server.setRawResponse(200, "application/json", R"({"candidates":[]})");

    GeminiBackend backend(geminiConfig(m_server.port()));
    ChatSession session;
    session.appendMessage(Role::User, QStringLiteral("hi"));
    QSignalSpy tokenSpy(&backend, &IBackend::tokenReceived);
    QSignalSpy finishedSpy(&backend, &IBackend::finished);
    backend.sendMessage(&session, QStringLiteral("gemini-test"));

    QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() >= 1, 10000);
    QCOMPARE(tokenSpy.count(), 0);
}

QTEST_MAIN(TestGeminiBackend)
#include "tst_geminibackend.moc"
