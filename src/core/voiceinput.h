#pragma once

#include "core/audiochunker.h"

#include <memory>

#include <QObject>
#include <QString>

namespace ocapi {

class AudioRecorder;
class SttClient;
class SttStreamClient;
struct SttServiceConfig;

// Glues audio capture and transcription together: while active, recorded
// audio is chunked and sent to the speech-to-text service; already spoken
// words appear live via intermediateTranscription. On stop() the remaining
// audio is flushed and transcriptionUpdated delivers the final text.
class VoiceInput : public QObject {
    Q_OBJECT

public:
    explicit VoiceInput(QObject *parent = nullptr);

    void setService(const SttServiceConfig &config);
    void setInputDevice(const QString &deviceId);

    [[nodiscard]] bool isActive() const;

    bool start();
    void stop();
    void toggle();

signals:
    void started();
    void stopped();
    // Intermediate text while the user is still speaking.
    void intermediateTranscription(const QString &text);
    // Final text once all pending requests completed after stop().
    void transcriptionUpdated(const QString &text);
    void error(const QString &message);

private:
    void onSamplesCaptured(const QByteArray &pcm);
    void connectStreamSignals();

    AudioChunker m_chunker;
    AudioRecorder *m_recorder = nullptr;
    SttClient *m_client = nullptr;             // chunked HTTP transcription
    SttStreamClient *m_streamClient = nullptr; // WebSocket streaming transcription
    bool m_active = false;
};

} // namespace ocapi
