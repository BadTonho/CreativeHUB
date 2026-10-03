#pragma once

#include <creative_suite/media/video_frame.h>

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

}  // namespace creative_suite::effects
