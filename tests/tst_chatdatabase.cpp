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
    void toolCallsRoundTrip();
    void toolCallStatusUpdatePersists();
    void toolCallStatusUpdateFailsForUnknownCall();
    void toolCallsColumnDefaultsToEmpty();

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
}

void TestChatDatabase::backendUpdateOverwritesEntry() {
    BackendConfig config;
    config.name = QStringLiteral("before");
    config.kind = BackendKind::OpenAi;
    db().insertBackend(config);
    config.name = QStringLiteral("after");
    config.kind = BackendKind::Groq;
    config.enabled = false;
    db().updateBackend(config);
    const auto loaded = db().loadBackends();
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(loaded.at(0).name, QStringLiteral("after"));
    QCOMPARE(loaded.at(0).kind, BackendKind::Groq);
    QVERIFY(!loaded.at(0).enabled);
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
    // The extracted text travels inside the message content, not the file row.
    QVERIFY(storedFile.extractedText.isEmpty());
    QVERIFY(stored.at(0).content.contains(QStringLiteral("see attached")));
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

QTEST_GUILESS_MAIN(TestChatDatabase)
#include "tst_chatdatabase.moc"
