#pragma once

#include <creative_suite/media/video_frame.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace creative_suite::media {

struct VideoEncoderOption {
    std::string name;
    std::string display_name;
    int codec_id = 0;
};

struct VideoContainerOption {
    std::string name;
    std::string display_name;
    std::string extensions;
    std::vector<VideoEncoderOption> video_encoders;
    std::vector<VideoEncoderOption> audio_encoders;
};

struct AudioEncodingSettings {
    std::string encoder_name;
    int bitrate_kbps = 192;
    int input_sample_rate = 48000;
    int channels = 2;
};

struct VideoEncodingSettings {
    std::filesystem::path output_path;
    std::string container_name;
    std::string video_encoder_name;
    int width = 0;
    int height = 0;
    std::int64_t frame_rate_numerator = 0;
    std::int64_t frame_rate_denominator = 1;
    double video_bitrate_mbps = 10.0;
    std::optional<AudioEncodingSettings> audio;
};

class VideoEncodingError final : public std::runtime_error {
public:
    VideoEncodingError(std::string message, int error_code)
        : std::runtime_error(std::move(message)), error_code_(error_code) {}

    [[nodiscard]] int errorCode() const noexcept { return error_code_; }

private:
    int error_code_;
};

// FFmpeg-backed output capability queries use only standard C++ value types.
[[nodiscard]] std::vector<VideoContainerOption> availableVideoContainers();
[[nodiscard]] bool supportsVideoEncoder(
    const VideoContainerOption& container,
    const std::string& encoder_name);
[[nodiscard]] bool supportsAudioEncoder(
    const VideoContainerOption& container,
    const std::string& encoder_name);

// Replaces an existing target only after a complete temporary output is ready.
[[nodiscard]] bool publishEncodedFileAtomically(
    const std::filesystem::path& temporary_path,
    const std::filesystem::path& target_path) noexcept;

// Encodes RGBA video frames and optional interleaved float stereo audio into a
// selected FFmpeg container. The caller owns output-path staging and publish.
class VideoEncoder final {
public:
    explicit VideoEncoder(VideoEncodingSettings settings);
    ~VideoEncoder();

    VideoEncoder(const VideoEncoder&) = delete;
    VideoEncoder& operator=(const VideoEncoder&) = delete;
    VideoEncoder(VideoEncoder&&) noexcept;
    VideoEncoder& operator=(VideoEncoder&&) noexcept;

    void writeVideo(const RgbaFrame& source, std::int64_t output_frame);
    void writeAudio(std::span<const float> interleaved_stereo, int sample_count);
    [[nodiscard]] int nextAudioInputSampleCount() const;
    [[nodiscard]] bool hasAudio() const noexcept;
    void finish();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace creative_suite::media
