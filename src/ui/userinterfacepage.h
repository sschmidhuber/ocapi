#pragma once

#include <QWidget>

class QCheckBox;

namespace ocapi {

class UiSettings;

// Settings page "User Interface" (Settings > General): general UI toggles
// such as showing the output token throughput (t/s) next to the copy button
// of assistant messages. Changes apply immediately and are persisted.
class UserInterfacePage : public QWidget {
    Q_OBJECT

public:
    explicit UserInterfacePage(UiSettings *uiSettings, QWidget *parent = nullptr);

private:
    UiSettings *m_uiSettings;
    QCheckBox *m_showThroughputBox = nullptr;
};

} // namespace ocapi
