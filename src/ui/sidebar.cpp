#include "ui/sidebar.h"
#include "core/chatsession.h"
#include "core/sessiongrouper.h"

#include <QHeaderView>
#include <QMap>
#include <QMenu>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace ocapi {

namespace {

constexpr auto kSessionIdRole = Qt::UserRole;
constexpr auto kIsGroupRole = Qt::UserRole + 1;

// Sessions are expected newest-first; group them by last activity and
// return the group keys in chronological order (newest group first).
QStringList orderedGroupKeys(const QVector<ChatSession *> &sessions) {
    QMap<QString, QString> keyToSort; // group key -> ISO sort key
    for (auto *session : sessions) {
        const QString key = SessionGrouper::groupKey(session->lastActivityAt());
        if (key.isEmpty())
            continue;
        // ISO dates sort chronologically; "today"/"yesterday" sort newest.
        QString sortKey;
        if (key == QLatin1String("today"))
            sortKey = QStringLiteral("9999");
        else if (key == QLatin1String("yesterday"))
            sortKey = QStringLiteral("9998");
        else
            sortKey = key.mid(key.indexOf(QLatin1Char(':')) + 1);
        if (!keyToSort.contains(key) || sortKey > keyToSort.value(key))
            keyToSort.insert(key, sortKey);
    }
    const QStringList keys(keyToSort.keyBegin(), keyToSort.keyEnd());
    QStringList sorted = keys;
    std::ranges::sort(sorted, [&keyToSort](const QString &a, const QString &b) {
        return keyToSort.value(a) > keyToSort.value(b);
    });
    return sorted;
}

} // namespace

Sidebar::Sidebar(QWidget *parent)
    : QWidget(parent), m_tree(new QTreeWidget(this)),
      m_newChatButton(new QPushButton(tr("+ New chat"), this)),
      m_settingsButton(new QPushButton(tr("Settings…"), this)) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);
    layout->addWidget(m_newChatButton);
    layout->addWidget(m_settingsButton);
    layout->addWidget(m_tree, 1);

    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(true);
    m_tree->setIndentation(12);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(m_settingsButton, &QPushButton::clicked, this,
            [this] { emit settingsRequested(QString()); });

    connect(m_newChatButton, &QPushButton::clicked, this, &Sidebar::newChatRequested);
    connect(m_tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item) {
        if (!item || item->data(0, kIsGroupRole).toBool())
            return;
        emit sessionSelected(QUuid(item->data(0, kSessionIdRole).toString()));
    });
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        auto *item = m_tree->itemAt(pos);
        if (!item || item->data(0, kIsGroupRole).toBool())
            return;
        QMenu menu(this);
        const QAction *deleteAction = menu.addAction(tr("Delete chat"));
        if (menu.exec(m_tree->viewport()->mapToGlobal(pos)) == deleteAction)
            emit sessionDeleteRequested(QUuid(item->data(0, kSessionIdRole).toString()));
    });
}

void Sidebar::setSessions(const QVector<ChatSession *> &sessions) {
    const QUuid current = currentSessionId();
    // Remember which groups were expanded so a rebuild keeps the state.
    QStringList expandedGroups;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        auto *group = m_tree->topLevelItem(i);
        if (group->isExpanded())
            expandedGroups.push_back(group->data(0, kSessionIdRole).toString());
    }

    m_tree->clear();
    for (const auto &key : orderedGroupKeys(sessions)) {
        auto *group = new QTreeWidgetItem(m_tree);
        group->setText(0, SessionGrouper::groupLabel(key));
        group->setData(0, kSessionIdRole, key);
        group->setData(0, kIsGroupRole, true);
        group->setExpanded(expandedGroups.contains(key) || expandedGroups.isEmpty());
        QFont groupFont = group->font(0);
        groupFont.setBold(true);
        group->setFont(0, groupFont);
        for (auto *session : sessions) {
            if (SessionGrouper::groupKey(session->lastActivityAt()) != key)
                continue;
            auto *item = new QTreeWidgetItem(group);
            item->setText(0, session->title());
            item->setData(0, kSessionIdRole, session->id().toString());
            item->setData(0, kIsGroupRole, false);
            item->setToolTip(0, session->title());
            if (session->id() == current)
                m_tree->setCurrentItem(item);
        }
    }
}

void Sidebar::selectSession(const QUuid &id) {
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        auto *group = m_tree->topLevelItem(i);
        for (int j = 0; j < group->childCount(); ++j) {
            auto *item = group->child(j);
            if (QUuid(item->data(0, kSessionIdRole).toString()) == id) {
                m_tree->setCurrentItem(item);
                return;
            }
        }
    }
}

QUuid Sidebar::currentSessionId() const {
    auto *item = m_tree->currentItem();
    if (!item || item->data(0, kIsGroupRole).toBool())
        return {};
    return QUuid(item->data(0, kSessionIdRole).toString());
}

} // namespace ocapi
