#include <creative_suite/effects/effects.h>

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

creative_suite::media::RgbaFrame frame() {
    creative_suite::media::RgbaFrame value;
    value.width = 1;
    value.height = 1;
    value.stride = 8;
    value.rgba_pixels = {200, 40, 20, 91, 7, 8, 9, 10};
    return value;
}

}  // namespace

int main() {
    using namespace creative_suite::effects;
    require(builtInEffects().size() == 4, "The built-in visual effect catalog is incomplete.");
    auto original = frame();
    const auto neutral = std::vector<EffectInstance>{
        makeDefaultInstance("video.brightness"),
        makeDefaultInstance("video.contrast"),
        makeDefaultInstance("video.saturation")};
    auto unchanged = original;
    require(applyStack(unchanged, neutral), "Neutral effects were rejected.");
    require(unchanged.rgba_pixels == original.rgba_pixels,
            "Neutral correction effects changed the frame.");

    auto grayscale = makeDefaultInstance("video.grayscale");
    auto monochrome = original;
    require(applyStack(monochrome, std::span<const EffectInstance>(&grayscale, 1)),
            "Grayscale was rejected.");
    require(monochrome.rgba_pixels[0] == monochrome.rgba_pixels[1] &&
            monochrome.rgba_pixels[1] == monochrome.rgba_pixels[2],
            "Grayscale did not set equal color channels.");
    require(monochrome.rgba_pixels[3] == original.rgba_pixels[3] &&
            monochrome.rgba_pixels[4] == original.rgba_pixels[4],
            "The effect changed alpha or row padding.");

    auto gray_zero = makeDefaultInstance("video.grayscale");
    require(setParameterValue(gray_zero, "amount", 0.0),
            "The grayscale lower bound could not be set.");
    auto zeroed = original;
    require(applyStack(zeroed, std::span<const EffectInstance>(&gray_zero, 1)) &&
                zeroed.rgba_pixels == original.rgba_pixels,
            "Zero grayscale amount must preserve the source image.");

    auto brightness = makeDefaultInstance("video.brightness");
    require(setParameterValue(brightness, "amount", 50.0),
            "A valid brightness value could not be set.");
    auto saturation = makeDefaultInstance("video.saturation");
    require(setParameterValue(saturation, "amount", 200.0),
            "The saturation upper bound could not be set.");
    const std::vector<EffectInstance> brightness_then_saturation{
        brightness, saturation};
    const std::vector<EffectInstance> saturation_then_brightness{
        saturation, brightness};
    auto first_order = original;
    auto second_order = original;
    require(applyStack(first_order, brightness_then_saturation) &&
                applyStack(second_order, saturation_then_brightness),
            "A multi-filter stack was rejected.");
    require(first_order.rgba_pixels != second_order.rgba_pixels,
            "The shared processor did not preserve filter order.");
    require(isValidStack(std::vector<EffectInstance>{grayscale, grayscale}),
            "Repeated filter instances must remain valid.");

    auto bad_effect = grayscale;
    bad_effect.id = "video.unknown";
    require(!isValid(bad_effect), "An unknown effect id was accepted.");
    auto bad_parameter = makeDefaultInstance("video.brightness");
    bad_parameter.parameters[0].value = 101.0;
    require(!isValid(bad_parameter), "An out-of-range effect parameter was accepted.");
    require(!setParameterValue(brightness, "amount", 101.0),
            "A parameter update beyond its upper bound was accepted.");
    require(!applyStack(original, std::span<const EffectInstance>(&bad_effect, 1)),
            "An invalid stack was applied.");
    auto invalid_frame = original;
    invalid_frame.width = 0;
    require(!applyStack(invalid_frame, std::span<const EffectInstance>(&grayscale, 1)),
            "An invalid RGBA frame was processed.");
    return 0;
}
