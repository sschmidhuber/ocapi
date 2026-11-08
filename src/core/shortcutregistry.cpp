#include "core/shortcutregistry.h"

#include <QSettings>

#include <algorithm>

namespace okapi {

namespace {

constexpr auto kGroup = "shortcuts";

bool isKnownAction(const QString &actionId) {
    return std::ranges::any_of(
        ShortcutRegistry::actions(),
        [&actionId](const ShortcutAction &action) { return action.id == actionId; });
}

} // namespace

ShortcutRegistry::ShortcutRegistry(QObject *parent) : QObject(parent) {}

QVector<ShortcutAction> ShortcutRegistry::actions() {
    return {
        {.id = QStringLiteral("newChat"),
         .description = tr("New chat"),
         .defaultSequence = QKeySequence(QStringLiteral("Ctrl+N"))},
        {.id = QStringLiteral("toggleSidebar"),
         .description = tr("Show/hide chat history"),
         .defaultSequence = QKeySequence(Qt::Key_F9)},
        {.id = QStringLiteral("nextChat"),
         .description = tr("Next chat"),
         .defaultSequence = QKeySequence(QStringLiteral("Ctrl+Tab"))},
        {.id = QStringLiteral("previousChat"),
         .description = tr("Previous chat"),
         .defaultSequence = QKeySequence(QStringLiteral("Ctrl+Shift+Tab"))},
        {.id = QStringLiteral("dictate"),
         .description = tr("Dictate message"),
         .defaultSequence = QKeySequence(QStringLiteral("Ctrl+."))},
        {.id = QStringLiteral("promptTemplates"),
         .description = tr("Insert prompt template"),
         .defaultSequence = QKeySequence(QStringLiteral("/")),
         .windowScope = false},
    };
}

QKeySequence ShortcutRegistry::sequence(const QString &actionId) const {
    const auto it = m_overrides.constFind(actionId);
    if (it != m_overrides.constEnd())
        return it.value();
    return defaultSequence(actionId);
}

QKeySequence ShortcutRegistry::defaultSequence(const QString &actionId) {
    const auto all = actions();
    for (const auto &action : all) {
        if (action.id == actionId)
            return action.defaultSequence;
    }
    return {};
}

void ShortcutRegistry::setSequence(const QString &actionId, const QKeySequence &sequence) {
    if (!isKnownAction(actionId))
        return;
    if (sequence == defaultSequence(actionId))
        m_overrides.remove(actionId);
    else
        m_overrides.insert(actionId, sequence);
    emit changed();
}

void ShortcutRegistry::resetToDefault(const QString &actionId) {
    if (m_overrides.remove(actionId) > 0)
        emit changed();
}

QString ShortcutRegistry::conflictingAction(const QString &actionId,
                                            const QKeySequence &sequence) const {
    if (sequence.isEmpty())
        return {};
    const auto all = actions();
    for (const auto &action : all) {
        if (action.id != actionId && this->sequence(action.id) == sequence)
            return action.description;
    }
    return {};
}

void ShortcutRegistry::load(QSettings &settings) {
    m_overrides.clear();
    settings.beginGroup(QString::fromLatin1(kGroup));
    const auto keys = settings.childKeys();
    for (const auto &key : keys) {
        if (!isKnownAction(key))
            continue;
        // An empty stored value is a deliberate "shortcut disabled"
        // override, not a missing one, so keep it as an empty sequence.
        m_overrides.insert(key, QKeySequence(settings.value(key).toString()));
    }
    settings.endGroup();
    emit changed();
}

void ShortcutRegistry::save(QSettings &settings) const {
    settings.beginGroup(QString::fromLatin1(kGroup));
    settings.remove(QString());
    for (auto it = m_overrides.constBegin(); it != m_overrides.constEnd(); ++it)
        settings.setValue(it.key(), it.value().toString(QKeySequence::PortableText));
    settings.endGroup();
}

} // namespace okapi
