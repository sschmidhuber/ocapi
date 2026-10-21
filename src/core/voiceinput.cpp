#include "core/voiceinput.h"

#include "backends/sttclient.h"
#include "core/audiorecorder.h"

namespace ocapi {

VoiceInput::VoiceInput(QObject *parent) : QObject(parent) {
    m_recorder = new AudioRecorder(this);
    connect(m_recorder, &AudioRecorder::samplesCaptured, this, &VoiceInput::onSamplesCaptured);
}

void VoiceInput::setService(const SttServiceConfig &config) {
    delete m_client;
    m_client = new SttClient(config, this);
    connect(m_client, &SttClient::transcriptionUpdated, this, &VoiceInput::transcriptionUpdated);
    connect(m_client, &SttClient::intermediateTranscription, this,
            &VoiceInput::intermediateTranscription);
    connect(m_client, &SttClient::error, this, &VoiceInput::error);
}

void VoiceInput::setInputDevice(const QString &deviceId) {
    m_recorder->setDevice(deviceId);
}

bool VoiceInput::isActive() const {
    return m_active;
}

bool VoiceInput::start() {
    if (m_active)
        return true;
    if (!m_client) {
        emit error(tr("No speech-to-text service configured."));
        return false;
    }
    if (!m_recorder->start()) {
        emit error(tr("Could not open the audio input device."));
        return false;
    }
    m_chunker.setSampleRate(m_recorder->sampleRate());
    m_client->start();
    m_active = true;
    emit started();
    return true;
}

void VoiceInput::stop() {
    if (!m_active)
        return;
    m_recorder->stop();
    const QByteArray tail = m_chunker.flush();
    if (!tail.isEmpty())
        m_client->transcribeChunk(tail, false);
    m_client->finish();
    m_active = false;
    emit stopped();
}

void VoiceInput::toggle() {
    if (m_active)
        stop();
    else
        start();
}

void VoiceInput::onSamplesCaptured(const QByteArray &pcm) {
    if (!m_active || !m_client)
        return;
    const auto chunks = m_chunker.feed(pcm);
    for (const auto &chunk : chunks)
        m_client->transcribeChunk(chunk);
}

} // namespace ocapi
