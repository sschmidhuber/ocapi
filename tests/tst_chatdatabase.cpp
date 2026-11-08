#include "storage/chatdatabase.h"

#include "core/chatsession.h"
#include <QDateTime>
#include <QTemporaryDir>
#include <QtTest>

using namespace ocapi;

class TestChatDatabase : public QObject {
    Q_OBJECT

private slots:
    void init();
    void openCreatesTables();
    void settingsRoundTrip();
    void settingsOverwriteExistingKey();
    void settingsMissingKeyReturnsDefault();
    void insertAndListSessions();
    void sessionMetadataRoundTrip();
    void loadSessionsReturnsTimestampsOrderedByActivity();
    void lastActivityColumnDefaultsToCreatedAt();
    void insertAndLoadMessages();
    void reasoningRoundTrip();
    void elementOrderRoundTrip();
    void elementOrderDefaultsToEmpty();
    void messageUpdateClearsError();
    void failMessagePersistsError();
    void deleteSessionRemovesMessages();
    void updateSessionChangesMetadata();
    void backendRoundTrip();
    void backendUpdateOverwritesEntry();
    void backendDeleteRemovesEntry();
    void messagePaginationReturnsLastPage();
    void messagePaginationSecondPageIsOlderMessages();
    void messagePaginationBeyondEndIsEmpty();
    void messageCountMatchesStoredMessages();
    void throughputRoundTrip();
    void imageAttachmentsRoundTrip();
    void deleteSessionRemovesImages();
    void fileAttachmentsRoundTrip();
    void deleteSessionRemovesFiles();
    void fileThumbnailSurvivesRoundTrip();
    void attachmentsLoadedOnlyForRequestedMessages();
    void toolCallsRoundTrip();
    void toolCallsSurviveReinsertUpsert();
    void toolCallStatusUpdatePersists();
    void toolCallStatusUpdateFailsForUnknownCall();
    void toolCallsColumnDefaultsToEmpty();
    void openUsesWalAndNormalSync();
    void persistStreamResultAsyncPersistsFinishedStream();
    void persistStreamResultAsyncPersistsFailure();

private:
    ChatDatabase &db() { return m_database; }
    QTemporaryDir m_tempDir;
    ChatDatabase m_database;
    QString m_filePath;
};

void TestChatDatabase::init() {
    m_database.close();
    static int counter = 0;
    m_filePath = m_tempDir.filePath(QStringLiteral("ocapi-%1.sqlite").arg(counter++));
    QVERIFY(m_tempDir.isValid());
    QVERIFY2(db().open(m_filePath), db().lastError().toUtf8().constData());
}

void TestChatDatabase::openCreatesTables() {
    QVERIFY(db().isOpen());
    QVERIFY(QFile::exists(m_filePath));
}

void TestChatDatabase::settingsRoundTrip() {
    QVERIFY(db().setValue(QStringLiteral("ui/theme"), QStringLiteral("breeze")));
    QCOMPARE(db().value(QStringLiteral("ui/theme")), QStringLiteral("breeze"));
}

void TestChatDatabase::settingsOverwriteExistingKey() {
    QVERIFY(db().setValue(QStringLiteral("key"), QStringLiteral("a")));
    QVERIFY(db().setValue(QStringLiteral("key"), QStringLiteral("b")));
    QCOMPARE(db().value(QStringLiteral("key")), QStringLiteral("b"));
}

void TestChatDatabase::settingsMissingKeyReturnsDefault() {
    QCOMPARE(db().value(QStringLiteral("missing"), QStringLiteral("fallback")),
             QStringLiteral("fallback"));
}

void TestChatDatabase::insertAndListSessions() {
    ChatSession first(QStringLiteral("First"), QStringLiteral("backend-1"));
    ChatSession second(QStringLiteral("Second"), QStringLiteral("backend-2"));
    QVERIFY(db().insertSession(first));
    QVERIFY(db().insertSession(second));
    const auto ids = db().sessionIds();
    QCOMPARE(ids.size(), 2);
    QVERIFY(ids.contains(first.id().toString(QUuid::WithoutBraces)));
    QVERIFY(ids.contains(second.id().toString(QUuid::WithoutBraces)));
}

void TestChatDatabase::sessionMetadataRoundTrip() {
    ChatSession session(QStringLiteral("My chat"), QStringLiteral("backend-1"));
    session.setModel(QStringLiteral("llama3"));
    QVERIFY(db().insertSession(session));
    const auto id = session.id().toString(QUuid::WithoutBraces);
    QCOMPARE(db().sessionTitle(id), QStringLiteral("My chat"));
    QCOMPARE(db().sessionBackendId(id), QStringLiteral("backend-1"));
    QCOMPARE(db().sessionModel(id), QStringLiteral("llama3"));
}

