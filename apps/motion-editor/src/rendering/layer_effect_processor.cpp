#include "layer_effect_processor.h"
#include "layer_effect_worker_pool.h"

#include <creative_suite/effects/effects.h>
#include <creative_suite/effects/gaussian_blur.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace motion::ui {
namespace {

template<typename Operation>
bool measureEffect(const EffectTimingRecorder& recorder,
                   LayerEffectKind effect,
                   Operation&& operation)
{
    const auto started = std::chrono::steady_clock::now();
    bool completed = false;
    try {
        completed = operation();
    } catch (...) {
        if (recorder) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - started).count();
            if (elapsed >= 0) {
                try { recorder(effect, static_cast<std::uint64_t>(elapsed)); } catch (...) {}
            }
        }
        throw;
    }
    if (recorder) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - started).count();
        if (elapsed >= 0) {
            try { recorder(effect, static_cast<std::uint64_t>(elapsed)); } catch (...) {}
        }
    }
    return completed;
}

void validateFrame(const creative_suite::media::RgbaFrame& frame)
{
    if (frame.width <= 0 || frame.height <= 0 ||
        frame.width > std::numeric_limits<int>::max() / 4 ||
        frame.stride < frame.width * 4 || frame.stride <= 0) {
        throw std::invalid_argument("The effect input is not a valid RGBA frame.");
    }
    const auto required = static_cast<std::uint64_t>(frame.stride) *
        static_cast<std::uint64_t>(frame.height);
    if (required > frame.rgba_pixels.size()) {
        throw std::invalid_argument("The effect input frame storage is incomplete.");
    }
}


} // namespace

bool hasEnabledLayerEffects(const std::vector<model::LayerEffect>& effects) noexcept
{
    return std::any_of(effects.begin(), effects.end(), [](const auto& effect) {
        return std::visit([](const auto& value) { using E=std::decay_t<decltype(value)>;if constexpr(std::is_same_v<E,model::DropShadowEffect>)return value.enabled&&value.parameters.opacity_percent>0;else return value.enabled; }, effect);
    });
}

bool applyLayerEffects(
    creative_suite::media::RgbaFrame& frame,
    const std::vector<model::LayerEffect>& effects,
    const std::function<bool()>& should_cancel,
    const EffectTimingRecorder& record_effect_timing,
    detail::LayerEffectWorkerPool* requested_worker_pool)
{
    if (effects.empty()) return true;
    if (!model::validLayerEffects(effects)) {
        throw std::invalid_argument("The layer contains invalid effect parameters.");
    }
    if (!hasEnabledLayerEffects(effects)) return true;
    validateFrame(frame);
    auto& worker_pool = requested_worker_pool != nullptr
        ? *requested_worker_pool
        : detail::sharedLayerEffectWorkerPool();

    for (const auto& effect : effects) {
        if (should_cancel && should_cancel()) return false;
        const bool completed = std::visit([&](const auto& value) {
            using Effect = std::decay_t<decltype(value)>;
            if (!value.enabled) return true;
            if constexpr (std::is_same_v<Effect, model::GaussianBlurEffect>) {
                return measureEffect(record_effect_timing, LayerEffectKind::GaussianBlur,
                    [&] {
                        const auto result = creative_suite::effects::applyGaussianBlur(frame, value.radius_pixels, should_cancel,
                            [&worker_pool](auto count, const auto& cancel, const auto& operation) {
                                return worker_pool.parallelFor(count, cancel, operation);
                            });
                        if (result == creative_suite::effects::ProcessingResult::InvalidInput)
                            throw std::invalid_argument("The shared Gaussian Blur processor rejected a valid Motion frame.");
                        return result == creative_suite::effects::ProcessingResult::Completed;
                    });
            } else if constexpr(std::is_same_v<Effect,model::DropShadowEffect>) {
                if(value.parameters.opacity_percent==0)return true;
                throw std::invalid_argument("Drop Shadow requires the origin-aware expanded layer pipeline");
            } else {
                return measureEffect(record_effect_timing, LayerEffectKind::ColorAdjustment,
                    [&] {
                        const auto result = creative_suite::effects::applyColorAdjustment(
                            frame,
                            {value.brightness, value.contrast_percent,
                             value.saturation_percent},
                            should_cancel);
                        if (result == creative_suite::effects::ProcessingResult::InvalidInput) {
                            throw std::invalid_argument(
                                "The shared Color Adjustment processor rejected a valid Motion frame.");
                        }
                        return result == creative_suite::effects::ProcessingResult::Completed;
                    });
            }
        }, effect);
        if (!completed) return false;
    }
    return true;
}

} // namespace motion::ui
