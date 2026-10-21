#include <creative_suite/effects/effects.h>
#include <creative_suite/effects/adjustment.h>
#include <creative_suite/effects/gaussian_blur.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

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

creative_suite::media::RgbaFrame colorFrame() {
    creative_suite::media::RgbaFrame value;
    value.width = 3;
    value.height = 3;
    value.stride = 16;
    value.rgba_pixels.assign(static_cast<std::size_t>(value.stride * value.height), 0xCD);
    for (int y = 0; y < value.height; ++y) {
        auto* row = value.rgba_pixels.data() +
            static_cast<std::size_t>(y) * static_cast<std::size_t>(value.stride);
        for (int x = 0; x < value.width; ++x) {
            auto* pixel = row + static_cast<std::size_t>(x) * 4U;
            pixel[0] = static_cast<std::uint8_t>(x * 61 + y * 17);
            pixel[1] = static_cast<std::uint8_t>(220 - x * 29 - y * 31);
            pixel[2] = static_cast<std::uint8_t>(x * 13 + y * 73);
            pixel[3] = static_cast<std::uint8_t>(std::array<int, 4>{0, 1, 127, 255}[
                static_cast<std::size_t>((x + y) % 4)]);
        }
    }
    return value;
}

void applyPreviousMotionColorAdjustment(
    creative_suite::media::RgbaFrame& frame,
    const creative_suite::effects::ColorAdjustmentParameters& parameters) {
    const double brightness = parameters.brightness / 100.0;
    const double contrast = parameters.contrast_percent / 100.0;
    const double saturation = parameters.saturation_percent / 100.0;
    for (int y = 0; y < frame.height; ++y) {
        auto* row = frame.rgba_pixels.data() +
            static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.stride);
        for (int x = 0; x < frame.width; ++x) {
            auto* pixel = row + static_cast<std::size_t>(x) * 4U;
            double red = static_cast<double>(pixel[0]) / 255.0 + brightness;
            double green = static_cast<double>(pixel[1]) / 255.0 + brightness;
            double blue = static_cast<double>(pixel[2]) / 255.0 + brightness;
            red = (red - 0.5) * contrast + 0.5;
            green = (green - 0.5) * contrast + 0.5;
            blue = (blue - 0.5) * contrast + 0.5;
            const double luma = 0.2126 * red + 0.7152 * green + 0.0722 * blue;
            red = luma + (red - luma) * saturation;
            green = luma + (green - luma) * saturation;
            blue = luma + (blue - luma) * saturation;
            pixel[0] = static_cast<std::uint8_t>(
                std::lround(std::clamp(red, 0.0, 1.0) * 255.0));
            pixel[1] = static_cast<std::uint8_t>(
                std::lround(std::clamp(green, 0.0, 1.0) * 255.0));
            pixel[2] = static_cast<std::uint8_t>(
                std::lround(std::clamp(blue, 0.0, 1.0) * 255.0));
        }
    }
}

}  // namespace

