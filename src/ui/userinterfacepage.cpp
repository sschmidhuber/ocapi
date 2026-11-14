#include "ui/userinterfacepage.h"
#include "ui_userinterfacepage.h"

#include "core/uisettings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>

namespace okapi {

UserInterfacePage::~UserInterfacePage() = default;

UserInterfacePage::UserInterfacePage(UiSettings *uiSettings, QWidget *parent)
    : QWidget(parent), m_uiSettings(uiSettings),
      m_ui(std::make_unique<Ui::m_ui = std::make_unique<Ui::UserInterfacePage>();>()) {
    m_ui->setupUi(this);
    m_themeCombo = m_ui->themeCombo;
    m_showThroughputBox = m_ui->showThroughputBox;
    m_themeCombo->addItem(tr("Auto (system)"),
                          QVariant::fromValue(static_cast<int>(UiSettings::Theme::Auto)));
    m_themeCombo->addItem(tr("Light"),
                          QVariant::fromValue(static_cast<int>(UiSettings::Theme::Light)));
    m_themeCombo->addItem(tr("Dark"),
                          QVariant::fromValue(static_cast<int>(UiSettings::Theme::Dark)));
    const int themeIndex =
        m_themeCombo->findData(QVariant::fromValue(static_cast<int>(m_uiSettings->theme())));
    m_themeCombo->setCurrentIndex(themeIndex < 0 ? 0 : themeIndex);
    m_showThroughputBox->setChecked(m_uiSettings->showThroughput());
    m_showThroughputBox->setToolTip(
        tr("Show the output token throughput next to the copy button of each "
           "assistant message"));

    connect(m_showThroughputBox, &QCheckBox::toggled, this,
            [this](bool enabled) { m_uiSettings->setShowThroughput(enabled); });
    connect(m_themeCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        m_uiSettings->setTheme(
            static_cast<UiSettings::Theme>(m_themeCombo->itemData(index).toInt()));
    });
}

} // namespace okapi
