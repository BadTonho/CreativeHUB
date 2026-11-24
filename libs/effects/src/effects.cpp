#include <creative_suite/effects/effects.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace creative_suite::effects {
namespace {

constexpr std::array<ParameterDefinition, 1> grayscale_parameters{{
    {"amount", "Amount", 0.0, 100.0, 100.0, 1.0}}};
constexpr std::array<ParameterDefinition, 1> brightness_parameters{{
    {"amount", "Brightness", -100.0, 100.0, 0.0, 1.0}}};
constexpr std::array<ParameterDefinition, 1> contrast_parameters{{
    {"amount", "Contrast", 0.0, 200.0, 100.0, 1.0}}};
constexpr std::array<ParameterDefinition, 1> saturation_parameters{{
    {"amount", "Saturation", 0.0, 200.0, 100.0, 1.0}}};
constexpr std::array<ParameterDefinition, 26> primary_grade_parameters{{
    {"lift_red", "Lift red", -100.0, 100.0, 0.0, 1.0},
    {"lift_green", "Lift green", -100.0, 100.0, 0.0, 1.0},
    {"lift_blue", "Lift blue", -100.0, 100.0, 0.0, 1.0},
    {"gamma_red", "Gamma red", 25.0, 400.0, 100.0, 1.0},
    {"gamma_green", "Gamma green", 25.0, 400.0, 100.0, 1.0},
    {"gamma_blue", "Gamma blue", 25.0, 400.0, 100.0, 1.0},
    {"red_gain", "Red gain", 0.0, 200.0, 100.0, 1.0},
    {"green_gain", "Green gain", 0.0, 200.0, 100.0, 1.0},
    {"blue_gain", "Blue gain", 0.0, 200.0, 100.0, 1.0},
    {"offset_red", "Offset red", -100.0, 100.0, 0.0, 1.0},
    {"offset_green", "Offset green", -100.0, 100.0, 0.0, 1.0},
    {"offset_blue", "Offset blue", -100.0, 100.0, 0.0, 1.0},
    {"contrast", "Contrast", 0.0, 200.0, 100.0, 1.0},
    {"saturation", "Saturation", 0.0, 200.0, 100.0, 1.0},
    {"pivot", "Pivot", 0.0, 100.0, 50.0, 1.0},
    {"temperature", "Temperature", -100.0, 100.0, 0.0, 1.0},
    {"tint", "Tint", -100.0, 100.0, 0.0, 1.0},
    {"log_shadows_red", "Log shadows red", -100.0, 100.0, 0.0, 1.0},
    {"log_shadows_green", "Log shadows green", -100.0, 100.0, 0.0, 1.0},
    {"log_shadows_blue", "Log shadows blue", -100.0, 100.0, 0.0, 1.0},
    {"log_midtones_red", "Log midtones red", -100.0, 100.0, 0.0, 1.0},
    {"log_midtones_green", "Log midtones green", -100.0, 100.0, 0.0, 1.0},
    {"log_midtones_blue", "Log midtones blue", -100.0, 100.0, 0.0, 1.0},
    {"log_highlights_red", "Log highlights red", -100.0, 100.0, 0.0, 1.0},
    {"log_highlights_green", "Log highlights green", -100.0, 100.0, 0.0, 1.0},
    {"log_highlights_blue", "Log highlights blue", -100.0, 100.0, 0.0, 1.0}}};

constexpr std::array<ParameterDefinition, 13> color_blur_parameters{{
    {"mode", "Mode", 0.0, 2.0, 0.0, 1.0},
    {"radius_r", "Radius Red", 0.0, 1.0, 0.5, 0.01},
    {"radius_g", "Radius Green", 0.0, 1.0, 0.5, 0.01},
    {"radius_b", "Radius Blue", 0.0, 1.0, 0.5, 0.01},
    {"hv_ratio_r", "H/V Ratio Red", 0.0, 1.0, 0.5, 0.01},
    {"hv_ratio_g", "H/V Ratio Green", 0.0, 1.0, 0.5, 0.01},
    {"hv_ratio_b", "H/V Ratio Blue", 0.0, 1.0, 0.5, 0.01},
    {"scaling_r", "Scaling Red", 0.0, 1.0, 0.25, 0.01},
    {"scaling_g", "Scaling Green", 0.0, 1.0, 0.25, 0.01},
    {"scaling_b", "Scaling Blue", 0.0, 1.0, 0.25, 0.01},
    {"coring_softness", "Coring Softness", 0.0, 100.0, 0.0, 0.1},
    {"level", "Level", 0.0, 100.0, 0.0, 0.1},
    {"mix", "Mix", 0.0, 100.0, 100.0, 0.1}}};

constexpr std::array<ParameterDefinition, 11> color_key_parameters{{
    {"input_enabled", "Key Input Enabled", 0.0, 1.0, 1.0, 1.0},
    {"input_gain", "Key Input Gain", 0.0, 10.0, 1.0, 0.01},
    {"input_offset", "Key Input Offset", -1.0, 1.0, 0.0, 0.01},
    {"input_blur_r", "Key Input Blur R", 0.0, 100.0, 0.0, 0.1},
    {"input_blur_hv", "Key Input Blur H/V", 0.0, 100.0, 0.0, 0.1},
    {"output_enabled", "Key Output Enabled", 0.0, 1.0, 0.0, 1.0},
    {"output_gain", "Key Output Gain", 0.0, 10.0, 1.0, 0.01},
    {"output_offset", "Key Output Offset", -1.0, 1.0, 0.0, 0.01},
    {"qualifier_enabled", "Qualifier Enabled", 0.0, 1.0, 0.0, 1.0},
    {"qualifier_gain", "Qualifier Gain", 0.0, 10.0, 1.0, 0.01},
    {"qualifier_offset", "Qualifier Offset", -1.0, 1.0, 0.0, 0.01}}};

constexpr std::array<ParameterDefinition, 11> input_sizing_parameters{{
    {"mode", "Mode", 0.0, 4.0, 1.0, 1.0},
    {"pan", "Pan", -10.0, 10.0, 0.0, 0.01},
    {"tilt", "Tilt", -10.0, 10.0, 0.0, 0.01},
    {"zoom", "Zoom", 0.0, 10.0, 1.0, 0.01},
    {"rotate", "Rotate", -360.0, 360.0, 0.0, 0.1},
    {"width", "Width", 0.0, 10.0, 1.0, 0.01},
    {"height", "Height", 0.0, 10.0, 1.0, 0.01},
    {"pitch", "Pitch", -90.0, 90.0, 0.0, 0.1},
    {"yaw", "Yaw", -90.0, 90.0, 0.0, 0.1},
    {"flip_h", "Flip Horizontal", 0.0, 1.0, 0.0, 1.0},
    {"flip_v", "Flip Vertical", 0.0, 1.0, 0.0, 1.0}}};

constexpr std::array<ParameterDefinition, 41> color_slice_parameters{{
    {"global_den", "Global Density", -1.0, 1.0, 0.0, 0.01},
    {"global_den_depth", "Global Density Depth", -1.0, 1.0, 0.0, 0.01},
    {"global_sat", "Global Saturation", 0.0, 2.0, 1.0, 0.01},
    {"global_sat_balance", "Global Saturation Balance", -1.0, 1.0, 0.0, 0.01},
    {"global_sat_depth", "Global Saturation Depth", -1.0, 1.0, 0.0, 0.01},
    {"global_hue", "Global Hue", -1.0, 1.0, 0.0, 0.01},
    {"red_enabled", "Red Enabled", 0.0, 1.0, 1.0, 1.0},
    {"red_center", "Red Center", -1.0, 1.0, 0.0, 0.01},
    {"red_hue", "Red Hue", -1.0, 1.0, 0.0, 0.01},
    {"red_density", "Red Density", -1.0, 1.0, 0.0, 0.01},
    {"red_sat", "Red Saturation", 0.0, 2.0, 1.0, 0.01},
    {"skin_enabled", "Skin Enabled", 0.0, 1.0, 1.0, 1.0},
    {"skin_center", "Skin Center", -1.0, 1.0, 0.0, 0.01},
    {"skin_hue", "Skin Hue", -1.0, 1.0, 0.0, 0.01},
    {"skin_density", "Skin Density", -1.0, 1.0, 0.0, 0.01},
    {"skin_sat", "Skin Saturation", 0.0, 2.0, 1.0, 0.01},
    {"yellow_enabled", "Yellow Enabled", 0.0, 1.0, 1.0, 1.0},
    {"yellow_center", "Yellow Center", -1.0, 1.0, 0.0, 0.01},
    {"yellow_hue", "Yellow Hue", -1.0, 1.0, 0.0, 0.01},
    {"yellow_density", "Yellow Density", -1.0, 1.0, 0.0, 0.01},
    {"yellow_sat", "Yellow Saturation", 0.0, 2.0, 1.0, 0.01},
    {"green_enabled", "Green Enabled", 0.0, 1.0, 1.0, 1.0},
    {"green_center", "Green Center", -1.0, 1.0, 0.0, 0.01},
    {"green_hue", "Green Hue", -1.0, 1.0, 0.0, 0.01},
    {"green_density", "Green Density", -1.0, 1.0, 0.0, 0.01},
    {"green_sat", "Green Saturation", 0.0, 2.0, 1.0, 0.01},
    {"cyan_enabled", "Cyan Enabled", 0.0, 1.0, 1.0, 1.0},
    {"cyan_center", "Cyan Center", -1.0, 1.0, 0.0, 0.01},
    {"cyan_hue", "Cyan Hue", -1.0, 1.0, 0.0, 0.01},
    {"cyan_density", "Cyan Density", -1.0, 1.0, 0.0, 0.01},
    {"cyan_sat", "Cyan Saturation", 0.0, 2.0, 1.0, 0.01},
    {"blue_enabled", "Blue Enabled", 0.0, 1.0, 1.0, 1.0},
    {"blue_center", "Blue Center", -1.0, 1.0, 0.0, 0.01},
    {"blue_hue", "Blue Hue", -1.0, 1.0, 0.0, 0.01},
    {"blue_density", "Blue Density", -1.0, 1.0, 0.0, 0.01},
    {"blue_sat", "Blue Saturation", 0.0, 2.0, 1.0, 0.01},
    {"magenta_enabled", "Magenta Enabled", 0.0, 1.0, 1.0, 1.0},
    {"magenta_center", "Magenta Center", -1.0, 1.0, 0.0, 0.01},
    {"magenta_hue", "Magenta Hue", -1.0, 1.0, 0.0, 0.01},
    {"magenta_density", "Magenta Density", -1.0, 1.0, 0.0, 0.01},
    {"magenta_sat", "Magenta Saturation", 0.0, 2.0, 1.0, 0.01}}};

constexpr std::array<Definition, 9> definitions{{
    {"video.grayscale", "Grayscale", "video", grayscale_parameters},
    {"video.brightness", "Brightness", "video", brightness_parameters},
    {"video.contrast", "Contrast", "video", contrast_parameters},
    {"video.saturation", "Saturation", "video", saturation_parameters},
    {"video.primary_grade", "Primary Grade", "video", primary_grade_parameters},
    {"video.color_blur", "Color Blur", "video", color_blur_parameters},
    {"video.color_key", "Color Key", "video", color_key_parameters},
    {"video.input_sizing", "Input Sizing", "video", input_sizing_parameters},
    {"video.color_slice", "Color Slice", "video", color_slice_parameters},
}};

const ParameterDefinition* findParameter(
    const Definition& definition,
    std::string_view id) noexcept {
    const auto found = std::find_if(
        definition.parameters.begin(), definition.parameters.end(),
        [id](const ParameterDefinition& parameter) { return parameter.id == id; });
    return found == definition.parameters.end() ? nullptr : &*found;
}

bool validFrame(const media::RgbaFrame& frame) noexcept {
    if (frame.width <= 0 || frame.height <= 0 ||
        frame.width > std::numeric_limits<int>::max() / 4 ||
        frame.stride < frame.width * 4) {
        return false;
    }
    const auto required = static_cast<std::uint64_t>(frame.stride) *
        static_cast<std::uint64_t>(frame.height);
    return required <= frame.rgba_pixels.size();
}

std::uint8_t toByte(double value) noexcept {
    return static_cast<std::uint8_t>(std::clamp(std::lround(value), 0L, 255L));
}

std::uint8_t toByteFromUnit(double value) noexcept {
    return static_cast<std::uint8_t>(
        std::lround(std::clamp(value, 0.0, 1.0) * 255.0));
}

bool validColorAdjustment(const ColorAdjustmentParameters& parameters) noexcept {
    if (parameters.video_curves) {
        return validVideoCurves(*parameters.video_curves) &&
            parameters.brightness == 0.0 && parameters.contrast_percent == 100.0 &&
            parameters.saturation_percent == 100.0 &&
            parameters.red_gain_percent == 100.0 && parameters.green_gain_percent == 100.0 &&
            parameters.blue_gain_percent == 100.0 && parameters.lift_red == 0.0 &&
            parameters.lift_green == 0.0 && parameters.lift_blue == 0.0 &&
            parameters.gamma_red == 100.0 && parameters.gamma_green == 100.0 &&
            parameters.gamma_blue == 100.0 && parameters.offset_red == 0.0 &&
            parameters.offset_green == 0.0 && parameters.offset_blue == 0.0 &&
            parameters.pivot_percent == 50.0 && parameters.temperature == 0.0 &&
            parameters.tint == 0.0 && parameters.log_shadows_red == 0.0 &&
            parameters.log_shadows_green == 0.0 && parameters.log_shadows_blue == 0.0 &&
            parameters.log_midtones_red == 0.0 && parameters.log_midtones_green == 0.0 &&
            parameters.log_midtones_blue == 0.0 && parameters.log_highlights_red == 0.0 &&
            parameters.log_highlights_green == 0.0 && parameters.log_highlights_blue == 0.0;
    }
    return std::isfinite(parameters.brightness) &&
        parameters.brightness >= -100.0 && parameters.brightness <= 100.0 &&
        std::isfinite(parameters.contrast_percent) &&
        parameters.contrast_percent >= 0.0 && parameters.contrast_percent <= 200.0 &&
        std::isfinite(parameters.saturation_percent) &&
        parameters.saturation_percent >= 0.0 && parameters.saturation_percent <= 200.0 &&
        std::isfinite(parameters.red_gain_percent) && parameters.red_gain_percent >= 0.0 && parameters.red_gain_percent <= 200.0 &&
        std::isfinite(parameters.green_gain_percent) && parameters.green_gain_percent >= 0.0 && parameters.green_gain_percent <= 200.0 &&
        std::isfinite(parameters.blue_gain_percent) && parameters.blue_gain_percent >= 0.0 && parameters.blue_gain_percent <= 200.0 &&
        std::isfinite(parameters.lift_red) && parameters.lift_red >= -100.0 && parameters.lift_red <= 100.0 &&
        std::isfinite(parameters.lift_green) && parameters.lift_green >= -100.0 && parameters.lift_green <= 100.0 &&
        std::isfinite(parameters.lift_blue) && parameters.lift_blue >= -100.0 && parameters.lift_blue <= 100.0 &&
        std::isfinite(parameters.gamma_red) && parameters.gamma_red >= 25.0 && parameters.gamma_red <= 400.0 &&
        std::isfinite(parameters.gamma_green) && parameters.gamma_green >= 25.0 && parameters.gamma_green <= 400.0 &&
        std::isfinite(parameters.gamma_blue) && parameters.gamma_blue >= 25.0 && parameters.gamma_blue <= 400.0 &&
        std::isfinite(parameters.offset_red) && parameters.offset_red >= -100.0 && parameters.offset_red <= 100.0 &&
        std::isfinite(parameters.offset_green) && parameters.offset_green >= -100.0 && parameters.offset_green <= 100.0 &&
        std::isfinite(parameters.offset_blue) && parameters.offset_blue >= -100.0 && parameters.offset_blue <= 100.0 &&
        std::isfinite(parameters.pivot_percent) && parameters.pivot_percent >= 0.0 && parameters.pivot_percent <= 100.0 &&
        std::isfinite(parameters.temperature) && parameters.temperature >= -100.0 && parameters.temperature <= 100.0 &&
        std::isfinite(parameters.tint) && parameters.tint >= -100.0 && parameters.tint <= 100.0 &&
        std::isfinite(parameters.log_shadows_red) && parameters.log_shadows_red >= -100.0 && parameters.log_shadows_red <= 100.0 &&
        std::isfinite(parameters.log_shadows_green) && parameters.log_shadows_green >= -100.0 && parameters.log_shadows_green <= 100.0 &&
        std::isfinite(parameters.log_shadows_blue) && parameters.log_shadows_blue >= -100.0 && parameters.log_shadows_blue <= 100.0 &&
        std::isfinite(parameters.log_midtones_red) && parameters.log_midtones_red >= -100.0 && parameters.log_midtones_red <= 100.0 &&
        std::isfinite(parameters.log_midtones_green) && parameters.log_midtones_green >= -100.0 && parameters.log_midtones_green <= 100.0 &&
        std::isfinite(parameters.log_midtones_blue) && parameters.log_midtones_blue >= -100.0 && parameters.log_midtones_blue <= 100.0 &&
        std::isfinite(parameters.log_highlights_red) && parameters.log_highlights_red >= -100.0 && parameters.log_highlights_red <= 100.0 &&
        std::isfinite(parameters.log_highlights_green) && parameters.log_highlights_green >= -100.0 && parameters.log_highlights_green <= 100.0 &&
        std::isfinite(parameters.log_highlights_blue) && parameters.log_highlights_blue >= -100.0 && parameters.log_highlights_blue <= 100.0;
}

ColorAdjustmentParameters primaryGradeParameters(const EffectInstance& effect) noexcept {
    ColorAdjustmentParameters parameters;
    parameters.contrast_percent = parameterValue(effect, "contrast");
    parameters.saturation_percent = parameterValue(effect, "saturation");
    parameters.red_gain_percent = parameterValue(effect, "red_gain");
    parameters.green_gain_percent = parameterValue(effect, "green_gain");
    parameters.blue_gain_percent = parameterValue(effect, "blue_gain");
    parameters.lift_red = parameterValue(effect, "lift_red");
    parameters.lift_green = parameterValue(effect, "lift_green");
    parameters.lift_blue = parameterValue(effect, "lift_blue");
    parameters.gamma_red = parameterValue(effect, "gamma_red");
    parameters.gamma_green = parameterValue(effect, "gamma_green");
    parameters.gamma_blue = parameterValue(effect, "gamma_blue");
    parameters.offset_red = parameterValue(effect, "offset_red");
    parameters.offset_green = parameterValue(effect, "offset_green");
    parameters.offset_blue = parameterValue(effect, "offset_blue");
    parameters.pivot_percent = parameterValue(effect, "pivot");
    parameters.temperature = parameterValue(effect, "temperature");
    parameters.tint = parameterValue(effect, "tint");
    parameters.log_shadows_red = parameterValue(effect, "log_shadows_red");
    parameters.log_shadows_green = parameterValue(effect, "log_shadows_green");
    parameters.log_shadows_blue = parameterValue(effect, "log_shadows_blue");
    parameters.log_midtones_red = parameterValue(effect, "log_midtones_red");
    parameters.log_midtones_green = parameterValue(effect, "log_midtones_green");
    parameters.log_midtones_blue = parameterValue(effect, "log_midtones_blue");
    parameters.log_highlights_red = parameterValue(effect, "log_highlights_red");
    parameters.log_highlights_green = parameterValue(effect, "log_highlights_green");
    parameters.log_highlights_blue = parameterValue(effect, "log_highlights_blue");
    return parameters;
}

}  // namespace