void TestChatDatabase::insertAndLoadMessages() {
    ChatSession session(QStringLiteral("Chat"), QStringLiteral("backend-1"));
    const auto sessionId = session.id().toString(QUuid::WithoutBraces);
    QVERIFY(db().insertSession(session));
    const auto userMsg = session.appendMessage(Role::User, QStringLiteral("hello"));
    const auto assistantMsg = session.appendMessage(Role::Assistant, QStringLiteral("hi"));
    QVERIFY(db().insertMessage(sessionId, userMsg.id.toString(QUuid::WithoutBraces),
                               userMsg.roleName(), userMsg.content, userMsg.timestamp));
    QVERIFY(db().insertMessage(sessionId, assistantMsg.id.toString(QUuid::WithoutBraces),
                               assistantMsg.roleName(), assistantMsg.content,
                               assistantMsg.timestamp));
    const auto stored = db().messagesForSession(sessionId);
    QCOMPARE(stored.size(), 2);
    QCOMPARE(stored.at(0).role, QStringLiteral("user"));
    QCOMPARE(stored.at(0).content, QStringLiteral("hello"));
    QCOMPARE(stored.at(1).role, QStringLiteral("assistant"));
    QCOMPARE(stored.at(1).content, QStringLiteral("hi"));
}

void TestChatDatabase::reasoningRoundTrip() {
    ChatSession session(QStringLiteral("Chat"), QStringLiteral("backend-1"));
    const auto sessionId = session.id().toString(QUuid::WithoutBraces);
    QVERIFY(db().insertSession(session));
    const auto assistantMsg = session.appendMessage(Role::Assistant, QStringLiteral("answer"));
    const auto messageId = assistantMsg.id.toString(QUuid::WithoutBraces);
    QVERIFY(db().insertMessage(sessionId, messageId, assistantMsg.roleName(), assistantMsg.content,
                               assistantMsg.timestamp, {}, {}, {},
                               QStringList{QStringLiteral("chain of thought")}));

    // Streaming reasoning updates arrive incrementally; a second thinking
    // phase is stored as its own segment.
    QVERIFY(db().setReasoning(messageId, QStringList({QStringLiteral("chain of thought, refined"),
                                                      QStringLiteral("second phase")})));
    const auto stored = db().messagesForSession(sessionId);
    QCOMPARE(stored.size(), 1);
    QCOMPARE(
        stored.at(0).reasoningSegments,
        QStringList({QStringLiteral("chain of thought, refined"), QStringLiteral("second phase")}));
}

void TestChatDatabase::elementOrderRoundTrip() {
    ChatSession session(QStringLiteral("Chat"), QStringLiteral("backend-1"));
    const auto sessionId = session.id().toString(QUuid::WithoutBraces);
    QVERIFY(db().insertSession(session));
    const auto assistantMsg = session.appendMessage(Role::Assistant, QStringLiteral("answer"));
    const auto messageId = assistantMsg.id.toString(QUuid::WithoutBraces);
    const QStringList order{QStringLiteral("thinking:0"), QStringLiteral("tool:0"),
                            QStringLiteral("thinking:1"), QStringLiteral("tool:1")};
    QVERIFY(db().insertMessage(
        sessionId, messageId, assistantMsg.roleName(), assistantMsg.content, assistantMsg.timestamp,
        {}, {}, {}, QStringList{QStringLiteral("phase one"), QStringLiteral("phase two")}, order));

    // Streaming updates rewrite the order (a new thinking phase is appended).
    QVERIFY(db().setElementOrder(messageId, order));
    const auto stored = db().messagesForSession(sessionId);
    QCOMPARE(stored.size(), 1);
    QCOMPARE(stored.at(0).elementOrder, order);
    QCOMPARE(stored.at(0).reasoningSegments.size(), 2);
}

void TestChatDatabase::elementOrderDefaultsToEmpty() {
    ChatSession session(QStringLiteral("Chat"), QStringLiteral("backend-1"));
    const auto sessionId = session.id().toString(QUuid::WithoutBraces);
    QVERIFY(db().insertSession(session));
    const auto msg = session.appendMessage(Role::Assistant, QStringLiteral("answer"));
    QVERIFY(db().insertMessage(sessionId, msg.id.toString(QUuid::WithoutBraces), msg.roleName(),
                               msg.content, msg.timestamp));
    const auto stored = db().messagesForSession(sessionId);
    QCOMPARE(stored.size(), 1);
    QVERIFY(stored.at(0).elementOrder.isEmpty());
}

void TestChatDatabase::messageUpdateClearsError() {
    ChatSession session(QStringLiteral("Chat"), QStringLiteral("backend-1"));
    const auto sessionId = session.id().toString(QUuid::WithoutBraces);
    QVERIFY(db().insertSession(session));
    const auto message = session.appendMessage(Role::Assistant, QStringLiteral("partial"));
    const auto messageId = message.id.toString(QUuid::WithoutBraces);
    QVERIFY2(db().insertMessage(sessionId, messageId, message.roleName(), message.content,
                                message.timestamp),
             db().lastError().toUtf8().constData());
    QVERIFY(db().failMessage(messageId, QStringLiteral("boom")));
    QVERIFY(db().updateMessage(messageId, QStringLiteral("full text"), false));
    const auto stored = db().messagesForSession(sessionId);
    QCOMPARE(stored.size(), 1);
    QCOMPARE(stored.at(0).content, QStringLiteral("full text"));
    QVERIFY(!stored.at(0).streaming);
    QVERIFY(stored.at(0).error.isEmpty());
}

