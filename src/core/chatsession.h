#pragma once

#include "core/message.h"

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QUuid>
#include <QVector>

namespace ocapi {

class ChatSession : public QObject {
    Q_OBJECT

public:
    explicit ChatSession(QObject *parent = nullptr);
    ChatSession(QString title, QString backendId, QObject *parent = nullptr);
    ChatSession(const QUuid &id, QString title, QString backendId, QObject *parent = nullptr);

    [[nodiscard]] QUuid id() const { return m_id; }
    [[nodiscard]] QString title() const { return m_title; }
    void setTitle(QString title);

    [[nodiscard]] QString backendId() const { return m_backendId; }
    void setBackendId(QString backendId);

    [[nodiscard]] QString model() const { return m_model; }
    void setModel(QString model);

    // When the session was created and when it last received a message.
    // Used to group the chat history by time in the sidebar.
    [[nodiscard]] QDateTime createdAt() const { return m_createdAt; }
    [[nodiscard]] QDateTime lastActivityAt() const { return m_lastActivityAt; }
    void setCreatedAt(const QDateTime &createdAt);
    void setLastActivityAt(const QDateTime &lastActivityAt);

    [[nodiscard]] const QVector<Message> &messages() const { return m_messages; }

    Message &appendMessage(Role role, const QString &content = {});
    Message &appendMessage(const Message &message);
    void prependMessages(const QVector<Message> &messages);
    void updateMessage(const QUuid &id, const QString &content, bool streaming);
    void setToolCalls(const QUuid &id, const QVector<ToolCall> &toolCalls);
    void setThroughput(const QUuid &id, double tokensPerSecond);
    void failMessage(const QUuid &id, const QString &error);
    void clear();

signals:
    void titleChanged(const QString &title);
    void messageAdded(const ocapi::Message &message);
    void messageUpdated(const ocapi::Message &message);

private:
    QUuid m_id = QUuid::createUuid();
    QString m_title;
    QString m_backendId;
    QString m_model;
    QVector<Message> m_messages;
    QDateTime m_createdAt = QDateTime::currentDateTime();
    QDateTime m_lastActivityAt = m_createdAt;
};

} // namespace ocapi
