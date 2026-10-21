#pragma once

#include "core/sentencesplitter.h"
#include "core/ttsconfig.h"

#include <QByteArray>
#include <QObject>
#include <QQueue>
#include <QString>

class QAudioDecoder;
class QAudioSink;
class QBuffer;
class QIODevice;
class QMediaDevices;
class QTimer;

namespace ocapi {

class TtsClient;

// Speaks assistant messages aloud while they stream in. Incoming text is
// split into sentences; each complete sentence is synthesized by the
// configured TTS service and the decoded PCM is appended to a QAudioSink so
// playback starts as soon as the first sentence is ready. The player is
// driven by feedText() (accumulated message text) and endOfText(); stop()
// aborts synthesis and playback (e.g. when the user presses Stop).
class TtsPlayer : public QObject {
    Q_OBJECT

public:
    explicit TtsPlayer(QObject *parent = nullptr);
    ~TtsPlayer() override;

    // Output devices for the settings page (mirrors AudioRecorder's input
    // device enumeration).
    [[nodiscard]] static QStringList availableOutputDevices();
    [[nodiscard]] static QStringList availableOutputDeviceNames();

    void setService(const TtsServiceConfig &config);
    void setOutputDevice(const QString &deviceId);

    [[nodiscard]] bool isSpeaking() const;

    // Feeds the accumulated text of the currently streaming message. New
    // text since the last call is split into sentences and synthesized.
    void feedText(const QString &accumulatedText);
    // Called when the message finished streaming: flushes the trailing
    // partial sentence into synthesis.
    void endOfText();
    // Aborts synthesis and playback immediately.
    void stop();

signals:
    void speakingChanged(bool speaking);
    void error(const QString &message);

private:
    void enqueueSentence(const QString &sentence);
    void onAudioReady(const QByteArray &audio, const QString &mimeType);
    void onDecodeBufferReady();
    void onDecodeFinished();
    void updateSpeaking();
    void writePendingPcm();
    bool ensureSink(int sampleRate, int channels);
    void teardownSink();

    TtsServiceConfig m_config;
    TtsClient *m_client = nullptr;
    SentenceSplitter m_splitter;
    QString m_lastFedText;
    QString m_outputDeviceId;
    QMediaDevices *m_devices = nullptr;
    QAudioSink *m_sink = nullptr;
    QIODevice *m_sinkIo = nullptr;
    QAudioDecoder *m_decoder = nullptr;
    QBuffer *m_sourceBuffer = nullptr;
    QTimer *m_feedTimer = nullptr;
    QQueue<QByteArray> m_pendingPcm; // decoded PCM waiting for sink space
    QByteArray m_decodedPcm;         // PCM accumulated during one decode run
    bool m_speaking = false;
    bool m_endOfText = false;
    bool m_stopped = true;
};

} // namespace ocapi