bool isValidColorAdjustment(const ColorAdjustmentParameters& parameters) noexcept {
    return validColorAdjustment(parameters);
}

std::span<const Definition> builtInEffects() noexcept {
    return definitions;
}

const Definition* findDefinition(std::string_view id) noexcept {
    const auto found = std::find_if(
        definitions.begin(), definitions.end(),
        [id](const Definition& definition) { return definition.id == id; });
    return found == definitions.end() ? nullptr : &*found;
}

EffectInstance makeDefaultInstance(std::string_view id) {
    if (id == "video.curves") {
        EffectInstance result;
        result.id = "video.curves";
        result.video_curves.emplace();
        return result;
    }
    const auto* definition = findDefinition(id);
    if (definition == nullptr) return {};
    EffectInstance result;
    result.id = std::string(definition->id);
    result.parameters.reserve(definition->parameters.size());
    for (const auto& parameter : definition->parameters) {
        result.parameters.push_back({std::string(parameter.id), parameter.default_value});
    }
    return result;
}

bool isValid(const EffectInstance& effect) noexcept {
    if (effect.id == "video.curves")
        return effect.parameters.empty() && effect.video_curves &&
            validVideoCurves(*effect.video_curves);
    if (effect.video_curves) return false;
    const auto* definition = findDefinition(effect.id);
    if (definition == nullptr || effect.parameters.size() != definition->parameters.size()) {
        return false;
    }
    for (const auto& parameter : effect.parameters) {
        const auto* spec = findParameter(*definition, parameter.id);
        if (spec == nullptr || !std::isfinite(parameter.value) ||
            parameter.value < spec->minimum || parameter.value > spec->maximum) {
            return false;
        }
    }
    return true;
}