void TestChatDatabase::failMessagePersistsError() {
    ChatSession session(QStringLiteral("Chat"), QStringLiteral("backend-1"));
    const auto sessionId = session.id().toString(QUuid::WithoutBraces);
    QVERIFY(db().insertSession(session));
    const auto message = session.appendMessage(Role::Assistant, QStringLiteral("..."));
    const auto messageId = message.id.toString(QUuid::WithoutBraces);
    QVERIFY2(db().insertMessage(sessionId, messageId, message.roleName(), message.content,
                                message.timestamp),
             db().lastError().toUtf8().constData());
    QVERIFY(db().failMessage(messageId, QStringLiteral("connection refused")));
    const auto stored = db().messagesForSession(sessionId);
    QCOMPARE(stored.at(0).error, QStringLiteral("connection refused"));
    QVERIFY(!stored.at(0).streaming);
}

void TestChatDatabase::deleteSessionRemovesMessages() {
    ChatSession session(QStringLiteral("Chat"), QStringLiteral("backend-1"));
    const auto sessionId = session.id().toString(QUuid::WithoutBraces);
    const auto message = session.appendMessage(Role::User, QStringLiteral("hi"));
    QVERIFY(db().insertSession(session));
    QVERIFY(db().insertMessage(sessionId, message.id.toString(QUuid::WithoutBraces),
                               message.roleName(), message.content, message.timestamp));
    QVERIFY(db().deleteSession(sessionId));
    QVERIFY(db().sessionIds().isEmpty());
    QVERIFY(db().messagesForSession(sessionId).isEmpty());
}

void TestChatDatabase::updateSessionChangesMetadata() {
    ChatSession session(QStringLiteral("Old title"), QStringLiteral("backend-1"));
    QVERIFY(db().insertSession(session));
    session.setTitle(QStringLiteral("New title"));
    session.setModel(QStringLiteral("mistral"));
    QVERIFY(db().updateSession(session));
    const auto id = session.id().toString(QUuid::WithoutBraces);
    QCOMPARE(db().sessionTitle(id), QStringLiteral("New title"));
    QCOMPARE(db().sessionModel(id), QStringLiteral("mistral"));
}

void TestChatDatabase::backendRoundTrip() {
    BackendConfig config;
    config.name = QStringLiteral("Ollama local");
    config.kind = BackendKind::Ollama;
    config.baseUrl = QStringLiteral("http://192.168.1.10:11434");
    config.apiKey = QStringLiteral("secret");
    config.defaultModel = QStringLiteral("llama3.2");
    config.enabled = false;
    config.contextWindow = 16384;
    db().insertBackend(config);
    QVERIFY(!db().loadBackends().isEmpty());
    const auto loaded = db().loadBackends();
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(loaded.at(0).name, QStringLiteral("Ollama local"));
    QCOMPARE(loaded.at(0).kind, BackendKind::Ollama);
    QCOMPARE(loaded.at(0).baseUrl, QStringLiteral("http://192.168.1.10:11434"));
    QCOMPARE(loaded.at(0).apiKey, QStringLiteral("secret"));
    QCOMPARE(loaded.at(0).defaultModel, QStringLiteral("llama3.2"));
    QVERIFY(!loaded.at(0).enabled);
    QCOMPARE(loaded.at(0).contextWindow, 16384);
}

void TestChatDatabase::backendUpdateOverwritesEntry() {
    BackendConfig config;
    config.name = QStringLiteral("before");
    config.kind = BackendKind::OpenAi;
    db().insertBackend(config);
    config.name = QStringLiteral("after");
    config.kind = BackendKind::Groq;
    config.enabled = false;
    config.contextWindow = 0;
    db().updateBackend(config);
    const auto loaded = db().loadBackends();
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(loaded.at(0).name, QStringLiteral("after"));
    QCOMPARE(loaded.at(0).kind, BackendKind::Groq);
    QVERIFY(!loaded.at(0).enabled);
    QCOMPARE(loaded.at(0).contextWindow, 0);
}

void TestChatDatabase::backendDeleteRemovesEntry() {
    BackendConfig config;
    config.name = QStringLiteral("doomed");
    db().insertBackend(config);
    QVERIFY(!db().loadBackends().isEmpty());
    db().deleteBackend(config.id.toString(QUuid::WithoutBraces));
    QVERIFY(db().loadBackends().isEmpty());
    QVERIFY(db().loadBackends().isEmpty());
}

void TestChatDatabase::messagePaginationReturnsLastPage() {
    ChatSession session(QStringLiteral("Chat"), QStringLiteral("backend-1"));
    const auto sessionId = session.id().toString(QUuid::WithoutBraces);
    QVERIFY(db().insertSession(session));
    for (int i = 0; i < 5; ++i) {
        const auto &message = session.appendMessage(Role::User, QStringLiteral("m%1").arg(i));
        QVERIFY(db().insertMessage(sessionId, message.id.toString(QUuid::WithoutBraces),
                                   message.roleName(), message.content, message.timestamp));
    }
    const auto page = db().messagesForSession(sessionId, 2, 0);
    QCOMPARE(page.size(), 2);
    QCOMPARE(page.at(0).content, QStringLiteral("m3"));
    QCOMPARE(page.at(1).content, QStringLiteral("m4"));
}

