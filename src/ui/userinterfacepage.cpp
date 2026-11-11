#include "ui/userinterfacepage.h"

#include "core/uisettings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace okapi {

UserInterfacePage::UserInterfacePage(UiSettings *uiSettings, QWidget *parent)
    : QWidget(parent), m_uiSettings(uiSettings) {
    auto *layout = new QVBoxLayout(this);

    auto *themeForm = new QFormLayout;
    themeForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_themeCombo = new QComboBox(this);
    m_themeCombo->setObjectName(QStringLiteral("themeCombo"));
    m_themeCombo->addItem(tr("Auto (system)"),
                          QVariant::fromValue(static_cast<int>(UiSettings::Theme::Auto)));
    m_themeCombo->addItem(tr("Light"),
                          QVariant::fromValue(static_cast<int>(UiSettings::Theme::Light)));
    m_themeCombo->addItem(tr("Dark"),
                          QVariant::fromValue(static_cast<int>(UiSettings::Theme::Dark)));
    const int themeIndex =
        m_themeCombo->findData(QVariant::fromValue(static_cast<int>(m_uiSettings->theme())));
    m_themeCombo->setCurrentIndex(themeIndex < 0 ? 0 : themeIndex);
    themeForm->addRow(tr("Theme:"), m_themeCombo);
    layout->addLayout(themeForm);

    m_showThroughputBox = new QCheckBox(tr("Show throughput (t/s)"), this);
    m_showThroughputBox->setChecked(m_uiSettings->showThroughput());
    m_showThroughputBox->setToolTip(
        tr("Show the output token throughput next to the copy button of each "
           "assistant message"));
    layout->addWidget(m_showThroughputBox);
    layout->addStretch(1);

    connect(m_showThroughputBox, &QCheckBox::toggled, this,
            [this](bool enabled) { m_uiSettings->setShowThroughput(enabled); });
    connect(m_themeCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        m_uiSettings->setTheme(
            static_cast<UiSettings::Theme>(m_themeCombo->itemData(index).toInt()));
    });
}

} // namespace okapi