void verifySharedBlur() {
    using namespace creative_suite::effects;
    creative_suite::media::RgbaFrame input{7,5,32,std::vector<std::uint8_t>(160,73)};
    for(int y=0;y<input.height;++y)for(int x=0;x<input.width;++x)for(int c=0;c<4;++c)
        input.rgba_pixels[y*input.stride+x*4+c]=std::uint8_t((x*43+y*67+c*79)%256);
    for(double amount:{0.,.4,.5,2.5,100.}) {
        auto expected=input;const int radius=int(std::lround(amount));
        if(radius) {
            for(int y=0;y<input.height;++y)for(int x=0;x<input.width;++x)for(int c=0;c<3;++c) {
                const auto i=y*input.stride+x*4;expected.rgba_pixels[i+c]=std::uint8_t((unsigned(input.rgba_pixels[i+c])*input.rgba_pixels[i+3]+127)/255);
            }
            for(int pass=0;pass<6;++pass) {
                const auto source=expected;const auto divisor=unsigned(2*radius+1);
                for(int y=0;y<input.height;++y)for(int x=0;x<input.width;++x)for(int c=0;c<4;++c) {
                    unsigned sum=0;
                    for(int offset=-radius;offset<=radius;++offset) {
                        const auto sx=pass%2==0?std::clamp(x+offset,0,input.width-1):x;
                        const auto sy=pass%2==1?std::clamp(y+offset,0,input.height-1):y;
                        sum+=source.rgba_pixels[sy*input.stride+sx*4+c];
                    }
                    expected.rgba_pixels[y*input.stride+x*4+c]=std::uint8_t((sum+divisor/2)/divisor);
                }
            }
            for(int y=0;y<input.height;++y)for(int x=0;x<input.width;++x)for(int c=0;c<3;++c) {
                const auto i=y*input.stride+x*4;const unsigned alpha=expected.rgba_pixels[i+3];
                expected.rgba_pixels[i+c]=alpha?std::uint8_t(std::min(255u,(unsigned(expected.rgba_pixels[i+c])*255+alpha/2)/alpha)):0;
            }
        }
        auto actual=input;require(applyGaussianBlur(actual,amount)==ProcessingResult::Completed && actual.rgba_pixels==expected.rgba_pixels,"shared blur reference pixels, alpha, rounding and padding");
        auto parallel=input;int dispatches=0;
        require(applyGaussianBlur(parallel,amount,{},[&](auto count,const auto& cancel,const auto& op){++dispatches;op(0,count/2,cancel);op(count/2,count,cancel);return true;})==ProcessingResult::Completed && parallel.rgba_pixels==expected.rgba_pixels,"range executor preserves blur output");
        require(dispatches==(radius?8:0),"radius zero bypasses premultiplication and all blur passes");
    }
    for(auto invalid:{-1.,101.,std::numeric_limits<double>::quiet_NaN()}) {
        auto copy=input;require(applyGaussianBlur(copy,invalid)==ProcessingResult::InvalidInput && copy.rgba_pixels==input.rgba_pixels,"invalid blur unchanged");
    }
    auto copy=input;require(applyGaussianBlur(copy,3,[]{return true;})==ProcessingResult::Cancelled && copy.rgba_pixels==input.rgba_pixels,"blur initial cancellation");
    int polls=0;require(applyGaussianBlur(copy,3,[&]{return ++polls>4;})==ProcessingResult::Cancelled,"blur cancellation during processing");
    copy=input;copy.rgba_pixels.resize(1);require(applyGaussianBlur(copy,3)==ProcessingResult::InvalidInput,"blur incomplete frame");
}

