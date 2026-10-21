#include <creative_suite/composition/frame_compositor.h>
#include <creative_suite/composition/shape_mask.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

creative_suite::media::RgbaFrame frame(
    int width,
    int height,
    std::vector<std::uint8_t> pixels) {
    creative_suite::media::RgbaFrame result;
    result.width = width;
    result.height = height;
    result.stride = width * 4;
    result.rgba_pixels = std::move(pixels);
    return result;
}

bool pixelEquals(
    const creative_suite::media::RgbaFrame& image,
    int x,
    int y,
    std::uint8_t red,
    std::uint8_t green,
    std::uint8_t blue,
    std::uint8_t alpha) {
    const auto offset = static_cast<std::size_t>(y) * image.stride +
        static_cast<std::size_t>(x) * 4;
    return image.rgba_pixels[offset] == red &&
        image.rgba_pixels[offset + 1] == green &&
        image.rgba_pixels[offset + 2] == blue &&
        image.rgba_pixels[offset + 3] == alpha;
}


void blendModes() {
    using namespace creative_suite;
    using composition::BlendMode;
    auto background=frame(1,1,{64,64,64,255}),source=frame(1,1,{192,192,192,255});
    const std::uint8_t expected[]{192,48,208,96,255,128};
    for(unsigned i=0;i<6;++i) {
        const auto mode=static_cast<BlendMode>(i);
        require(composition::parseBlendMode(composition::blendModeIdentifier(mode))==mode,"Blend identifier roundtrip");
        composition::CompositionLayer layer{&source};layer.blend_mode=mode;
        const auto result=composition::FrameCompositor::compose(1,1,{{&background},layer});
        require(result && pixelEquals(*result,0,0,expected[i],expected[i],expected[i],255),"Independent blend color table");
        source.rgba_pixels={200,100,50,128};
        const auto empty=composition::FrameCompositor::compose(1,1,{layer});
        require(empty && pixelEquals(*empty,0,0,100,50,25,255),"Empty backdrop is transparent, not black for blending");
        source.rgba_pixels={192,192,192,255};
    }
    for(int backdrop:{127,128}) {
        auto b=frame(1,1,{static_cast<std::uint8_t>(backdrop),0,0,255}),f=frame(1,1,{64,0,0,255});
        composition::CompositionLayer overlay{&f};overlay.blend_mode=BlendMode::Overlay;
        const auto edge=composition::FrameCompositor::compose(1,1,{{&b},overlay});
        require(edge&&edge->rgba_pixels[0]==(backdrop==127?64:65),"Overlay threshold neighbors");
    }
    {
        auto b=frame(1,1,{64,64,64,128}),a=frame(1,1,{192,192,192,128}),f=frame(1,1,{64,64,64,255});
        composition::CompositionLayer add{&a},multiply{&f};add.blend_mode=BlendMode::Add;multiply.blend_mode=BlendMode::Multiply;
        const auto layered=composition::FrameCompositor::compose(1,1,{{&b},add,multiply});
        require(layered&&pixelEquals(*layered,0,0,48,48,48,255),"Add preserves source-over coverage for subsequent layers");
    }
    composition::CompositionLayer invalid{&source};invalid.blend_mode=static_cast<BlendMode>(255);
    require(!composition::FrameCompositor::compose(1,1,{invalid}) && !composition::validBlendMode(invalid.blend_mode),"Invalid mode rejected");
    require(!composition::parseBlendMode("Multiply")&&!composition::parseBlendMode(""),"Identifiers are strict");
    // Independent per-layer premultiplied RGBA8 reference, including coverage
    // quantization and the straight-color backdrop used by blend functions.
    for(unsigned mode=1;mode<6;++mode)for(int alpha:{0,1,64,128,254,255})for(double opacity:{0.,.375,1.}) {
        background.rgba_pixels={43,129,217,73};source.rgba_pixels={232,81,155,static_cast<std::uint8_t>(alpha)};
        composition::CompositionLayer top{&source};top.blend_mode=static_cast<BlendMode>(mode);top.transform.opacity=opacity;
        const auto output=composition::FrameCompositor::compose(1,1,{{&background},top});require(output.has_value(),"Blend fixture");
        const auto ab=73./255.,as=alpha/255.*opacity;
        for(int c=0;c<3;++c) {
            const auto pb=std::lround(background.rgba_pixels[c]*ab)/255.;
            const auto cb=std::clamp(pb/ab,0.,1.),cs=source.rgba_pixels[c]/255.;double b=0;
            switch(mode){case 1:b=cb*cs;break;case 2:b=cb+cs-cb*cs;break;case 3:b=cb<=.5?2*cb*cs:2*(cb+cs-cb*cs)-1;break;case 4:b=std::min(1.,cb+cs);break;case 5:b=std::max(cb,cs)-std::min(cb,cs);break;}
            const auto expected_byte=std::lround(std::clamp(pb*(1-as)+as*((1-ab)*cs+ab*b),0.,1.)*255);
            require(output->rgba_pixels[c]==expected_byte,"Alpha/opacity independent numeric reference");
        }
        require(output->rgba_pixels[3]==255,"Flattened output remains opaque");
        // Affine geometry shares sampling, coverage and blend semantics.
        top.source_to_canvas=composition::AffineTransform2D{};
        const auto affine=composition::FrameCompositor::compose(1,1,{{&background},top});
        require(affine && affine->rgba_pixels==output->rgba_pixels,"Affine blend parity");
    }
    source.rgba_pixels={255,0,0,255};background.rgba_pixels={0,255,0,255};
    composition::CompositionLayer top{&source};top.blend_mode=BlendMode::Difference;top.source_to_canvas=composition::AffineTransform2D{1,0,0,1,1,0};
    auto wide=frame(2,1,{0,255,0,255,0,255,0,255});
    const auto partial=composition::FrameCompositor::compose(2,1,{{&wide},top});
    require(partial && pixelEquals(*partial,0,0,0,255,0,255)&&pixelEquals(*partial,1,0,255,255,0,255),"Partial overlap preserves unrelated pixels");
    auto coverage=composition::FrameCompositor::buildAlphaCoverage(source);top.source_to_canvas.reset();top.alpha_coverage=coverage;
    require(!composition::FrameCompositor::canUseAlphaCoverageFastPath(top),"Normal-only fast path cannot bypass a blend mode");
    auto huge=top;huge.alpha_coverage.reset();huge.transform.scale=1e308;
    require(composition::FrameCompositor::compose(2,1,{huge}).has_value(),"Large axis-aligned blending remains bounded");
    huge.transform.rotation_degrees=45;
    const auto huge_rotated=composition::FrameCompositor::compose(2,1,{huge});
    require(huge_rotated&&pixelEquals(*huge_rotated,0,0,255,0,0,255),"Huge rotated blend bounds do not overflow integers");
    auto extreme = animation::Transform2D{};
    extreme.position_x = 1e12;
    const auto offscreen = composition::FrameCompositor::compose(
        2, 1, {composition::CompositionLayer{&source, extreme}});
    require(offscreen && pixelEquals(*offscreen, 0, 0, 0, 0, 0, 255) &&
                pixelEquals(*offscreen, 1, 0, 0, 0, 0, 255),
            "Extreme finite non-blending positions are clipped safely");
    extreme.position_x = std::numeric_limits<double>::quiet_NaN();
    const auto non_finite = composition::FrameCompositor::compose(
        2, 1, {composition::CompositionLayer{&source, extreme}});
    require(non_finite && pixelEquals(*non_finite, 0, 0, 0, 0, 0, 255),
            "Non-finite non-blending positions are skipped safely");
    top.blend_mode=BlendMode::Normal;
    const auto normal=composition::FrameCompositor::compose(1,1,{{&background},top});
    const auto legacy=composition::FrameCompositor::compose(1,1,{{&background},{&source}});
    require(normal&&legacy&&normal->rgba_pixels==legacy->rgba_pixels,"Normal path remains byte-identical");
}

} // namespace

