#include "ui/userinterfacepage.h"

#include "core/uisettings.h"

#include <QCheckBox>
#include <QVBoxLayout>

namespace okapi {

UserInterfacePage::UserInterfacePage(UiSettings *uiSettings, QWidget *parent)
    : QWidget(parent), m_uiSettings(uiSettings) {
    auto *layout = new QVBoxLayout(this);

    m_showThroughputBox = new QCheckBox(tr("Show throughput (t/s)"), this);
    m_showThroughputBox->setChecked(m_uiSettings->showThroughput());
    m_showThroughputBox->setToolTip(
        tr("Show the output token throughput next to the copy button of each "
           "assistant message"));
    layout->addWidget(m_showThroughputBox);
    layout->addStretch(1);

    connect(m_showThroughputBox, &QCheckBox::toggled, this,
            [this](bool enabled) { m_uiSettings->setShowThroughput(enabled); });
}

} // namespace okapi