void TestChatDatabase::messagePaginationSecondPageIsOlderMessages() {
    ChatSession session(QStringLiteral("Chat"), QStringLiteral("backend-1"));
    const auto sessionId = session.id().toString(QUuid::WithoutBraces);
    QVERIFY(db().insertSession(session));
    for (int i = 0; i < 5; ++i) {
        const auto &message = session.appendMessage(Role::User, QStringLiteral("m%1").arg(i));
        QVERIFY(db().insertMessage(sessionId, message.id.toString(QUuid::WithoutBraces),
                                   message.roleName(), message.content, message.timestamp));
    }
    const auto page = db().messagesForSession(sessionId, 2, 2);
    QCOMPARE(page.size(), 2);
    QCOMPARE(page.at(0).content, QStringLiteral("m1"));
    QCOMPARE(page.at(1).content, QStringLiteral("m2"));
}

void TestChatDatabase::messagePaginationBeyondEndIsEmpty() {
    ChatSession session(QStringLiteral("Chat"), QStringLiteral("backend-1"));
    const auto sessionId = session.id().toString(QUuid::WithoutBraces);
    QVERIFY(db().insertSession(session));
    const auto &message = session.appendMessage(Role::User, QStringLiteral("only"));
    QVERIFY(db().insertMessage(sessionId, message.id.toString(QUuid::WithoutBraces),
                               message.roleName(), message.content, message.timestamp));
    QVERIFY(db().messagesForSession(sessionId, 10, 5).isEmpty());
}

void TestChatDatabase::messageCountMatchesStoredMessages() {
    ChatSession session(QStringLiteral("Chat"), QStringLiteral("backend-1"));
    const auto sessionId = session.id().toString(QUuid::WithoutBraces);
    QVERIFY(db().insertSession(session));
    QCOMPARE(db().messageCount(sessionId), 0);
    for (int i = 0; i < 3; ++i) {
        const auto &message = session.appendMessage(Role::User, QStringLiteral("m%1").arg(i));
        QVERIFY(db().insertMessage(sessionId, message.id.toString(QUuid::WithoutBraces),
                                   message.roleName(), message.content, message.timestamp));
    }
    QCOMPARE(db().messageCount(sessionId), 3);
}

void TestChatDatabase::throughputRoundTrip() {
    ChatSession session(QStringLiteral("Chat"), QStringLiteral("backend-1"));
    const auto sessionId = session.id().toString(QUuid::WithoutBraces);
    QVERIFY(db().insertSession(session));
    const auto &message = session.appendMessage(Role::Assistant, QStringLiteral("hi"));
    const auto messageId = message.id.toString(QUuid::WithoutBraces);
    QVERIFY(db().insertMessage(sessionId, messageId, message.roleName(), message.content,
                               message.timestamp));
    QVERIFY(db().setThroughput(messageId, 37.4));
    const auto stored = db().messagesForSession(sessionId);
    QCOMPARE(stored.size(), 1);
    QCOMPARE(stored.at(0).tokensPerSecond, 37.4);
}

void TestChatDatabase::imageAttachmentsRoundTrip() {
    ChatSession session(QStringLiteral("Images"), QStringLiteral("backend-1"));
    const auto sessionId = session.id().toString(QUuid::WithoutBraces);
    QVERIFY(db().insertSession(session));

    Message message;
    message.role = Role::User;
    message.content = QStringLiteral("look at this");
    ImageAttachment image;
    image.mimeType = QStringLiteral("image/png");
    image.fileName = QStringLiteral("cat.png");
    image.width = 32;
    image.height = 16;
    image.resized = true;
    image.data = QByteArrayLiteral("\x89PNG\r\n\x1a\nfakebytes");
    message.images.push_back(image);
    session.appendMessage(message);

    QVERIFY(db().insertMessage(sessionId, message.id.toString(QUuid::WithoutBraces),
                               message.roleName(), message.content, message.timestamp,
                               message.images));

    const auto stored = db().messagesForSession(sessionId);
    QCOMPARE(stored.size(), 1);
    QCOMPARE(stored.at(0).images.size(), 1);
    const auto &storedImage = stored.at(0).images.at(0);
    QCOMPARE(storedImage.mimeType, QStringLiteral("image/png"));
    QCOMPARE(storedImage.fileName, QStringLiteral("cat.png"));
    QCOMPARE(storedImage.width, 32);
    QCOMPARE(storedImage.height, 16);
    QVERIFY(storedImage.resized);
    QCOMPARE(storedImage.data, QByteArrayLiteral("\x89PNG\r\n\x1a\nfakebytes"));
}

