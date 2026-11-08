#include "ui/sidebar.h"
#include "core/chatsession.h"
#include "core/sessiongrouper.h"

#include <QApplication>
#include <QHeaderView>
#include <QIcon>
#include <QIconEngine>
#include <QKeySequence>
#include <QMap>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QShortcut>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace okapi {

namespace {

constexpr auto kSessionIdRole = Qt::UserRole;
constexpr auto kIsGroupRole = Qt::UserRole + 1;

// Vector icon: paints its callback at whatever pixel size Qt requests, so
// the badge stays sharp on high-DPI screens (Qt asks the engine for
// size * devicePixelRatio) instead of upscaling a fixed low-resolution
// bitmap.
class VectorIconEngine : public QIconEngine {
public:
    using Painter = std::function<void(QPainter &, const QRectF &)>;
    explicit VectorIconEngine(Painter painter) : m_painter(std::move(painter)) {}
    [[nodiscard]] QIconEngine *clone() const override { return new VectorIconEngine(*this); }
    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode,
               QIcon::State state) override {
        Q_UNUSED(mode);
        Q_UNUSED(state);
        painter->setRenderHint(QPainter::Antialiasing, true);
        m_painter(*painter, QRectF(rect));
    }
    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override {
        QPixmap pixmap(size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        paint(&painter, QRect(QPoint(0, 0), size), mode, state);
        return pixmap;
    }

private:
    Painter m_painter;
};

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
    // Multi-selection: Ctrl+click adds items, Shift+click selects a range,
    // group headers are selectable like chats.
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(m_tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item) {
        if (!item || item->data(0, kIsGroupRole).toBool())
            return;
        // Ctrl/Shift+click only extends the selection (for multi-delete); it
        // must not switch the visible chat.
        if (QApplication::keyboardModifiers() & (Qt::ControlModifier | Qt::ShiftModifier))
            return;
        emit sessionSelected(QUuid(item->data(0, kSessionIdRole).toString()));
    });
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        auto *item = m_tree->itemAt(pos);
        if (!item)
            return;
        // Right-clicking an item outside the selection selects only that
        // item; right-clicking a selected item keeps the multi-selection.
        // (setCurrentItem alone does not clear the selection in
        // ExtendedSelection mode.)
        if (!item->isSelected()) {
            m_tree->clearSelection();
            m_tree->setCurrentItem(item);
        }
        QMenu menu(this);
        auto *deleteAction = menu.addAction(tr("Delete chat"));
        const QList<QUuid> deletable = deletableSelectedSessionIds();
        // Running and unread chats can never be deleted.
        deleteAction->setEnabled(!deletable.isEmpty());
        if (menu.exec(m_tree->viewport()->mapToGlobal(pos)) == deleteAction)
            emit sessionDeleteRequested(deletable);
    });
    // The Delete key removes the selected chats while the tree has focus.
    auto *deleteShortcut = new QShortcut(QKeySequence(Qt::Key_Delete), m_tree);
    deleteShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(deleteShortcut, &QShortcut::activated, this, &Sidebar::requestDeleteSelected);
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
                // With ExtendedSelection setCurrentItem() does not clear the
                // previous selection (unlike SingleSelection), so clear it
                // explicitly: switching chats must show exactly one selected.
                m_tree->clearSelection();
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

QList<QUuid> Sidebar::selectedSessionIds() const {
    QSet<QUuid> ids;
    const auto selected = m_tree->selectedItems();
    for (const auto *item : selected) {
        if (item->data(0, kIsGroupRole).toBool()) {
            // A selected group header contributes all of its chats.
            for (int j = 0; j < item->childCount(); ++j)
                ids.insert(QUuid(item->child(j)->data(0, kSessionIdRole).toString()));
        } else {
            ids.insert(QUuid(item->data(0, kSessionIdRole).toString()));
        }
    }
    return {ids.begin(), ids.end()};
}

QList<QUuid> Sidebar::deletableSelectedSessionIds() const {
    QList<QUuid> ids = selectedSessionIds();
    const auto removed = std::ranges::remove_if(
        ids, [this](const QUuid &id) { return m_running.contains(id) || m_unread.contains(id); });
    ids.erase(removed.begin(), removed.end());
    return ids;
}

void Sidebar::requestDeleteSelected() {
    const QList<QUuid> deletable = deletableSelectedSessionIds();
    if (!deletable.isEmpty())
        emit sessionDeleteRequested(deletable);
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
    const QPen pen(palette().color(QPalette::Text), 2.4, Qt::SolidLine, Qt::RoundCap);
    const int angle = m_spinnerAngle;
    return QIcon(new VectorIconEngine([pen, angle](QPainter &painter, const QRectF &rect) {
        // The pen width scales with the icon so the spinner keeps its look
        // at every size; the arc leaves a gap that rotates with the timer.
        QPen scaled = pen;
        scaled.setWidthF(pen.widthF() * (rect.width() / 16.0));
        painter.setPen(scaled);
        const QRectF circle = rect.marginsRemoved(QMarginsF(
            rect.width() * 0.14, rect.height() * 0.14, rect.width() * 0.14, rect.height() * 0.14));
        painter.drawArc(circle, -angle * 16, 300 * 16);
    }));
}

QIcon Sidebar::unreadDotIcon() const {
    // Unread badge: a filled circle in the theme's highlight color.
    const QBrush brush(palette().color(QPalette::Highlight));
    return QIcon(new VectorIconEngine([brush](QPainter &painter, const QRectF &rect) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(brush);
        painter.drawEllipse(rect.center(), rect.width() * 0.25, rect.height() * 0.25);
    }));
}

} // namespace okapi
