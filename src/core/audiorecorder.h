#pragma once

#include <QObject>
#include <QString>

#include <memory>

class QAudioSource;
class QIODevice;
class QMediaDevices;

namespace ocapi {

// Captures audio from the configured input device as 16-bit mono PCM and
// emits the samples in small blocks. The device (or the system default when
// the setting is empty) is selected via setDevice().
class AudioRecorder : public QObject {
    Q_OBJECT

public:
    explicit AudioRecorder(QObject *parent = nullptr);
    ~AudioRecorder() override;

    [[nodiscard]] static QStringList availableInputDevices();
    // Human-readable device descriptions for the settings page.
    [[nodiscard]] static QStringList availableInputDeviceNames();

    void setDevice(const QString &deviceId);

    bool start();
    void stop();
    [[nodiscard]] bool isActive() const;
    [[nodiscard]] int sampleRate() const { return m_sampleRate; }

signals:
    // Raw 16-bit mono PCM samples at sampleRate().
    void samplesCaptured(const QByteArray &pcm);
    void activeChanged();

private:
    QString m_deviceId;
    QMediaDevices *m_devices = nullptr;
    std::unique_ptr<QAudioSource> m_source;
    QIODevice *m_io = nullptr;
    int m_sampleRate = 16000;
    bool m_active = false;
};

} // namespace ocapi
