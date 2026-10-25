#include "ui/settingsdialog.h"

#include "core/backendregistry.h"
#include "core/mcpmanager.h"
#include "core/mcpregistry.h"
#include "core/sttregistry.h"
#include "core/ttsregistry.h"
#include "ui/backendpage.h"
#include "ui/mcppage.h"
#include "ui/sttpage.h"
#include "ui/ttspage.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QListWidget>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
#include <QVBoxLayout>

namespace ocapi {

SettingsDialog::SettingsDialog(BackendRegistry *backendRegistry, McpRegistry *mcpRegistry,
                               McpManager *mcpManager, SttRegistry *sttRegistry,
                               TtsRegistry *ttsRegistry, QWidget *parent)
    : QDialog(parent) {
    setWindowTitle(tr("Settings"));
    resize(880, 580);

    auto *splitter = new QSplitter(this);
    m_categories = new QListWidget(splitter);
    m_categories->setMinimumWidth(160);
    m_categories->setMaximumWidth(220);

    m_stack = new QStackedWidget(splitter);

    addCategory(tr("Backend"), QStringLiteral("backend"));
    addTab(tr("LLM"), QStringLiteral("llm"), new BackendPage(backendRegistry, m_stack));
    addTab(tr("MCP"), QStringLiteral("mcp"), new McpPage(mcpRegistry, mcpManager, m_stack));

    addCategory(tr("Speech"), QStringLiteral("speech"));
    addTab(tr("Speech-to-text"), QStringLiteral("stt"), new SttPage(sttRegistry, m_stack));
    addTab(tr("Text-to-speech"), QStringLiteral("tts"), new TtsPage(ttsRegistry, m_stack));

    splitter->addWidget(m_categories);
    splitter->addWidget(m_stack);
    splitter->setStretchFactor(1, 1);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(splitter);
    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    layout->addWidget(buttonBox);

    connect(m_categories, &QListWidget::currentRowChanged, this,
            [this](int row) { m_stack->setCurrentIndex(row); });
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    m_categories->setCurrentRow(0);
}

void SettingsDialog::selectPage(const QString &pageId) {
    const QString category = pageId.section(u'/', 0, 0);
    const QString tab = pageId.section(u'/', 1);

    for (int i = 0; i < m_categories->count(); ++i) {
        if (m_categories->item(i)->data(Qt::UserRole + 1).toString() == category) {
            m_categories->setCurrentRow(i);
            m_stack->setCurrentIndex(i);
            break;
        }
    }

    const int tabIndex = m_tabIds.indexOf(tab);
    if (tabIndex >= 0) {
        const int categoryRow = m_categories->currentRow();
        if (categoryRow >= 0 && categoryRow < m_tabWidgets.size()) {
            const int offset = m_tabOffsets.value(categoryRow, 0);
            m_tabWidgets.at(categoryRow)->setCurrentIndex(tabIndex - offset);
        }
    }
}

SettingsDialog::~SettingsDialog() = default;

void SettingsDialog::addCategory(const QString &title, const QString &categoryId) {
    auto *tabs = new QTabWidget(m_stack);
    tabs->setDocumentMode(true);
    m_stack->addWidget(tabs);
    m_tabWidgets.append(tabs);
    m_tabOffsets.append(m_tabIds.count());

    auto *item = new QListWidgetItem(title, m_categories);
    item->setData(Qt::UserRole, m_stack->count() - 1);
    item->setData(Qt::UserRole + 1, categoryId);
}

void SettingsDialog::addTab(const QString &title, const QString &tabId, QWidget *page) {
    const int categoryRow = m_categories->count() - 1;
    if (categoryRow < 0 || categoryRow >= m_tabWidgets.size())
        return;

    m_tabWidgets.at(categoryRow)->addTab(page, title);
    m_tabIds.append(tabId);
}

} // namespace ocapi
