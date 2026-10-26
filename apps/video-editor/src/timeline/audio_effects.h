#pragma once
#include "audio_processing.h"
#include <array>
#include <variant>

namespace timeline {
struct AudioEqualizerParameters {
    double low_shelf_frequency_hz = 120.0;
    double low_shelf_gain_db = 0.0;
    double mid_frequency_hz = 1000.0;
    double mid_gain_db = 0.0;
    double mid_q = 1.0;
    double high_shelf_frequency_hz = 8000.0;
    double high_shelf_gain_db = 0.0;

    friend bool operator==(const AudioEqualizerParameters&,
                           const AudioEqualizerParameters&) = default;
};

struct AudioCompressorParameters {
    double threshold_db = -18.0;
    double ratio = 4.0;
    double attack_ms = 10.0;
    double release_ms = 100.0;
    double makeup_gain_db = 0.0;

    friend bool operator==(const AudioCompressorParameters&,
                           const AudioCompressorParameters&) = default;
};

using AudioEffectParameters = std::variant<
    AudioEqualizerParameters, AudioCompressorParameters, AudioDialogueCleanupParameters,
    AudioNoiseReductionParameters, AudioRepairParameters>;

struct AudioEffectInstance {
    AudioEffectParameters parameters = AudioEqualizerParameters{};
    bool enabled = false;

    friend bool operator==(const AudioEffectInstance&,
                           const AudioEffectInstance&) = default;
};

[[nodiscard]] inline bool validAudioEffectChain(
    const std::vector<AudioEffectInstance>& effects) noexcept {
    if (effects.size() > 5) return false;
    std::array<bool, 5> seen{};
    bool has_equalizer = false;
    bool has_compressor = false;
    for (const auto& effect : effects) {
        if (effect.parameters.valueless_by_exception() || seen[effect.parameters.index()]) return false;
        seen[effect.parameters.index()] = true;
        if (const auto* eq = std::get_if<AudioEqualizerParameters>(
                &effect.parameters)) {
            if (has_equalizer ||
                !std::isfinite(eq->low_shelf_frequency_hz) ||
                eq->low_shelf_frequency_hz < 20.0 ||
                eq->low_shelf_frequency_hz > 500.0 ||
                !std::isfinite(eq->low_shelf_gain_db) ||
                eq->low_shelf_gain_db < -18.0 ||
                eq->low_shelf_gain_db > 18.0 ||
                !std::isfinite(eq->mid_frequency_hz) ||
                eq->mid_frequency_hz < 20.0 ||
                eq->mid_frequency_hz > 20000.0 ||
                !std::isfinite(eq->mid_gain_db) ||
                eq->mid_gain_db < -18.0 || eq->mid_gain_db > 18.0 ||
                !std::isfinite(eq->mid_q) || eq->mid_q < 0.1 ||
                eq->mid_q > 10.0 ||
                !std::isfinite(eq->high_shelf_frequency_hz) ||
                eq->high_shelf_frequency_hz < 500.0 ||
                eq->high_shelf_frequency_hz > 20000.0 ||
                !std::isfinite(eq->high_shelf_gain_db) ||
                eq->high_shelf_gain_db < -18.0 ||
                eq->high_shelf_gain_db > 18.0) {
                return false;
            }
            has_equalizer = true;
        } else if (const auto* compressor = std::get_if<
                       AudioCompressorParameters>(&effect.parameters)) {
            if (has_compressor ||
                !std::isfinite(compressor->threshold_db) ||
                compressor->threshold_db < -60.0 ||
                compressor->threshold_db > 0.0 ||
                !std::isfinite(compressor->ratio) ||
                compressor->ratio < 1.0 || compressor->ratio > 20.0 ||
                !std::isfinite(compressor->attack_ms) ||
                compressor->attack_ms < 0.1 ||
                compressor->attack_ms > 200.0 ||
                !std::isfinite(compressor->release_ms) ||
                compressor->release_ms < 10.0 ||
                compressor->release_ms > 1000.0 ||
                !std::isfinite(compressor->makeup_gain_db) ||
                compressor->makeup_gain_db < 0.0 ||
                compressor->makeup_gain_db > 24.0) {
                return false;
            }
            has_compressor = true;
        } else if (const auto* cleanup = std::get_if<AudioDialogueCleanupParameters>(&effect.parameters)) {
            if (!audioParameterInRange(cleanup->highpass_frequency_hz, 20, 300) ||
                !audioParameterInRange(cleanup->deesser_intensity, 0, 1)) return false;
        } else if (const auto* noise = std::get_if<AudioNoiseReductionParameters>(&effect.parameters)) {
            if (!audioParameterInRange(noise->reduction_db, 0, 30) ||
                !audioParameterInRange(noise->noise_floor_db, -80, -20)) return false;
        } else if (const auto* repair = std::get_if<AudioRepairParameters>(&effect.parameters)) {
            if ((repair->hum_frequency_hz != 0 && repair->hum_frequency_hz != 50 && repair->hum_frequency_hz != 60) ||
                !audioParameterInRange(repair->declick_sensitivity, 1, 10)) return false;
        } else {
            return false;
        }
    }
    return true;
}

} // namespace timeline
