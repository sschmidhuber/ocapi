#include "core/audiorecorder.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSource>
#include <QMediaDevices>

#include <algorithm>

namespace ocapi {

namespace {

QAudioFormat captureFormat(int sampleRate) {
    QAudioFormat format;
    format.setSampleRate(sampleRate);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);
    return format;
}

} // namespace

QStringList AudioRecorder::availableInputDevices() {
    QStringList devices;
    for (const QAudioDevice &device : QMediaDevices::audioInputs())
        devices.push_back(device.id());
    return devices;
}

QStringList AudioRecorder::availableInputDeviceNames() {
    QStringList names;
    for (const QAudioDevice &device : QMediaDevices::audioInputs())
        names.push_back(device.description());
    return names;
}

AudioRecorder::AudioRecorder(QObject *parent) : QObject(parent) {}

AudioRecorder::~AudioRecorder() {
    stop();
    delete m_devices;
}

void AudioRecorder::setDevice(const QString &deviceId) {
    if (m_deviceId == deviceId)
        return;
    const bool wasActive = isActive();
    stop();
    m_deviceId = deviceId;
    if (wasActive)
        start();
}

bool AudioRecorder::start() {
    if (isActive())
        return true;

    m_devices = new QMediaDevices(this);

    QAudioDevice device;
    const auto inputs = QMediaDevices::audioInputs();
    if (m_deviceId.isEmpty()) {
        device = QMediaDevices::defaultAudioInput();
    } else {
        for (const QAudioDevice &candidate : inputs) {
            if (candidate.id() == m_deviceId) {
                device = candidate;
                break;
            }
        }
        if (device.isNull())
            device = QMediaDevices::defaultAudioInput();
    }
    if (device.isNull())
        return false;

    QAudioFormat format = captureFormat(m_sampleRate);
    if (!device.isFormatSupported(format))
        format = device.preferredFormat();
    if (format.sampleRate() <= 0)
        return false;
    m_sampleRate = format.sampleRate();

    m_source = std::make_unique<QAudioSource>(device, format, this);
    m_io = m_source->start();
    if (!m_io)
        return false;

    connect(m_io, &QIODevice::readyRead, this, [this]() { emit samplesCaptured(m_io->readAll()); });

    m_active = true;
    emit activeChanged();
    return true;
}

void AudioRecorder::stop() {
    if (m_source) {
        m_source->stop();
        m_source.reset();
    }
    m_io = nullptr;
    delete m_devices;
    m_devices = nullptr;
    if (m_active) {
        m_active = false;
        emit activeChanged();
    }
}

bool AudioRecorder::isActive() const {
    return m_active;
}

} // namespace ocapi