int main() {
    {
        using namespace creative_suite::effects;
        GradientMapParameters map{{{0,{220,20,50}},{.4,{20,210,60}},{1,{30,70,240}}}};
        require(validGradientMap(map),"Gradient Map points rejected");
        require(gradientMapColor(map,.2)==std::array<unsigned char,3>{120,115,55},"Gradient Map interpolation changed");
        require(gradientMapColor(map,-1)==map.stops.front().color && gradientMapColor(map,2)==map.stops.back().color,"Gradient Map endpoints changed");
        map.stops[1].position=std::numeric_limits<double>::quiet_NaN();require(!validGradientMap(map),"Nonfinite Gradient Map accepted");
        map.stops[1].position=0;require(!validGradientMap(map),"Duplicate Gradient Map position accepted");
        map.stops.resize(33);require(!validGradientMap(map),"Unbounded Gradient Map accepted");
    }

    {
        using namespace creative_suite::effects;
        require(validAdjustment(BrightnessContrastParameters{-100,100}) && !validAdjustment(BrightnessContrastParameters{101,0}),"Brightness/Contrast validation");
        require(adjustmentRgb(BrightnessContrastParameters{25,50},{.2,.4,.8})==std::array<unsigned char,3>{108,185,255},"Brightness-before-contrast reference");
        require(adjustmentRgb(BrightnessContrastParameters{100,-100},{0.,.5,1.})==std::array<unsigned char,3>{128,128,128},"Zero contrast midpoint");
        require(adjustmentRgb(BrightnessContrastParameters{-100,0},{0.,.5,1.})==std::array<unsigned char,3>{0,0,0},"Brightness saturation");
    }
    {
        using namespace creative_suite::effects;
        require(validAdjustment(ExposureOffsetGammaParameters{-20,-.5,.01}) &&
            validAdjustment(ExposureOffsetGammaParameters{20,.5,9.99}), "Exposure bounds");
        require(!validAdjustment(ExposureOffsetGammaParameters{0,0,0}) &&
            !validAdjustment(ExposureOffsetGammaParameters{0,std::numeric_limits<double>::quiet_NaN(),1}), "Exposure invalid parameters");
        require(identityAdjustment(ExposureOffsetGammaParameters{}), "Neutral exposure identity");
        require(adjustmentRgb(ExposureOffsetGammaParameters{1,0,1},{.5,.5,.5}) ==
            std::array<unsigned char,3>{175,175,175}, "One stop uses linear light");
        require(adjustmentRgb(ExposureOffsetGammaParameters{0,0,2},{0,0,0}) ==
            std::array<unsigned char,3>{0,0,0}, "Gamma preserves zero");
        require(adjustmentRgb(ExposureOffsetGammaParameters{20,.5,.01},{0,1,.5}) ==
            std::array<unsigned char,3>{0,255,255}, "Extreme exposure/gamma remains bounded");
    }
    {
        using namespace creative_suite::effects;
        require(validAdjustment(HueSaturationParameters{-180,-100,100}) && !validAdjustment(HueSaturationParameters{181,0,0}), "HSL control validation");
        require(identityAdjustment(HueSaturationParameters{}), "Neutral HSL identity");
        require(colorAdjustmentRgb({1,0,0},{0,100,0})==std::array<unsigned char,3>{54,54,54}, "Video keeps Rec709 saturation semantics");
        require(adjustmentRgb(HueSaturationParameters{120,0,0},{1,0,0})==std::array<unsigned char,3>{0,255,0}, "Positive hue rotates red to green");
        require(adjustmentRgb(HueSaturationParameters{0,-100,0},{1,0,0})==std::array<unsigned char,3>{128,128,128}, "HSL uses lightness rather than Rec709 luma");
        require(adjustmentRgb(HueSaturationParameters{80,100,0},{.5,.5,.5})==std::array<unsigned char,3>{128,128,128}, "HSL does not colorize gray");
        require(adjustmentRgb(HueSaturationParameters{0,0,100},{.2,.4,.8})==std::array<unsigned char,3>{255,255,255}, "HSL lightness ceiling");
    }
    verifySharedBlur();
    using namespace creative_suite::effects;
    require(builtInEffects().size() == 4, "The built-in visual effect catalog is incomplete.");
    auto original = frame();
    const auto neutral = std::vector<EffectInstance>{
        makeDefaultInstance("video.brightness"),
        makeDefaultInstance("video.contrast"),
        makeDefaultInstance("video.saturation")};
    require(neutral.front().enabled,
            "New effect instances must be enabled by default.");
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
    auto disabled_grayscale = grayscale;
    disabled_grayscale.enabled = false;
    auto disabled_stack_frame = original;
    auto brightness_only_frame = original;
    require(applyStack(
                disabled_stack_frame,
                std::vector<EffectInstance>{disabled_grayscale, brightness}) &&
                applyStack(brightness_only_frame,
                           std::span<const EffectInstance>(&brightness, 1)) &&
                disabled_stack_frame.rgba_pixels == brightness_only_frame.rgba_pixels,
            "A disabled filter must be skipped while later enabled filters retain their order.");
    auto all_disabled_frame = original;
    disabled_grayscale.enabled = false;
    require(applyStack(
                all_disabled_frame,
                std::span<const EffectInstance>(&disabled_grayscale, 1)) &&
                all_disabled_frame.rgba_pixels == original.rgba_pixels,
            "A stack containing only disabled effects must preserve every frame byte.");
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

    auto fused_original = colorFrame();
    auto fused_neutral = fused_original;
    require(applyColorAdjustment(fused_neutral, {}) == ProcessingResult::Completed &&
                fused_neutral.rgba_pixels == fused_original.rgba_pixels,
            "neutral fused color adjustment preserves every byte");
    for (const ColorAdjustmentParameters parameters : {
             ColorAdjustmentParameters{-100.0, 200.0, 0.0},
             ColorAdjustmentParameters{100.0, 0.0, 200.0},
             ColorAdjustmentParameters{0.0, 200.0, 200.0}}) {
        auto expected = fused_original;
        applyPreviousMotionColorAdjustment(expected, parameters);
        auto actual = fused_original;
        require(applyColorAdjustment(actual, parameters) == ProcessingResult::Completed &&
                    actual.rgba_pixels == expected.rgba_pixels,
                "fused adjustment matches Motion's prior pixel math at parameter limits");
    }

    auto cancelled = fused_original;
    int cancellation_checks = 0;
    const auto cancellation_result = applyColorAdjustment(
        cancelled, {30.0, 100.0, 100.0},
        [&] { return ++cancellation_checks == 2; });
    require(cancellation_result == ProcessingResult::Cancelled &&
                cancellation_checks == 2,
            "fused adjustment reports row-boundary cancellation");
    require(!std::equal(cancelled.rgba_pixels.begin(), cancelled.rgba_pixels.begin() +
                            fused_original.stride, fused_original.rgba_pixels.begin()) &&
                std::equal(cancelled.rgba_pixels.begin() + fused_original.stride,
                           cancelled.rgba_pixels.end(),
                           fused_original.rgba_pixels.begin() + fused_original.stride),
            "cancellation may leave completed rows changed and discards no later rows");

    auto callback_exception_frame = fused_original;
    bool callback_exception_propagated = false;
    try {
        (void)applyColorAdjustment(callback_exception_frame, {30.0, 100.0, 100.0}, []() -> bool {
            throw std::runtime_error("cancel callback failure");
        });
    } catch (const std::runtime_error&) {
        callback_exception_propagated = true;
    }
    require(callback_exception_propagated &&
                callback_exception_frame.rgba_pixels == fused_original.rgba_pixels,
            "cancellation callback exceptions propagate before the first row changes");

    for (const auto invalid_parameters : {
             ColorAdjustmentParameters{std::numeric_limits<double>::quiet_NaN(), 100.0, 100.0},
             ColorAdjustmentParameters{0.0, std::numeric_limits<double>::infinity(), 100.0},
             ColorAdjustmentParameters{100.01, 100.0, 100.0},
             ColorAdjustmentParameters{0.0, 200.01, 100.0},
             ColorAdjustmentParameters{0.0, 100.0, -0.01}}) {
        auto unchanged_frame = fused_original;
        require(applyColorAdjustment(unchanged_frame, invalid_parameters) ==
                    ProcessingResult::InvalidInput &&
                    unchanged_frame.rgba_pixels == fused_original.rgba_pixels,
                "invalid color parameters are rejected before changing the frame");
    }
    auto incomplete_frame = fused_original;
    incomplete_frame.rgba_pixels.pop_back();
    const auto incomplete_before = incomplete_frame.rgba_pixels;
    require(applyColorAdjustment(incomplete_frame, {}) == ProcessingResult::InvalidInput &&
                incomplete_frame.rgba_pixels == incomplete_before,
            "incomplete frame storage is rejected without mutation");
    {
    auto brightness = makeDefaultInstance("video.brightness");
    auto grayscale = makeDefaultInstance("video.grayscale");
    auto contrast = makeDefaultInstance("video.contrast");
    auto saturation = makeDefaultInstance("video.saturation");
    require(setParameterValue(brightness, "amount", 17.0) &&
        setParameterValue(grayscale, "amount", 37.0) &&
        setParameterValue(contrast, "amount", 123.0) &&
        setParameterValue(saturation, "amount", 141.0), "pass fixture parameters");
    auto disabled = brightness;
    disabled.enabled = false;
    const std::vector<EffectInstance> ordered{brightness, grayscale, disabled, contrast, saturation};
    const auto passes = colorAdjustmentPasses(ordered);
    require(passes && passes->size() == 4, "pass preparation preserves order and skips disabled effects");
    auto direct_stack = fused_original;
    auto pass_stack = fused_original;
    require(applyStack(direct_stack, ordered), "ordered reference stack");
    for (const auto& pass : *passes)
        require(applyColorAdjustment(pass_stack, pass) == ProcessingResult::Completed, "prepared pass");
    for (std::size_t i = 0; i < direct_stack.rgba_pixels.size(); ++i)
        require(std::abs(int(direct_stack.rgba_pixels[i]) - int(pass_stack.rgba_pixels[i])) <=
            (i % 4 == 3 ? 0 : 2), "prepared passes retain byte rounding and alpha");
    const std::vector<EffectInstance> invalid_passes{EffectInstance{"invalid-effect", {}, true}};
    require(!colorAdjustmentPasses(invalid_passes),
        "invalid stacks cannot be prepared");
    }
    return 0;
}