bool isValidStack(std::span<const EffectInstance> effects) noexcept {
    return std::all_of(effects.begin(), effects.end(), isValid);
}

double parameterValue(
    const EffectInstance& effect,
    std::string_view parameter_id) noexcept {
    const auto found = std::find_if(
        effect.parameters.begin(), effect.parameters.end(),
        [parameter_id](const ParameterValue& value) { return value.id == parameter_id; });
    return found == effect.parameters.end() ? 0.0 : found->value;
}

bool setParameterValue(
    EffectInstance& effect,
    std::string_view parameter_id,
    double value) noexcept {
    const auto* definition = findDefinition(effect.id);
    if (definition == nullptr || !std::isfinite(value)) return false;
    const auto* spec = findParameter(*definition, parameter_id);
    if (spec == nullptr || value < spec->minimum || value > spec->maximum) return false;
    const auto found = std::find_if(
        effect.parameters.begin(), effect.parameters.end(),
        [parameter_id](const ParameterValue& current) { return current.id == parameter_id; });
    if (found == effect.parameters.end()) return false;
    found->value = value;
    return true;
}

std::optional<std::vector<ColorAdjustmentParameters>> colorAdjustmentPasses(
    std::span<const EffectInstance> effects) {
    if (!isValidStack(effects)) return std::nullopt;
    std::vector<ColorAdjustmentParameters> result;
    result.reserve(effects.size());
    for (const auto& effect : effects) {
        if (!effect.enabled) continue;
        ColorAdjustmentParameters pass;
        if (effect.id == "video.curves") {
            if (identityVideoCurves(*effect.video_curves)) continue;
            pass.video_curves = effect.video_curves;
            result.push_back(std::move(pass));
            continue;
        }
        const auto amount = parameterValue(effect, "amount");
        if (effect.id == "video.brightness") pass.brightness = amount;
        else if (effect.id == "video.contrast") pass.contrast_percent = amount;
        else if (effect.id == "video.saturation") pass.saturation_percent = amount;
        else if (effect.id == "video.grayscale") pass.saturation_percent = 100.0 - amount;
        else if (effect.id == "video.primary_grade") pass = primaryGradeParameters(effect);
        else return std::nullopt;
        result.push_back(pass);
    }
    return result;
}

