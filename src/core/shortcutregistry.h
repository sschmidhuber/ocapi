#pragma once

#include <QHash>
#include <QKeySequence>
#include <QObject>
#include <QVector>

class QSettings;

namespace okapi {

// One configurable keyboard shortcut: a stable id, the user-visible action
// description and the default key sequence. windowScope=false marks
// shortcuts that only fire inside a specific widget (e.g. the composer's
// prompt-template trigger), not on the whole window.
struct ShortcutAction {
    QString id;
    QString description;
    QKeySequence defaultSequence;
    bool windowScope = true;
};

// Registry of the app's keyboard shortcuts. The catalog of actions is fixed;
// each action's sequence can be overridden (an empty override disables the
// shortcut) and the overrides are persisted in the application settings under
// the "shortcuts" group. Emits changed() whenever an override changes so the
// main window can re-create its QShortcut objects.
class ShortcutRegistry : public QObject {
    Q_OBJECT

public:
    explicit ShortcutRegistry(QObject *parent = nullptr);

    // The actions that have a keyboard shortcut, in display order.
    [[nodiscard]] static QVector<ShortcutAction> actions();

    // The configured sequence of an action, or its default when no override
    // is stored. Unknown ids yield an empty sequence.
    [[nodiscard]] QKeySequence sequence(const QString &actionId) const;
    // The factory default of an action; empty for unknown ids.
    [[nodiscard]] static QKeySequence defaultSequence(const QString &actionId);

    // Overrides the sequence of an action (an empty sequence removes the
    // shortcut) and emits changed(). Unknown ids are ignored. Does not
    // persist; call save() for that.
    void setSequence(const QString &actionId, const QKeySequence &sequence);
    // Removes the override of an action, restoring the default.
    void resetToDefault(const QString &actionId);
    // Returns the description of the other action currently using the given
    // sequence, or an empty string when the sequence is free (or empty).
    [[nodiscard]] QString conflictingAction(const QString &actionId,
                                            const QKeySequence &sequence) const;

    void load(QSettings &settings);
    void save(QSettings &settings) const;

signals:
    void changed();

private:
    QHash<QString, QKeySequence> m_overrides;
};

} // namespace okapi
