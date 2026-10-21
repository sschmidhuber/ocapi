#include "core/audiochunker.h"

#include <QByteArray>
#include <QtTest>

using namespace ocapi;

namespace {

QByteArray pcm(qsizetype bytes, char fill = 0) {
    return {bytes, fill};
}

} // namespace

class TestAudioChunker : public QObject {
    Q_OBJECT

private slots:
    void wavHeaderIsWellFormed();
    void feedEmitsNothingBelowChunkSize();
    void feedEmitsOneChunkAtExactSize();
    void feedEmitsMultipleChunks();
    void flushReturnsRemainder();
    void flushReturnsEmptyWhenBufferEmpty();
    void resetClearsBuffer();
    void setSampleRateClearsBuffer();
    void bufferedMsTracksBuffer();

private:
    static const int kSampleRate = 16000;
};

const int TestAudioChunker::kSampleRate;

void TestAudioChunker::wavHeaderIsWellFormed() {
    const QByteArray wav = AudioChunker::wavFromPcm(pcm(320), kSampleRate);
    QCOMPARE(wav.size(), 44 + 320);
    QVERIFY(wav.startsWith("RIFF"));
    QCOMPARE(QByteArray(wav.mid(8, 4)), QByteArray("WAVE"));
    QCOMPARE(QByteArray(wav.mid(36, 4)), QByteArray("data"));
    const int dataSize = static_cast<unsigned char>(wav.at(40)) |
                         (static_cast<unsigned char>(wav.at(41)) << 8) |
                         (static_cast<unsigned char>(wav.at(42)) << 16) |
                         (static_cast<unsigned char>(wav.at(43)) << 24);
    QCOMPARE(dataSize, 320);
}

void TestAudioChunker::feedEmitsNothingBelowChunkSize() {
    AudioChunker chunker(kSampleRate, 6000);
    QVERIFY(chunker.feed(pcm(qsizetype{16000})).isEmpty());
}

void TestAudioChunker::feedEmitsOneChunkAtExactSize() {
    AudioChunker chunker(kSampleRate, 6000);
    const auto chunks = chunker.feed(pcm(qsizetype{16000} * 2 * 6));
    QCOMPARE(chunks.size(), 1);
    QCOMPARE(chunks.first().size(), qsizetype(44 + 16000 * 2 * 6));
}

void TestAudioChunker::feedEmitsMultipleChunks() {
    AudioChunker chunker(kSampleRate, 1000);
    const auto chunks = chunker.feed(pcm(qsizetype{16000} * 2 * 3));
    QCOMPARE(chunks.size(), 3);
}

void TestAudioChunker::flushReturnsRemainder() {
    AudioChunker chunker(kSampleRate, 6000);
    chunker.feed(pcm(qsizetype{16000} * 2 * 2));
    const QByteArray tail = chunker.flush();
    QVERIFY(!tail.isEmpty());
    QCOMPARE(tail.size(), 44 + 16000 * 2 * 2);
}

void TestAudioChunker::flushReturnsEmptyWhenBufferEmpty() {
    AudioChunker chunker(kSampleRate, 1000);
    chunker.feed(pcm(32000));
    QVERIFY(chunker.flush().isEmpty());
}

void TestAudioChunker::resetClearsBuffer() {
    AudioChunker chunker(kSampleRate, 6000);
    chunker.feed(pcm(1000));
    chunker.reset();
    QVERIFY(chunker.flush().isEmpty());
}

void TestAudioChunker::setSampleRateClearsBuffer() {
    AudioChunker chunker(kSampleRate, 6000);
    chunker.feed(pcm(1000));
    chunker.setSampleRate(44100);
    QCOMPARE(chunker.sampleRate(), 44100);
    QVERIFY(chunker.flush().isEmpty());
}

void TestAudioChunker::bufferedMsTracksBuffer() {
    AudioChunker chunker(kSampleRate, 6000);
    chunker.feed(pcm(qsizetype{16000} * 2));
    QCOMPARE(chunker.bufferedMs(), 1000);
}

QTEST_MAIN(TestAudioChunker)
#include "tst_audiochunker.moc"
