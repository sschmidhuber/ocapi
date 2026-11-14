#pragma once

#include <QWidget>
#include <memory>

namespace Ui {
class UserInterfacePage;
}

class QCheckBox;
class QComboBox;

namespace okapi {

class UiSettings;

// Settings page "User Interface" (Settings > General): general UI toggles
// such as showing the output token throughput (t/s) next to the copy button
// of assistant messages. Changes apply immediately and are persisted.
class UserInterfacePage : public QWidget {
    Q_OBJECT

public:
    ~UserInterfacePage() override;

    explicit UserInterfacePage(UiSettings *uiSettings, QWidget *parent = nullptr);

private:
    std::unique_ptr<Ui::UserInterfacePage> m_ui;
    UiSettings *m_uiSettings;
    QCheckBox *m_showThroughputBox = nullptr;
    QComboBox *m_themeCombo = nullptr;
};

} // namespace okapi
