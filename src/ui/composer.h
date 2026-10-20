#pragma once

#include <QPlainTextEdit>

class QToolButton;

namespace ocapi {

class Composer : public QPlainTextEdit
{
    Q_OBJECT

public:
    explicit Composer(QWidget* parent = nullptr);

    void setSendEnabled(bool enabled);
    void setBusy(bool busy);
    void focusInput();

signals:
    void sendRequested(const QString& text);
    void stopRequested();

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    bool m_busy = false;
};

} // namespace ocapi
