#include <creative_suite/effects/effects.h>
#include <creative_suite/effects/adjustment.h>
#include <creative_suite/effects/gaussian_blur.h>
#include <creative_suite/effects/glow.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
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
            red *= parameters.red_gain_percent / 100.0;
            green *= parameters.green_gain_percent / 100.0;
            blue *= parameters.blue_gain_percent / 100.0;
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

void verifyGlow()
{
    using namespace creative_suite::effects;
    using creative_suite::media::RgbaFrame;
    RgbaFrame source{1, 1, 8, {200, 40, 20, 128, 0xCD, 0xCD, 0xCD, 0xCD}};
    GlowParameters parameters;
    parameters.color = {10, 80, 210};
    parameters.opacity_percent = 100;
    parameters.radius_pixels = 1;

    const auto outer_bounds = glowBounds(1, 1, parameters);
    require(outer_bounds.left == 4 && outer_bounds.top == 4 &&
                outer_bounds.width == 9 && outer_bounds.height == 9,
            "outer Glow bounds include the bounded blur halo");
    const auto outer = applyGlow(source, parameters);
    require(outer && outer->width == 9 && outer->height == 9,
            "outer Glow expands the source frame");
    const auto* source_pixel = outer->rgba_pixels.data() + 4 * outer->stride + 4 * 4;
    require(source_pixel[0] == source.rgba_pixels[0] && source_pixel[1] == source.rgba_pixels[1] &&
                source_pixel[2] == source.rgba_pixels[2] && source_pixel[3] == source.rgba_pixels[3],
            "outer Glow preserves source RGB and alpha over the halo");
    require(outer->rgba_pixels[3] == 0 && outer->rgba_pixels[4 * outer->stride + 1 * 4 + 3] > 0,
            "outer Glow retains transparent bounds and a colored alpha halo");

    parameters.mode = GlowMode::Inner;
    const auto inner_bounds = glowBounds(1, 1, parameters);
    const auto inner = applyGlow(source, parameters);
    require(inner_bounds.left == 0 && inner_bounds.top == 0 && inner_bounds.width == 1 &&
                inner_bounds.height == 1 && inner && inner->width == 1 && inner->height == 1,
            "inner Glow keeps the original layer bounds");
    require(inner->rgba_pixels[3] == source.rgba_pixels[3] && inner->rgba_pixels[2] > source.rgba_pixels[2],
            "inner Glow adds its static color without changing source alpha");

    parameters.mode = GlowMode::Both;
    const auto both = applyGlow(source, parameters);
    require(both && both->width == 9 && both->height == 9 && both->rgba_pixels != outer->rgba_pixels,
            "Both combines inner color with the expanded outer halo");

    RgbaFrame transparent{3, 2, 16, std::vector<std::uint8_t>(32, 0)};
    parameters.mode = GlowMode::Outer;
    const auto empty = applyGlow(transparent, parameters);
    require(empty && std::all_of(empty->rgba_pixels.begin(), empty->rgba_pixels.end(),
                                 [](std::uint8_t value) { return value == 0; }),
            "transparent source pixels do not create a Glow");
    require(!applyGlow(source, parameters, [] { return true; }),
            "Glow honors cancellation before processing");
    int polls = 0;
    require(!applyGlow(source, parameters, [&] { return ++polls > 3; }) && polls > 3,
            "Glow checks cancellation while processing its rows");

    for (const auto radius : {-1.0, 100.5, std::numeric_limits<double>::quiet_NaN()}) {
        auto invalid = parameters;
        invalid.radius_pixels = radius;
        bool rejected = false;
        try { (void)glowBounds(1, 1, invalid); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "Glow rejects an out-of-range or non-finite radius");
    }
    bool bounded = false;
    try { (void)glowBounds(16384, 16384, parameters); }
    catch (const std::runtime_error&) { bounded = true; }
    require(bounded, "Glow refuses output beyond its bounded allocation");
    auto invalid_mode = parameters;
    invalid_mode.mode = static_cast<GlowMode>(255);
    require(!validGlow(invalid_mode), "Glow rejects unknown modes");
}

