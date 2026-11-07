#include "core/shortcutregistry.h"

#include <QSettings>

namespace ocapi {

namespace {

constexpr auto kGroup = "shortcuts";

bool isKnownAction(const QString &actionId) {
    const auto all = ShortcutRegistry::actions();
    for (const auto &action : all) {
        if (action.id == actionId)
            return true;
    }
    return false;
}

} // namespace

ShortcutRegistry::ShortcutRegistry(QObject *parent) : QObject(parent) {}

QVector<ShortcutAction> ShortcutRegistry::actions() {
    return {
        {QStringLiteral("newChat"), tr("New chat"), QKeySequence(QStringLiteral("Ctrl+N"))},
        {QStringLiteral("toggleSidebar"), tr("Show/hide chat history"), QKeySequence(Qt::Key_F9)},
        {QStringLiteral("nextChat"), tr("Next chat"), QKeySequence(QStringLiteral("Ctrl+Tab"))},
        {QStringLiteral("previousChat"), tr("Previous chat"),
         QKeySequence(QStringLiteral("Ctrl+Shift+Tab"))},
        {QStringLiteral("dictate"), tr("Dictate message"), QKeySequence(QStringLiteral("Ctrl+."))},
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

} // namespace ocapi
