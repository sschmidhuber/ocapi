#include "core/chatsession.h"

namespace ocapi {

ChatSession::ChatSession(QObject *parent) : QObject(parent), m_title(tr("New chat")) {}

ChatSession::ChatSession(QString title, QString backendId, QObject *parent)
    : QObject(parent), m_title(std::move(title)), m_backendId(std::move(backendId)) {}

ChatSession::ChatSession(const QUuid &id, QString title, QString backendId, QObject *parent)
    : QObject(parent), m_id(id), m_title(std::move(title)), m_backendId(std::move(backendId)) {}

void ChatSession::setTitle(QString title) {
    if (m_title == title)
        return;
    m_title = std::move(title);
    emit titleChanged(m_title);
}

void ChatSession::setBackendId(QString backendId) {
    m_backendId = std::move(backendId);
}

void ChatSession::setModel(QString model) {
    m_model = std::move(model);
}

void ChatSession::setCreatedAt(const QDateTime &createdAt) {
    if (createdAt.isValid())
        m_createdAt = createdAt;
}

void ChatSession::setLastActivityAt(const QDateTime &lastActivityAt) {
    if (lastActivityAt.isValid())
        m_lastActivityAt = lastActivityAt;
}

Message &ChatSession::appendMessage(Role role, const QString &content, bool streaming) {
    Message message;
    message.role = role;
    message.content = content;
    message.streaming = streaming;
    m_messages.push_back(message);
    m_lastActivityAt =
        message.timestamp.isValid() ? message.timestamp : QDateTime::currentDateTime();
    emit messageAdded(m_messages.constLast());
    return m_messages.last();
}

Message &ChatSession::appendMessage(const Message &message) {
    m_messages.push_back(message);
    m_lastActivityAt =
        message.timestamp.isValid() ? message.timestamp : QDateTime::currentDateTime();
    emit messageAdded(m_messages.constLast());
    return m_messages.last();
}

void ChatSession::prependMessages(const QVector<Message> &messages) {
    if (messages.isEmpty())
        return;
    // Keep any live (streaming) tail intact while splicing in older history.
    m_messages = messages + m_messages;
}

void ChatSession::updateMessage(const QUuid &id, const QString &content, bool streaming) {
    for (auto &message : m_messages) {
        if (message.id == id) {
            message.content = content;
            message.streaming = streaming;
            message.error.clear();
            emit messageUpdated(message);
            return;
        }
    }
}

void ChatSession::updateReasoning(const QUuid &id, const QString &reasoning, bool streaming) {
    for (auto &message : m_messages) {
        if (message.id == id) {
            // A new thinking phase starts when reasoning streams again after
            // a previous phase ended (reasoningStreaming went false, e.g. the
            // model produced an answer or tool calls in between). Each phase
            // gets its own segment so the chat shows one Thinking element per
            // phase, in order.
            if (streaming && !message.reasoningStreaming && !message.reasoningSegments.isEmpty()) {
                message.reasoningSegments.append(QString());
                message.elementOrder.append(
                    QStringLiteral("thinking:%1")
                        .arg(static_cast<int>(message.reasoningSegments.size() - 1)));
            }
            if (!message.reasoningSegments.isEmpty() &&
                message.reasoningSegments.constLast() == reasoning &&
                message.reasoningStreaming == streaming)
                return;
            if (message.reasoningSegments.isEmpty()) {
                message.reasoningSegments.append(reasoning);
                message.elementOrder.append(QStringLiteral("thinking:0"));
            } else {
                message.reasoningSegments.last() = reasoning;
            }
            message.reasoningStreaming = streaming;
            message.error.clear();
            emit messageUpdated(message);
            return;
        }
    }
}

void ChatSession::setReasoningStreaming(const QUuid &id, bool streaming) {
    for (auto &message : m_messages) {
        if (message.id == id) {
            if (message.reasoningStreaming == streaming)
                return;
            message.reasoningStreaming = streaming;
            emit messageUpdated(message);
            return;
        }
    }
}

void ChatSession::setToolCalls(const QUuid &id, const QVector<ToolCall> &toolCalls) {
    for (auto &message : m_messages) {
        if (message.id == id) {
            if (message.toolCalls == toolCalls)
                return;
            message.toolCalls = toolCalls;
            message.streaming = false;
            emit messageUpdated(message);
            return;
        }
    }
}

void ChatSession::appendToolCalls(const QUuid &id, const QVector<ToolCall> &toolCalls) {
    for (auto &message : m_messages) {
        if (message.id == id) {
            if (toolCalls.isEmpty())
                return;
            const int firstNew = message.toolCalls.size();
            message.toolCalls += toolCalls;
            for (int i = 0; i < toolCalls.size(); ++i)
                message.elementOrder.append(QStringLiteral("tool:%1").arg(firstNew + i));
            message.streaming = false;
            emit messageUpdated(message);
            return;
        }
    }
}

void ChatSession::setToolCallStatus(const QUuid &messageId, const QString &toolCallId,
                                    ToolCallStatus status, const QString &result) {
    for (auto &message : m_messages) {
        if (message.id != messageId)
            continue;
        for (auto &call : message.toolCalls) {
            if (call.id != toolCallId)
                continue;
            if (call.status == status && call.result == result)
                return;
            call.status = status;
            call.result = result;
            emit messageUpdated(message);
            return;
        }
        return;
    }
}

void ChatSession::setThroughput(const QUuid &id, double tokensPerSecond) {
    for (auto &message : m_messages) {
        if (message.id == id) {
            if (message.tokensPerSecond == tokensPerSecond)
                return;
            message.tokensPerSecond = tokensPerSecond;
            emit messageUpdated(message);
            return;
        }
    }
}

void ChatSession::failMessage(const QUuid &id, const QString &error) {
    for (auto &message : m_messages) {
        if (message.id == id) {
            message.streaming = false;
            message.reasoningStreaming = false;
            message.error = error;
            emit messageUpdated(message);
            return;
        }
    }
}

void ChatSession::clear() {
    m_messages.clear();
}

} // namespace ocapi
