#pragma once

#include "core/sttconfig.h"

#include <QObject>
#include <QString>

class QNetworkAccessManager;

namespace ocapi {

// Client for speech-to-text services: posts WAV audio to the configured
// transcription endpoint and reports the transcribed text. Chunked requests
// are marked "final=false" so intermediate results can be merged live; the
// service merges incremental results and emits intermediateTranscription for
// partial updates and transcriptionUpdated once text is committed.
class SttClient : public QObject {
    Q_OBJECT

public:
    explicit SttClient(SttServiceConfig config, QObject *parent = nullptr);
    ~SttClient() override;

    [[nodiscard]] const SttServiceConfig &config() const { return m_config; }

    // Starts a new transcription session (clears accumulated text).
    void start();
    // Sends one WAV blob; emitReady controls whether intermediate text is
    // emitted immediately (live transcription) or only when marked final.
    void transcribeChunk(const QByteArray &wav, bool emitReady = true);
    // Flushes buffered chunks and emits transcriptionUpdated once all
    // pending requests have completed; emits error when any request failed.
    void finish();
    void cancel();

signals:
    void transcriptionUpdated(const QString &text);
    void intermediateTranscription(const QString &text);
    void error(const QString &message);
    void finished();

private:
    void onChunkDone(const QString &text, bool live);
    void maybeFlush();

    SttServiceConfig m_config;
    QNetworkAccessManager *m_nam;
    QString m_accumulated;
    int m_pendingRequests = 0;
    bool m_finishing = false;
    bool m_failed = false;
};

} // namespace ocapi
