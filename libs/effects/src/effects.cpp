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
constexpr std::array<ParameterDefinition, 17> primary_grade_parameters{{
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
    {"tint", "Tint", -100.0, 100.0, 0.0, 1.0}}};

constexpr std::array<Definition, 5> definitions{{
    {"video.grayscale", "Grayscale", "video", grayscale_parameters},
    {"video.brightness", "Brightness", "video", brightness_parameters},
    {"video.contrast", "Contrast", "video", contrast_parameters},
    {"video.saturation", "Saturation", "video", saturation_parameters},
    {"video.primary_grade", "Primary Grade", "video", primary_grade_parameters},
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
        std::isfinite(parameters.tint) && parameters.tint >= -100.0 && parameters.tint <= 100.0;
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

bool applyStack(
    media::RgbaFrame& frame,
    std::span<const EffectInstance> effects) noexcept {
    if (!validFrame(frame) || !isValidStack(effects)) return false;
    if (effects.empty()) return true;

    for (const auto& effect : effects) {
        if (!effect.enabled) continue;
        const auto amount = parameterValue(effect, "amount");
        const auto id = std::string_view(effect.id);
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
        parameters.temperature == 0.0 && parameters.tint == 0.0) {
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

}  // namespace creative_suite::effects
