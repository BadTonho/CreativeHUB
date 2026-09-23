#include "audio_output.h"

#if defined(CREATIVE_SUITE_HAS_QT_MULTIMEDIA)
#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QMediaDevices>
#endif

#include <QIODevice>

#include <algorithm>
#include <cmath>

namespace playback {

AudioOutput::~AudioOutput() {
    stop();
#if defined(CREATIVE_SUITE_HAS_QT_MULTIMEDIA)
    delete sink_;
#endif
    sink_ = nullptr;
    device_ = nullptr;
}

bool AudioOutput::initialize(QString* error_message, qint64* error_code) {
    if (error_message != nullptr) error_message->clear();
    if (error_code != nullptr) *error_code = 0;

    if (qEnvironmentVariable("CREATIVE_SUITE_DISABLE_AUDIO_OUTPUT") == "1") {
        disabled_by_environment_ = true;
        return false;
    }

#if defined(CREATIVE_SUITE_HAS_QT_MULTIMEDIA)
    const auto device = QMediaDevices::defaultAudioOutput();
    if (device.isNull()) {
        if (error_message != nullptr) *error_message = "No default audio output device is available.";
        return false;
    }

    auto format = device.preferredFormat();
    if (format.sampleRate() <= 0 || format.channelCount() <= 0) {
        if (error_message != nullptr) *error_message = "The default audio device has no usable format.";
        return false;
    }

    QAudioFormat signed_format = format;
    signed_format.setSampleFormat(QAudioFormat::Int16);
    if (device.isFormatSupported(signed_format)) format = signed_format;
    if (format.sampleFormat() != QAudioFormat::Int16) {
        if (error_message != nullptr) *error_message = "The default audio device does not support signed 16-bit PCM.";
        return false;
    }

    sample_rate_ = format.sampleRate();
    channel_count_ = format.channelCount();
    sink_ = new QAudioSink(device, format);
    sink_->setBufferSize(sample_rate_ * channel_count_ * bytesPerSample() / 5);
    setVolume(volume_gain_);
    available_ = true;
    return true;
#else
    if (error_message != nullptr) {
        *error_message = "Qt Multimedia is not available in this build.";
    }
    return false;
#endif
}

bool AudioOutput::start(QString* error_message, qint64* error_code) {
    if (error_message != nullptr) error_message->clear();
    if (error_code != nullptr) *error_code = 0;
    if (!available_ || sink_ == nullptr) return false;

#if defined(CREATIVE_SUITE_HAS_QT_MULTIMEDIA)
    device_ = sink_->start();
    if (device_ == nullptr) {
        if (error_message != nullptr) *error_message = "The audio output could not be started.";
        if (error_code != nullptr) *error_code = static_cast<qint64>(sink_->error());
        return false;
    }
    return true;
#else
    return false;
#endif
}

double AudioOutput::normalizeVolume(double gain) noexcept {
    return std::isfinite(gain) && gain >= 0.0 && gain <= 2.0
        ? gain
        : 1.0;
}

double AudioOutput::outputVolume(double gain) noexcept {
    return std::min(normalizeVolume(gain), 1.0);
}

double AudioOutput::sampleBoost(double gain) noexcept {
    return std::max(normalizeVolume(gain), 1.0);
}

void AudioOutput::setVolume(double gain) noexcept {
    volume_gain_ = normalizeVolume(gain);
#if defined(CREATIVE_SUITE_HAS_QT_MULTIMEDIA)
    if (sink_ != nullptr) {
        sink_->setVolume(static_cast<qreal>(outputVolume(volume_gain_)));
    }
#endif
}

bool AudioOutput::resume(QString* error_message, qint64* error_code) {
    if (error_message != nullptr) error_message->clear();
    if (error_code != nullptr) *error_code = 0;
    if (!available_ || sink_ == nullptr) return false;
#if defined(CREATIVE_SUITE_HAS_QT_MULTIMEDIA)
    if (device_ == nullptr) return start(error_message, error_code);
    sink_->resume();
    if (sink_->error() != QAudio::NoError) {
        if (error_message != nullptr) *error_message = "The audio output could not be resumed.";
        if (error_code != nullptr) *error_code = static_cast<qint64>(sink_->error());
        return false;
    }
    return true;
#else
    return false;
#endif
}

void AudioOutput::pause() noexcept {
#if defined(CREATIVE_SUITE_HAS_QT_MULTIMEDIA)
    if (sink_ != nullptr) sink_->suspend();
#endif
}

void AudioOutput::stop() noexcept {
#if defined(CREATIVE_SUITE_HAS_QT_MULTIMEDIA)
    if (sink_ != nullptr) sink_->stop();
#endif
    device_ = nullptr;
}

qint64 AudioOutput::bytesFree() const noexcept {
#if defined(CREATIVE_SUITE_HAS_QT_MULTIMEDIA)
    return sink_ != nullptr ? sink_->bytesFree() : 0;
#else
    return 0;
#endif
}

qint64 AudioOutput::processedUsecs() const noexcept {
#if defined(CREATIVE_SUITE_HAS_QT_MULTIMEDIA)
    return sink_ != nullptr ? sink_->processedUSecs() : 0;
#else
    return 0;
#endif
}

std::optional<qint64> AudioOutput::bufferedUsecs() const noexcept {
#if defined(CREATIVE_SUITE_HAS_QT_MULTIMEDIA)
    if (sink_ == nullptr || sample_rate_ <= 0 || channel_count_ <= 0) {
        return std::nullopt;
    }
    const auto buffer_size = sink_->bufferSize();
    const auto free_bytes = sink_->bytesFree();
    const auto bytes_per_second = static_cast<qint64>(sample_rate_) *
        static_cast<qint64>(channel_count_) * bytesPerSample();
    if (buffer_size <= 0 || free_bytes < 0 || free_bytes > buffer_size ||
        bytes_per_second <= 0) {
        return std::nullopt;
    }
    const auto buffered_bytes = buffer_size - free_bytes;
    return buffered_bytes * 1'000'000 / bytes_per_second;
#else
    return std::nullopt;
#endif
}

qint64 AudioOutput::write(const QByteArray& data) noexcept {
#if defined(CREATIVE_SUITE_HAS_QT_MULTIMEDIA)
    if (device_ == nullptr || data.isEmpty()) return 0;
    return device_->write(data);
#else
    static_cast<void>(data);
    return 0;
#endif
}

} // namespace playback
