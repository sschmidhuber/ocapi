#pragma once

#include <QDateTime>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>

#include "core/backendconfig.h"
#include "core/imagemessage.h"
#include "core/mcpserverconfig.h"
#include "core/message.h"

namespace ocapi {

class ChatSession;

// One chat session row with its timestamps, for time-based grouping.
struct StoredSession {
    QString id;
    QString title;
    QString backendId;
    QString model;
    QDateTime createdAt;
    QDateTime lastActivityAt;
};

struct StoredMessage {
    QString id;
    QString role;
    QString content;
    QStringList reasoningSegments; // one entry per thinking phase
    QStringList elementOrder;      // "thinking:<i>" / "tool:<i>" tokens in display order
    QString error;
    QDateTime timestamp;
    bool streaming = false;
    double tokensPerSecond = -1.0;
    QVector<ImageAttachment> images;
    QVector<FileAttachment> files;
    QVector<ToolCall> toolCalls;
};

// Snapshot of one finished (or failed) assistant stream. Only completed
// streams are persisted; intermediate token states stay in memory.
struct StreamResult {
    QString messageId;
    QString content;
    QString error; // non-empty when the stream failed
    QStringList reasoningSegments;
    QStringList elementOrder;
    double tokensPerSecond = -1.0; // negative when unknown
};

class ChatDatabase {
public:
    ChatDatabase();
    ~ChatDatabase();

    ChatDatabase(const ChatDatabase &) = delete;
    ChatDatabase &operator=(const ChatDatabase &) = delete;
    ChatDatabase(ChatDatabase &&) = delete;
    ChatDatabase &operator=(ChatDatabase &&) = delete;

    [[nodiscard]] bool open(const QString &filePath);
    [[nodiscard]] bool isOpen() const { return m_database.isOpen(); }
    // The file this connection was opened on; lets long-lived components
    // persist by reopening a fresh connection from the path instead of
    // retaining a pointer to this object (which they could outlive).
    [[nodiscard]] QString filePath() const { return m_database.databaseName(); }
    void close();
    [[nodiscard]] QString lastError() const { return m_lastError; }

    [[nodiscard]] bool setValue(const QString &key, const QString &value);
    [[nodiscard]] QString value(const QString &key, const QString &defaultValue = {}) const;

    void insertBackend(const BackendConfig &config);
    void updateBackend(const BackendConfig &config);
    void deleteBackend(const QString &backendId);
    [[nodiscard]] QVector<BackendConfig> loadBackends() const;

    void insertMcpServer(const McpServerConfig &config);
    void deleteMcpServer(const QString &serverId);
    [[nodiscard]] QVector<McpServerConfig> loadMcpServers() const;

    [[nodiscard]] QStringList sessionIds() const;
    // All sessions ordered by last activity, newest first.
    [[nodiscard]] QVector<StoredSession> loadSessions() const;
    bool insertSession(const ChatSession &session);
    bool updateSession(const ChatSession &session);
    bool deleteSession(const QString &sessionId);
    // Deletes the given messages of a session (attachments first); used when
    // a user message is edited and the tail of the history is resent.
    bool deleteMessagesFrom(const QString &sessionId, const QStringList &messageIds);
    bool insertMessage(const QString &sessionId, const QString &messageId, const QString &role,
                       const QString &content, const QDateTime &timestamp,
                       const QVector<ImageAttachment> &images = {},
                       const QVector<FileAttachment> &files = {},
                       const QVector<ToolCall> &toolCalls = {},
                       const QStringList &reasoningSegments = {},
                       const QStringList &elementOrder = {});
    bool updateMessage(const QString &messageId, const QString &content, bool streaming);
    // Stores the message's thinking phases without touching the visible
    // content (the reasoning stream runs before the answer). Segments are
    // persisted as a JSON array in the reasoning column.
    bool setReasoning(const QString &messageId, const QStringList &reasoningSegments);
    // Stores the message's element order (thinking/tool tokens) without
    // touching the reasoning text or visible content.
    bool setElementOrder(const QString &messageId, const QStringList &elementOrder);
    // Marks one tool call of an assistant message finished and stores its
    // result text (empty for still-running calls).
    bool updateToolCallStatus(const QString &messageId, const QString &toolCallId,
                              ToolCallStatus status, const QString &result);
    bool setThroughput(const QString &messageId, double tokensPerSecond);
    bool failMessage(const QString &messageId, const QString &error);

    [[nodiscard]] QString sessionTitle(const QString &sessionId) const;
    [[nodiscard]] QString sessionBackendId(const QString &sessionId) const;
    [[nodiscard]] QString sessionModel(const QString &sessionId) const;
    [[nodiscard]] QVector<StoredMessage> messagesForSession(const QString &sessionId,
                                                            int limit = -1, int offset = 0) const;
    [[nodiscard]] int messageCount(const QString &sessionId) const;

    // Persists a finished stream from a thread-pool thread using its own
    // connection to the same file, so the caller's (UI) thread is never
    // blocked by the write. With WAL enabled, the concurrent write does not
    // stall readers on the main connection.
    static void persistStreamResultAsync(const QString &filePath, const StreamResult &result);

    // Runs a raw SQL statement on the database connection (for tests and
    // schema fixes that do not fit the typed API).
    bool executeRaw(const QString &query);
    // Runs a raw SQL query and returns the first column of the first row
    // (for tests and diagnostics, e.g. reading back PRAGMA values).
    [[nodiscard]] QVariant queryScalar(const QString &query) const;

private:
    bool execute(const QString &query, const QVariantList &bindings);
    bool insertImage(const QString &messageId, const ImageAttachment &image);
    bool insertFile(const QString &messageId, const FileAttachment &file);
    void loadAttachmentsFor(QVector<StoredMessage> &messages) const;

    QString m_connectionName;
    QSqlDatabase m_database;
    mutable QString m_lastError;
};

} // namespace ocapi
