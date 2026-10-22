#include "ui/sidebar.h"
#include "core/chatsession.h"

#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QVBoxLayout>

namespace ocapi {

Sidebar::Sidebar(QWidget *parent)
    : QWidget(parent), m_list(new QListWidget(this)),
      m_newChatButton(new QPushButton(tr("+ New chat"), this)),
      m_toolsButton(new QPushButton(tr("Tools"), this)) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);
    layout->addWidget(m_newChatButton);
    layout->addWidget(m_list, 1);
    layout->addWidget(m_toolsButton);

    m_list->setContextMenuPolicy(Qt::CustomContextMenu);

    auto *toolsMenu = new QMenu(m_toolsButton);
    toolsMenu->addAction(tr("Model backends…"), this,
                         [this]() { emit settingsRequested(QStringLiteral("backends")); });
    toolsMenu->addAction(tr("MCP servers…"), this,
                         [this]() { emit settingsRequested(QStringLiteral("mcp")); });
    toolsMenu->addAction(tr("Speech-to-text…"), this,
                         [this]() { emit settingsRequested(QStringLiteral("stt")); });
    toolsMenu->addAction(tr("Text-to-speech…"), this,
                         [this]() { emit settingsRequested(QStringLiteral("tts")); });
    m_toolsButton->setMenu(toolsMenu);

    connect(m_newChatButton, &QPushButton::clicked, this, &Sidebar::newChatRequested);
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (item)
            emit sessionSelected(QUuid(item->data(Qt::UserRole).toString()));
    });
    connect(m_list, &QListWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        auto *item = m_list->itemAt(pos);
        if (!item)
            return;
        QMenu menu(this);
        const QAction *deleteAction = menu.addAction(tr("Delete chat"));
        if (menu.exec(m_list->viewport()->mapToGlobal(pos)) == deleteAction)
            emit sessionDeleteRequested(QUuid(item->data(Qt::UserRole).toString()));
    });
}

void Sidebar::setSessions(const QVector<ChatSession *> &sessions) {
    const QUuid current = currentSessionId();
    m_list->clear();
    for (auto *session : sessions) {
        auto *item = new QListWidgetItem(session->title(), m_list);
        item->setData(Qt::UserRole, session->id().toString());
        item->setToolTip(session->title());
        if (session->id() == current)
            m_list->setCurrentItem(item);
    }
}

void Sidebar::selectSession(const QUuid &id) {
    for (int i = 0; i < m_list->count(); ++i) {
        auto *item = m_list->item(i);
        if (QUuid(item->data(Qt::UserRole).toString()) == id) {
            m_list->setCurrentItem(item);
            return;
        }
    }
}

QUuid Sidebar::currentSessionId() const {
    auto *item = m_list->currentItem();
    return item ? QUuid(item->data(Qt::UserRole).toString()) : QUuid();
}

} // namespace ocapi
