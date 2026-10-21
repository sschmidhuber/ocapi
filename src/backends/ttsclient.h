#pragma once

#include "core/ttsconfig.h"

#include <QHash>
#include <QObject>
#include <QPair>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

namespace ocapi {

// Client for text-to-speech services: posts the text to the configured
// speech endpoint and reports the returned audio bytes. Requests are queued
// internally so callers can enqueue the next sentence while the previous
// one is still being synthesized. Up to two requests run in parallel, but
// audioReady is always emitted in enqueue order (out-of-order replies are
// buffered), which keeps playback gapless. ElevenLabs uses its own path and
// xi-api-key header; Piper expects a GET with query parameters and answers
// with WAV; all other services speak the OpenAI-compatible
// POST /audio/speech JSON protocol with MP3 output.
class TtsClient : public QObject {
    Q_OBJECT

public:
    explicit TtsClient(TtsServiceConfig config, QObject *parent = nullptr);
    ~TtsClient() override;

    [[nodiscard]] const TtsServiceConfig &config() const { return m_config; }

    // Enqueues one synthesis request. Emits audioReady with the audio bytes
    // (and detected format) in enqueue order, or error on failure. Empty or
    // whitespace-only text is ignored.
    void synthesize(const QString &text);
    // Aborts all pending requests and clears the queue.
    void cancel();

signals:
    void audioReady(const QByteArray &audio, const QString &mimeType);
    void error(const QString &message);

private:
    void sendNext();
    void onReplyFinished(QNetworkReply *reply, int sequence);
    void flushCompleted();

    TtsServiceConfig m_config;
    QNetworkAccessManager *m_nam;
    struct PendingRequest {
        QString text;
        int sequence = 0;
    };
    QVector<PendingRequest> m_queue;
    QVector<PendingRequest> m_inFlight;
    // Completed replies waiting for earlier sequences, keyed by sequence.
    QHash<int, QPair<QByteArray, QString>> m_completed;
    int m_nextSequence = 0;
    int m_nextPlaySequence = 0;
    bool m_cancelled = false;
};

} // namespace ocapi