void blurChannelSeparable(
    const media::RgbaFrame& src, int channel, double sigma_x, double sigma_y,
    std::vector<float>& out_channel) {
    const int w = src.width;
    const int h = src.height;
    out_channel.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
    for (int y = 0; y < h; ++y) {
        const auto* row = src.rgba_pixels.data() + static_cast<std::size_t>(y) * src.stride;
        for (int x = 0; x < w; ++x) {
            out_channel[static_cast<std::size_t>(y) * w + x] =
                static_cast<float>(row[x * 4 + channel]);
        }
    }
    const auto make_kernel = [](double sigma) {
        const int radius = std::clamp(static_cast<int>(std::ceil(sigma * 2.5)), 0, 48);
        std::vector<float> k(static_cast<std::size_t>(radius) * 2 + 1, 1.0F);
        if (radius == 0 || sigma <= 0.05) return k;
        float sum = 0.0F;
        const double inv2s2 = 1.0 / (2.0 * sigma * sigma);
        for (int i = -radius; i <= radius; ++i) {
            const float v = static_cast<float>(std::exp(-(i * i) * inv2s2));
            k[static_cast<std::size_t>(i + radius)] = v;
            sum += v;
        }
        for (float& v : k) v /= sum;
        return k;
    };
    const auto kx = make_kernel(sigma_x);
    const auto ky = make_kernel(sigma_y);
    const int rx = static_cast<int>(kx.size() / 2);
    const int ry = static_cast<int>(ky.size() / 2);
    if (rx > 0) {
        std::vector<float> tmp(out_channel.size());
        for (int y = 0; y < h; ++y) {
            const float* in_row = out_channel.data() + static_cast<std::size_t>(y) * w;
            float* out_row = tmp.data() + static_cast<std::size_t>(y) * w;
            for (int x = 0; x < w; ++x) {
                float acc = 0.0F;
                for (int i = -rx; i <= rx; ++i) {
                    const int sx = std::clamp(x + i, 0, w - 1);
                    acc += in_row[sx] * kx[static_cast<std::size_t>(i + rx)];
                }
                out_row[x] = acc;
            }
        }
        out_channel.swap(tmp);
    }
    if (ry > 0) {
        std::vector<float> tmp(out_channel.size());
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                float acc = 0.0F;
                for (int i = -ry; i <= ry; ++i) {
                    const int sy = std::clamp(y + i, 0, h - 1);
                    acc += out_channel[static_cast<std::size_t>(sy) * w + x] *
                        ky[static_cast<std::size_t>(i + ry)];
                }
                tmp[static_cast<std::size_t>(y) * w + x] = acc;
            }
        }
        out_channel.swap(tmp);
    }
}

