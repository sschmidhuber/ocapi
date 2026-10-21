#include "ui/settingsdialog.h"

#include "core/backendregistry.h"
#include "core/mcpmanager.h"
#include "core/mcpregistry.h"
#include "core/sttregistry.h"
#include "ui/backendpage.h"
#include "ui/mcppage.h"
#include "ui/sttpage.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QListWidget>
#include <QSplitter>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace ocapi {

SettingsDialog::SettingsDialog(BackendRegistry *backendRegistry, McpRegistry *mcpRegistry,
                               McpManager *mcpManager, SttRegistry *sttRegistry, QWidget *parent)
    : QDialog(parent) {
    setWindowTitle(tr("Settings"));
    resize(880, 580);

    auto *splitter = new QSplitter(this);
    m_pages = new QListWidget(splitter);
    m_pages->setMinimumWidth(160);
    m_pages->setMaximumWidth(220);

    m_stack = new QStackedWidget(splitter);

    addPage(tr("Model backends"), QStringLiteral("backends"),
            new BackendPage(backendRegistry, m_stack));
    addPage(tr("MCP servers"), QStringLiteral("mcp"),
            new McpPage(mcpRegistry, mcpManager, m_stack));
    addPage(tr("Speech-to-text"), QStringLiteral("stt"), new SttPage(sttRegistry, m_stack));

    splitter->addWidget(m_pages);
    splitter->addWidget(m_stack);
    splitter->setStretchFactor(1, 1);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(splitter);
    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    layout->addWidget(buttonBox);

    connect(m_pages, &QListWidget::currentRowChanged, this,
            [this](int row) { m_stack->setCurrentIndex(row); });
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    m_pages->setCurrentRow(0);
}

void SettingsDialog::selectPage(const QString &pageId) {
    for (int i = 0; i < m_pages->count(); ++i) {
        if (m_pages->item(i)->data(Qt::UserRole + 1).toString() == pageId) {
            m_pages->setCurrentRow(i);
            m_stack->setCurrentIndex(i);
            return;
        }
    }
}

SettingsDialog::~SettingsDialog() = default;

void SettingsDialog::addPage(const QString &title, const QString &pageId, QWidget *page) {
    m_stack->addWidget(page);
    auto *item = new QListWidgetItem(title, m_pages);
    item->setData(Qt::UserRole, m_stack->count() - 1);
    item->setData(Qt::UserRole + 1, pageId);
}

} // namespace ocapi
