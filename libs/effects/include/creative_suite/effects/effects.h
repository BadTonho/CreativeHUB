#pragma once

#include <creative_suite/media/video_frame.h>

#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace creative_suite::effects {

struct ParameterValue {
    std::string id;
    double value = 0.0;

    friend bool operator==(const ParameterValue&, const ParameterValue&) = default;
};

struct EffectInstance {
    std::string id;
    std::vector<ParameterValue> parameters;
    bool enabled = true;

    friend bool operator==(const EffectInstance&, const EffectInstance&) = default;
};

struct ParameterDefinition {
    std::string_view id;
    std::string_view name;
    double minimum = 0.0;
    double maximum = 1.0;
    double default_value = 0.0;
    double step = 1.0;
};

struct Definition {
    std::string_view id;
    std::string_view name;
    std::string_view category_id;
    std::span<const ParameterDefinition> parameters;
};

struct ColorAdjustmentParameters {
    // Brightness is [-100, 100]; contrast and saturation are [0, 200] percent.
    double brightness = 0.0;
    double contrast_percent = 100.0;
    double saturation_percent = 100.0;
};

enum class ProcessingResult {
    Completed,
    Cancelled,
    InvalidInput,
};

[[nodiscard]] std::span<const Definition> builtInEffects() noexcept;
[[nodiscard]] const Definition* findDefinition(std::string_view id) noexcept;
[[nodiscard]] EffectInstance makeDefaultInstance(std::string_view id);
[[nodiscard]] bool isValid(const EffectInstance& effect) noexcept;
[[nodiscard]] bool isValidStack(std::span<const EffectInstance> effects) noexcept;
[[nodiscard]] double parameterValue(
    const EffectInstance& effect,
    std::string_view parameter_id) noexcept;
[[nodiscard]] bool setParameterValue(
    EffectInstance& effect,
    std::string_view parameter_id,
    double value) noexcept;

// Applies the effect stack in order to the frame's RGB channels. Alpha bytes
// are preserved. The caller owns and may reuse the frame storage.
[[nodiscard]] bool applyStack(
    media::RgbaFrame& frame,
    std::span<const EffectInstance> effects) noexcept;

// Applies brightness, contrast around 0.5, and Rec. 709 saturation in one
// pixel pass, rounding RGB only after all three operations. Cancellation is
// checked before each row. A cancelled call may have changed earlier rows, so
// the caller must discard that frame. Invalid input leaves the frame unchanged.
// Callback exceptions propagate.
[[nodiscard]] ProcessingResult applyColorAdjustment(
    media::RgbaFrame& frame,
    const ColorAdjustmentParameters& parameters,
    const std::function<bool()>& should_cancel = {});

}  // namespace creative_suite::effects