void verifyVideoCurves()
{
    using namespace creative_suite::effects;
    VideoCurvesParameters neutral_curves;
    require(validVideoCurves(neutral_curves) && identityVideoCurves(neutral_curves),
        "Neutral video curves must be valid and identity.");
    auto curves_effect = makeDefaultInstance("video.curves");
    curves_effect.video_curves = neutral_curves;
    const auto original = colorFrame();
    auto unchanged = original;
    require(applyStack(unchanged, std::span<const EffectInstance>(&curves_effect, 1)) &&
            unchanged.rgba_pixels == original.rgba_pixels,
        "Neutral video curves must preserve RGB, alpha and row padding.");

    for (int kind = 0; kind < 9; ++kind) {
        auto parameters = neutral_curves;
        const CurvesPointList points = kind <= 3
            ? CurvesPointList{{0, 0}, {64, 110}, {255, 255}}
            : CurvesPointList{{0, 176}, {255, 128}};
        auto lut_frame = frame();
        lut_frame.rgba_pixels = {220, 52, 36, 91, 7, 8, 9, 10};
        switch (kind) {
        case 0: parameters.rgb.points = points; break;
        case 1: parameters.rgb.channels[0] = points; break;
        case 2: parameters.rgb.channels[1] = points; break;
        case 3: parameters.rgb.channels[2] = points; break;
        case 4: parameters.targeted[0] = points; break;
        case 5: parameters.targeted[1] = points; break;
        case 6: parameters.targeted[2] = points; break;
        case 7: parameters.targeted[3] = points; break;
        case 8: parameters.targeted[4] = points; break;
        }
        curves_effect.video_curves = parameters;
        const auto before = lut_frame.rgba_pixels;
        require(applyStack(lut_frame, std::span<const EffectInstance>(&curves_effect, 1)),
            "An individual video curve family must evaluate.");
        require(lut_frame.rgba_pixels[0] != before[0] ||
                lut_frame.rgba_pixels[1] != before[1] ||
                lut_frame.rgba_pixels[2] != before[2],
            "Each of the nine curves must affect its designated input range.");
        require(lut_frame.rgba_pixels[3] == before[3] &&
                lut_frame.rgba_pixels[7] == before[7],
            "Video Curves must preserve alpha and row padding.");
    }

    TonalQualifier wrapped;
    wrapped.enabled = true;
    wrapped.hue_min = 350.0;
    wrapped.hue_max = 12.0;
    require(tonalQualifierMatte(wrapped, {1.0, 0.0, 0.0}) > 0.99 &&
            tonalQualifierMatte(wrapped, {0.0, 0.0, 1.0}) < 0.01,
        "Hue qualifiers must correctly wrap across 0 degrees.");
    wrapped.saturation_min = 80.0;
    wrapped.saturation_max = 100.0;
    wrapped.saturation_softness = 10.0;
    const auto partial_matte = tonalQualifierMatte(wrapped, {1.0, 0.25, 0.25});
    require(partial_matte > 0.0 && partial_matte < 1.0,
        "Qualifier softness must produce a partial matte at its range edge.");
    auto nonneutral_refinement = wrapped;
    nonneutral_refinement.denoise_percent = 1.0;
    require(tonalQualifierRefinementIsNeutral(TonalQualifier{}) &&
            !tonalQualifierRefinementIsNeutral(nonneutral_refinement),
        "Default matte refinement controls must preserve the fast path.");
    auto invalid_qualifier = wrapped;
    invalid_qualifier.denoise_percent = 101.0;
    require(!validTonalQualifier(invalid_qualifier), "Denoise rejects values outside 0-100%.");
    invalid_qualifier = wrapped;
    invalid_qualifier.clean_black_percent = 70.0;
    invalid_qualifier.clean_white_percent = 60.0;
    require(!validTonalQualifier(invalid_qualifier), "Clean Black must stay below Clean White.");
    invalid_qualifier = wrapped;
    invalid_qualifier.in_out_percent = -101.0;
    require(!validTonalQualifier(invalid_qualifier), "In/Out rejects values outside its range.");
    invalid_qualifier = wrapped;
    invalid_qualifier.blur_radius_pixels = 8.1;
    require(!validTonalQualifier(invalid_qualifier), "Qualifier blur is capped at eight pixels.");

    std::vector<std::uint8_t> qualifier_pixels(3U * 3U * 4U, 255);
    for (std::size_t i = 0; i < qualifier_pixels.size(); i += 4) {
        qualifier_pixels[i] = 255; qualifier_pixels[i + 1] = 0; qualifier_pixels[i + 2] = 0;
    }
    qualifier_pixels[4U * 4U] = 0; qualifier_pixels[4U * 4U + 1] = 255;
    qualifier_pixels[4U * 4U + 2] = 0;
    TonalQualifier refined;
    refined.enabled = true; refined.hue_min = 350.0; refined.hue_max = 12.0;
    refined.denoise_percent = 100.0;
    auto matte = refinedTonalQualifierMatte(refined, qualifier_pixels, 3, 3, 12);
    require(matte.size() == 9 && matte[4] == 255,
        "Denoise mixes the tonal matte with a clamped 3x3 median.");
    refined.denoise_percent = 0.0;
    refined.in_out_percent = -100.0;
    matte = refinedTonalQualifierMatte(refined, qualifier_pixels, 3, 3, 12);
    require(matte[0] == 0 && matte[4] == 0,
        "Negative In/Out removes matte coverage.");
    refined.in_out_percent = 100.0;
    refined.blur_radius_pixels = 2.0;
    matte = refinedTonalQualifierMatte(refined, qualifier_pixels, 3, 3, 12);
    require(matte[0] == 255 && matte[4] == 255,
        "Positive In/Out clamps coverage and blur handles frame edges.");
    auto curves_frame = frame();
    curves_frame.rgba_pixels = {255, 0, 0, 79, 0, 255, 0, 31};
    auto curve_grade = neutral_curves;
    curve_grade.rgb.points = {{0, 0}, {255, 128}};
    curve_grade.qualifier = TonalQualifier{};
    curve_grade.qualifier.denoise_percent = 100.0;
    require(applyVideoCurvesFrame(curves_frame, curve_grade) == ProcessingResult::Completed &&
            curves_frame.rgba_pixels[3] == 79 && curves_frame.rgba_pixels[7] == 31,
        "Matte refinement applies the curve while preserving alpha bytes.");
    auto invalid = neutral_curves;
    invalid.targeted[0].push_back({128, 200});
    require(!validVideoCurves(invalid),
        "Video Curves must enforce the shared maximum point count.");
}

