#pragma once

#include "timeline/audio_effects.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace audio_effects {

struct BiquadCoefficients {
    double b0 = 1.0;
    double b1 = 0.0;
    double b2 = 0.0;
    double a1 = 0.0;
    double a2 = 0.0;
};

struct BiquadDelay {
    double x1 = 0.0;
    double x2 = 0.0;
    double y1 = 0.0;
    double y2 = 0.0;
};

struct AudioDspState {
    std::array<std::vector<BiquadDelay>, 3> equalizer_delays;
    double compressor_reduction_db = 0.0;
    double limiter_gain = 1.0;
    std::vector<BiquadDelay> lfe_delays;
    std::vector<float> delay_buffer;
    std::size_t delay_write = 0;
    std::array<std::vector<float>, 4> reverb_combs;
    std::array<std::size_t, 4> reverb_pos{};
    std::vector<float> mod_buffer;
    std::size_t mod_write = 0;
    double mod_phase = 0.0;
    std::vector<float> pitch_buffer;
    std::size_t pitch_write = 0;
    double pitch_phase = 0.0;
    int sample_rate = 0;
    int channel_count = 0;
    std::int64_t next_source_sample_index = -1;
    std::vector<timeline::AudioEffectInstance> configured_effects;
};

[[nodiscard]] inline BiquadCoefficients normalizeBiquad(
    double b0, double b1, double b2,
    double a0, double a1, double a2) noexcept {
    if (!std::isfinite(a0) || std::abs(a0) < 1.0e-12) return {};
    const auto inverse_a0 = 1.0 / a0;
    return {b0 * inverse_a0, b1 * inverse_a0, b2 * inverse_a0,
            a1 * inverse_a0, a2 * inverse_a0};
}

[[nodiscard]] inline double safeFrequency(double requested, int sample_rate) noexcept {
    if (sample_rate <= 0) return 20.0;
    return std::clamp(requested, 1.0,
        std::max(1.0, static_cast<double>(sample_rate) * 0.45));
}

}  // namespace audio_effects
