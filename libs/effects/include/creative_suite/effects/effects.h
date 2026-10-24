#pragma once

#include <creative_suite/media/video_frame.h>
#include <creative_suite/effects/color_curves.h>

#include <functional>
#include <array>
#include <algorithm>
#include <cmath>
#include <span>
#include <optional>
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
    std::optional<VideoCurvesParameters> video_curves;

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
    // Legacy brightness remains for fused callers and maps to equal RGB offsets.
    // The expanded controls use normalized encoded RGB values, not a physical
    // temperature or color-management model.
    double brightness = 0.0;
    double contrast_percent = 100.0;
    double saturation_percent = 100.0;
    double red_gain_percent = 100.0;
    double green_gain_percent = 100.0;
    double blue_gain_percent = 100.0;
    double lift_red = 0.0;
    double lift_green = 0.0;
    double lift_blue = 0.0;
    double gamma_red = 100.0;
    double gamma_green = 100.0;
    double gamma_blue = 100.0;
    double offset_red = 0.0;
    double offset_green = 0.0;
    double offset_blue = 0.0;
    double pivot_percent = 50.0;
    double temperature = 0.0;
    double tint = 0.0;
    double log_shadows_red = 0.0;
    double log_shadows_green = 0.0;
    double log_shadows_blue = 0.0;
    double log_midtones_red = 0.0;
    double log_midtones_green = 0.0;
    double log_midtones_blue = 0.0;
    double log_highlights_red = 0.0;
    double log_highlights_green = 0.0;
    double log_highlights_blue = 0.0;
    std::optional<VideoCurvesParameters> video_curves;
};

[[nodiscard]] bool isValidColorAdjustment(
    const ColorAdjustmentParameters& parameters) noexcept;

[[nodiscard]] inline std::array<double, 4> colorAdjustmentAffine(
    const ColorAdjustmentParameters& parameters) noexcept {
    const double scale = parameters.contrast_percent / 100.0;
    const double pivot = parameters.pivot_percent / 100.0;
    const double temperature = parameters.temperature / 400.0;
    const double tint = parameters.tint / 400.0;
    const std::array<double, 3> offsets{
        parameters.brightness + parameters.offset_red,
        parameters.brightness + parameters.offset_green,
        parameters.brightness + parameters.offset_blue};
    const std::array<double, 3> relative_color{
        temperature + tint, -2.0 * tint, -temperature + tint};
    return {scale,
        (offsets[0] / 100.0 - pivot) * scale + pivot + relative_color[0],
        (offsets[1] / 100.0 - pivot) * scale + pivot + relative_color[1],
        (offsets[2] / 100.0 - pivot) * scale + pivot + relative_color[2]};
}

// Encoded straight RGB evaluation; callers validate parameters. The order is
// offset/contrast/relative temperature and tint, shadow-weighted lift, gamma,
// Rec. 709 saturation, per-channel gain, then separate log tonal offsets.
// Log ranges use soft Rec. 709 luma weights on the corrected encoded RGB.
// Alpha is untouched by callers.
[[nodiscard]] inline std::array<unsigned char,3> colorAdjustmentRgb(
    std::array<double,3> rgb,const ColorAdjustmentParameters& parameters) noexcept {
    const auto affine = colorAdjustmentAffine(parameters);
    const std::array<double, 3> lifts{parameters.lift_red, parameters.lift_green,
                                      parameters.lift_blue};
    const std::array<double, 3> gammas{parameters.gamma_red, parameters.gamma_green,
                                       parameters.gamma_blue};
    for (int channel = 0; channel < 3; ++channel) {
        rgb[channel] = rgb[channel] * affine[0] + affine[channel + 1];
    }
    const double shadow_luma = .2126 * rgb[0] + .7152 * rgb[1] + .0722 * rgb[2];
    const double shadow_amount = std::clamp(1.0 - shadow_luma, 0.0, 1.0);
    const double shadow_weight = shadow_amount * shadow_amount;
    for (int channel = 0; channel < 3; ++channel) {
        rgb[channel] += lifts[channel] / 100.0 * shadow_weight;
        if (gammas[channel] != 100.0)
            rgb[channel] = std::pow(std::max(rgb[channel], 0.0), 100.0 / gammas[channel]);
    }
    const double luma=.2126*rgb[0]+.7152*rgb[1]+.0722*rgb[2];
    const std::array<double, 3> gains{parameters.red_gain_percent,
                                      parameters.green_gain_percent,
                                      parameters.blue_gain_percent};
    for (int channel = 0; channel < 3; ++channel) {
        rgb[channel] = (luma + (rgb[channel] - luma) *
            (parameters.saturation_percent / 100.)) * (gains[channel] / 100.);
    }
    const double log_luma = std::clamp(
        .2126 * rgb[0] + .7152 * rgb[1] + .0722 * rgb[2], 0.0, 1.0);
    const auto smoothstep = [](double edge0, double edge1, double value) {
        const double t = std::clamp((value - edge0) / (edge1 - edge0), 0.0, 1.0);
        return t * t * (3.0 - 2.0 * t);
    };
    const double shadows = 1.0 - smoothstep(.20, .45, log_luma);
    const double highlights = smoothstep(.55, .80, log_luma);
    const double midtones = std::clamp(1.0 - shadows - highlights, 0.0, 1.0);
    const std::array<double, 3> log_shadows{
        parameters.log_shadows_red, parameters.log_shadows_green,
        parameters.log_shadows_blue};
    const std::array<double, 3> log_midtones{
        parameters.log_midtones_red, parameters.log_midtones_green,
        parameters.log_midtones_blue};
    const std::array<double, 3> log_highlights{
        parameters.log_highlights_red, parameters.log_highlights_green,
        parameters.log_highlights_blue};
    std::array<unsigned char,3> result{};
    for (int channel = 0; channel < 3; ++channel) {
        rgb[channel] += (log_shadows[channel] * shadows +
            log_midtones[channel] * midtones +
            log_highlights[channel] * highlights) / 100.0;
        result[channel] = static_cast<unsigned char>(std::lround(
            std::clamp(rgb[channel], 0.0, 1.0) * 255.0));
    }
    return result;
}

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

// Each returned pass retains the built-in stack's byte rounding boundary.
// Invalid stacks are rejected before any frame is changed.
[[nodiscard]] std::optional<std::vector<ColorAdjustmentParameters>> colorAdjustmentPasses(
    std::span<const EffectInstance> effects);

// Applies normalized RGB offset, contrast about pivot, relative temperature / tint,
// shadow-weighted Lift, per-channel Gamma, Rec. 709 saturation and per-channel
// Gain in one pixel pass. Alpha and row padding are preserved; RGB rounds once
// after the full correction. Cancellation is checked before each row. A cancelled
// call may have changed earlier rows, so the caller must discard that frame.
// Invalid input leaves the frame unchanged.
// Callback exceptions propagate.
[[nodiscard]] ProcessingResult applyColorAdjustment(
    media::RgbaFrame& frame,
    const ColorAdjustmentParameters& parameters,
    const std::function<bool()>& should_cancel = {});

}  // namespace creative_suite::effects
