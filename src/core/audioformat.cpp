#include "core/audioformat.h"

#include <QtEndian>

namespace ocapi {

namespace {

quint16 readUint16(const QByteArray &data, qsizetype offset) {
    return qFromLittleEndian<quint16>(data.constData() + offset);
}

quint32 readUint32(const QByteArray &data, qsizetype offset) {
    return qFromLittleEndian<quint32>(data.constData() + offset);
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
    while (offset + 8 <= data.size()) {
        const QByteArray chunkId = data.mid(offset, 4);
        const quint32 chunkSize = readUint32(data, offset + 4);
        if (chunkId == "fmt " && offset + 8 + 16 <= data.size()) {
            if (channels)
                *channels = readUint16(data, offset + 10);
            if (sampleRate)
                *sampleRate = static_cast<int>(readUint32(data, offset + 12));
            if (bitsPerSample)
                *bitsPerSample = readUint16(data, offset + 22);
            return true;
        }
        offset += 8 + chunkSize + (chunkSize % 2); // chunks are word-aligned
    }
    return false;
}

QByteArray AudioFormat::wavPcmData(const QByteArray &data) {
    if (detect(data) != Kind::Wav)
        return {};
    qsizetype offset = 12;
    while (offset + 8 <= data.size()) {
        const QByteArray chunkId = data.mid(offset, 4);
        const quint32 chunkSize = readUint32(data, offset + 4);
        if (chunkId == "data") {
            const qsizetype available = qMin<qsizetype>(chunkSize, data.size() - offset - 8);
            return data.mid(offset + 8, available);
        }
        offset += 8 + chunkSize + (chunkSize % 2);
    }
    return {};
}

} // namespace ocapi
