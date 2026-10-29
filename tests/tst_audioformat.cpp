#include "core/audiochunker.h"
#include "core/audioformat.h"

#include <QByteArray>
#include <QtTest>

#include <array>

using namespace ocapi;

namespace {

// Reads the Int16 samples of a PCM byte array (little-endian host assumed,
// like the rest of the audio code).
[[nodiscard]] qint16 pcmSampleAt(const QByteArray &pcm, qsizetype index) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    const auto *samples = reinterpret_cast<const qint16 *>(pcm.constData());
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    return samples[index];
}

} // namespace

class TestAudioFormat : public QObject {
    Q_OBJECT

private slots:
    void detectsWav();
    void detectsMp3FrameSync();
    void detectsMp3Id3Tag();
    void unknownForGarbage();
    void wavInfoReadsFmtChunk();
    void wavInfoRejectsNonWav();
    void wavPcmDataExtractsDataChunk();
    void mimeTypes();
    void toInt16PcmCopiesInt16();
    void toInt16PcmConvertsFloat();
    void toInt16PcmConvertsInt32();
    void toInt16PcmRejectsUnknown();
};

void TestAudioFormat::detectsWav() {
    const QByteArray wav = AudioChunker::wavFromPcm(QByteArray(320, 0), 16000);
    QCOMPARE(AudioFormat::detect(wav), AudioFormat::Kind::Wav);
}

void TestAudioFormat::detectsMp3FrameSync() {
    const QByteArray mp3 = QByteArray::fromHex("FFFB9000");
    QCOMPARE(AudioFormat::detect(mp3), AudioFormat::Kind::Mp3);
}

void TestAudioFormat::detectsMp3Id3Tag() {
    const QByteArray mp3 = QByteArray("ID3\x04\x00\x00\x00\x00\x00\x00", 10);
    QCOMPARE(AudioFormat::detect(mp3), AudioFormat::Kind::Mp3);
}

void TestAudioFormat::unknownForGarbage() {
    QCOMPARE(AudioFormat::detect(QByteArray()), AudioFormat::Kind::Unknown);
    QCOMPARE(AudioFormat::detect(QByteArray("not audio at all")), AudioFormat::Kind::Unknown);
    QCOMPARE(AudioFormat::detect(QByteArray("OggS....")), AudioFormat::Kind::Unknown);
}

void TestAudioFormat::wavInfoReadsFmtChunk() {
    const QByteArray wav = AudioChunker::wavFromPcm(QByteArray(320, 0), 22050);
    int sampleRate = 0;
    int channels = 0;
    int bits = 0;
    QVERIFY(AudioFormat::wavInfo(wav, &sampleRate, &channels, &bits));
    QCOMPARE(sampleRate, 22050);
    QCOMPARE(channels, 1);
    QCOMPARE(bits, 16);
}

void TestAudioFormat::wavInfoRejectsNonWav() {
    int sampleRate = 0;
    int channels = 0;
    int bits = 0;
    QVERIFY(!AudioFormat::wavInfo(QByteArray("nope"), &sampleRate, &channels, &bits));
    QVERIFY(!AudioFormat::wavInfo(QByteArray::fromHex("FFFB0000"), &sampleRate, &channels, &bits));
}

void TestAudioFormat::wavPcmDataExtractsDataChunk() {
    const QByteArray pcm(640, '\x01');
    const QByteArray wav = AudioChunker::wavFromPcm(pcm, 16000);
    QCOMPARE(AudioFormat::wavPcmData(wav), pcm);
    QVERIFY(AudioFormat::wavPcmData(QByteArray("nope")).isEmpty());
}

void TestAudioFormat::mimeTypes() {
    QCOMPARE(AudioFormat::mimeType(AudioFormat::Kind::Mp3), QStringLiteral("audio/mpeg"));
    QCOMPARE(AudioFormat::mimeType(AudioFormat::Kind::Wav), QStringLiteral("audio/wav"));
    QCOMPARE(AudioFormat::mimeType(AudioFormat::Kind::Unknown),
             QStringLiteral("application/octet-stream"));
}

void TestAudioFormat::toInt16PcmCopiesInt16() {
    const std::array<qint16, 5> samples{0, 1000, -1000, 32767, -32768};
    const QByteArray pcm =
        AudioFormat::toInt16Pcm(samples.data(), 5, 1, AudioFormat::SampleFormat::Int16);
    QCOMPARE(pcm.size(), 5 * int(sizeof(qint16)));
    QCOMPARE(pcmSampleAt(pcm, 1), qint16(1000));
    QCOMPARE(pcmSampleAt(pcm, 4), qint16(-32768));
}

void TestAudioFormat::toInt16PcmConvertsFloat() {
    // FFmpeg decodes MP3 to float32; the conversion must scale to Int16.
    // Float 0.5 must become ~16383, float 1.0 must clip to 32767 (not wrap).
    const std::array<float, 6> samples{0.0F, 0.5F, -0.5F, 1.0F, -1.0F, 2.0F};
    const QByteArray pcm =
        AudioFormat::toInt16Pcm(samples.data(), 6, 1, AudioFormat::SampleFormat::Float);
    QCOMPARE(pcm.size(), 6 * int(sizeof(qint16)));
    QCOMPARE(pcmSampleAt(pcm, 0), qint16(0));
    QVERIFY(qAbs(pcmSampleAt(pcm, 1) - 16383) <= 1);
    QVERIFY(qAbs(pcmSampleAt(pcm, 2) + 16383) <= 1);
    QCOMPARE(pcmSampleAt(pcm, 3), qint16(32767));
    QCOMPARE(pcmSampleAt(pcm, 4), qint16(-32767));
    QCOMPARE(pcmSampleAt(pcm, 5), qint16(32767)); // clipped, not wrapped
}

void TestAudioFormat::toInt16PcmConvertsInt32() {
    constexpr auto kOneShifted16 = static_cast<qint32>(1U << 16U);
    constexpr auto kMinInt32 = static_cast<qint32>(0x80000000U);
    const std::array<qint32, 5> samples{0, kOneShifted16, -kOneShifted16, 0x7FFFFFFF, kMinInt32};
    const QByteArray pcm =
        AudioFormat::toInt16Pcm(samples.data(), 5, 1, AudioFormat::SampleFormat::Int32);
    QCOMPARE(pcmSampleAt(pcm, 0), qint16(0));
    QCOMPARE(pcmSampleAt(pcm, 1), qint16(1));
    QCOMPARE(pcmSampleAt(pcm, 2), qint16(-1));
    QCOMPARE(pcmSampleAt(pcm, 3), qint16(32767));
    QCOMPARE(pcmSampleAt(pcm, 4), qint16(-32768));
}

void TestAudioFormat::toInt16PcmRejectsUnknown() {
    const std::array<float, 1> samples{0.5F};
    QVERIFY(AudioFormat::toInt16Pcm(samples.data(), 1, 1, AudioFormat::SampleFormat::Unknown)
                .isEmpty());
    QVERIFY(AudioFormat::toInt16Pcm(nullptr, 1, 1, AudioFormat::SampleFormat::Float).isEmpty());
    QVERIFY(
        AudioFormat::toInt16Pcm(samples.data(), 0, 1, AudioFormat::SampleFormat::Float).isEmpty());
}

QTEST_GUILESS_MAIN(TestAudioFormat)
#include "tst_audioformat.moc"
