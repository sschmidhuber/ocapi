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

    Message &appendMessage(Role role, const QString &content = {}, bool streaming = false);
    Message &appendMessage(const Message &message);
    void prependMessages(const QVector<Message> &messages);
    void updateMessage(const QUuid &id, const QString &content, bool streaming);
    // Appends reasoning tokens to a message and marks whether the reasoning
    // stream is still running; emits messageUpdated so the collapsed
    // "Thinking" element repaints with its progress indicator.
    void updateReasoning(const QUuid &id, const QString &reasoning, bool streaming);
    // Ends the reasoning phase (first answer token arrived or the stream
    // finished) so the progress indicator disappears.
    void setReasoningStreaming(const QUuid &id, bool streaming);
    void setToolCalls(const QUuid &id, const QVector<ToolCall> &toolCalls);
    // Updates one tool call of a message (status/result); emits
    // messageUpdated so the chat view repaints the collapsed element.
    void setToolCallStatus(const QUuid &messageId, const QString &toolCallId, ToolCallStatus status,
                           const QString &result);
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
