#pragma once

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <optional>

class QAudioSink;
class QIODevice;

namespace playback {

class AudioOutput final {
public:
    AudioOutput() = default;
    ~AudioOutput();

    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;

    [[nodiscard]] bool initialize(QString* error_message, qint64* error_code);
    [[nodiscard]] bool available() const noexcept { return available_; }
    [[nodiscard]] bool disabledByEnvironment() const noexcept { return disabled_by_environment_; }
    [[nodiscard]] int sampleRate() const noexcept { return sample_rate_; }
    [[nodiscard]] int channelCount() const noexcept { return channel_count_; }
    [[nodiscard]] int bytesPerSample() const noexcept { return 2; }

    [[nodiscard]] bool start(QString* error_message, qint64* error_code);
    [[nodiscard]] bool resume(QString* error_message, qint64* error_code);
    void setVolume(double gain) noexcept;
    void pause() noexcept;
    void stop() noexcept;
    [[nodiscard]] qint64 bytesFree() const noexcept;
    [[nodiscard]] qint64 processedUsecs() const noexcept;
    [[nodiscard]] std::optional<qint64> bufferedUsecs() const noexcept;
    [[nodiscard]] qint64 write(const QByteArray& data) noexcept;

    [[nodiscard]] static double normalizeVolume(double gain) noexcept;
    [[nodiscard]] static double outputVolume(double gain) noexcept;
    [[nodiscard]] static double sampleBoost(double gain) noexcept;

private:
    QAudioSink* sink_ = nullptr;
    QIODevice* device_ = nullptr;
    int sample_rate_ = 48000;
    int channel_count_ = 2;
    double volume_gain_ = 1.0;
    bool available_ = false;
    bool disabled_by_environment_ = false;
};

} // namespace playback