void TestChatDatabase::deleteSessionRemovesImages() {
    ChatSession session(QStringLiteral("Images"), QStringLiteral("backend-1"));
    const auto sessionId = session.id().toString(QUuid::WithoutBraces);
    QVERIFY(db().insertSession(session));
    Message message;
    message.role = Role::User;
    ImageAttachment image;
    image.mimeType = QStringLiteral("image/png");
    image.data = QByteArrayLiteral("fake");
    message.images.push_back(image);
    session.appendMessage(message);
    QVERIFY(db().insertMessage(sessionId, message.id.toString(QUuid::WithoutBraces),
                               message.roleName(), message.content, message.timestamp,
                               message.images));
    QVERIFY(db().deleteSession(sessionId));
    QCOMPARE(db().messageCount(sessionId), 0);
    QCOMPARE(db().messagesForSession(sessionId).size(), 0);
}

void TestChatDatabase::fileAttachmentsRoundTrip() {
    ChatSession session(QStringLiteral("Files"), QStringLiteral("backend-1"));
    const auto sessionId = session.id().toString(QUuid::WithoutBraces);
    QVERIFY(db().insertSession(session));

    Message message;
    message.role = Role::User;
    message.content = QStringLiteral("see attached");
    FileAttachment file;
    file.fileName = QStringLiteral("report.pdf");
    file.mimeType = QStringLiteral("application/pdf");
    file.sourcePath = QStringLiteral("/tmp/report.pdf");
    file.thumbnail = QByteArrayLiteral("\x89PNG\r\n\x1a\nthumbbytes");
    file.extractedText = QStringLiteral("extracted text");
    message.files.push_back(file);
    session.appendMessage(message);

    QVERIFY(db().insertMessage(sessionId, message.id.toString(QUuid::WithoutBraces),
                               message.roleName(), message.content, message.timestamp,
                               message.images, message.files));

    const auto stored = db().messagesForSession(sessionId);
    QCOMPARE(stored.size(), 1);
    QCOMPARE(stored.at(0).files.size(), 1);
    const auto &storedFile = stored.at(0).files.at(0);
    QCOMPARE(storedFile.id, file.id);
    QCOMPARE(storedFile.fileName, QStringLiteral("report.pdf"));
    QCOMPARE(storedFile.mimeType, QStringLiteral("application/pdf"));
    QCOMPARE(storedFile.sourcePath, QStringLiteral("/tmp/report.pdf"));
    QCOMPARE(storedFile.thumbnail, QByteArrayLiteral("\x89PNG\r\n\x1a\nthumbbytes"));
    // The extracted text travels with the file row; the message content
    // keeps only what the user typed.
    QCOMPARE(storedFile.extractedText, QStringLiteral("extracted text"));
    QCOMPARE(stored.at(0).content, QStringLiteral("see attached"));
}

void TestChatDatabase::deleteSessionRemovesFiles() {
    ChatSession session(QStringLiteral("Files"), QStringLiteral("backend-1"));
    const auto sessionId = session.id().toString(QUuid::WithoutBraces);
    QVERIFY(db().insertSession(session));
    Message message;
    message.role = Role::User;
    FileAttachment file;
    file.fileName = QStringLiteral("notes.txt");
    message.files.push_back(file);
    session.appendMessage(message);
    QVERIFY(db().insertMessage(sessionId, message.id.toString(QUuid::WithoutBraces),
                               message.roleName(), message.content, message.timestamp,
                               message.images, message.files));
    QVERIFY(db().deleteSession(sessionId));
    QCOMPARE(db().messageCount(sessionId), 0);
    QCOMPARE(db().messagesForSession(sessionId).size(), 0);
}