void applyColorBlurEffect(media::RgbaFrame& frame, const EffectInstance& effect) {
    const int mode = static_cast<int>(std::lround(parameterValue(effect, "mode")));
    const std::array<double, 3> radii{
        parameterValue(effect, "radius_r"),
        parameterValue(effect, "radius_g"),
        parameterValue(effect, "radius_b")};
    const std::array<double, 3> ratios{
        parameterValue(effect, "hv_ratio_r"),
        parameterValue(effect, "hv_ratio_g"),
        parameterValue(effect, "hv_ratio_b")};
    const std::array<double, 3> scalings{
        parameterValue(effect, "scaling_r"),
        parameterValue(effect, "scaling_g"),
        parameterValue(effect, "scaling_b")};
    const double coring = parameterValue(effect, "coring_softness") * 2.55;
    const double level = parameterValue(effect, "level") * 2.55;
    const double mix = std::clamp(parameterValue(effect, "mix") / 100.0, 0.0, 1.0);
    if (mix <= 1e-4) return;
    if (radii[0] == 0.5 && radii[1] == 0.5 && radii[2] == 0.5) return;

    const media::RgbaFrame src = frame;
    std::vector<float> blurred;
    for (int c = 0; c < 3; ++c) {
        const double delta = radii[c] - 0.5;
        if (std::abs(delta) < 1e-4) continue;
        const double scale_mult = std::max(0.1, scalings[c] * 4.0);
        const double hv = std::clamp(ratios[c], 0.05, 0.95);
        const double h_mult = 2.0 * hv;
        const double v_mult = 2.0 * (1.0 - hv);
        const double base_sigma = std::abs(delta) * 24.0 * scale_mult;
        blurChannelSeparable(src, c, base_sigma * h_mult, base_sigma * v_mult, blurred);
        for (int y = 0; y < frame.height; ++y) {
            auto* dst_row = frame.rgba_pixels.data() + static_cast<std::size_t>(y) * frame.stride;
            const auto* src_row = src.rgba_pixels.data() + static_cast<std::size_t>(y) * src.stride;
            for (int x = 0; x < frame.width; ++x) {
                const double orig = src_row[x * 4 + c];
                const double bval = blurred[static_cast<std::size_t>(y) * frame.width + x];
                double res = orig;
                if (mode == 0) {
                    res = (delta > 0.0) ? bval : orig + (orig - bval) * (std::abs(delta) * 2.0);
                } else if (mode == 1) {
                    const double diff = orig - bval;
                    const double adiff = std::abs(diff);
                    double coring_weight = 1.0;
                    if (level > 0.0) {
                        const double soft = std::max(1.0, coring);
                        coring_weight = std::clamp((adiff - level) / soft, 0.0, 1.0);
                    }
                    const double strength = (delta < 0.0 ? -delta : delta) * 3.0;
                    res = orig + diff * strength * coring_weight;
                } else {
                    const double thresh = level;
                    const double excess = std::max(0.0, bval - thresh);
                    res = orig + excess * std::max(0.0, delta) * 2.0;
                }
                dst_row[x * 4 + c] = toByte(orig + (res - orig) * mix);
            }
        }
    }
}

