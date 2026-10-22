#include "ui/chatview.h"
#include "core/chatsession.h"
#include "ui/chatdelegate.h"

#include <QAbstractListModel>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QGuiApplication>
#include <QMenu>
#include <QResizeEvent>
#include <QScrollBar>

namespace ocapi {

namespace {

enum DataRoles {
    IsUserRole = Qt::UserRole,
    RoleLabelRole,
    StreamingRole,
    ErrorRole,
    ThroughputRole,
    ImagesRole,
    ToolCallsRole,
    ToolNameRole,
};

class ChatModel : public QAbstractListModel {
    Q_OBJECT

public:
    explicit ChatModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}

    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override {
        return parent.isValid() ? 0 : static_cast<int>(m_messages.size());
    }

    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override {
        if (!index.isValid() || index.row() >= m_messages.size())
            return {};
        const Message &message = m_messages.at(index.row());
        switch (role) {
        case Qt::DisplayRole:
            return message.content;
        case IsUserRole:
            return message.role == Role::User;
        case RoleLabelRole:
            if (message.role == Role::User)
                return tr("You");
            if (message.role == Role::Tool)
                return tr("Tool");
            return tr("Assistant");
        case StreamingRole:
            return message.streaming;
        case ErrorRole:
            return message.error;
        case ThroughputRole:
            return message.tokensPerSecond;
        case ImagesRole:
            return QVariant::fromValue(message.images);
        case ToolCallsRole:
            return QVariant::fromValue(message.toolCalls);
        case ToolNameRole:
            return message.role == Role::Tool ? message.toolName : QString();
        default:
            return {};
        }
    }

    void setMessages(const QVector<Message> &messages) {
        beginResetModel();
        m_messages = messages;
        endResetModel();
    }

    void prependMessages(const QVector<Message> &messages) {
        if (messages.isEmpty())
            return;
        const int added = static_cast<int>(messages.size());
        beginInsertRows(QModelIndex(), 0, added - 1);
        m_messages = messages + m_messages;
        endInsertRows();
    }

    void updateMessage(const Message &message) {
        for (qsizetype i = 0; i < m_messages.size(); ++i) {
            if (m_messages.at(i).id == message.id) {
                m_messages[i] = message;
                const QModelIndex idx = index(static_cast<int>(i));
                emit dataChanged(idx, idx);
                return;
            }
        }
    }

    [[nodiscard]] const QVector<Message> &messages() const { return m_messages; }

private:
    QVector<Message> m_messages;
};

} // namespace

ChatView::ChatView(QWidget *parent) : QListView(parent) {
    auto *model = new ChatModel(this);
    setModel(model);
    auto *delegate = new ChatDelegate(this);
    setItemDelegate(delegate);
    connect(delegate, &ChatDelegate::copiedFeedbackChanged, viewport(),
            QOverload<>::of(&QWidget::update));
    connect(verticalScrollBar(), &QAbstractSlider::valueChanged, this, &ChatView::checkScrollTop);
    setSelectionMode(QAbstractItemView::NoSelection);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setWordWrap(true);
    setUniformItemSizes(false);
    setSpacing(2);
    // White conversation background, independent of the widget theme. The
    // 1px frame and slim scrollbar are styled by MainWindow.
    QPalette viewPalette = palette();
    viewPalette.setColor(QPalette::Base, Qt::white);
    viewPalette.setColor(QPalette::Window, Qt::white);
    setPalette(viewPalette);
    viewport()->setAutoFillBackground(true);
    viewport()->setPalette(viewPalette);
}

void ChatView::prependMessages(const QVector<Message> &messages) {
    if (messages.isEmpty())
        return;
    const int anchor = verticalScrollBar()->value();
    m_messages = messages + m_messages;
    qobject_cast<ChatModel *>(model())->prependMessages(messages);
    // Keep the previously topmost message in view after prepending.
    if (anchor > 0)
        verticalScrollBar()->setValue(anchor + static_cast<int>(messages.size()));
}

void ChatView::checkScrollTop() {
    if (verticalScrollBar()->value() <= 0 && m_session)
        emit olderMessagesRequested();
}

void ChatView::setSession(ChatSession *session) {
    if (m_session)
        m_session->disconnect(this);
    m_session = session;
    auto *model = qobject_cast<ChatModel *>(this->model());
    if (!m_session) {
        m_messages.clear();
        model->setMessages(m_messages);
        return;
    }
    connect(m_session, &ChatSession::messageAdded, this, [this, model](const Message &message) {
        m_messages.push_back(message);
        qobject_cast<ChatModel *>(model)->setMessages(m_messages);
        scrollToEnd();
    });
    connect(m_session, &ChatSession::messageUpdated, this, [this, model](const Message &message) {
        for (auto &m : m_messages) {
            if (m.id == message.id) {
                m = message;
                break;
            }
        }
        qobject_cast<ChatModel *>(model)->updateMessage(message);
        scrollToEnd();
    });
    rebuild();
}

void ChatView::rebuild() {
    m_messages = m_session ? m_session->messages() : QVector<Message>{};
    qobject_cast<ChatModel *>(model())->setMessages(m_messages);
    scrollToEnd();
}

void ChatView::scrollToEnd() {
    if (verticalScrollBar())
        verticalScrollBar()->setValue(verticalScrollBar()->maximum());
}

void ChatView::resizeEvent(QResizeEvent *event) {
    QListView::resizeEvent(event);
    // Force size hints to be recomputed for the new width.
    if (model())
        emit model() -> layoutChanged();
}

void ChatView::contextMenuEvent(QContextMenuEvent *event) {
    const QModelIndex index = indexAt(event->pos());
    if (!index.isValid())
        return;
    QMenu menu(this);
    const QAction *copyAction = menu.addAction(tr("Copy message"));
    if (menu.exec(event->globalPos()) == copyAction)
        copyMessage(index);
}

void ChatView::copyMessage(const QModelIndex &index) {
    const QString content = index.data(Qt::DisplayRole).toString();
    const QString error = index.data(ErrorRole).toString();
    QString text = content;
    if (!error.isEmpty())
        text = error;
    if (text.isEmpty())
        return;
    QGuiApplication::clipboard()->setText(text);
}

} // namespace ocapi

#include "chatview.moc"
