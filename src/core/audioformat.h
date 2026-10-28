#pragma once

#include <QByteArray>
#include <QString>

namespace ocapi {

// Helpers to detect and parse the audio formats produced by TTS services so
// they can be handed to QAudioDecoder: MP3 (OpenAI-compatible services) and
// WAV (Piper). Pure functions, no Qt object — unit-testable in isolation.
class AudioFormat {
public:
    enum class Kind {
        Unknown,
        Mp3,
        Wav,
    };

    // Detects the container format from the leading bytes.
    [[nodiscard]] static Kind detect(const QByteArray &data);
    // Human-readable MIME type for the detected format.
    [[nodiscard]] static QString mimeType(Kind kind);
    // Extracts the WAV fmt chunk parameters (sample rate, channels, bits).
    // Returns false when the data is not a parseable WAV file.
    [[nodiscard]] static bool wavInfo(const QByteArray &data, int *sampleRate, int *channels,
                                      int *bitsPerSample);
    // Returns the PCM data chunk of a WAV file, or an empty array.
    [[nodiscard]] static QByteArray wavPcmData(const QByteArray &data);

    // Sample formats a decoded audio buffer can carry (mirrors
    // QAudioFormat::SampleFormat for the formats decoders emit).
    enum class SampleFormat { Int16, Int32, Float, Unknown };

    // Converts interleaved PCM samples of \p format with \p channels into
    // 16-bit PCM. Int16 input is copied unchanged. Returns an empty array
    // for unsupported formats.
    [[nodiscard]] static QByteArray toInt16Pcm(const void *samples, qsizetype frameCount,
                                               int channels, SampleFormat format);
};

} // namespace ocapi
