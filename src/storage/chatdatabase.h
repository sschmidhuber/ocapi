#pragma once

#include <QDateTime>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>

#include "core/backendconfig.h"
#include "core/imagemessage.h"

namespace ocapi {

class ChatSession;

struct StoredMessage {
    QString id;
    QString role;
    QString content;
    QString error;
    QDateTime timestamp;
    bool streaming = false;
    double tokensPerSecond = -1.0;
    QVector<ImageAttachment> images;
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
    void close();
    [[nodiscard]] QString lastError() const { return m_lastError; }

    [[nodiscard]] bool setValue(const QString &key, const QString &value);
    [[nodiscard]] QString value(const QString &key, const QString &defaultValue = {}) const;

    void insertBackend(const BackendConfig &config);
    void updateBackend(const BackendConfig &config);
    void deleteBackend(const QString &backendId);
    [[nodiscard]] QVector<BackendConfig> loadBackends() const;

    [[nodiscard]] QStringList sessionIds() const;
    bool insertSession(const ChatSession &session);
    bool updateSession(const ChatSession &session);
    bool deleteSession(const QString &sessionId);
    bool insertMessage(const QString &sessionId, const QString &messageId, const QString &role,
                       const QString &content, const QDateTime &timestamp,
                       const QVector<ImageAttachment> &images = {});
    bool updateMessage(const QString &messageId, const QString &content, bool streaming);
    bool setThroughput(const QString &messageId, double tokensPerSecond);
    bool failMessage(const QString &messageId, const QString &error);

    [[nodiscard]] QString sessionTitle(const QString &sessionId) const;
    [[nodiscard]] QString sessionBackendId(const QString &sessionId) const;
    [[nodiscard]] QString sessionModel(const QString &sessionId) const;
    [[nodiscard]] QVector<StoredMessage> messagesForSession(const QString &sessionId,
                                                            int limit = -1, int offset = 0) const;
    [[nodiscard]] int messageCount(const QString &sessionId) const;

private:
    bool execute(const QString &query, const QVariantList &bindings);
    bool insertImage(const QString &messageId, const ImageAttachment &image);
    void loadAttachmentsFor(QVector<StoredMessage> &messages) const;

    QString m_connectionName;
    QSqlDatabase m_database;
    mutable QString m_lastError;
};

} // namespace ocapi
