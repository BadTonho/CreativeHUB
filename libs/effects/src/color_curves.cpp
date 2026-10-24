#include <creative_suite/effects/color_curves.h>

#include <algorithm>
#include <cmath>

namespace creative_suite::effects {
namespace {

constexpr float kRed = 0.2126f;
constexpr float kGreen = 0.7152f;
constexpr float kBlue = 0.0722f;

bool finiteRange(double value, double minimum, double maximum) noexcept {
    return std::isfinite(value) && value >= minimum && value <= maximum;
}

float positiveModulo(float value, float divisor) noexcept {
    return value - divisor * std::floor(value / divisor);
}

float smooth(float left, float right, float value) noexcept {
    if (right <= left) return value >= right ? 1.0f : 0.0f;
    const float t = std::clamp((value - left) / (right - left), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float rangeWeight(float value, float minimum, float maximum,
                   float softness) noexcept {
    const float lower = softness == 0.0f ? (value >= minimum ? 1.0f : 0.0f)
        : smooth(minimum - softness, minimum + softness, value);
    const float upper = softness == 0.0f ? (value <= maximum ? 1.0f : 0.0f)
        : 1.0f - smooth(maximum - softness, maximum + softness, value);
    return std::min(lower, upper);
}

float hueRangeWeight(float hue, float minimum, float maximum,
                     float softness) noexcept {
    if (minimum == 0.0f && maximum == 360.0f) return 1.0f;
    float end = maximum;
    float sample = hue;
    if (end < minimum) {
        end += 360.0f;
        if (sample < minimum) sample += 360.0f;
    }
    return rangeWeight(sample, minimum, end, softness);
}

struct Hsv {
    float hue = 0.0f;
    float saturation = 0.0f;
    float value = 0.0f;
};

Hsv toHsv(const std::array<float, 3>& rgb) noexcept {
    const float maximum = std::max({rgb[0], rgb[1], rgb[2]});
    const float minimum = std::min({rgb[0], rgb[1], rgb[2]});
    const float delta = maximum - minimum;
    Hsv result;
    result.value = maximum;
    result.saturation = maximum <= 0.0f ? 0.0f : delta / maximum;
    if (delta <= 0.0f) return result;
    if (maximum == rgb[0]) result.hue = 60.0f * positiveModulo((rgb[1] - rgb[2]) / delta, 6.0f);
    else if (maximum == rgb[1]) result.hue = 60.0f * ((rgb[2] - rgb[0]) / delta + 2.0f);
    else result.hue = 60.0f * ((rgb[0] - rgb[1]) / delta + 4.0f);
    if (result.hue < 0.0f) result.hue += 360.0f;
    return result;
}

std::array<float, 3> fromHsv(Hsv hsv) noexcept {
    hsv.hue = positiveModulo(hsv.hue, 360.0f);
    if (hsv.hue < 0.0f) hsv.hue += 360.0f;
    hsv.saturation = std::clamp(hsv.saturation, 0.0f, 1.0f);
    hsv.value = std::clamp(hsv.value, 0.0f, 1.0f);
    const float chroma = hsv.value * hsv.saturation;
    const float x = chroma * (1.0f - std::abs(positiveModulo(hsv.hue / 60.0f, 2.0f) - 1.0f));
    const float m = hsv.value - chroma;
    if (hsv.hue < 60.0) return {chroma + m, x + m, m};
    if (hsv.hue < 120.0) return {x + m, chroma + m, m};
    if (hsv.hue < 180.0) return {m, chroma + m, x + m};
    if (hsv.hue < 240.0) return {m, x + m, chroma + m};
    if (hsv.hue < 300.0) return {x + m, m, chroma + m};
    return {chroma + m, m, x + m};
}

int curveIndex(float normalized) noexcept {
    // Use a small tie margin so CPU and shader arithmetic choose the same LUT
    // entry when a normalized value is mathematically halfway between bins.
    return static_cast<int>(std::floor(
        std::clamp(normalized, 0.0f, 1.0f) * 255.0f + 0.50001f));
}

float lookupOffset(const VideoCurvesLuts& tables, std::size_t curve,
                   float input) noexcept {
    return (static_cast<float>(tables[curve][curveIndex(input)]) - 128.0f) / 127.0f;
}

float qualifierMatteFloat(const TonalQualifier& qualifier,
                          const std::array<float, 3>& rgb) noexcept {
    if (!qualifier.enabled) return 1.0f;
    const auto hsv = toHsv(rgb);
    const float luminance = std::clamp(kRed * rgb[0] + kGreen * rgb[1] +
        kBlue * rgb[2], 0.0f, 1.0f);
    const float hue_weight = hueRangeWeight(hsv.hue,
        static_cast<float>(qualifier.hue_min), static_cast<float>(qualifier.hue_max),
        static_cast<float>(qualifier.hue_softness) * 3.6f);
    const float saturation_weight = rangeWeight(hsv.saturation * 100.0f,
        static_cast<float>(qualifier.saturation_min),
        static_cast<float>(qualifier.saturation_max),
        static_cast<float>(qualifier.saturation_softness));
    const float luminance_weight = rangeWeight(luminance * 100.0f,
        static_cast<float>(qualifier.luminance_min),
        static_cast<float>(qualifier.luminance_max),
        static_cast<float>(qualifier.luminance_softness));
    return std::clamp(hue_weight * saturation_weight * luminance_weight,
        0.0f, 1.0f);
}

}  // namespace

bool validTonalQualifier(const TonalQualifier& value) noexcept {
    return finiteRange(value.hue_min, 0.0, 360.0) &&
        finiteRange(value.hue_max, 0.0, 360.0) &&
        finiteRange(value.saturation_min, 0.0, 100.0) &&
        finiteRange(value.saturation_max, 0.0, 100.0) &&
        value.saturation_min <= value.saturation_max &&
        finiteRange(value.luminance_min, 0.0, 100.0) &&
        finiteRange(value.luminance_max, 0.0, 100.0) &&
        value.luminance_min <= value.luminance_max &&
        finiteRange(value.hue_softness, 0.0, 50.0) &&
        finiteRange(value.saturation_softness, 0.0, 50.0) &&
        finiteRange(value.luminance_softness, 0.0, 50.0);
}

bool validVideoCurves(const VideoCurvesParameters& value) noexcept {
    return validCurves(value.rgb) &&
        std::all_of(value.targeted.begin(), value.targeted.end(), validCurvePoints) &&
        validTonalQualifier(value.qualifier);
}

VideoCurvesLuts videoCurvesLuts(const VideoCurvesParameters& value) noexcept {
    VideoCurvesLuts result{};
    result[0] = curvesLut(value.rgb.points);
    for (std::size_t i = 0; i < value.rgb.channels.size(); ++i)
        result[1 + i] = curvesLut(value.rgb.channels[i]);
    for (std::size_t i = 0; i < value.targeted.size(); ++i)
        result[4 + i] = curvesLut(value.targeted[i]);
    return result;
}

double tonalQualifierMatte(
    const TonalQualifier& qualifier, const std::array<double, 3>& rgb) noexcept {
    const std::array<float, 3> source{static_cast<float>(rgb[0]),
        static_cast<float>(rgb[1]), static_cast<float>(rgb[2])};
    return qualifierMatteFloat(qualifier, source);
}

std::array<std::uint8_t, 3> evaluateVideoCurves(
    const VideoCurvesParameters& parameters, const VideoCurvesLuts& tables,
    const std::array<double, 3>& source, double* matte) noexcept {
    const std::array<float, 3> source_rgb{static_cast<float>(source[0]),
        static_cast<float>(source[1]), static_cast<float>(source[2])};
    const float coverage = qualifierMatteFloat(parameters.qualifier, source_rgb);
    if (matte) *matte = coverage;
    if (coverage <= 0.0f) {
        return {static_cast<std::uint8_t>(std::lround(std::clamp(source_rgb[0], 0.0f, 1.0f) * 255.0f)),
            static_cast<std::uint8_t>(std::lround(std::clamp(source_rgb[1], 0.0f, 1.0f) * 255.0f)),
            static_cast<std::uint8_t>(std::lround(std::clamp(source_rgb[2], 0.0f, 1.0f) * 255.0f))};
    }

    std::array<float, 3> adjusted = source_rgb;
    const float luminance = std::clamp(kRed * adjusted[0] + kGreen * adjusted[1] +
        kBlue * adjusted[2], 0.0f, 1.0f);
    const float mapped_luminance = static_cast<float>(tables[0][curveIndex(luminance)]) / 255.0f;
    if (luminance > 1.0e-12f) {
        const float scale = mapped_luminance / luminance;
        for (auto& channel : adjusted) channel = std::clamp(channel * scale, 0.0f, 1.0f);
    } else {
        adjusted = {mapped_luminance, mapped_luminance, mapped_luminance};
    }
    for (std::size_t channel = 0; channel < 3; ++channel)
        adjusted[channel] = static_cast<float>(tables[1 + channel][curveIndex(adjusted[channel])]) / 255.0f;

    const float old_luminance = std::clamp(kRed * adjusted[0] + kGreen * adjusted[1] +
        kBlue * adjusted[2], 0.0f, 1.0f);
    auto hsv = toHsv(adjusted);
    const float hue_offset = lookupOffset(tables, 4, hsv.hue / 360.0f);
    hsv.hue += hue_offset * (360.0f / 255.0f);
    hsv.saturation += lookupOffset(tables, 5, hsv.hue / 360.0f);
    const float luminance_delta = lookupOffset(tables, 6, hsv.hue / 360.0f);
    hsv.saturation += lookupOffset(tables, 7, old_luminance);
    hsv.saturation += lookupOffset(tables, 8, hsv.saturation);
    auto secondary = fromHsv(hsv);
    const float target_luminance = std::clamp(old_luminance + luminance_delta, 0.0f, 1.0f);
    if (luminance_delta != 0.0f) {
        const float secondary_luminance = kRed * secondary[0] + kGreen * secondary[1] +
            kBlue * secondary[2];
        if (secondary_luminance > 1.0e-12f) {
            const float scale = target_luminance / secondary_luminance;
            for (auto& channel : secondary) channel = std::clamp(channel * scale, 0.0f, 1.0f);
        }
    }
    std::array<std::uint8_t, 3> result{};
    for (std::size_t channel = 0; channel < 3; ++channel) {
        const float blended = source_rgb[channel] * (1.0f - coverage) +
            secondary[channel] * coverage;
        result[channel] = static_cast<std::uint8_t>(std::floor(
            std::clamp(blended, 0.0f, 1.0f) * 255.0f + 0.5f));
    }
    return result;
}

bool identityVideoCurves(const VideoCurvesParameters& value) noexcept {
    if (!validVideoCurves(value)) return false;
    const auto tables = videoCurvesLuts(value);
    for (std::size_t curve = 0; curve < tables.size(); ++curve) {
        for (int x = 0; x < 256; ++x) {
            const int expected = curve < 4 ? x : 128;
            if (tables[curve][x] != expected) return false;
        }
    }
    return true;
}

}  // namespace creative_suite::effects
