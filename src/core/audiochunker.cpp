#include "core/audiochunker.h"

#include <QVector>

#include <algorithm>

namespace ocapi {

namespace {

constexpr int kBytesPerSample = 2;
constexpr int kBytesPerFrame = kBytesPerSample; // mono
constexpr int kMinChunkMs = 250;

void appendUint16(QByteArray &buffer, quint16 value) {
    buffer.append(static_cast<char>(value & 0xFF));
    buffer.append(static_cast<char>((value >> 8) & 0xFF));
}

void appendUint32(QByteArray &buffer, quint32 value) {
    for (int i = 0; i < 4; ++i)
        buffer.append(static_cast<char>((value >> (8 * i)) & 0xFF));
}

} // namespace

AudioChunker::AudioChunker(int sampleRate, int chunkDurationMs)
    : m_sampleRate(sampleRate), m_chunkDurationMs(std::max(chunkDurationMs, kMinChunkMs)) {}

void AudioChunker::setSampleRate(int sampleRate) {
    if (m_sampleRate == sampleRate)
        return;
    reset();
    m_sampleRate = sampleRate;
}

QVector<QByteArray> AudioChunker::feed(const QByteArray &pcmSamples) {
    m_buffer.append(pcmSamples);
    QVector<QByteArray> chunks;
    const int chunkSize = chunkSizeBytes();
    while (m_buffer.size() >= chunkSize) {
        chunks.push_back(wavFromPcm(m_buffer.left(chunkSize), m_sampleRate));
        m_buffer.remove(0, chunkSize);
    }
    return chunks;
}

QByteArray AudioChunker::flush() {
    const QByteArray pcm = m_buffer;
    m_buffer.clear();
    if (pcm.isEmpty())
        return {};
    return wavFromPcm(pcm, m_sampleRate);
}

void AudioChunker::reset() {
    m_buffer.clear();
}

qint64 AudioChunker::bufferedMs() const {
    return bytesToMs(m_buffer.size());
}

QByteArray AudioChunker::wavFromPcm(const QByteArray &pcmSamples, int sampleRate) {
    const quint32 dataSize = static_cast<quint32>(pcmSamples.size());
    const quint32 byteRate = static_cast<quint32>(sampleRate * kBytesPerFrame);

    QByteArray wav;
    wav.reserve(44 + static_cast<qsizetype>(dataSize));
    wav.append("RIFF", 4);
    appendUint32(wav, 36 + dataSize);
    wav.append("WAVE", 4);
    wav.append("fmt ", 4);
    appendUint32(wav, 16);
    appendUint16(wav, 1); // PCM
    appendUint16(wav, 1); // mono
    appendUint32(wav, static_cast<quint32>(sampleRate));
    appendUint32(wav, byteRate);
    appendUint16(wav, static_cast<quint16>(kBytesPerFrame));
    appendUint16(wav, 16); // bits per sample
    wav.append("data", 4);
    appendUint32(wav, dataSize);
    wav.append(pcmSamples);
    return wav;
}

int AudioChunker::chunkSizeBytes() const {
    return static_cast<int>(static_cast<qint64>(m_sampleRate) * kBytesPerFrame * m_chunkDurationMs /
                            1000);
}

qint64 AudioChunker::bytesToMs(qsizetype bytes) const {
    return static_cast<qint64>(bytes) * 1000 / (m_sampleRate * kBytesPerFrame);
}

} // namespace ocapi