void applyColorKeyEffect(media::RgbaFrame& frame, const EffectInstance& effect) {
    const bool in_en = parameterValue(effect, "input_enabled") >= 0.5;
    const double in_gain = parameterValue(effect, "input_gain");
    const double in_off = parameterValue(effect, "input_offset") * 255.0;
    const double in_blur = parameterValue(effect, "input_blur_r");
    const bool out_en = parameterValue(effect, "output_enabled") >= 0.5;
    const double out_gain = parameterValue(effect, "output_gain");
    const double out_off = parameterValue(effect, "output_offset") * 255.0;
    const bool q_en = parameterValue(effect, "qualifier_enabled") >= 0.5;
    const double q_gain = parameterValue(effect, "qualifier_gain");
    const double q_off = parameterValue(effect, "qualifier_offset") * 255.0;

    if (in_blur > 0.05 && in_en) {
        std::vector<float> blurred_alpha;
        const double hv = std::clamp(parameterValue(effect, "input_blur_hv") / 100.0, 0.05, 0.95);
        blurChannelSeparable(frame, 3, in_blur * 0.5 * (2.0 * hv),
                             in_blur * 0.5 * (2.0 * (1.0 - hv)), blurred_alpha);
        for (int y = 0; y < frame.height; ++y) {
            auto* row = frame.rgba_pixels.data() + static_cast<std::size_t>(y) * frame.stride;
            for (int x = 0; x < frame.width; ++x) {
                row[x * 4 + 3] = toByte(blurred_alpha[static_cast<std::size_t>(y) * frame.width + x]);
            }
        }
    }
    for (int y = 0; y < frame.height; ++y) {
        auto* row = frame.rgba_pixels.data() + static_cast<std::size_t>(y) * frame.stride;
        for (int x = 0; x < frame.width; ++x) {
            double a = row[x * 4 + 3];
            if (in_en) a = std::clamp(a * in_gain + in_off, 0.0, 255.0);
            if (q_en) a = std::clamp(a * q_gain + q_off, 0.0, 255.0);
            if (out_en) a = std::clamp(a * out_gain + out_off, 0.0, 255.0);
            row[x * 4 + 3] = toByte(a);
        }
    }
}

void applyInputSizingEffect(media::RgbaFrame& frame, const EffectInstance& effect) {
    const double pan = parameterValue(effect, "pan");
    const double tilt = parameterValue(effect, "tilt");
    const double zoom = std::max(0.001, parameterValue(effect, "zoom"));
    const double rotate = parameterValue(effect, "rotate");
    const double width_s = std::max(0.001, parameterValue(effect, "width"));
    const double height_s = std::max(0.001, parameterValue(effect, "height"));
    const double pitch = parameterValue(effect, "pitch");
    const double yaw = parameterValue(effect, "yaw");
    const bool flip_h = parameterValue(effect, "flip_h") >= 0.5;
    const bool flip_v = parameterValue(effect, "flip_v") >= 0.5;
    if (std::abs(pan) < 1e-5 && std::abs(tilt) < 1e-5 && std::abs(zoom - 1.0) < 1e-5 &&
        std::abs(rotate) < 1e-5 && std::abs(width_s - 1.0) < 1e-5 &&
        std::abs(height_s - 1.0) < 1e-5 && std::abs(pitch) < 1e-5 &&
        std::abs(yaw) < 1e-5 && !flip_h && !flip_v) {
        return;
    }
    const media::RgbaFrame src = frame;
    const int w = frame.width;
    const int h = frame.height;
    const double cx = (w - 1) * 0.5;
    const double cy = (h - 1) * 0.5;
    const double rad = -rotate * 3.14159265358979323846 / 180.0;
    const double c = std::cos(rad);
    const double s = std::sin(rad);
    const double inv_sx = 1.0 / (zoom * width_s);
    const double inv_sy = 1.0 / (zoom * height_s);
    const double yaw_k = std::tan(yaw * 3.14159265358979323846 / 180.0) * 0.5;
    const double pitch_k = std::tan(pitch * 3.14159265358979323846 / 180.0) * 0.5;
    const double tx = pan * 0.1 * w;
    const double ty = tilt * 0.1 * h;

    for (int y = 0; y < h; ++y) {
        auto* dst = frame.rgba_pixels.data() + static_cast<std::size_t>(y) * frame.stride;
        for (int x = 0; x < w; ++x) {
            double nx = (x - cx) - tx;
            double ny = (y - cy) - ty;
            const double denom = std::max(0.15, 1.0 + (nx / std::max(1, w)) * yaw_k + (ny / std::max(1, h)) * pitch_k);
            nx /= denom;
            ny /= denom;
            double rx = (nx * c - ny * s) * inv_sx;
            double ry = (nx * s + ny * c) * inv_sy;
            if (flip_h) rx = -rx;
            if (flip_v) ry = -ry;
            const double sx = rx + cx;
            const double sy = ry + cy;
            if (sx < 0.0 || sx >= w - 1 || sy < 0.0 || sy >= h - 1) {
                dst[x * 4 + 0] = 0; dst[x * 4 + 1] = 0; dst[x * 4 + 2] = 0; dst[x * 4 + 3] = 0;
                continue;
            }
            const int x0 = static_cast<int>(sx);
            const int y0 = static_cast<int>(sy);
            const double fx = sx - x0;
            const double fy = sy - y0;
            const auto* p00 = src.rgba_pixels.data() + static_cast<std::size_t>(y0) * src.stride + x0 * 4;
            const auto* p10 = p00 + 4;
            const auto* p01 = p00 + src.stride;
            const auto* p11 = p01 + 4;
            for (int ch = 0; ch < 4; ++ch) {
                const double top = p00[ch] + (p10[ch] - p00[ch]) * fx;
                const double bot = p01[ch] + (p11[ch] - p01[ch]) * fx;
                dst[x * 4 + ch] = toByte(top + (bot - top) * fy);
            }
        }
    }
}

