#pragma once

#include "../model/composition_document.h"

#include <creative_suite/animation/animation.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace motion::audio {

struct AudioEnvelope final {
    std::vector<double> rms_by_frame;
    double peak_rms = 0.0;
};

class AudioAnalysisError final : public std::runtime_error {
public:
    AudioAnalysisError(std::string message, int error_code = -1);
    [[nodiscard]] int errorCode() const noexcept { return error_code_; }

private:
    int error_code_;
};

class AudioAnalysisCancelled final : public std::exception {
public:
    [[nodiscard]] const char* what() const noexcept override
    {
        return "Audio keyframe generation was canceled.";
    }
};

class AudioEnvelopeAnalyzer final {
public:
    static constexpr int analysis_sample_rate = 48'000;
    using ProgressCallback = std::function<void(int)>;

    [[nodiscard]] static AudioEnvelope analyze(
        const std::filesystem::path& audio_path,
        model::FrameRate frame_rate,
        std::int64_t maximum_frames,
        const std::atomic_bool& cancel_requested,
        ProgressCallback report_progress = {});
};

[[nodiscard]] std::vector<creative_suite::animation::Keyframe> generateAudioKeyframes(
    const AudioEnvelope& envelope,
    std::int64_t layer_duration_frames,
    creative_suite::animation::TransformProperty property,
    double minimum_value,
    double maximum_value,
    std::size_t maximum_keyframe_count,
    const std::atomic_bool* cancel_requested = nullptr);

} // namespace motion::audio
