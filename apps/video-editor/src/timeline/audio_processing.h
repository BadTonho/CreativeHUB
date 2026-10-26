#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <limits>
#include <vector>

namespace timeline {

struct AudioDialogueCleanupParameters {
    double highpass_frequency_hz = 80.0;
    double deesser_intensity = 0.5;
    friend bool operator==(const AudioDialogueCleanupParameters&, const AudioDialogueCleanupParameters&) = default;
};
struct AudioNoiseReductionParameters {
    double reduction_db = 12.0;
    double noise_floor_db = -50.0;
    friend bool operator==(const AudioNoiseReductionParameters&, const AudioNoiseReductionParameters&) = default;
};
struct AudioRepairParameters {
    int hum_frequency_hz = 0;
    bool declick_enabled = true;
    double declick_sensitivity = 2.0;
    friend bool operator==(const AudioRepairParameters&, const AudioRepairParameters&) = default;
};
enum class AudioDuckingMode { Off, Automatic, Curve };
// Absolute timeline time; reduction is positive attenuation, interpolated in dB.
struct AudioDuckingKeyframe {
    std::int64_t time_us = 0;
    double reduction_db = 0.0;
    friend bool operator==(const AudioDuckingKeyframe&, const AudioDuckingKeyframe&) = default;
};
struct AudioDuckingSettings {
    AudioDuckingMode mode = AudioDuckingMode::Off;
    std::optional<std::uint64_t> source_track_id;
    double threshold_db = -30.0;
    double reduction_db = 12.0;
    double attack_ms = 50.0;
    double hold_ms = 200.0;
    double release_ms = 500.0;
    std::vector<AudioDuckingKeyframe> curve;
    friend bool operator==(const AudioDuckingSettings&, const AudioDuckingSettings&) = default;
};
inline bool audioParameterInRange(double value, double minimum, double maximum) noexcept {
    return std::isfinite(value) && value >= minimum && value <= maximum;
}
inline bool validAudioDucking(const AudioDuckingSettings& settings) noexcept {
    if (settings.mode != AudioDuckingMode::Off && settings.mode != AudioDuckingMode::Automatic &&
        settings.mode != AudioDuckingMode::Curve) return false;
    if ((settings.source_track_id && *settings.source_track_id > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) ||
        settings.source_track_id == 0 ||
        (settings.mode == AudioDuckingMode::Automatic && !settings.source_track_id) ||
        !audioParameterInRange(settings.threshold_db, -60, 0) ||
        !audioParameterInRange(settings.reduction_db, 0, 36) ||
        !audioParameterInRange(settings.attack_ms, 1, 1000) ||
        !audioParameterInRange(settings.hold_ms, 0, 2000) ||
        !audioParameterInRange(settings.release_ms, 10, 5000)) return false;
    std::int64_t previous = -1;
    for (const auto& point : settings.curve) {
        if (point.time_us <= previous || !audioParameterInRange(point.reduction_db, 0, 36)) return false;
        previous = point.time_us;
    }
    return true;
}
inline double audioDuckingReductionAt(const std::vector<AudioDuckingKeyframe>& points,
                                      std::int64_t time_us) noexcept {
    if (points.empty() || time_us < points.front().time_us || time_us > points.back().time_us) return 0.0;
    const auto right = std::upper_bound(points.begin(), points.end(), time_us,
        [](auto time, const auto& point) { return time < point.time_us; });
    if (right == points.end()) return points.back().reduction_db;
    const auto& left = *(right - 1);
    const double fraction = static_cast<double>(time_us - left.time_us) /
        static_cast<double>(right->time_us - left.time_us);
    return left.reduction_db + fraction * (right->reduction_db - left.reduction_db);
}
} // namespace timeline
