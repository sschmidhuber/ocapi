#include "core/ttsplayer.h"

#include "backends/ttsclient.h"
#include "core/audioformat.h"

#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QBuffer>
#include <QMediaDevices>
#include <QTimer>

#include <cstring>

namespace ocapi {

TtsPlayer::TtsPlayer(QObject *parent) : QObject(parent) {
    // Feeds queued PCM to the sink in small slices as buffer space frees up.
    m_feedTimer = new QTimer(this);
    m_feedTimer->setInterval(25);
    connect(m_feedTimer, &QTimer::timeout, this, &TtsPlayer::writePendingPcm);
}

TtsPlayer::~TtsPlayer() {
    stop();
    teardownSink();
}

QStringList TtsPlayer::availableOutputDevices() {
    QStringList devices;
    for (const QAudioDevice &device : QMediaDevices::audioOutputs())
        devices.push_back(device.id());
    return devices;
}

QStringList TtsPlayer::availableOutputDeviceNames() {
    QStringList names;
    for (const QAudioDevice &device : QMediaDevices::audioOutputs())
        names.push_back(device.description());
    return names;
}

void TtsPlayer::setService(const TtsServiceConfig &config) {
    stop();
    m_config = config;
    delete m_client;
    m_client = new TtsClient(config, this);
    connect(m_client, &TtsClient::audioReady, this, &TtsPlayer::onAudioReady);
    connect(m_client, &TtsClient::error, this, &TtsPlayer::onServiceError);
}

void TtsPlayer::setOutputDevice(const QString &deviceId) {
    if (m_outputDeviceId == deviceId)
        return;
    m_outputDeviceId = deviceId;
    // Recreate the sink on the next playback so the new device takes effect.
    if (m_sink)
        teardownSink();
}

bool TtsPlayer::isSpeaking() const {
    return m_speaking;
}

void TtsPlayer::feedText(const QString &accumulatedText) {
    if (!m_client)
        return;
    if (m_stopped) {
        // New playback after a stop: start fresh. cancel() left the client
        // in the cancelled state; resume it so new requests go out.
        m_client->resume();
        m_stopped = false;
        m_endOfText = false;
        m_pendingRequests = 0;
        m_hasPending = false;
        m_splitter.reset();
        m_lastFedText.clear();
        m_pendingPcm.clear();
    }
    if (accumulatedText.startsWith(m_lastFedText)) {
        const QString delta = accumulatedText.mid(m_lastFedText.size());
        m_lastFedText = accumulatedText;
        for (const auto &sentence : m_splitter.feed(delta))
            enqueueSentence(sentence);
    } else {
        // The message was edited or restarted: start over.
        m_splitter.reset();
        m_lastFedText = accumulatedText;
        for (const auto &sentence : m_splitter.feed(accumulatedText))
            enqueueSentence(sentence);
    }
}

void TtsPlayer::endOfText() {
    if (m_stopped)
        return;
    m_endOfText = true;
    const QString tail = m_splitter.flush();
    if (!tail.isEmpty())
        enqueueSentence(tail);
    updateSpeaking();
}

void TtsPlayer::stop() {
    m_stopped = true;
    m_endOfText = false;
    m_pendingRequests = 0;
    m_hasPending = false;
    m_splitter.reset();
    m_lastFedText.clear();
    m_pendingPcm.clear();
    if (m_client)
        m_client->cancel();
    if (m_decoder) {
        m_decoder->stop();
        m_decoder->deleteLater();
        m_decoder = nullptr;
        m_sourceBuffer = nullptr;
    }
    m_decodeQueue.clear();
    m_decodedPcm.clear();
    if (m_sink) {
        m_sink->stop();
        m_sink->reset();
    }
    m_feedTimer->stop();
    updateSpeaking();
}

void TtsPlayer::enqueueSentence(const QString &sentence) {
    if (!m_client || sentence.trimmed().isEmpty())
        return;
    // Synthesis requests are still in flight; speaking ends when they all
    // completed (successfully or not), even without audible output.
    ++m_pendingRequests;
    m_hasPending = true;
    updateSpeaking();
    m_client->synthesize(sentence);
}

void TtsPlayer::onServiceError(const QString &message) {
    // A failed request completes as empty in the client, so waiting work
    // still drains; drop the pending marker so the player leaves the
    // speaking state even when no audio ever arrives.
    if (m_pendingRequests > 0)
        --m_pendingRequests;
    m_hasPending = m_pendingRequests > 0;
    emit error(message);
    updateSpeaking();
}

void TtsPlayer::onAudioReady(const QByteArray &audio, const QString &mimeType) {
    if (m_stopped)
        return;
    if (m_pendingRequests > 0)
        --m_pendingRequests;
    m_hasPending = m_pendingRequests > 0;
    if (mimeType == QLatin1String("audio/wav")) {
        // WAV (Piper) is already PCM; hand the samples to the sink directly.
        int sampleRate = 0;
        int channels = 0;
        int bits = 0;
        if (!AudioFormat::wavInfo(audio, &sampleRate, &channels, &bits) || bits != 16)
            return;
        if (!ensureSink(sampleRate, channels))
            return;
        m_pendingPcm.enqueue(AudioFormat::wavPcmData(audio));
        writePendingPcm();
        return;
    }

    // MP3 (and any other compressed format): decode to PCM first. Only one
    // QAudioDecoder runs at a time; further responses queue up and are
    // decoded in arrival order once the active decode finishes.
    m_decodeQueue.enqueue(audio);
    startNextDecode();
    updateSpeaking();
}

void TtsPlayer::startNextDecode() {
    if (m_stopped || m_decoder || m_decodeQueue.isEmpty())
        return;
    const QByteArray audio = m_decodeQueue.dequeue();
    m_decodedPcm.clear();
    m_decodedFormat = QAudioFormat();
    m_decoder = new QAudioDecoder(this);
    // Ask for Int16 PCM: the sink is configured with Int16, and without an
    // explicit format the decoder's native output (often Float32) would be
    // copied as raw bytes and play back as noise.
    QAudioFormat decodeFormat;
    decodeFormat.setSampleFormat(QAudioFormat::Int16);
    m_decoder->setAudioFormat(decodeFormat);
    m_sourceBuffer = new QBuffer(m_decoder);
    m_sourceBuffer->setData(audio);
    m_sourceBuffer->open(QIODevice::ReadOnly);
    // Capture the decoder pointer: a finished/error signal from a previous
    // decoder (deleteLater'd but not yet destroyed) must not touch the new
    // one. The context object also drops the connection if either dies.
    QAudioDecoder *decoder = m_decoder;
    connect(decoder, &QAudioDecoder::bufferReady, this, [this, decoder] {
        if (m_decoder != decoder)
            return; // stale signal from a replaced decoder
        onDecodeBufferReady();
    });
    connect(decoder, &QAudioDecoder::finished, this, [this, decoder] {
        if (m_decoder != decoder)
            return; // stale signal from a replaced decoder
        onDecodeFinished();
    });
    connect(decoder, QOverload<QAudioDecoder::Error>::of(&QAudioDecoder::error), this,
            [this, decoder](QAudioDecoder::Error decodeError) {
                Q_UNUSED(decodeError)
                if (m_decoder != decoder)
                    return; // stale signal from a replaced decoder
                emit error(decoder->errorString());
                decoder->deleteLater();
                m_decoder = nullptr;
                m_sourceBuffer = nullptr;
                m_decodedPcm.clear();
                startNextDecode(); // keep the queue moving past failures
            });
    decoder->setSourceDevice(m_sourceBuffer);
    decoder->start();
}

void TtsPlayer::onDecodeBufferReady() {
    if (!m_decoder)
        return;
    while (m_decoder->isDecoding()) {
        const QAudioBuffer buffer = m_decoder->read();
        if (buffer.frameCount() <= 0)
            break;
        // The decoder may output any sample format (FFmpeg typically
        // delivers Float32 for MP3); convert to the Int16 PCM the sink
        // is configured with, otherwise the raw bytes play back as noise.
        const QAudioFormat format = buffer.format();
        AudioFormat::SampleFormat sampleFormat = AudioFormat::SampleFormat::Unknown;
        switch (format.sampleFormat()) {
        case QAudioFormat::Int16:
            sampleFormat = AudioFormat::SampleFormat::Int16;
            break;
        case QAudioFormat::Int32:
            sampleFormat = AudioFormat::SampleFormat::Int32;
            break;
        case QAudioFormat::Float:
            sampleFormat = AudioFormat::SampleFormat::Float;
            break;
        default:
            break;
        }
        const QByteArray pcm = AudioFormat::toInt16Pcm(
            buffer.constData<void>(), buffer.frameCount(), format.channelCount(), sampleFormat);
        if (!pcm.isEmpty()) {
            // Remember the buffer's real format: QAudioDecoder::audioFormat()
            // can stay invalid (0 Hz) when no explicit format was set, and
            // the sink must be created with the decoded rate/channels.
            m_decodedFormat = format;
            m_decodedPcm.append(pcm);
        }
    }
}

void TtsPlayer::onDecodeFinished() {
    if (!m_decoder)
        return;
    // The decoder's reported format can be invalid when no audioFormat was
    // set and the backend never produced a buffer; fall back to the format
    // of the last decoded buffer (stored in m_decodedFormat).
    QAudioFormat format = m_decoder->audioFormat();
    if (!format.isValid() || format.sampleRate() <= 0 || format.channelCount() <= 0)
        format = m_decodedFormat;
    onDecodeBufferReady(); // drain whatever is left after the final buffer
    m_decoder->deleteLater();
    m_decoder = nullptr;
    m_sourceBuffer = nullptr;
    if (!m_stopped && !m_decodedPcm.isEmpty() &&
        ensureSink(format.sampleRate(), format.channelCount())) {
        m_pendingPcm.enqueue(m_decodedPcm);
        writePendingPcm();
    }
    m_decodedPcm.clear();
    // Decode the next queued response, if any.
    startNextDecode();
}

bool TtsPlayer::ensureSink(int sampleRate, int channels) {
    if (m_sink && m_sink->format().sampleRate() == sampleRate &&
        m_sink->format().channelCount() == channels)
        return true;
    teardownSink();

    QAudioDevice device;
    if (!m_outputDeviceId.isEmpty()) {
        for (const QAudioDevice &candidate : QMediaDevices::audioOutputs()) {
            if (candidate.id() == m_outputDeviceId) {
                device = candidate;
                break;
            }
        }
    }
    if (device.isNull())
        device = QMediaDevices::defaultAudioOutput();
    if (device.isNull())
        return false;
    m_devices = new QMediaDevices(this);
    QAudioFormat format = device.preferredFormat();
    format.setSampleRate(sampleRate);
    format.setChannelCount(channels);
    format.setSampleFormat(QAudioFormat::Int16);
    if (!device.isFormatSupported(format)) {
        // The sink does not resample; fall back to the device's preferred
        // format (playback may be at the wrong rate on exotic devices).
        format = device.preferredFormat();
        format.setSampleFormat(QAudioFormat::Int16);
    }
    m_sink = new QAudioSink(device, format, this);
    m_sinkIo = m_sink->start();
    if (!m_sinkIo) {
        teardownSink();
        return false;
    }
    connect(m_sink, &QAudioSink::stateChanged, this, [this](QAudio::State state) {
        if (state == QAudio::IdleState && m_endOfText && m_pendingPcm.isEmpty())
            updateSpeaking();
    });
    return true;
}

void TtsPlayer::writePendingPcm() {
    while (m_sink && !m_pendingPcm.isEmpty() && m_sink->bytesFree() > 0) {
        const QByteArray chunk = m_pendingPcm.head();
        const qint64 written =
            m_sinkIo->write(chunk.constData(), qMin<qint64>(chunk.size(), m_sink->bytesFree()));
        if (written <= 0)
            break;
        if (written == chunk.size())
            m_pendingPcm.dequeue();
        else
            m_pendingPcm.head().remove(0, written);
    }
    if (!m_pendingPcm.isEmpty() && m_sink && m_sink->state() == QAudio::ActiveState)
        m_feedTimer->start();
    else
        m_feedTimer->stop();
    updateSpeaking();
}

void TtsPlayer::updateSpeaking() {
    const bool sinkActive = m_sink && m_sink->state() == QAudio::ActiveState;
    const bool speaking =
        !m_stopped && (m_hasPending || !m_pendingPcm.isEmpty() || m_decoder != nullptr ||
                       !m_decodeQueue.isEmpty() || sinkActive);
    if (speaking != m_speaking) {
        m_speaking = speaking;
        emit speakingChanged(speaking);
    }
}

void TtsPlayer::teardownSink() {
    m_feedTimer->stop();
    if (m_sink) {
        m_sink->stop();
        m_sink->deleteLater();
        m_sink = nullptr;
    }
    m_sinkIo = nullptr;
    delete m_devices;
    m_devices = nullptr;
}

} // namespace ocapi
