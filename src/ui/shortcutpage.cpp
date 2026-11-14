#include "ui/shortcutpage.h"
#include "ui_shortcutpage.h"

#include "core/shortcutregistry.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTableWidget>
#include <QVBoxLayout>

namespace okapi {

ShortcutPage::~ShortcutPage() = default;

ShortcutPage::ShortcutPage(ShortcutRegistry *registry, QWidget *parent)
    : QWidget(parent), m_registry(registry),
      m_ui(std::make_unique<Ui::m_ui = std::make_unique<Ui::ShortcutPage>();>()) {
    m_ui->setupUi(this);
    m_search = m_ui->search;
    m_table = m_ui->table;
    m_editButton = m_ui->editButton;
    m_resetButton = m_ui->resetButton;

    connect(m_search, &QLineEdit::textChanged, this, &ShortcutPage::onSearchChanged);
    connect(m_editButton, &QPushButton::clicked, this, &ShortcutPage::onEditShortcut);
    connect(m_resetButton, &QPushButton::clicked, this, &ShortcutPage::onResetDefault);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, &ShortcutPage::onCellDoubleClicked);
    connect(m_registry, &ShortcutRegistry::changed, this, &ShortcutPage::onRegistryChanged);

    rebuildTable();
}

void ShortcutPage::rebuildTable() {
    m_table->setRowCount(0);
    const auto actions = ShortcutRegistry::actions();
    m_table->setRowCount(static_cast<int>(actions.size()));
    for (int row = 0; row < static_cast<int>(actions.size()); ++row) {
        const ShortcutAction &action = actions.at(row);
        auto *actionItem = new QTableWidgetItem(action.description);
        actionItem->setData(Qt::UserRole, action.id);
        actionItem->setFlags(actionItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(row, 0, actionItem);

        const QKeySequence sequence = m_registry->sequence(action.id);
        auto *shortcutItem = new QTableWidgetItem(
            sequence.isEmpty() ? tr("None") : sequence.toString(QKeySequence::NativeText));
        shortcutItem->setFlags(shortcutItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(row, 1, shortcutItem);
    }
    // Preselect the first row so "Edit Shortcut..." works right away.
    if (m_table->rowCount() > 0)
        m_table->selectRow(0);
    applyFilter();
}

void ShortcutPage::applyFilter() {
    const QString needle = m_search->text().trimmed();
    for (int row = 0; row < m_table->rowCount(); ++row) {
        const QTableWidgetItem *item = m_table->item(row, 0);
        const bool matches =
            needle.isEmpty() || (item && item->text().contains(needle, Qt::CaseInsensitive));
        m_table->setRowHidden(row, !matches);
    }
}

void ShortcutPage::onSearchChanged(const QString &text) {
    Q_UNUSED(text)
    applyFilter();
}

void ShortcutPage::onCellDoubleClicked(int row, int column) {
    Q_UNUSED(column)
    captureAndApply(row);
}

void ShortcutPage::onEditShortcut() {
    const int row = m_table->currentRow();
    if (row >= 0)
        captureAndApply(row);
}

void ShortcutPage::onResetDefault() {
    const int row = m_table->currentRow();
    if (row >= 0) {
        const QTableWidgetItem *item = m_table->item(row, 0);
        if (item)
            m_registry->resetToDefault(item->data(Qt::UserRole).toString());
    } else {
        // No row selected: restore the defaults of all actions.
        const auto actions = ShortcutRegistry::actions();
        for (const auto &action : actions)
            m_registry->resetToDefault(action.id);
    }
    persist();
}

void ShortcutPage::onRegistryChanged() {
    rebuildTable();
}

bool ShortcutPage::captureAndApply(int row) {
    const QTableWidgetItem *item = m_table->item(row, 0);
    if (!item)
        return false;
    const QString actionId = item->data(Qt::UserRole).toString();
    const QString description = item->text();

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Set Shortcut"));
    auto *dialogLayout = new QVBoxLayout(&dialog);
    auto *label = new QLabel(tr("Press the new shortcut for \"%1\".\n"
                                "Clear the field to remove the shortcut.")
                                 .arg(description),
                             &dialog);
    label->setWordWrap(true);
    auto *edit = new QKeySequenceEdit(m_registry->sequence(actionId), &dialog);
    dialogLayout->addWidget(label);
    dialogLayout->addWidget(edit);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    dialogLayout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted)
        return false;

    const QKeySequence sequence = edit->keySequence();
    // A bare modifier (Ctrl, Shift, ...) is not a usable shortcut.
    if (sequence.count() == 1 &&
        (sequence[0].toCombined() & ~static_cast<quint32>(Qt::KeyboardModifierMask)) == 0) {
        QMessageBox::warning(this, tr("Invalid Shortcut"),
                             tr("A modifier key alone cannot be used as a shortcut."));
        return false;
    }
    const QString conflict = m_registry->conflictingAction(actionId, sequence);
    if (!conflict.isEmpty()) {
        QMessageBox::warning(this, tr("Shortcut Conflict"),
                             tr("The shortcut %1 is already used by \"%2\".")
                                 .arg(sequence.toString(QKeySequence::NativeText), conflict));
        return false;
    }

    m_registry->setSequence(actionId, sequence);
    persist();
    return true;
}

void ShortcutPage::persist() {
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(configDir);
    QSettings settings(configDir + QStringLiteral("/okapi.conf"), QSettings::IniFormat);
    m_registry->save(settings);
}

} // namespace okapi