void TestChatDatabase::fileThumbnailSurvivesRoundTrip() {
    ChatSession session(QStringLiteral("Thumbs"), QStringLiteral("backend-1"));
    const auto sessionId = session.id().toString(QUuid::WithoutBraces);
    QVERIFY(db().insertSession(session));

    // A real PNG thumbnail blob (1x1 transparent pixel).
    QImage image(1, 1, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QByteArray png;
    QBuffer buffer(&png);
    QVERIFY(buffer.open(QIODevice::WriteOnly));
    QVERIFY(image.save(&buffer, "PNG"));

    Message message;
    message.role = Role::User;
    FileAttachment file;
    file.fileName = QStringLiteral("doc.pdf");
    file.thumbnail = png;
    message.files.push_back(file);
    session.appendMessage(message);

    QVERIFY(db().insertMessage(sessionId, message.id.toString(QUuid::WithoutBraces),
                               message.roleName(), message.content, message.timestamp,
                               message.images, message.files));

    const auto stored = db().messagesForSession(sessionId);
    QCOMPARE(stored.size(), 1);
    QCOMPARE(stored.at(0).files.size(), 1);
    QCOMPARE(stored.at(0).files.at(0).thumbnail, png);
    QVERIFY(!stored.at(0).files.at(0).decodeThumbnail().isNull());
}

void TestChatDatabase::toolCallsRoundTrip() {
    ChatSession session(QStringLiteral("Tools"), QStringLiteral("backend-1"));
    const auto sessionId = session.id().toString(QUuid::WithoutBraces);
    QVERIFY(db().insertSession(session));

    Message message;
    message.role = Role::Assistant;
    ToolCall call;
    call.id = QStringLiteral("call-1");
    call.name = QStringLiteral("srv__echo");
    call.arguments = QJsonObject{{QStringLiteral("text"), QStringLiteral("hi")}};
    message.toolCalls.push_back(call);
    session.appendMessage(message);

    QVERIFY(db().insertMessage(sessionId, message.id.toString(QUuid::WithoutBraces),
                               message.roleName(), message.content, message.timestamp, {}, {},
                               message.toolCalls));

    const auto stored = db().messagesForSession(sessionId);
    QCOMPARE(stored.size(), 1);
    QCOMPARE(stored.at(0).toolCalls.size(), 1);
    QCOMPARE(stored.at(0).toolCalls.at(0).id, QStringLiteral("call-1"));
    QCOMPARE(stored.at(0).toolCalls.at(0).name, QStringLiteral("srv__echo"));
    QCOMPARE(stored.at(0).toolCalls.at(0).arguments,
             QJsonObject({{QStringLiteral("text"), QStringLiteral("hi")}}));
    // Calls are stored as running until a result arrives.
    QCOMPARE(stored.at(0).toolCalls.at(0).status, ToolCallStatus::Running);
    QVERIFY(stored.at(0).toolCalls.at(0).result.isEmpty());
}

void TestChatDatabase::toolCallsSurviveReinsertUpsert() {
    // Real flow: the message row is inserted when the message is appended
    // (no tool calls yet), and persistToolCalls re-inserts the same id once
    // the model requested calls. The upsert must keep the tool calls (and
    // element order), otherwise they are gone after a reload.
    ChatSession session(QStringLiteral("Tools"), QStringLiteral("backend-1"));
    const auto sessionId = session.id().toString(QUuid::WithoutBraces);
    QVERIFY(db().insertSession(session));

    Message message;
    message.role = Role::Assistant;
    session.appendMessage(message);
    const auto messageId = message.id.toString(QUuid::WithoutBraces);
    QVERIFY(db().insertMessage(sessionId, messageId, message.roleName(), message.content,
                               message.timestamp));

    ToolCall call;
    call.id = QStringLiteral("call-1");
    call.name = QStringLiteral("srv__echo");
    call.arguments = QJsonObject{{QStringLiteral("text"), QStringLiteral("hi")}};
    message.toolCalls.push_back(call);
    message.elementOrder = QStringList{QStringLiteral("tool:0")};
    QVERIFY(db().insertMessage(sessionId, messageId, message.roleName(), message.content,
                               message.timestamp, {}, {}, message.toolCalls, {},
                               message.elementOrder));

    // The tool result then arrives and updates the persisted call.
    QVERIFY(db().updateToolCallStatus(messageId, QStringLiteral("call-1"), ToolCallStatus::Done,
                                      QStringLiteral("echo: hi")));
    const auto stored = db().messagesForSession(sessionId);
    QCOMPARE(stored.size(), 1);
    QCOMPARE(stored.at(0).toolCalls.size(), 1);
    QCOMPARE(stored.at(0).toolCalls.at(0).id, QStringLiteral("call-1"));
    QCOMPARE(stored.at(0).toolCalls.at(0).status, ToolCallStatus::Done);
    QCOMPARE(stored.at(0).toolCalls.at(0).result, QStringLiteral("echo: hi"));
    QCOMPARE(stored.at(0).elementOrder, QStringList{QStringLiteral("tool:0")});
}

void TestChatDatabase::toolCallStatusUpdatePersists() {
    ChatSession session(QStringLiteral("Tools"), QStringLiteral("backend-1"));
    const auto sessionId = session.id().toString(QUuid::WithoutBraces);
    QVERIFY(db().insertSession(session));

    Message message;
    message.role = Role::Assistant;
    ToolCall call;
    call.id = QStringLiteral("call-1");
    call.name = QStringLiteral("srv__echo");
    message.toolCalls.push_back(call);
    session.appendMessage(message);
    const auto messageId = message.id.toString(QUuid::WithoutBraces);
    QVERIFY(db().insertMessage(sessionId, messageId, message.roleName(), message.content,
                               message.timestamp, {}, {}, message.toolCalls));

    QVERIFY(db().updateToolCallStatus(messageId, QStringLiteral("call-1"), ToolCallStatus::Done,
                                      QStringLiteral("echo: hi")));
    const auto stored = db().messagesForSession(sessionId);
    QCOMPARE(stored.at(0).toolCalls.at(0).status, ToolCallStatus::Done);
    QCOMPARE(stored.at(0).toolCalls.at(0).result, QStringLiteral("echo: hi"));

    QVERIFY(db().updateToolCallStatus(messageId, QStringLiteral("call-1"), ToolCallStatus::Failed,
                                      QStringLiteral("boom")));
    const auto storedAgain = db().messagesForSession(sessionId);
    QCOMPARE(storedAgain.at(0).toolCalls.at(0).status, ToolCallStatus::Failed);
    QCOMPARE(storedAgain.at(0).toolCalls.at(0).result, QStringLiteral("boom"));
}

void TestChatDatabase::toolCallStatusUpdateFailsForUnknownCall() {
    ChatSession session(QStringLiteral("Tools"), QStringLiteral("backend-1"));
    const auto sessionId = session.id().toString(QUuid::WithoutBraces);
    QVERIFY(db().insertSession(session));

    Message message;
    message.role = Role::Assistant;
    ToolCall call;
    call.id = QStringLiteral("call-1");
    call.name = QStringLiteral("srv__echo");
    message.toolCalls.push_back(call);
    session.appendMessage(message);
    const auto messageId = message.id.toString(QUuid::WithoutBraces);
    QVERIFY(db().insertMessage(sessionId, messageId, message.roleName(), message.content,
                               message.timestamp, {}, {}, message.toolCalls));

    QVERIFY(!db().updateToolCallStatus(messageId, QStringLiteral("no-such-call"),
                                       ToolCallStatus::Done, QStringLiteral("x")));
    QVERIFY(!db().updateToolCallStatus(QUuid::createUuid().toString(QUuid::WithoutBraces),
                                       QStringLiteral("call-1"), ToolCallStatus::Done,
                                       QStringLiteral("x")));
}

void TestChatDatabase::toolCallsColumnDefaultsToEmpty() {
    // Simulate an older database row without tool-call state.
    ChatSession session(QStringLiteral("Legacy"), QStringLiteral("backend-1"));
    const auto sessionId = session.id().toString(QUuid::WithoutBraces);
    QVERIFY(db().insertSession(session));
    const auto message = session.appendMessage(Role::Assistant, QStringLiteral("hi"));
    QVERIFY(db().insertMessage(sessionId, message.id.toString(QUuid::WithoutBraces),
                               message.roleName(), message.content, message.timestamp));
    QVERIFY(db().executeRaw(QStringLiteral("UPDATE messages SET tool_calls = ''")));

    const auto stored = db().messagesForSession(sessionId);
    QCOMPARE(stored.size(), 1);
    QVERIFY(stored.at(0).toolCalls.isEmpty());
}

void TestChatDatabase::loadSessionsReturnsTimestampsOrderedByActivity() {
    ChatSession older(QStringLiteral("Older"), QStringLiteral("backend-1"));
    older.setCreatedAt(QDateTime(QDate(2024, 1, 1), QTime(10, 0)));
    older.setLastActivityAt(QDateTime(QDate(2024, 1, 2), QTime(10, 0)));
    ChatSession newer(QStringLiteral("Newer"), QStringLiteral("backend-1"));
    newer.setCreatedAt(QDateTime(QDate(2026, 10, 20), QTime(10, 0)));
    newer.setLastActivityAt(QDateTime(QDate(2026, 10, 23), QTime(10, 0)));
    QVERIFY(db().insertSession(older));
    QVERIFY(db().insertSession(newer));

    const auto sessions = db().loadSessions();
    QCOMPARE(sessions.size(), 2);
    // Newest activity first.
    QCOMPARE(sessions.at(0).id, newer.id().toString(QUuid::WithoutBraces));
    QCOMPARE(sessions.at(1).id, older.id().toString(QUuid::WithoutBraces));
    QCOMPARE(sessions.at(0).title, QStringLiteral("Newer"));
    QCOMPARE(sessions.at(0).lastActivityAt, newer.lastActivityAt());
    QCOMPARE(sessions.at(0).createdAt, newer.createdAt());
}

void TestChatDatabase::lastActivityColumnDefaultsToCreatedAt() {
    // Simulate an older database row that has last_activity_at = 0.
    ChatSession session(QStringLiteral("Legacy"), QStringLiteral("backend-1"));
    session.setCreatedAt(QDateTime(QDate(2024, 6, 1), QTime(10, 0)));
    QVERIFY(db().insertSession(session));
    QVERIFY(db().executeRaw(QStringLiteral("UPDATE sessions SET last_activity_at = 0")));

    const auto sessions = db().loadSessions();
    QCOMPARE(sessions.size(), 1);
    QCOMPARE(sessions.at(0).lastActivityAt, sessions.at(0).createdAt);
}

void TestChatDatabase::openUsesWalAndNormalSync() {
    QCOMPARE(db().queryScalar(QStringLiteral("PRAGMA journal_mode")).toString().toLower(),
             QStringLiteral("wal"));
    QCOMPARE(db().queryScalar(QStringLiteral("PRAGMA synchronous")).toInt(), 1);
}

void TestChatDatabase::persistStreamResultAsyncPersistsFinishedStream() {
    ChatSession session(QStringLiteral("Chat"), QStringLiteral("backend-1"));
    const auto sessionId = session.id().toString(QUuid::WithoutBraces);
    QVERIFY(db().insertSession(session));
    const auto message = session.appendMessage(Role::Assistant, QString());
    const auto messageId = message.id.toString(QUuid::WithoutBraces);
    QVERIFY(db().insertMessage(sessionId, messageId, message.roleName(), message.content,
                               message.timestamp));

    StreamResult result;
    result.messageId = messageId;
    result.content = QStringLiteral("full answer");
    result.reasoningSegments = QStringList{QStringLiteral("thinking hard")};
    result.elementOrder = QStringList{QStringLiteral("thinking:0"), QStringLiteral("text")};
    result.tokensPerSecond = 42.5;
    ChatDatabase::persistStreamResultAsync(m_filePath, result);

    QTRY_COMPARE_WITH_TIMEOUT(db().messagesForSession(sessionId).at(0).content,
                              QStringLiteral("full answer"), 5000);
    const auto stored = db().messagesForSession(sessionId);
    QVERIFY(!stored.at(0).streaming);
    QCOMPARE(stored.at(0).reasoningSegments, QStringList{QStringLiteral("thinking hard")});
    QCOMPARE(stored.at(0).elementOrder,
             QStringList({QStringLiteral("thinking:0"), QStringLiteral("text")}));
    QCOMPARE(stored.at(0).tokensPerSecond, 42.5);
}

void TestChatDatabase::persistStreamResultAsyncPersistsFailure() {
    ChatSession session(QStringLiteral("Chat"), QStringLiteral("backend-1"));
    const auto sessionId = session.id().toString(QUuid::WithoutBraces);
    QVERIFY(db().insertSession(session));
    const auto message = session.appendMessage(Role::Assistant, QString());
    const auto messageId = message.id.toString(QUuid::WithoutBraces);
    QVERIFY(db().insertMessage(sessionId, messageId, message.roleName(), message.content,
                               message.timestamp));

    StreamResult result;
    result.messageId = messageId;
    result.error = QStringLiteral("connection refused");
    ChatDatabase::persistStreamResultAsync(m_filePath, result);

    QTRY_COMPARE_WITH_TIMEOUT(db().messagesForSession(sessionId).at(0).error,
                              QStringLiteral("connection refused"), 5000);
    const auto stored = db().messagesForSession(sessionId);
    QVERIFY(!stored.at(0).streaming);
}

void TestChatDatabase::attachmentsLoadedOnlyForRequestedMessages() {
    ChatSession sessionA(QStringLiteral("A"), QStringLiteral("backend-1"));
    ChatSession sessionB(QStringLiteral("B"), QStringLiteral("backend-1"));
    const auto idA = sessionA.id().toString(QUuid::WithoutBraces);
    const auto idB = sessionB.id().toString(QUuid::WithoutBraces);
    QVERIFY(db().insertSession(sessionA));
    QVERIFY(db().insertSession(sessionB));

    Message first;
    first.role = Role::User;
    first.content = QStringLiteral("with image");
    ImageAttachment imageA;
    imageA.mimeType = QStringLiteral("image/png");
    imageA.fileName = QStringLiteral("a.png");
    imageA.data = QByteArrayLiteral("image-of-A");
    first.images.push_back(imageA);
    QVERIFY(db().insertMessage(idA, first.id.toString(QUuid::WithoutBraces), first.roleName(),
                               first.content, first.timestamp, first.images));

    Message second;
    second.role = Role::Assistant;
    second.content = QStringLiteral("with file");
    FileAttachment fileA;
    fileA.fileName = QStringLiteral("a.pdf");
    fileA.thumbnail = QByteArrayLiteral("thumb-of-A");
    second.files.push_back(fileA);
    QVERIFY(db().insertMessage(idA, second.id.toString(QUuid::WithoutBraces), second.roleName(),
                               second.content, second.timestamp, second.images, second.files));

    Message other;
    other.role = Role::User;
    other.content = QStringLiteral("other session");
    ImageAttachment imageB;
    imageB.mimeType = QStringLiteral("image/png");
    imageB.data = QByteArrayLiteral("image-of-B");
    other.images.push_back(imageB);
    QVERIFY(db().insertMessage(idB, other.id.toString(QUuid::WithoutBraces), other.roleName(),
                               other.content, other.timestamp, other.images));

    const auto all = db().messagesForSession(idA);
    QCOMPARE(all.size(), 2);
    QCOMPARE(all.at(0).images.size(), 1);
    QCOMPARE(all.at(0).images.at(0).data, QByteArrayLiteral("image-of-A"));
    QCOMPARE(all.at(0).files.size(), 0);
    QCOMPARE(all.at(1).images.size(), 0);
    QCOMPARE(all.at(1).files.size(), 1);
    QCOMPARE(all.at(1).files.at(0).fileName, QStringLiteral("a.pdf"));

    const auto page = db().messagesForSession(idA, 1, 0);
    QCOMPARE(page.size(), 1);
    QCOMPARE(page.at(0).content, QStringLiteral("with file"));
    QCOMPARE(page.at(0).files.size(), 1);
    QCOMPARE(page.at(0).images.size(), 0);

    const auto otherSession = db().messagesForSession(idB);
    QCOMPARE(otherSession.size(), 1);
    QCOMPARE(otherSession.at(0).images.size(), 1);
    QCOMPARE(otherSession.at(0).images.at(0).data, QByteArrayLiteral("image-of-B"));
}

QTEST_GUILESS_MAIN(TestChatDatabase)
#include "tst_chatdatabase.moc"
