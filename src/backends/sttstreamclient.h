#pragma once

#include "core/sttconfig.h"

#include <QObject>
#include <QString>

class QWebSocket;

namespace ocapi {

// Streaming speech-to-text client for services with a WebSocket protocol
// (OpenAI Realtime transcription API, Mistral Voxtral Realtime). Sends raw
// 16-bit mono PCM audio as it is captured and emits transcript deltas while
// the user is still speaking. Compared to the chunked HTTP client
// (SttClient) there is no per-request round-trip: text appears with the
// provider's own streaming delay (typically well under one second).
class SttStreamClient : public QObject {
    Q_OBJECT

public:
    explicit SttStreamClient(SttServiceConfig config, QObject *parent = nullptr);
    ~SttStreamClient() override;

    [[nodiscard]] const SttServiceConfig &config() const { return m_config; }

    // Opens the WebSocket session and waits for the session handshake.
    // sampleRate is the rate of the PCM audio fed via sendAudio().
    bool start(int sampleRate);
    // Sends one block of raw 16-bit mono PCM samples.
    void sendAudio(const QByteArray &pcm);
    // Closes the audio turn and the session; emits finished() once the
    // provider confirmed the final transcript (or immediately on error).
    void stop();
    void abort();

signals:
    // Live text while the user is still speaking.
    void intermediateTranscription(const QString &text);
    // Final text once the turn completed.
    void transcriptionUpdated(const QString &text);
    void error(const QString &message);
    void finished();

private:
    void onTextMessageReceived(const QString &message);
    void onConnected();
    void onErrorOccurred();
    [[nodiscard]] QUrl sessionUrl() const;
    [[nodiscard]] QByteArray sessionUpdateJson(int sampleRate) const;
    void emitAccumulated();

    SttServiceConfig m_config;
    QWebSocket *m_socket = nullptr;
    QString m_accumulated;
    int m_sampleRate = 16000;
    bool m_closing = false;
    bool m_failed = false;
    // Mistral's session.update must wait for the session.created event.
    bool m_sessionCreated = false;
};

} // namespace ocapi
