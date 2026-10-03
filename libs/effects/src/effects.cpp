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

constexpr std::array<Definition, 4> definitions{{
    {"video.grayscale", "Grayscale", "video", grayscale_parameters},
    {"video.brightness", "Brightness", "video", brightness_parameters},
    {"video.contrast", "Contrast", "video", contrast_parameters},
    {"video.saturation", "Saturation", "video", saturation_parameters},
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

}  // namespace

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

bool applyStack(
    media::RgbaFrame& frame,
    std::span<const EffectInstance> effects) noexcept {
    if (!validFrame(frame) || !isValidStack(effects)) return false;
    if (effects.empty()) return true;

    for (const auto& effect : effects) {
        const auto amount = parameterValue(effect, "amount");
        const auto id = std::string_view(effect.id);
        for (int y = 0; y < frame.height; ++y) {
            auto* pixel = frame.rgba_pixels.data() +
                static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.stride);
            for (int x = 0; x < frame.width; ++x, pixel += 4) {
                const double red = pixel[0];
                const double green = pixel[1];
                const double blue = pixel[2];
                if (id == "video.grayscale") {
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

}  // namespace creative_suite::effects
