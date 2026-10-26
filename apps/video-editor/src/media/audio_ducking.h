#pragma once
#include "timeline/audio_processing.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <span>
#include <vector>

namespace media {
// Worker-local detector. The same sample-wise implementation is used by live
// mixing and curve generation, independent of decoder/mixer block sizes.
class AudioDuckingDetector final {
public:
    double process(std::span<const float> frame, int rate,
                   const timeline::AudioDuckingSettings& settings) {
        const auto window = static_cast<std::size_t>(std::max(1, rate / 50));
        if (energy_.size() != window) { *this = {}; energy_.assign(window, 0.0); }
        double peak = 0;
        for (float sample : frame) if (std::isfinite(sample)) peak = std::max(peak, std::abs(static_cast<double>(sample)));
        sum_ += peak * peak - energy_[cursor_];
        energy_[cursor_] = peak * peak;
        cursor_ = (cursor_ + 1) % window;
        const auto level = 10.0 * std::log10(std::max(sum_ / window, 1e-24));
        const bool detected = level >= settings.threshold_db - (active_ ? 3.0 : 0.0);
        if (detected) { active_ = true; hold_ = static_cast<std::int64_t>(settings.hold_ms * rate / 1000.0); }
        else if (hold_ > 0) --hold_;
        else active_ = false;
        const auto target = active_ ? settings.reduction_db : 0.0;
        const auto ms = target > reduction_ ? settings.attack_ms : settings.release_ms;
        const auto coefficient = std::exp(-1000.0 / (ms * rate));
        reduction_ = coefficient * reduction_ + (1.0 - coefficient) * target;
        return reduction_;
    }
private:
    std::vector<double> energy_;
    std::size_t cursor_ = 0;
    double sum_ = 0, reduction_ = 0;
    std::int64_t hold_ = 0;
    bool active_ = false;
};

// Streaming bounded-error simplification. Keeps only a slope corridor and the
// resulting control points, never a full project's per-sample envelope.
class AudioDuckingCurveBuilder final {
public:
    void append(std::int64_t time, double reduction) {
        if (points_.empty()) { points_.push_back({time, reduction}); last_ = points_.back(); return; }
        if (time <= last_.time_us) return;
        const auto origin = points_.back();
        const double dt = static_cast<double>(time - origin.time_us);
        const auto low = std::max((reduction - origin.reduction_db - tolerance) / dt, -origin.reduction_db / dt);
        const auto high = std::min((reduction - origin.reduction_db + tolerance) / dt, (36.0 - origin.reduction_db) / dt);
        if (std::max(low_, low) > std::min(high_, high) && last_.time_us > origin.time_us) {
            const auto end_dt = static_cast<double>(last_.time_us - origin.time_us);
            const auto slope = std::clamp((last_.reduction_db - origin.reduction_db) / end_dt, low_, high_);
            points_.push_back({last_.time_us, std::clamp(origin.reduction_db + slope * end_dt, 0.0, 36.0)});
            low_ = -std::numeric_limits<double>::infinity();
            high_ = std::numeric_limits<double>::infinity();
            append(time, reduction);
            return;
        }
        low_ = std::max(low_, low); high_ = std::min(high_, high);
        last_ = {time, reduction};
    }
    std::vector<timeline::AudioDuckingKeyframe> finish() {
        if (!points_.empty() && last_.time_us > points_.back().time_us) {
            const auto origin = points_.back();
            const auto dt = static_cast<double>(last_.time_us - origin.time_us);
            const auto slope = std::clamp((last_.reduction_db - origin.reduction_db) / dt, low_, high_);
            points_.push_back({last_.time_us, std::clamp(origin.reduction_db + slope * dt, 0.0, 36.0)});
        }
        return std::move(points_);
    }
private:
    static constexpr double tolerance = .099;
    std::vector<timeline::AudioDuckingKeyframe> points_;
    timeline::AudioDuckingKeyframe last_;
    double low_ = -std::numeric_limits<double>::infinity();
    double high_ = std::numeric_limits<double>::infinity();
};
} // namespace media