int main(int argc, char** argv) {
    if (argc > 1 && std::string_view(argv[1]) == "--video-curves-only") {
        verifyVideoCurves();
        return 0;
    }
    if (argc > 1 && std::string_view(argv[1]) == "--primary-grade-only") {
        using namespace creative_suite::effects;
        auto grade = makeDefaultInstance("video.primary_grade");
        const auto* definition = findDefinition("video.primary_grade");
        require(definition != nullptr, "Primary Grade definition is missing");
        for (const auto& parameter : definition->parameters) {
            auto at_minimum = grade;
            auto at_maximum = grade;
            auto below_minimum = grade;
            auto above_maximum = grade;
            require(setParameterValue(at_minimum, parameter.id, parameter.minimum) &&
                    setParameterValue(at_maximum, parameter.id, parameter.maximum) &&
                    !setParameterValue(below_minimum, parameter.id, parameter.minimum - 1.0) &&
                    !setParameterValue(above_maximum, parameter.id, parameter.maximum + 1.0),
                "Primary Grade control bounds changed");
        }
        require(isValid(grade) && grade.parameters.size() == 26 &&
                setParameterValue(grade, "red_gain", 150.0) &&
                setParameterValue(grade, "green_gain", 50.0) &&
                setParameterValue(grade, "blue_gain", 0.0) &&
                !setParameterValue(grade, "blue_gain", 201.0) &&
                setParameterValue(grade, "lift_red", -100.0) &&
                setParameterValue(grade, "gamma_green", 400.0) &&
                setParameterValue(grade, "offset_blue", 100.0) &&
                setParameterValue(grade, "pivot", 0.0) &&
                setParameterValue(grade, "temperature", -100.0) &&
                setParameterValue(grade, "tint", 100.0) &&
                setParameterValue(grade, "log_shadows_red", -100.0) &&
                setParameterValue(grade, "log_midtones_green", 100.0) &&
                setParameterValue(grade, "log_highlights_blue", 100.0),
                "Primary Grade parameter contract failed");
        auto direct = colorFrame();
        auto prepared = direct;
        const auto original = direct;
        const auto passes = colorAdjustmentPasses(std::span<const EffectInstance>(&grade, 1));
        require(passes && passes->size() == 1 &&
                applyStack(direct, std::span<const EffectInstance>(&grade, 1)) &&
                applyColorAdjustment(prepared, passes->front()) == ProcessingResult::Completed &&
                direct.rgba_pixels == prepared.rgba_pixels,
                "Primary Grade CPU stack and prepared passes disagree");
        for (int y = 0; y < direct.height; ++y)
            for (int x = 0; x < direct.width; ++x)
                require(direct.rgba_pixels[std::size_t(y) * direct.stride + x * 4 + 3] ==
                    original.rgba_pixels[std::size_t(y) * original.stride + x * 4 + 3],
                    "Primary Grade changed alpha");
        auto neutral = makeDefaultInstance("video.primary_grade");
        auto neutral_frame = original;
        require(applyStack(neutral_frame, std::span<const EffectInstance>(&neutral, 1)) &&
                    neutral_frame.rgba_pixels == original.rgba_pixels,
                "Neutral Primary Grade changed RGB, alpha, or row padding.");
        auto tonal_probe = [](double value, const ColorAdjustmentParameters& parameters) {
            return colorAdjustmentRgb({value, value, value}, parameters);
        };
        ColorAdjustmentParameters shadow_log;
        shadow_log.log_shadows_red = 100.0;
        ColorAdjustmentParameters midtone_log;
        midtone_log.log_midtones_green = 100.0;
        ColorAdjustmentParameters highlight_log;
        highlight_log.log_highlights_blue = 100.0;
        const auto neutral_shadow = tonal_probe(.1, {});
        const auto corrected_shadow = tonal_probe(.1, shadow_log);
        const auto neutral_midtones = tonal_probe(.5, {});
        const auto corrected_midtones = tonal_probe(.5, midtone_log);
        const auto neutral_highlights = tonal_probe(.9, {});
        const auto corrected_highlights = tonal_probe(.9, highlight_log);
        require(corrected_shadow[0] > neutral_shadow[0] &&
                    corrected_shadow[1] == neutral_shadow[1] &&
                    corrected_midtones[1] > neutral_midtones[1] &&
                    corrected_midtones[0] == neutral_midtones[0] &&
                    corrected_highlights[2] > neutral_highlights[2] &&
                    corrected_highlights[0] == neutral_highlights[0],
                "Log controls must affect their matching soft tonal range and RGB channel.");
        ColorAdjustmentParameters legacy{18.0, 132.0, 74.0, 145.0, 82.0, 112.0};
        auto previous_motion_result = original;
        auto current_motion_result = original;
        applyPreviousMotionColorAdjustment(previous_motion_result, legacy);
        require(applyColorAdjustment(current_motion_result, legacy) == ProcessingResult::Completed &&
                    current_motion_result.rgba_pixels == previous_motion_result.rgba_pixels,
                "Expanded neutral controls changed Motion's previous fused correction result.");
        auto migrated_legacy_grade = ColorAdjustmentParameters{};
        migrated_legacy_grade.offset_red = legacy.brightness;
        migrated_legacy_grade.offset_green = legacy.brightness;
        migrated_legacy_grade.offset_blue = legacy.brightness;
        migrated_legacy_grade.contrast_percent = legacy.contrast_percent;
        migrated_legacy_grade.saturation_percent = legacy.saturation_percent;
        migrated_legacy_grade.red_gain_percent = legacy.red_gain_percent;
        migrated_legacy_grade.green_gain_percent = legacy.green_gain_percent;
        migrated_legacy_grade.blue_gain_percent = legacy.blue_gain_percent;
        auto old_grade_result = original;
        auto migrated_grade_result = original;
        applyPreviousMotionColorAdjustment(old_grade_result, legacy);
        require(applyColorAdjustment(migrated_grade_result, migrated_legacy_grade) ==
                    ProcessingResult::Completed &&
                    migrated_grade_result.rgba_pixels == old_grade_result.rgba_pixels,
                "Migrated Primary Grade offsets changed the six-parameter output.");
        auto affine_only = neutral;
        require(setParameterValue(affine_only, "contrast", 0.0) &&
                    setParameterValue(affine_only, "pivot", 25.0) &&
                    setParameterValue(affine_only, "offset_red", 25.0) &&
                    setParameterValue(affine_only, "temperature", 50.0),
                "Primary Grade affine parameters were rejected.");
        auto affine_passes = colorAdjustmentPasses(std::span<const EffectInstance>(&affine_only, 1));
        auto affine_stack_frame = original;
        auto affine_fused_frame = original;
        require(affine_passes && applyStack(affine_stack_frame,
                    std::span<const EffectInstance>(&affine_only, 1)) &&
                    applyColorAdjustment(affine_fused_frame, affine_passes->front()) ==
                        ProcessingResult::Completed &&
                    affine_stack_frame.rgba_pixels == affine_fused_frame.rgba_pixels,
                "CPU Primary Grade and GPU prepared parameters diverged.");
        auto invalid = passes->front();
        invalid.red_gain_percent = std::numeric_limits<double>::quiet_NaN();
        require(applyColorAdjustment(prepared, invalid) == ProcessingResult::InvalidInput,
                "Primary Grade accepted a nonfinite RGB gain");
        return 0;
    }
    verifyGlow();
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
    {
        using namespace creative_suite::effects;
        const LevelsParameters neutral{};
        require(validAdjustment(neutral) && identityAdjustment(neutral), "Neutral Levels validation and identity");
        require(adjustmentRgb(neutral, {0., 128. / 255., 1.}) ==
            std::array<unsigned char,3>{0,128,255}, "Neutral Levels preserves encoded RGB bytes");
        require(validAdjustment(LevelsParameters{64,192,1.,32,224}) &&
            validAdjustment(LevelsParameters{0,255,.1,255,255}) &&
            validAdjustment(LevelsParameters{0,255,9.99,0,255}), "Levels inclusive bounds");
        require(!validAdjustment(LevelsParameters{255,255,1.,0,255}) &&
            !validAdjustment(LevelsParameters{0,256,1.,0,255}) &&
            !validAdjustment(LevelsParameters{0,255,1.,200,199}) &&
            !validAdjustment(LevelsParameters{0,255,0.,0,255}) &&
            !validAdjustment(LevelsParameters{0,255,std::numeric_limits<double>::quiet_NaN(),0,255}),
            "Levels invalid bounds and nonfinite gamma");
        require(adjustmentRgb(LevelsParameters{64,192,1.,32,224}, {0.,128. / 255.,1.}) ==
            std::array<unsigned char,3>{32,128,224}, "Levels clips, normalizes and maps RGB channels");
        require(adjustmentRgb(LevelsParameters{0,255,2.,0,255}, {64. / 255.,0.,1.}) ==
            std::array<unsigned char,3>{128,0,255}, "Levels gamma uses reciprocal exponent in encoded sRGB");
        LevelsParameters per_channel;
        per_channel.channels[0] = {0,255,1,255,255};
        per_channel.channels[1] = {0,255,1,0,0};
        require(adjustmentRgb(per_channel, {32. / 255.,64. / 255.,96. / 255.}) ==
            std::array<unsigned char,3>{255,0,96}, "Levels channel controls are independent");
        LevelsParameters ordered{0,255,1,0,128};
        ordered.channels[0] = {0,255,1,128,255};
        require(adjustmentRgb(ordered, {64. / 255.,64. / 255.,64. / 255.}) ==
            std::array<unsigned char,3>{144,32,32},
            "Levels RGB master is quantized before its individual channel");
        LevelsParameters blue_gamma;
        blue_gamma.channels[2] = {0,255,2,0,255};
        require(adjustmentRgb(blue_gamma, {0.,0.,64. / 255.}) ==
            std::array<unsigned char,3>{0,0,128},
            "Blue Levels applies its own gamma mapping");
        LevelsParameters invalid_channel;
        invalid_channel.channels[2].gamma = std::numeric_limits<double>::infinity();
        require(!validAdjustment(invalid_channel), "Levels validates every channel set");
    }
    {
        using namespace creative_suite::effects;
        require(validAdjustment(CurvesParameters{}) && identityAdjustment(CurvesParameters{}),
            "Neutral Curves validation and identity");
        const CurvesParameters shape{{{0,10},{64,200},{128,40},{255,245}}};
        require(validCurves(shape) && !identityCurves(shape), "Curves accept reversals");
        const auto table=curvesLut(shape);
        require(table[0]==10 && table[64]==200 && table[128]==40 && table[255]==245,
            "Curves interpolate exact control points");
        for(int x=1;x<64;++x)require(table[x]>=10 && table[x]<=200,"Curves first segment overshot");
        for(int x=65;x<128;++x)require(table[x]>=40 && table[x]<=200,"Curves reversal overshot");
        require(adjustmentRgb(shape,{0.,64./255.,1.})==
            std::array<unsigned char,3>{10,200,245},"Curves map global encoded channels");
        require(curvesLut(CurvesParameters{{{0,0},{255,255}}})[128]==128,
            "Two-point Curves are linear");
        CurvesParameters sixteen;
        sixteen.points.clear();
        for(int i=0;i<15;++i)sixteen.points.push_back({i*17,i*17});
        sixteen.points.push_back({255,255});
        require(validCurves(sixteen),"Curves permit sixteen points");
        sixteen.points.insert(sixteen.points.begin()+1,{1,1});
        require(!validCurves(sixteen) && !identityCurves(sixteen),
            "Curves reject more than sixteen points");
        require(!validCurves(CurvesParameters{{{0,0},{0,10},{255,255}}}) &&
            !validCurves(CurvesParameters{{{0,0},{255,256}}}) &&
            !validCurves(CurvesParameters{{{1,0},{255,255}}}),
            "Curves reject invalid points");
        CurvesParameters channels;
        channels.points={{0,0},{255,128}};
        channels.channels[0]={{0,0},{64,200},{255,255}};
        const auto master=curvesLut(channels.points);
        const auto red=curvesLut(channels.channels[0]);
        const auto combined=curvesLuts(channels);
        require(combined[0][255]==red[master[255]] &&
            combined[0][255]!=master[red[255]],
            "Curves apply master before the selected channel");
        require(combined[1]==curvesLut(channels.points) &&
            combined[2]==curvesLut(channels.points),
            "Curves keep unselected channels on the master curve");
        require(curvesRgb(channels,{1.,1.,1.})==
            std::array<unsigned char,3>{combined[0][255],combined[1][255],combined[2][255]},
            "Curves map RGB channels independently");
        channels.channels[1].push_back({128,20});
        require(!validCurves(channels),"Curves validate every channel");
    }
    verifySharedBlur();
    using namespace creative_suite::effects;
    require(builtInEffects().size() == 5, "The built-in visual effect catalog is incomplete.");
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
    auto pass_brightness = makeDefaultInstance("video.brightness");
    auto pass_grayscale = makeDefaultInstance("video.grayscale");
    auto pass_contrast = makeDefaultInstance("video.contrast");
    auto pass_saturation = makeDefaultInstance("video.saturation");
    require(setParameterValue(pass_brightness, "amount", 17.0) &&
        setParameterValue(pass_grayscale, "amount", 37.0) &&
        setParameterValue(pass_contrast, "amount", 123.0) &&
        setParameterValue(pass_saturation, "amount", 141.0), "pass fixture parameters");
    auto disabled = pass_brightness;
    disabled.enabled = false;
    const std::vector<EffectInstance> ordered{
        pass_brightness, pass_grayscale, disabled, pass_contrast, pass_saturation};
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
    {
        auto grade = makeDefaultInstance("video.primary_grade");
        require(isValid(grade) && grade.parameters.size() == 26,
                "Primary Grade has 26 persisted controls");
        require(setParameterValue(grade, "red_gain", 150.0) &&
                setParameterValue(grade, "green_gain", 50.0) &&
                setParameterValue(grade, "blue_gain", 0.0) &&
                !setParameterValue(grade, "blue_gain", 201.0),
                "RGB gain ranges are validated");
        auto direct = fused_original;
        auto prepared = fused_original;
        require(applyStack(direct, std::span<const EffectInstance>(&grade, 1)),
                "Primary Grade applies to the effect stack");
        const auto passes = colorAdjustmentPasses(std::span<const EffectInstance>(&grade, 1));
        require(passes && passes->size() == 1 &&
                applyColorAdjustment(prepared, passes->front()) == ProcessingResult::Completed &&
                direct.rgba_pixels == prepared.rgba_pixels,
                "Primary Grade CPU paths preserve RGB pixels and alpha identically");
    }
    return 0;
}