void trackMattes() {
    using namespace creative_suite;
    const media::RgbaFrame matte{6,1,24,{255,0,0,0, 0,0,0,255, 255,255,255,255, 255,0,0,128, 0,255,0,64, 0,0,255,200}};
    media::RgbaFrame target{6,1,28,std::vector<std::uint8_t>(28,123)};
    for(int x=0;x<6;++x)target.rgba_pixels[x*4+3]=200;
    const int expected[4][6]{{0,200,200,100,50,157},{200,0,0,100,150,43},{0,0,200,21,36,11},{200,200,0,179,164,189}};
    for(int m=0;m<4;++m) {
        auto out=target;require(composition::applyTrackMatte(out,&matte,static_cast<composition::TrackMatteMode>(m)),"valid CPU track matte");
        for(int x=0;x<6;++x) {require(out.rgba_pixels[x*4+3]==expected[m][x],"independent matte alpha reference");for(int c=0;c<3;++c)require(out.rgba_pixels[x*4+c]==123,"track matte preserves straight RGB");}
        require(out.rgba_pixels[27]==123,"track matte preserves row padding");
        out=target;require(composition::applyTrackMatte(out,nullptr,static_cast<composition::TrackMatteMode>(m)),"empty matte input");
        require(out.rgba_pixels[3]==(m%2?200:0),"inversion includes transparent backdrop");
    }
    auto tie=frame(1,1,{20,115,0,255});auto rounded=frame(1,1,{1,2,3,255});
    require(composition::applyTrackMatte(rounded,&tie,composition::TrackMatteMode::Luma)&&rounded.rgba_pixels[3]==87,"Luma half-byte tie rounds up exactly");
    rounded.rgba_pixels[3]=255;require(composition::applyTrackMatte(rounded,&tie,composition::TrackMatteMode::LumaInverted)&&rounded.rgba_pixels[3]==169,"Inverted luma half-byte tie rounds up exactly");
    auto out=target;auto invalid=matte;invalid.width=5;
    require(!composition::applyTrackMatte(out,&invalid,composition::TrackMatteMode::Alpha)&&out.rgba_pixels==target.rgba_pixels,"invalid matte geometry cannot alter output");
    require(!composition::applyTrackMatte(out,&matte,static_cast<composition::TrackMatteMode>(255))&&out.rgba_pixels==target.rgba_pixels,"invalid matte mode cannot alter output");
    require(!composition::applyTrackMatte(out,&matte,composition::TrackMatteMode::Alpha,[]{return true;}),"matte cancellation");
}

