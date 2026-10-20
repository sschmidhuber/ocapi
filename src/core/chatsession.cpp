#include "core/chatsession.h"

namespace ocapi {

ChatSession::ChatSession(QObject* parent)
    : QObject(parent)
    , m_title(tr("New chat"))
{
}

ChatSession::ChatSession(QString title, QString backendId, QObject* parent)
    : QObject(parent)
    , m_title(std::move(title))
    , m_backendId(std::move(backendId))
{
}

void ChatSession::setTitle(QString title)
{
    if (m_title == title)
        return;
    m_title = std::move(title);
    emit titleChanged(m_title);
}

void ChatSession::setBackendId(QString backendId)
{
    m_backendId = std::move(backendId);
}

void ChatSession::setModel(QString model)
{
    m_model = std::move(model);
}

Message& ChatSession::appendMessage(Role role, const QString& content)
{
    Message message;
    message.role = role;
    message.content = content;
    m_messages.push_back(message);
    emit messageAdded(m_messages.constLast());
    return m_messages.last();
}

void ChatSession::updateMessage(const QUuid& id, const QString& content, bool streaming)
{
    for (auto& message : m_messages) {
        if (message.id == id) {
            message.content = content;
            message.streaming = streaming;
            message.error.clear();
            emit messageUpdated(message);
            return;
        }
    }
}

void ChatSession::failMessage(const QUuid& id, const QString& error)
{
    for (auto& message : m_messages) {
        if (message.id == id) {
            message.streaming = false;
            message.error = error;
            emit messageUpdated(message);
            return;
        }
    }
}

void ChatSession::clear()
{
    m_messages.clear();
}

} // namespace ocapi
