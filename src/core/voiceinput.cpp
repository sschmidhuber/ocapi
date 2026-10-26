#include "core/voiceinput.h"

#include "backends/sttclient.h"
#include "backends/sttstreamclient.h"
#include "core/audiorecorder.h"

namespace ocapi {

VoiceInput::VoiceInput(QObject *parent) : QObject(parent) {
    m_recorder = new AudioRecorder(this);
    connect(m_recorder, &AudioRecorder::samplesCaptured, this, &VoiceInput::onSamplesCaptured);
}

void VoiceInput::setService(const SttServiceConfig &config) {
    delete m_client;
    delete m_streamClient;
    m_client = nullptr;
    m_streamClient = nullptr;
    if (config.isStreaming()) {
        // Streaming services speak a WebSocket protocol; their wss:// URL
        // must not go through the HTTP client ("unknown protocol" error).
        m_streamClient = new SttStreamClient(config, this);
        connect(m_streamClient, &SttStreamClient::transcriptionUpdated, this,
                &VoiceInput::transcriptionUpdated);
        connect(m_streamClient, &SttStreamClient::intermediateTranscription, this,
                &VoiceInput::intermediateTranscription);
        connect(m_streamClient, &SttStreamClient::error, this, &VoiceInput::error);
        return;
    }
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
    if (!m_client && !m_streamClient) {
        emit error(tr("No speech-to-text service configured."));
        return false;
    }
    if (!m_recorder->start()) {
        emit error(tr("Could not open the audio input device."));
        return false;
    }
    if (m_streamClient) {
        m_streamClient->start(m_recorder->sampleRate());
    } else {
        m_chunker.setSampleRate(m_recorder->sampleRate());
        m_chunker.setChunkDurationMs(m_client->config().effectiveChunkDurationMs());
        m_client->start();
    }
    m_active = true;
    emit started();
    return true;
}

void VoiceInput::stop() {
    if (!m_active)
        return;
    m_recorder->stop();
    if (m_streamClient) {
        m_streamClient->stop();
    } else {
        const QByteArray tail = m_chunker.flush();
        if (!tail.isEmpty())
            m_client->transcribeChunk(tail, false);
        m_client->finish();
    }
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
    if (!m_active)
        return;
    if (m_streamClient) {
        m_streamClient->sendAudio(pcm);
        return;
    }
    if (!m_client)
        return;
    const auto chunks = m_chunker.feed(pcm);
    for (const auto &chunk : chunks)
        m_client->transcribeChunk(chunk);
}

} // namespace ocapi
