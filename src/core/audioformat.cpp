#include "core/audioformat.h"

#include <QtEndian>

#include <algorithm>
#include <cstring>

namespace ocapi {

namespace {

quint16 readUint16(const QByteArray &data, qsizetype offset) {
    const auto bytes = data.mid(offset, 2);
    return qFromLittleEndian<quint16>(bytes.constData());
}

quint32 readUint32(const QByteArray &data, qsizetype offset) {
    const auto bytes = data.mid(offset, 4);
    return qFromLittleEndian<quint32>(bytes.constData());
}

// One RIFF chunk: its ID and declared size.
struct RiffChunk {
    QByteArray id;
    quint32 size = 0;
};

// Reads the chunk header at \p offset. Returns false when fewer than 8 bytes
// remain (no complete header).
bool readChunkHeader(const QByteArray &data, qsizetype offset, RiffChunk &chunk) {
    if (offset + 8 > data.size())
        return false;
    chunk.id = data.mid(offset, 4);
    chunk.size = readUint32(data, offset + 4);
    return true;
}

} // namespace

AudioFormat::Kind AudioFormat::detect(const QByteArray &data) {
    if (data.size() >= 3 && static_cast<unsigned char>(data.at(0)) == 0xFF &&
        (static_cast<unsigned char>(data.at(1)) & 0xE0) == 0xE0)
        return Kind::Mp3; // MPEG audio frame sync
    if (data.startsWith("ID3"))
        return Kind::Mp3; // MP3 with an ID3v2 tag
    if (data.startsWith("RIFF") && data.size() >= 12 && data.mid(8, 4) == "WAVE")
        return Kind::Wav;
    if (data.startsWith("OggS"))
        return Kind::Unknown; // Ogg containers are not handled
    return Kind::Unknown;
}

QString AudioFormat::mimeType(Kind kind) {
    switch (kind) {
    case Kind::Mp3:
        return QStringLiteral("audio/mpeg");
    case Kind::Wav:
        return QStringLiteral("audio/wav");
    case Kind::Unknown:
        return QStringLiteral("application/octet-stream");
    }
    return QStringLiteral("application/octet-stream");
}

bool AudioFormat::wavInfo(const QByteArray &data, int *sampleRate, int *channels,
                          int *bitsPerSample) {
    if (detect(data) != Kind::Wav)
        return false;
    // Walk the RIFF chunks to find "fmt ".
    qsizetype offset = 12;
    RiffChunk chunk;
    while (readChunkHeader(data, offset, chunk)) {
        if (chunk.id == "fmt " && offset + 8 + 16 <= data.size()) {
            if (channels)
                *channels = readUint16(data, offset + 10);
            if (sampleRate)
                *sampleRate = static_cast<int>(readUint32(data, offset + 12));
            if (bitsPerSample)
                *bitsPerSample = readUint16(data, offset + 22);
            return true;
        }
        offset += 8 + chunk.size + (chunk.size % 2); // chunks are word-aligned
    }
    return false;
}

QByteArray AudioFormat::wavPcmData(const QByteArray &data) {
    if (detect(data) != Kind::Wav)
        return {};
    qsizetype offset = 12;
    RiffChunk chunk;
    while (readChunkHeader(data, offset, chunk)) {
        if (chunk.id == "data") {
            const qsizetype available = qMin<qsizetype>(chunk.size, data.size() - offset - 8);
            return data.mid(offset + 8, available);
        }
        offset += 8 + chunk.size + (chunk.size % 2);
    }
    return {};
}

QByteArray AudioFormat::toInt16Pcm(const void *samples, qsizetype frameCount, int channels,
                                   SampleFormat format) {
    if (frameCount <= 0 || channels <= 0 || samples == nullptr)
        return {};
    const qsizetype count = frameCount * channels;
    QByteArray out;
    out.resize(count * qsizetype(sizeof(qint16)));
    auto *destination = reinterpret_cast<qint16 *>(out.data());
    switch (format) {
    case SampleFormat::Int16:
        std::memcpy(destination, samples, size_t(count * sizeof(qint16)));
        break;
    case SampleFormat::Int32: {
        const auto *src = static_cast<const qint32 *>(samples);
        for (qsizetype i = 0; i < count; ++i)
            destination[i] = qint16(src[i] >> 16);
        break;
    }
    case SampleFormat::Float: {
        const auto *src = static_cast<const float *>(samples);
        for (qsizetype i = 0; i < count; ++i)
            destination[i] = qint16(std::clamp(src[i], -1.0f, 1.0f) * 32767.0f);
        break;
    }
    case SampleFormat::Unknown:
        return {};
    }
    return out;
}

} // namespace ocapi