void applyColorSliceEffect(media::RgbaFrame& frame, const EffectInstance& effect) {
    struct Slice {
        double base_deg;
        bool enabled;
        double center;
        double hue;
        double density;
        double sat;
    };
    const std::array<const char*, 7> names{"red", "skin", "yellow", "green", "cyan", "blue", "magenta"};
    const std::array<double, 7> base_hues{0.0, 28.0, 58.0, 120.0, 185.0, 240.0, 305.0};
    std::array<Slice, 7> slices{};
    for (std::size_t i = 0; i < 7; ++i) {
        const std::string p(names[i]);
        slices[i] = {
            base_hues[i],
            parameterValue(effect, p + "_enabled") >= 0.5,
            parameterValue(effect, p + "_center"),
            parameterValue(effect, p + "_hue"),
            parameterValue(effect, p + "_density"),
            parameterValue(effect, p + "_sat"),
        };
    }
    const double g_den = parameterValue(effect, "global_den");
    const double g_sat = parameterValue(effect, "global_sat");
    const double g_hue = parameterValue(effect, "global_hue");

    for (int y = 0; y < frame.height; ++y) {
        auto* row = frame.rgba_pixels.data() + static_cast<std::size_t>(y) * frame.stride;
        for (int x = 0; x < frame.width; ++x, row += 4) {
            double r = row[0] / 255.0;
            double g = row[1] / 255.0;
            double b = row[2] / 255.0;
            const double cmax = std::max({r, g, b});
            const double cmin = std::min({r, g, b});
            const double delta = cmax - cmin;
            double h_deg = 0.0;
            if (delta > 1e-5) {
                if (cmax == r) h_deg = 60.0 * std::fmod(((g - b) / delta) + 6.0, 6.0);
                else if (cmax == g) h_deg = 60.0 * (((b - r) / delta) + 2.0);
                else h_deg = 60.0 * (((r - g) / delta) + 4.0);
            }
            const double s = (cmax > 1e-5) ? (delta / cmax) : 0.0;
            double v = cmax;

            double hue_shift = g_hue * 30.0;
            double sat_mult = g_sat;
            double den_shift = g_den;
            for (const auto& sl : slices) {
                if (!sl.enabled) continue;
                const double target = std::fmod(sl.base_deg + sl.center * 30.0 + 360.0, 360.0);
                double diff = std::abs(h_deg - target);
                if (diff > 180.0) diff = 360.0 - diff;
                if (diff >= 55.0) continue;
                const double w = 0.5 * (1.0 + std::cos(diff / 55.0 * 3.14159265358979323846)) * s;
                hue_shift += sl.hue * 30.0 * w;
                sat_mult *= (1.0 + (sl.sat - 1.0) * w);
                den_shift += sl.density * w;
            }
            const double nh = std::fmod(h_deg + hue_shift + 360.0, 360.0);
            const double ns = std::clamp(s * sat_mult, 0.0, 1.0);
            v = std::clamp(v * (1.0 - den_shift * 0.35 * s), 0.0, 1.0);

            const double C = v * ns;
            const double X = C * (1.0 - std::abs(std::fmod(nh / 60.0, 2.0) - 1.0));
            const double m = v - C;
            double rr = 0, gg = 0, bb = 0;
            if (nh < 60) { rr = C; gg = X; }
            else if (nh < 120) { rr = X; gg = C; }
            else if (nh < 180) { gg = C; bb = X; }
            else if (nh < 240) { gg = X; bb = C; }
            else if (nh < 300) { rr = X; bb = C; }
            else { rr = C; bb = X; }
            row[0] = toByteFromUnit(rr + m);
            row[1] = toByteFromUnit(gg + m);
            row[2] = toByteFromUnit(bb + m);
        }
    }
}

bool applyStack(
    media::RgbaFrame& frame,
    std::span<const EffectInstance> effects) noexcept {
    if (!validFrame(frame) || !isValidStack(effects)) return false;
    if (effects.empty()) return true;

    for (const auto& effect : effects) {
        if (!effect.enabled) continue;
        const auto amount = parameterValue(effect, "amount");
        const auto id = std::string_view(effect.id);
        if (id == "video.curves") {
            if (identityVideoCurves(*effect.video_curves)) continue;
            try {
                if (applyVideoCurvesFrame(frame, *effect.video_curves) !=
                    ProcessingResult::Completed) return false;
            } catch (...) { return false; }
            continue;
        }
        if (id == "video.color_blur") { applyColorBlurEffect(frame, effect); continue; }
        if (id == "video.color_key") { applyColorKeyEffect(frame, effect); continue; }
        if (id == "video.input_sizing") { applyInputSizingEffect(frame, effect); continue; }
        if (id == "video.color_slice") { applyColorSliceEffect(frame, effect); continue; }
        const auto primary_grade = id == "video.primary_grade"
            ? primaryGradeParameters(effect) : ColorAdjustmentParameters{};
        for (int y = 0; y < frame.height; ++y) {
            auto* pixel = frame.rgba_pixels.data() +
                static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.stride);
            for (int x = 0; x < frame.width; ++x, pixel += 4) {
                const double red = pixel[0];
                const double green = pixel[1];
                const double blue = pixel[2];
                if (id == "video.primary_grade") {
                    const auto adjusted = colorAdjustmentRgb(
                        {red / 255.0, green / 255.0, blue / 255.0}, primary_grade);
                    pixel[0] = adjusted[0]; pixel[1] = adjusted[1]; pixel[2] = adjusted[2];
                } else if (id == "video.grayscale") {
                    const double luminance = 0.2126 * red + 0.7152 * green + 0.0722 * blue;
                    const double blend = amount / 100.0;
                    pixel[0] = toByte(red + (luminance - red) * blend);
                    pixel[1] = toByte(green + (luminance - green) * blend);
                    pixel[2] = toByte(blue + (luminance - blue) * blend);
                } else if (id == "video.brightness") {
                    const double offset = amount * 2.55;
                    pixel[0] = toByte(red + offset);
                    pixel[1] = toByte(green + offset);
                    pixel[2] = toByte(blue + offset);
                } else if (id == "video.contrast") {
                    const double factor = amount / 100.0;
                    pixel[0] = toByte((red - 127.5) * factor + 127.5);
                    pixel[1] = toByte((green - 127.5) * factor + 127.5);
                    pixel[2] = toByte((blue - 127.5) * factor + 127.5);
                } else if (id == "video.saturation") {
                    const double luminance = 0.2126 * red + 0.7152 * green + 0.0722 * blue;
                    const double factor = amount / 100.0;
                    pixel[0] = toByte(luminance + (red - luminance) * factor);
                    pixel[1] = toByte(luminance + (green - luminance) * factor);
                    pixel[2] = toByte(luminance + (blue - luminance) * factor);
                }
            }
        }
    }
    return true;
}