void logicalMaskRasterContract(){
    using namespace creative_suite::composition;
    for(auto kind:{ShapeMaskKind::Rectangle,ShapeMaskKind::Ellipse})for(bool invert:{false,true}){
        ShapeMaskParameters parameters;parameters.rotation=27;parameters.soft_edge=.12;parameters.invert=invert;
        ShapeMaskRasterCache full_cache,window_cache;
        const auto full=full_cache.coverage(kind,parameters,80,50);
        const auto window=window_cache.coverage(kind,parameters,30,20,{}, {80,50,18,11});
        require(full&&window,"logical mask raster window");
        for(int y=0;y<20;++y)for(int x=0;x<30;++x)for(int channel=0;channel<4;++channel)
            require(window->rgba_pixels[std::size_t(y)*window->stride+x*4+channel]==full->rgba_pixels[std::size_t(y+11)*full->stride+(x+18)*4+channel],"logical mask crop preserves legacy pixels");
        require(window_cache.coverage(kind,parameters,30,20,{}, {80,50,18,11})==window,"raster window is part of cache identity");
        require(!window_cache.coverage(kind,parameters,30,20,{}, {-1,50,0,0}),"negative receiver size rejected");
        require(!window_cache.coverage(kind,parameters,30,20,{}, {80,50,std::numeric_limits<double>::quiet_NaN(),0}),"nonfinite raster origin rejected");
    }
}
int main() {
    try {
        {
            using namespace creative_suite::composition;
            const double over[]{.49,.355,.505,.4,.55,.43},atop[]{.22,.085,.235,.13,.28,.16};
            for(unsigned i=0;i<6;++i) {
                const auto mode=static_cast<BlendMode>(i);
                require(std::abs(composeBlendChannel(mode,.1,.4,.75,.6)-over[i])<1e-12,"Shared transparent source-over math changed.");
                require(std::abs(composeBlendChannel(mode,.1,.4,.75,.6,true)-atop[i])<1e-12,"Shared source-atop math changed.");
            }
            require(std::abs(composeBlendAlpha(.3,.7)-.79)<1e-12 && composeBlendAlpha(.3,.7,true)==.3,"Blend alpha was flattened or added.");
        }
 blendModes(); trackMattes(); logicalMaskRasterContract(); } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
    using namespace creative_suite;

    try {
        // This source-pixel override must bypass aspect-fit and all legacy fast paths.
        const auto affine_source=frame(2,2,{255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,255,255});
        composition::CompositionLayer affine_layer{&affine_source};
        affine_layer.transform={.1,.9,8,0,1}; // ignored geometry
        affine_layer.source_to_canvas=composition::AffineTransform2D{2,0,1,1,1,1};
        const auto affine=composition::FrameCompositor::compose(9,6,{affine_layer});
        require(affine && pixelEquals(*affine,1,1,255,0,0,255) && pixelEquals(*affine,4,1,0,255,0,255) &&
            pixelEquals(*affine,3,2,0,0,255,255) && pixelEquals(*affine,5,2,255,255,255,255) &&
            pixelEquals(*affine,0,0,0,0,0,255),"Known sheared affine source sampling failed.");
        require(!composition::FrameCompositor::canUseAlphaCoverageFastPath(affine_layer),"Affine override entered legacy fast path.");
        const auto stage=composition::transformAffineFrame(affine_source,9,6,*affine_layer.source_to_canvas,.5);
        require(stage && pixelEquals(*stage,1,1,255,0,0,128) && pixelEquals(*stage,0,0,0,0,0,0),
            "Affine Transform stage changed sampling or transparency.");
        require(!composition::transformAffineFrame(affine_source,9,6,{1,0,0,1,1e200,1e200},1,[]{return true;}),
            "Cancelled off-canvas Transform produced an image.");
        require(!composition::validAffine({0,0,0,1,0,0}) &&
            !composition::validAffine({1,0,0,1,std::numeric_limits<double>::infinity(),0}),"Invalid affine accepted.");
        auto panorama=frame(20000,1,std::vector<std::uint8_t>(80000,255));
        composition::CompositionLayer wide{&panorama};wide.source_to_canvas=composition::AffineTransform2D{.0002,0,0,1,0,0};
        const auto wide_output=composition::FrameCompositor::compose(5,2,{wide});
        require(wide_output&&pixelEquals(*wide_output,0,0,255,255,255,255),"Affine CPU composition imposed a new source-dimension limit.");
        bool singular=false;try{(void)composition::transformAffineFrame(affine_source,9,6,{0,0,0,1,0,0},1);}catch(const std::exception&){singular=true;}
        require(singular,"Singular affine Transform did not report an error.");

        auto original=frame(3,1,{255,0,0,0, 100,20,40,128, 7,8,9,255});
        auto processed=frame(3,1,{0,0,255,255, 30,40,50,180, 99,88,77,0});
        const auto processed_source=processed;
        auto mask=frame(3,1,{128,128,128,255, 0,0,0,255, 255,255,255,255});
        require(composition::maskedBlend(original,processed,mask),"CPU MaskedBlend rejected valid inputs.");
        require(pixelEquals(processed,0,0,0,0,255,128),"Premultiplied mask blending introduced a transparent halo.");
        require(pixelEquals(processed,1,0,100,20,40,128) && pixelEquals(processed,2,0,99,88,77,0),"Mask blend endpoints changed RGBA bytes.");
        processed=processed_source; require(composition::applyMask(processed,mask),"CPU ApplyMask failed.");
        require(pixelEquals(processed,0,0,0,0,255,128) && pixelEquals(processed,1,0,30,40,50,0),"ApplyMask changed straight RGB or alpha coverage.");
        auto colored_mask = frame(1, 1, {255, 0, 0, 255});
        auto colored_target = frame(1, 1, {1, 2, 3, 255});
        require(composition::applyMask(colored_target, colored_mask) &&
                    colored_target.rgba_pixels[3] == 54,
                "CPU ApplyMask must use Rec.709 luminance for colored masks.");
        auto original_pixel = frame(1, 1, {10, 20, 30, 255});
        auto processed_pixel = frame(1, 1, {200, 210, 220, 255});
        require(composition::maskedBlend(original_pixel, processed_pixel, colored_mask) &&
                    processed_pixel.rgba_pixels[0] == 50,
                "CPU MaskedBlend must use Rec.709 luminance for colored masks.");
        require(!composition::applyMask(processed,mask,[]{return true;}),"Cancelled ApplyMask completed.");
        mask.height=2; require(!composition::maskedBlend(original,processed,mask),"Invalid mask dimensions were accepted.");

        const auto empty = composition::FrameCompositor::compose(3, 2, {});
        require(empty.has_value() && empty->width == 3 && empty->height == 2 &&
                    empty->stride == 12,
                "An empty layer list did not create the requested canvas.");
        for (int y = 0; y < empty->height; ++y) {
            for (int x = 0; x < empty->width; ++x) {
                require(pixelEquals(*empty, x, y, 0, 0, 0, 255),
                        "An empty composition did not return opaque black.");
            }
        }

        const auto source = frame(2, 2, {
            255, 0, 0, 255, 0, 255, 0, 255,
            0, 0, 255, 255, 255, 255, 255, 255});
        composition::CompositionLayer identity_layer;
        identity_layer.frame = &source;
        const auto identity = composition::FrameCompositor::compose(
            2, 2, {identity_layer});
        require(identity.has_value() &&
                    identity->rgba_pixels == source.rgba_pixels,
                "Identity composition changed the source RGBA pixels.");

        auto shifted_transform = animation::Transform2D{};
        shifted_transform.position_x = 0.25;
        const auto shifted = composition::FrameCompositor::compose(
            4,
            2,
            {composition::CompositionLayer{&source, shifted_transform}});
        require(shifted.has_value() &&
                    pixelEquals(*shifted, 0, 0, 255, 0, 0, 255) &&
                    pixelEquals(*shifted, 1, 0, 0, 255, 0, 255) &&
                    pixelEquals(*shifted, 0, 1, 0, 0, 255, 255) &&
                    pixelEquals(*shifted, 1, 1, 255, 255, 255, 255) &&
                    pixelEquals(*shifted, 2, 0, 0, 0, 0, 255) &&
                    pixelEquals(*shifted, 3, 1, 0, 0, 0, 255),
                "Normalized position or aspect-fit placement changed.");

        auto scaled_transform = animation::Transform2D{};
        scaled_transform.scale = 2.0;
        const auto scaled = composition::FrameCompositor::compose(
            4,
            2,
            {composition::CompositionLayer{&source, scaled_transform}});
        require(scaled.has_value() &&
                    pixelEquals(*scaled, 0, 0, 255, 0, 0, 255) &&
                    pixelEquals(*scaled, 1, 0, 255, 0, 0, 255) &&
                    pixelEquals(*scaled, 2, 0, 0, 255, 0, 255) &&
                    pixelEquals(*scaled, 3, 0, 0, 255, 0, 255) &&
                    pixelEquals(*scaled, 0, 1, 0, 0, 255, 255) &&
                    pixelEquals(*scaled, 2, 1, 255, 255, 255, 255),
                "Uniform layer scaling changed its source sampling.");

        auto rotated_transform = animation::Transform2D{};
        rotated_transform.rotation_degrees = 90.0;
        const auto rotated = composition::FrameCompositor::compose(
            2,
            2,
            {composition::CompositionLayer{&source, rotated_transform}});
        require(rotated.has_value() &&
                    pixelEquals(*rotated, 0, 0, 0, 0, 255, 255) &&
                    pixelEquals(*rotated, 1, 0, 255, 0, 0, 255) &&
                    pixelEquals(*rotated, 0, 1, 255, 255, 255, 255) &&
                    pixelEquals(*rotated, 1, 1, 0, 255, 0, 255),
                "Rotation was not applied around the layer center.");

        const auto opaque_red = frame(1, 1, {255, 0, 0, 255});
        const auto half_alpha_blue = frame(1, 1, {0, 0, 255, 128});
        const auto red_then_blue = composition::FrameCompositor::compose(
            1,
            1,
            {composition::CompositionLayer{&opaque_red, {}},
             composition::CompositionLayer{&half_alpha_blue, {}}});
        require(red_then_blue.has_value() &&
                    pixelEquals(*red_then_blue, 0, 0, 127, 0, 128, 255),
                "Layer order or source-over alpha blending changed.");

        const auto blue_then_red = composition::FrameCompositor::compose(
            1,
            1,
            {composition::CompositionLayer{&half_alpha_blue, {}},
             composition::CompositionLayer{&opaque_red, {}}});
        require(blue_then_red.has_value() &&
                    pixelEquals(*blue_then_red, 0, 0, 255, 0, 0, 255),
                "Later layers were not composed above earlier layers.");

        auto invalid_transform = animation::Transform2D{};
        invalid_transform.opacity = 1.5;
        media::RgbaFrame invalid_dimensions;
        invalid_dimensions.width = 0;
        invalid_dimensions.height = 1;
        const auto missing_pixels = frame(1, 1, {});
        const std::vector<composition::CompositionLayer> with_invalid_layers{
            {&opaque_red, {}},
            {nullptr, {}},
            {&half_alpha_blue, invalid_transform},
            {&invalid_dimensions, {}},
            {&missing_pixels, {}}};
        const auto invalid_layers_skipped = composition::FrameCompositor::compose(
            1, 1, with_invalid_layers);
        require(invalid_layers_skipped.has_value() &&
                    invalid_layers_skipped->rgba_pixels == opaque_red.rgba_pixels,
                "Invalid layers were not skipped while valid layers were composed.");

        require(!composition::FrameCompositor::compose(0, 1, {}).has_value() &&
                    !composition::FrameCompositor::compose(1, 0, {}).has_value(),
                "A non-positive canvas dimension was accepted.");
        const int first_unrepresentable_stride_width =
            std::numeric_limits<int>::max() / 4 + 1;
        require(!composition::FrameCompositor::compose(
                     first_unrepresentable_stride_width, 1, {}).has_value(),
                "A canvas whose RGBA stride overflows int was accepted.");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    return 0;
}
