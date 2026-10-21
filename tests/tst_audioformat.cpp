#include "core/audiochunker.h"
#include "core/audioformat.h"

#include <QtTest>

using namespace ocapi;

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

QTEST_GUILESS_MAIN(TestAudioFormat)
#include "tst_audioformat.moc"
