#include "ui/sidebar.h"
#include "core/chatsession.h"
#include "core/sessiongrouper.h"

#include <QHeaderView>
#include <QIcon>
#include <QMap>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
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

Sidebar::Sidebar(QWidget *parent) : QWidget(parent), m_tree(new QTreeWidget(this)) {
    // The spinner of running chats rotates continuously; started lazily in
    // setIndicators() so an idle sidebar keeps no timer running.
    m_spinnerTimer = new QTimer(this);
    m_spinnerTimer->setInterval(120);
    connect(m_spinnerTimer, &QTimer::timeout, this, &Sidebar::advanceSpinner);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);
    layout->addWidget(m_tree, 1);

    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(true);
    m_tree->setIndentation(12);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);

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
    const bool firstBuild = m_tree->topLevelItemCount() == 0;
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
        // On the first build only "Today" is expanded; afterwards the
        // user's expand state is preserved exactly.
        group->setExpanded(firstBuild ? key == QLatin1String("today")
                                      : expandedGroups.contains(key));
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
    applyIndicators();
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

void Sidebar::setIndicators(const QSet<QUuid> &running, const QSet<QUuid> &unread) {
    m_running = running;
    m_unread = unread;
    if (!m_running.isEmpty())
        m_spinnerTimer->start();
    else
        m_spinnerTimer->stop();
    applyIndicators();
}

void Sidebar::applyIndicators() {
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        auto *group = m_tree->topLevelItem(i);
        for (int j = 0; j < group->childCount(); ++j) {
            auto *item = group->child(j);
            const QUuid id(item->data(0, kSessionIdRole).toString());
            QIcon icon;
            QStringList state;
            if (m_running.contains(id)) {
                icon = spinnerIcon();
                state << tr("generating");
            }
            if (m_unread.contains(id)) {
                if (icon.isNull())
                    icon = unreadDotIcon();
                state << tr("unread messages");
            }
            item->setIcon(0, icon);
            item->setToolTip(0, state.isEmpty() ? item->text(0)
                                                : item->text(0) + QLatin1String(" — ") +
                                                      state.join(QLatin1String(", ")));
        }
    }
}

void Sidebar::advanceSpinner() {
    m_spinnerAngle = (m_spinnerAngle + 45) % 360;
    applyIndicators();
}

QIcon Sidebar::spinnerIcon() const {
    // Breeze-style loading spinner: a thick arc sweeping most of the circle,
    // rotating with the timer.
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    const QPen pen(palette().color(QPalette::Text), 2.4, Qt::SolidLine, Qt::RoundCap);
    painter.setPen(pen);
    const QRectF rect(2.2, 2.2, 11.6, 11.6);
    painter.drawArc(rect, -m_spinnerAngle * 16, 300 * 16);
    return {pixmap};
}

QIcon Sidebar::unreadDotIcon() const {
    // Unread badge: a filled circle in the theme's highlight color.
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(palette().color(QPalette::Highlight));
    painter.drawEllipse(QPointF(8, 8), 4.0, 4.0);
    return {pixmap};
}

} // namespace ocapi
