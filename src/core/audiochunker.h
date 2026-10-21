#pragma once

#include <QByteArray>

namespace ocapi {

// Accumulates raw PCM samples (16-bit, mono, sampleRate) and pops complete
// WAV blobs once a minimum chunk duration has been captured. Used to feed a
// speech-to-text service incrementally so that already-spoken words are
// transcribed while the user keeps talking.
class AudioChunker {
public:
    explicit AudioChunker(int sampleRate = 16000, int chunkDurationMs = 2500);

    void setChunkDurationMs(int chunkDurationMs);
    [[nodiscard]] int chunkDurationMs() const { return m_chunkDurationMs; }

    void setSampleRate(int sampleRate);
    [[nodiscard]] int sampleRate() const { return m_sampleRate; }

    // Returns complete WAV blobs encoded from the buffered PCM samples.
    QVector<QByteArray> feed(const QByteArray &pcmSamples);
    // Flushes the remaining samples into a final (possibly shorter) blob.
    [[nodiscard]] QByteArray flush();
    void reset();

    [[nodiscard]] qint64 bufferedMs() const;
    [[nodiscard]] int chunkSizeBytes() const;

    static QByteArray wavFromPcm(const QByteArray &pcmSamples, int sampleRate);

private:
    [[nodiscard]] qint64 bytesToMs(qsizetype bytes) const;

    int m_sampleRate;
    int m_chunkDurationMs;
    QByteArray m_buffer;
};

} // namespace ocapi