ProcessingResult applyColorAdjustment(
    media::RgbaFrame& frame,
    const ColorAdjustmentParameters& parameters,
    const std::function<bool()>& should_cancel) {
    if (!validFrame(frame) || !validColorAdjustment(parameters))
        return ProcessingResult::InvalidInput;
    if (parameters.video_curves) {
        return applyVideoCurvesFrame(frame, *parameters.video_curves, should_cancel);
    }
    if (parameters.brightness == 0.0 && parameters.contrast_percent == 100.0 &&
        parameters.saturation_percent == 100.0 &&
        parameters.red_gain_percent == 100.0 &&
        parameters.green_gain_percent == 100.0 &&
        parameters.blue_gain_percent == 100.0 &&
        parameters.lift_red == 0.0 && parameters.lift_green == 0.0 &&
        parameters.lift_blue == 0.0 && parameters.gamma_red == 100.0 &&
        parameters.gamma_green == 100.0 && parameters.gamma_blue == 100.0 &&
        parameters.offset_red == 0.0 && parameters.offset_green == 0.0 &&
        parameters.offset_blue == 0.0 && parameters.pivot_percent == 50.0 &&
        parameters.temperature == 0.0 && parameters.tint == 0.0 &&
        parameters.log_shadows_red == 0.0 && parameters.log_shadows_green == 0.0 &&
        parameters.log_shadows_blue == 0.0 && parameters.log_midtones_red == 0.0 &&
        parameters.log_midtones_green == 0.0 && parameters.log_midtones_blue == 0.0 &&
        parameters.log_highlights_red == 0.0 && parameters.log_highlights_green == 0.0 &&
        parameters.log_highlights_blue == 0.0) {
        return ProcessingResult::Completed;
    }

    for (int y = 0; y < frame.height; ++y) {
        if (should_cancel && should_cancel()) return ProcessingResult::Cancelled;
        auto* row = frame.rgba_pixels.data() +
            static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.stride);
        for (int x = 0; x < frame.width; ++x) {
            auto* pixel = row + static_cast<std::size_t>(x) * 4U;
            const auto rgb=colorAdjustmentRgb({pixel[0]/255.,pixel[1]/255.,pixel[2]/255.},parameters);
            for(int c=0;c<3;++c)pixel[c]=rgb[c];
        }
    }
    return ProcessingResult::Completed;
}

ProcessingResult applyVideoCurvesFrame(
    media::RgbaFrame& frame, const VideoCurvesParameters& parameters,
    const std::function<bool()>& should_cancel) {
    if (!validFrame(frame) || !validVideoCurves(parameters))
        return ProcessingResult::InvalidInput;
    if (identityVideoCurves(parameters)) return ProcessingResult::Completed;
    const auto tables = videoCurvesLuts(parameters);
    if (tonalQualifierRefinementIsNeutral(parameters.qualifier)) {
        for (int y = 0; y < frame.height; ++y) {
            if (should_cancel && should_cancel()) return ProcessingResult::Cancelled;
            auto* row = frame.rgba_pixels.data() +
                static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.stride);
            for (int x = 0; x < frame.width; ++x) {
                auto* pixel = row + static_cast<std::size_t>(x) * 4U;
                const auto result = evaluateVideoCurves(parameters, tables,
                    {pixel[0] / 255.0, pixel[1] / 255.0, pixel[2] / 255.0});
                pixel[0] = result[0]; pixel[1] = result[1]; pixel[2] = result[2];
            }
        }
        return ProcessingResult::Completed;
    }
    auto matte = refinedTonalQualifierMatte(parameters.qualifier, frame.rgba_pixels,
        frame.width, frame.height, frame.stride, should_cancel);
    if (matte.size() != static_cast<std::size_t>(frame.width) * frame.height)
        return should_cancel && should_cancel()
            ? ProcessingResult::Cancelled : ProcessingResult::InvalidInput;
    auto full_coverage = parameters;
    full_coverage.qualifier.enabled = false;
    for (int y = 0; y < frame.height; ++y) {
        if (should_cancel && should_cancel()) return ProcessingResult::Cancelled;
        auto* row = frame.rgba_pixels.data() +
            static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.stride);
        for (int x = 0; x < frame.width; ++x) {
            auto* pixel = row + static_cast<std::size_t>(x) * 4U;
            const double coverage = matte[static_cast<std::size_t>(y) * frame.width + x] / 255.0;
            const auto adjusted = evaluateVideoCurves(full_coverage, tables,
                {pixel[0] / 255.0, pixel[1] / 255.0, pixel[2] / 255.0});
            for (std::size_t channel = 0; channel < 3; ++channel)
                pixel[channel] = static_cast<std::uint8_t>(std::clamp(std::lround(
                    pixel[channel] * (1.0 - coverage) + adjusted[channel] * coverage), 0L, 255L));
        }
    }
    return ProcessingResult::Completed;
}

}  // namespace creative_suite::effects
