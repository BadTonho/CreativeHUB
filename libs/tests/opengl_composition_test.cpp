#include <creative_suite/composition/opengl_frame_compositor.h>

#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QThread>
#include <QSurfaceFormat>

#include <chrono>
#include <algorithm>
#include <cmath>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>

using namespace creative_suite;
using namespace creative_suite::composition;
namespace {
void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}
media::RgbaFrame fixture(int width, int height, bool transparent, int padding = 0) {
    media::RgbaFrame f{width, height, width * 4 + padding, {}};
    f.rgba_pixels.resize(static_cast<std::size_t>(f.stride) * height, 237);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        auto* p = f.rgba_pixels.data() + static_cast<std::size_t>(y) * f.stride + x * 4;
        p[0] = static_cast<std::uint8_t>((x * 17 + y * 23) % 256);
        p[1] = static_cast<std::uint8_t>((x * 37 + y * 3) % 256);
        p[2] = static_cast<std::uint8_t>((x * 7 + y * 41) % 256);
        p[3] = transparent ? static_cast<std::uint8_t>((x * 29 + y * 31) % 256) : 255;
    }
    return f;
}
void compare(OpenGlFrameCompositor& gpu, int width, int height,
    const std::vector<CompositionLayer>& layers, const std::string& name) {
    const auto cpu = FrameCompositor::compose(width, height, layers);
    OpenGlCompositionTimings timings;
    auto output = gpu.compose(width, height, layers, {}, &timings);
    if (output.status == OpenGlCompositionStatus::Unsupported && output.operation == "check-precision") {
        require(!output.frame && std::any_of(layers.begin(), layers.end(), [](const auto& layer) {
            return layer.transform.rotation_degrees != 0 || layer.source_to_canvas.has_value();
        }), "Unexpected precision fallback.");
        std::cout << "LIMITED: " << name << ": precise GPU parity pending; " << output.cause << '\n';
        return;
    }
    require(output.status == OpenGlCompositionStatus::Complete && output.frame.has_value(),
        name + ": GPU failed: " + output.operation + ": " + output.cause);
    const auto& frame = *output.frame;
    require(cpu && frame.width == cpu->width && frame.height == cpu->height &&
        frame.stride == cpu->stride, name + ": invalid output geometry");
    for (std::size_t i = 0; i < frame.rgba_pixels.size(); ++i) {
        const int delta = std::abs(int(frame.rgba_pixels[i]) - int(cpu->rgba_pixels[i]));
        if (delta > (i % 4 == 3 ? 0 : 2)) require(false, name + ": mismatch at pixel " +
            std::to_string(i / 4) + " channel " + std::to_string(i % 4) +
            " CPU=" + std::to_string(cpu->rgba_pixels[i]) +
            " GPU=" + std::to_string(frame.rgba_pixels[i]));
    }
    require(timings.readback_bytes == static_cast<std::uint64_t>(width) * height * 4,
        name + ": readback bytes missing");
    OpenGlCompositionTimings direct_timings;
    auto direct = gpu.composeTexture(width, height, layers, {}, &direct_timings);
    require(direct.status == OpenGlCompositionStatus::Complete && direct.frame,
        name + ": direct texture composition failed: " + direct.cause);
    require(direct_timings.readback_bytes == 0 && direct_timings.readback_nanoseconds == 0 &&
        direct.frame->bottomLeftOrigin() && direct.frame->valid(), name + ": invalid direct contract");
    const auto recovered = gpu.readback(direct.frame);
    require(recovered.frame && recovered.frame->rgba_pixels == frame.rgba_pixels,
        name + ": on-demand texture readback differs from RGBA composition");
}
void parity(OpenGlFrameCompositor& gpu) {
    {
        auto b=fixture(17,13,true),f=fixture(9,7,true);CompositionLayer front{&f};front.transform.opacity=.375;
        for(unsigned m=1;m<6;++m) {
            front.blend_mode=static_cast<BlendMode>(m);
            compare(gpu,53,39,{{&b},front},"blend alpha/opacity and empty backdrop");
            compare(gpu,53,39,{front},"blend over entirely empty canvas");
            front.source_to_canvas=AffineTransform2D{2,.25,.5,1.5,3,4};
            compare(gpu,53,39,{{&b},front,{&f}},"affine blend followed by Normal");front.source_to_canvas.reset();
        }
        const auto before=gpu.resourceUsage().texture_bytes;
        compare(gpu,53,39,{{&b},front},"reuse warmed backdrop");
        require(gpu.resourceUsage().texture_bytes==before,"Backdrop allocation grew after warmup");
        auto invalid=front;invalid.blend_mode=static_cast<BlendMode>(255);
        require(gpu.compose(53,39,{invalid}).status==OpenGlCompositionStatus::Unsupported,"Invalid blend mode is explicit");
        require(gpu.compose(53,39,{front},[]{return true;}).status==OpenGlCompositionStatus::Cancelled,"Blend cancellation");
        compare(gpu,53,39,{{&b}},"Normal reuse after blended frames");
    }

    {
        auto affine_source=fixture(13,7,true,4);
        CompositionLayer affine_layer{&affine_source};
        affine_layer.source_to_canvas=AffineTransform2D{1.75,.35,.7,.625,8.125,4.25};
        affine_layer.transform={.9,.1,3,0,.73};
        compare(gpu,53,39,{affine_layer},"sheared source-pixel affine override");
        OpenGlImageOperation affine_operation;
        affine_operation.kind=OpenGlImageOperationKind::Transform;
        affine_operation.background={&affine_source};affine_operation.output_width=53;affine_operation.output_height=39;
        affine_operation.transform.opacity=.73;affine_operation.source_to_canvas=affine_layer.source_to_canvas;
        auto affine_result=gpu.processImage(affine_operation);
        if(affine_result.status==OpenGlCompositionStatus::Unsupported) {
            require(!affine_result.frame && affine_result.operation=="affine-image-transform","Affine stage fallback contract changed.");
        } else {
            require(affine_result.status==OpenGlCompositionStatus::Complete && affine_result.frame,"Affine image Transform failed.");
            auto rgba=gpu.readback(affine_result.frame);
            auto expected=transformAffineFrame(affine_source,53,39,*affine_layer.source_to_canvas,.73);
            require(rgba.frame && expected && rgba.frame->rgba_pixels==expected->rgba_pixels,"Affine Transform CPU/OpenGL parity failed.");
        }
    } // Release the affine result lease before exercising the bounded mask pool.
    // Generic mask operations accept coverage uploaded separately and retain
    // resident image intermediates until the caller explicitly requests RGBA.
    auto original=fixture(16,12,true), processed=fixture(16,12,true), mask=fixture(16,12,false);
    for (std::size_t i=0;i<processed.rgba_pixels.size();i+=4) {
        processed.rgba_pixels[i]=255-processed.rgba_pixels[i];
        mask.rgba_pixels[i]=static_cast<std::uint8_t>((i/4)%3==0 ? 0 : (i/4)%3==1 ? 128 : 255);
    }
    for (const auto kind : {OpenGlImageOperationKind::ApplyMask,OpenGlImageOperationKind::MaskedBlend}) {
        auto cpu=processed;
        require(kind==OpenGlImageOperationKind::ApplyMask ? applyMask(cpu,mask) : maskedBlend(original,cpu,mask),"CPU mask operation failed.");
        OpenGlImageOperation copy;copy.background={&processed};
        auto resident=gpu.processImage(copy);require(bool(resident.frame),"Mask fixture upload failed.");
        OpenGlImageOperation operation;operation.kind=kind;operation.mask={&mask};
        operation.background=kind==OpenGlImageOperationKind::ApplyMask ? OpenGlImageInput{nullptr,resident.frame} : OpenGlImageInput{&original};
        if (kind==OpenGlImageOperationKind::MaskedBlend) operation.foreground={nullptr,resident.frame};
        OpenGlCompositionTimings measured;auto output=gpu.processImage(operation,{},&measured);
        require(output.status==OpenGlCompositionStatus::Complete && output.frame && measured.readback_bytes==0,"Mask operation downloaded its intermediate.");
        auto rgba=gpu.readback(output.frame);require(rgba.frame && rgba.frame->rgba_pixels==cpu.rgba_pixels,"Shared mask CPU/GPU parity failed.");
        mask.width=15;
        auto invalid=gpu.processImage(operation);require(invalid.status==OpenGlCompositionStatus::Unsupported,"Mismatched mask geometry accepted.");mask.width=16;
    }

    auto opaque = fixture(16, 12, false);
    {
        OpenGlImageOperation transform;
        transform.kind = OpenGlImageOperationKind::Transform;
        transform.background = {&opaque};
        transform.output_width = 30;
        transform.output_height = 20;
        transform.transform.position_x = .6;
        transform.transform.opacity = .5;
        transform.transform.scale = std::min(30.0 / opaque.width, 20.0 / opaque.height);
        auto output = gpu.processImage(transform);
        require(output.status == OpenGlCompositionStatus::Complete && output.frame,
            "Project-canvas image transform failed.");
        auto actual = gpu.readback(output.frame);
        require(actual.frame && actual.frame->width == 30 && actual.frame->height == 20,
            "Project-canvas transform lost output geometry.");
        const auto* center = actual.frame->rgba_pixels.data() + 10 * actual.frame->stride + 18 * 4;
        const auto* source = opaque.rgba_pixels.data() + 6 * opaque.stride + 8 * 4;
        require(center[0] == source[0] && center[1] == source[1] &&
            center[2] == source[2] && center[3] == 128,
            "Project-canvas transform lost source geometry or alpha.");
        require(actual.frame->rgba_pixels[3] == 0,
            "Project-canvas transform did not retain transparent exposed areas.");
    }
    auto alpha = fixture(13, 7, true);
    auto padding = fixture(9, 11, true, 12);
    auto odd_padding = fixture(11, 9, true, 3);
    compare(gpu, 16, 12, {}, "empty black canvas");
    compare(gpu, 16, 12, {{&opaque}}, "opaque identity");
    compare(gpu, 53, 39, {{&alpha}}, "transparent aspect fit");
    compare(gpu, 48, 36, {{&opaque}, {&alpha}}, "ordered layers");
    compare(gpu, 48, 36, {{&alpha}, {&opaque}}, "reversed layers");
    compare(gpu, 51, 41, {{&padding}, {&odd_padding}}, "padded RGBA rows");
    for (const double rotation : {0., 15., 45., 90., -37., 180., 270.}) {
        animation::Transform2D t;
        t.position_x = .41; t.position_y = .63; t.scale = .73;
        t.rotation_degrees = rotation; t.opacity = .47;
        compare(gpu, 61, 47, {{&opaque}, {&alpha, t}}, "transform " + std::to_string(rotation));
        t = {};
        t.rotation_degrees = rotation;
        compare(gpu, 48, 36, {{&opaque, t}}, "centered rotation " + std::to_string(rotation));
        compare(gpu, 79, 51, {{&opaque, t}}, "rotation at texel boundaries " + std::to_string(rotation));
    }
    animation::Transform2D t;
    t.position_x = -.07; t.position_y = 1.03; t.scale = 1.7; t.rotation_degrees = 23;
    compare(gpu, 53, 39, {{&odd_padding, t}}, "canvas edge clipping");
    // A sparse alpha raster represents text; coverage hints must not alter GPU pixels.
    auto text = fixture(31, 17, true);
    for (int y = 0; y < text.height; ++y) for (int x = 0; x < text.width; ++x)
        text.rgba_pixels[static_cast<std::size_t>(y) * text.stride + x * 4 + 3] =
            x > 6 && x < 23 && y > 5 && y < 11 ? 190 : 0;
    CompositionLayer text_layer{&text};
    text_layer.alpha_coverage = FrameCompositor::buildAlphaCoverage(text);
    compare(gpu, 79, 51, {{&opaque}, text_layer}, "text raster with coverage");
    auto invalid = opaque; invalid.stride = 1;
    t.opacity = 0;
    compare(gpu, 16, 12, {{nullptr}, {&invalid}, {&opaque, t}}, "unusable layers");
    t.scale = std::numeric_limits<double>::quiet_NaN();
    compare(gpu, 16, 12, {{&opaque, t}}, "invalid transform");
    // Quality changes resize only the output, preserving the reusable source.
    for (const auto dimensions : {std::pair{1920, 1080}, {960, 540}, {480, 270}})
        compare(gpu, dimensions.first, dimensions.second, {{&opaque}}, "preview quality");
}
void asynchronousReadbackParity(OpenGlFrameCompositor& gpu) {
    auto first_source = fixture(37, 23, true, 8);
    auto second_source = fixture(31, 19, false, 4);
    animation::Transform2D second_transform;
    second_transform.position_x = .43;
    second_transform.position_y = .58;
    second_transform.opacity = .72;
    const std::vector<CompositionLayer> first_layers{{&first_source}};
    std::vector<CompositionLayer> second_layers{{&first_source},
        {&second_source, second_transform}};
    second_layers.back().blend_mode=BlendMode::Screen;
    const auto first_reference = gpu.compose(73, 51, first_layers);
    const auto second_reference = gpu.compose(73, 51, second_layers);
    require(first_reference.frame && second_reference.frame,
        "synchronous readback references failed before asynchronous comparison");
    unsigned slot_count = 0;
    const auto frame_bytes = 73ULL * 51 * 4;
    unsigned failed_slot_count = 0;
    const auto allocation_failure = gpu.prepareAsyncReadback(
        73, 51, frame_bytes * 2, 2, &failed_slot_count, true);
    require(allocation_failure.status == OpenGlCompositionStatus::Failed &&
        allocation_failure.operation == "allocate-pbo" && failed_slot_count == 0,
        "injected PBO allocation failure did not report a clean fallback condition");
    const auto prepared = gpu.prepareAsyncReadback(73, 51, frame_bytes * 2, 2, &slot_count);
    require(prepared.status == OpenGlCompositionStatus::Complete && slot_count == 2 &&
        gpu.asyncReadbackSlotCount() == 2,
        "two PBO slots were not prepared within the explicit staging budget");
    const auto first = gpu.submitAsyncReadback(73, 51, first_layers);
    const auto second = gpu.submitAsyncReadback(73, 51, second_layers);
    require(first.status == OpenGlCompositionStatus::Complete &&
        second.status == OpenGlCompositionStatus::Complete &&
        first.ticket.value != second.ticket.value,
        "two asynchronous readbacks did not receive distinct tickets");
    const auto polled_first = gpu.pollAsyncReadback(first.ticket);
    require(polled_first.status == OpenGlCompositionStatus::Busy ||
            polled_first.status == OpenGlCompositionStatus::Complete,
        "nonblocking PBO poll returned an invalid readback state");
    require(gpu.pollAsyncReadback(second.ticket).status == OpenGlCompositionStatus::Busy,
        "PBO polling accepted an out-of-order ticket");
    require(gpu.submitAsyncReadback(73, 51, first_layers).status ==
        OpenGlCompositionStatus::Busy,
        "PBO backpressure did not stop submission beyond the bounded pool");
    require(gpu.collectAsyncReadback(second.ticket).status == OpenGlCompositionStatus::Busy,
        "out-of-order PBO ticket collection was not rejected");
    const auto cancelled = gpu.collectAsyncReadback(first.ticket, [] { return true; });
    require(cancelled.status == OpenGlCompositionStatus::Cancelled,
        "pre-cancelled asynchronous collection did not preserve its ticket");
    const auto collected_first = gpu.collectAsyncReadback(first.ticket);
    const auto collected_second = gpu.collectAsyncReadback(second.ticket);
    require(collected_first.status == OpenGlCompositionStatus::Complete &&
        collected_first.frame && collected_first.frame->rgba_pixels ==
            first_reference.frame->rgba_pixels,
        "first asynchronous frame differs from synchronous GPU output");
    require(collected_second.status == OpenGlCompositionStatus::Complete &&
        collected_second.frame && collected_second.frame->rgba_pixels ==
            second_reference.frame->rgba_pixels,
        "second asynchronous frame differs from synchronous GPU output or order");
    const auto alpha_cpu = FrameCompositor::compose(73, 51, second_layers, nullptr,
        CompositionOutputAlpha::Preserve);
    const auto alpha_sync = gpu.compose(73, 51, second_layers, {}, nullptr,
        CompositionOutputAlpha::Preserve);
    require(alpha_cpu && alpha_sync.frame &&
        alpha_sync.frame->rgba_pixels.size() == alpha_cpu->rgba_pixels.size(),
        "transparent GPU composition did not produce an alpha frame");
    for (std::size_t index = 0; index < alpha_cpu->rgba_pixels.size(); ++index)
        require(std::abs(int(alpha_cpu->rgba_pixels[index]) -
                    int(alpha_sync.frame->rgba_pixels[index])) <= (index % 4 == 3 ? 0 : 2),
            "transparent CPU/OpenGL composition differs");
    const auto alpha_empty = gpu.compose(13, 9, {}, {}, nullptr,
        CompositionOutputAlpha::Preserve);
    require(alpha_empty.frame && std::all_of(alpha_empty.frame->rgba_pixels.begin(),
            alpha_empty.frame->rgba_pixels.end(), [](std::uint8_t value) { return value == 0; }),
        "empty OpenGL alpha output must be transparent");
    const auto alpha_submitted = gpu.submitAsyncReadback(73, 51, second_layers, {},
        CompositionOutputAlpha::Preserve);
    require(alpha_submitted.status == OpenGlCompositionStatus::Complete,
        "transparent PBO readback submission failed");
    const auto alpha_collected = gpu.collectAsyncReadback(alpha_submitted.ticket);
    require(alpha_collected.status == OpenGlCompositionStatus::Complete && alpha_collected.frame &&
        alpha_collected.frame->rgba_pixels == alpha_sync.frame->rgba_pixels,
        "asynchronous OpenGL readback changed transparent straight-alpha pixels");
    const auto reused = gpu.submitAsyncReadback(73, 51, second_layers);
    require(reused.status == OpenGlCompositionStatus::Complete,
        "a collected PBO slot could not be reused");
    const auto collection_failure = gpu.collectAsyncReadback(reused.ticket, {}, true);
    require(collection_failure.status == OpenGlCompositionStatus::Failed &&
        collection_failure.operation == "map-pbo",
        "injected PBO collection failure did not preserve the failed ticket");
    gpu.discardAsyncReadbacks();
    const auto after_failure = gpu.submitAsyncReadback(73, 51, second_layers);
    require(after_failure.status == OpenGlCompositionStatus::Complete &&
        gpu.collectAsyncReadback(after_failure.ticket).status == OpenGlCompositionStatus::Complete,
        "discarding tickets after collection failure did not release the PBO pool");
    require(gpu.collectAsyncReadback(reused.ticket).status == OpenGlCompositionStatus::Unsupported,
        "discarded PBO ticket remained usable after failure recovery");
    require(gpu.asyncReadbackSlotCount() == 2,
        "collection failure recovery unexpectedly destroyed the reusable PBO pool");
    unsigned reduced_slots = 0;
    const auto reduced = gpu.prepareAsyncReadback(73, 51, frame_bytes, 2, &reduced_slots);
    require(reduced.status == OpenGlCompositionStatus::Complete && reduced_slots == 1,
        "PBO pool sizing did not reduce the slot count to fit the staging budget");
    const auto no_budget = gpu.prepareAsyncReadback(73, 51, frame_bytes - 1, 2);
    require(no_budget.status == OpenGlCompositionStatus::Unsupported,
        "a staging budget smaller than one readback frame was not rejected");
    gpu.discardAsyncReadbacks();
}
void trackMatteParity(OpenGlFrameCompositor& gpu) {
    auto target=fixture(16,12,true),matte=fixture(16,12,true);
    target.rgba_pixels[3]=255;matte.rgba_pixels[0]=20;matte.rgba_pixels[1]=115;matte.rgba_pixels[2]=0;matte.rgba_pixels[3]=255;
    for(int mode=0;mode<4;++mode)for(bool empty:{false,true}) {
        auto expected=target;
        require(applyTrackMatte(expected,empty?nullptr:&matte,static_cast<TrackMatteMode>(mode)),"CPU matte reference");
        OpenGlImageOperation op;op.kind=OpenGlImageOperationKind::ApplyTrackMatte;op.background={&target};
        op.track_matte_mode=static_cast<TrackMatteMode>(mode);if(!empty)op.mask={&matte};
        OpenGlCompositionTimings timings;auto output=gpu.processImage(op,{},&timings);
        require(output.status==OpenGlCompositionStatus::Complete&&output.frame&&timings.readback_bytes==0,"Matte image operation must remain on GPU: "+output.operation+": "+output.cause);
        auto read=gpu.readback(output.frame);require(read.frame&&read.frame->rgba_pixels==expected.rgba_pixels,"Straight RGBA track matte parity");
        CompositionLayer layer{&target};layer.texture_frame=output.frame;layer.blend_mode=BlendMode::Screen;
        CompositionLayer reference{&expected};reference.blend_mode=BlendMode::Screen;
        auto cpu=FrameCompositor::compose(16,12,{{&matte},reference});
        auto composed=gpu.compose(16,12,{{&matte},layer});
        require(cpu&&composed.frame,"Matted composition delivery");
        for(std::size_t x=0;x<cpu->rgba_pixels.size();++x)require(std::abs(int(cpu->rgba_pixels[x])-int(composed.frame->rgba_pixels[x]))<=(x%4==3?0:2),"Matted blend RGBA tolerance");
        const auto cpu_alpha=FrameCompositor::compose(16,12,{{&matte},reference},nullptr,CompositionOutputAlpha::Preserve);
        const auto gpu_alpha=gpu.compose(16,12,{{&matte},layer}, {}, nullptr,CompositionOutputAlpha::Preserve);
        require(cpu_alpha&&gpu_alpha.frame,"Transparent matted composition delivery");
        for (std::size_t x = 0; x < cpu_alpha->rgba_pixels.size(); ++x) {
            const int cpu_value = cpu_alpha->rgba_pixels[x];
            const int gpu_value = gpu_alpha.frame->rgba_pixels[x];
            if (std::abs(cpu_value - gpu_value) > (x % 4 == 3 ? 0 : 2))
                throw std::runtime_error("Transparent matted CPU/OpenGL alpha parity at " +
                    std::to_string(x / 4) + " channel " + std::to_string(x % 4) +
                    " CPU=" + std::to_string(cpu_value) + " GPU=" +
                    std::to_string(gpu_value) + " mode=" + std::to_string(mode) +
                    " empty=" + std::to_string(empty));
        }
        {
            auto direct=gpu.composeTexture(16,12,{{&matte},layer});
            require(direct.frame!=nullptr,"Matted texture delivery");
            auto rgba=gpu.readback(direct.frame);require(rgba.frame&&rgba.frame->rgba_pixels==composed.frame->rgba_pixels,"Matted direct texture matches RGBA");
            require(gpu.prepareAsyncReadback(16,12,16*12*4*2).status==OpenGlCompositionStatus::Complete,"Matted async staging");
            auto submitted=gpu.submitAsyncReadback(16,12,{{&matte},layer});require(submitted.ticket.value!=0,"Matted async submission");
            auto collected=gpu.collectAsyncReadback(submitted.ticket);
            require(collected.frame&&collected.frame->rgba_pixels==composed.frame->rgba_pixels,"Matted async RGBA matches direct output");
        }
        if(!empty) {
            OpenGlImageOperation resident=op;resident.background={nullptr,output.frame};resident.mask={nullptr,output.frame};
            auto nested=expected;require(applyTrackMatte(nested,&expected,static_cast<TrackMatteMode>(mode)),"chain reference");
            auto next=gpu.processImage(resident);require(next.frame!=nullptr,"Resident matte inputs");
            auto nested_rgba=gpu.readback(next.frame);require(nested_rgba.frame&&nested_rgba.frame->rgba_pixels==nested.rgba_pixels,"Resident matte chain parity");
            op.mask.rgba=nullptr;op.mask={&matte};matte.width=15;
            require(gpu.processImage(op).status==OpenGlCompositionStatus::Unsupported,"Mismatched matte dimensions refused");
            matte.width=0;require(gpu.processImage(op).status==OpenGlCompositionStatus::Unsupported,"A provided zero-width matte is invalid, not an empty source");matte.width=16;
        }
        op.track_matte_mode=static_cast<TrackMatteMode>(255);
        require(gpu.processImage(op).status==OpenGlCompositionStatus::Unsupported,"Invalid matte mode refused");
        op.track_matte_mode=TrackMatteMode::Alpha;
        require(gpu.processImage(op,[]{return true;}).status==OpenGlCompositionStatus::Cancelled,"Matte cancellation");
    }
}

void glowParity(OpenGlFrameCompositor& gpu, bool allow_precision_fallback = false)
{
    auto source = fixture(9, 7, true, 12);
    for (const auto mode : {effects::GlowMode::Outer, effects::GlowMode::Inner,
                            effects::GlowMode::Both}) {
        effects::GlowParameters parameters;
        parameters.color = {231, 42, 117};
        parameters.opacity_percent = 73.0;
        parameters.radius_pixels = 1.5;
        parameters.mode = mode;
        const auto expected = effects::applyGlow(source, parameters);
        OpenGlImageOperation operation;
        operation.kind = OpenGlImageOperationKind::Glow;
        operation.background = {&source};
        operation.glow = parameters;
        OpenGlCompositionTimings timings;
        const auto output = gpu.processImage(operation, {}, &timings);
        if (allow_precision_fallback && output.status == OpenGlCompositionStatus::Unsupported) {
            require(output.operation == "affine-image-transform" && !output.frame,
                "Core-only Glow must request precision fallback without an output frame");
            continue;
        }
        require(expected && output.status == OpenGlCompositionStatus::Complete && output.frame,
            "OpenGL Glow operation failed: " + output.operation + ": " + output.cause);
        const auto actual = gpu.readback(output.frame);
        require(actual.frame && actual.frame->width == expected->width &&
                actual.frame->height == expected->height,
            "OpenGL Glow output bounds differ from CPU");
        for (int y = 0; y < expected->height; ++y) {
            for (int x = 0; x < expected->width * 4; ++x) {
                const auto expected_byte = expected->rgba_pixels[std::size_t(y) * expected->stride + x];
                const auto actual_byte = actual.frame->rgba_pixels[std::size_t(y) * actual.frame->stride + x];
                require(std::abs(int(expected_byte) - int(actual_byte)) <= (x % 4 == 3 ? 0 : 2),
                    "OpenGL Glow pixels differ from the CPU implementation");
            }
        }
        require(timings.gaussian_blur_count == 1,
            "OpenGL Glow records its internal alpha blur");
    }
    OpenGlImageOperation cancelled;
    cancelled.kind = OpenGlImageOperationKind::Glow;
    cancelled.background = {&source};
    require(gpu.processImage(cancelled, [] { return true; }).status ==
                OpenGlCompositionStatus::Cancelled,
            "OpenGL Glow checks cancellation before allocating scratch targets");
}

void colorAdjustmentParity(OpenGlFrameCompositor& gpu) {
    auto source = fixture(23, 13, true, 7);
    auto adjusted = source;
    const std::vector<effects::ColorAdjustmentParameters> adjustments{
        {18.0, 132.0, 74.0, 145.0, 82.0, 112.0},
        {-7.0, 83.0, 145.0}, {2.0, 105.0, 91.0}};
    effects::ColorAdjustmentParameters expanded;
    expanded.lift_red = -23.0; expanded.lift_green = 11.0; expanded.lift_blue = 7.0;
    expanded.gamma_red = 72.0; expanded.gamma_green = 138.0; expanded.gamma_blue = 221.0;
    expanded.offset_red = 6.0; expanded.offset_green = -9.0; expanded.offset_blue = 13.0;
    expanded.contrast_percent = 127.0; expanded.pivot_percent = 43.0;
    expanded.temperature = 34.0; expanded.tint = -19.0;
    expanded.saturation_percent = 126.0;
    expanded.red_gain_percent = 116.0; expanded.green_gain_percent = 91.0;
    expanded.blue_gain_percent = 137.0;
    expanded.log_shadows_red = 38.0; expanded.log_shadows_green = -12.0;
    expanded.log_midtones_blue = 24.0; expanded.log_highlights_red = -31.0;
    effects::VideoCurvesParameters curves;
    curves.rgb.points = {{0, 0}, {92, 124}, {255, 255}};
    curves.rgb.channels[0] = {{0, 0}, {166, 233}, {255, 255}};
    curves.targeted[0] = {{0, 129}, {255, 127}};
    curves.targeted[1] = {{0, 128}, {255, 129}};
    curves.targeted[2] = {{0, 128}, {255, 127}};
    curves.targeted[3] = {{0, 129}, {255, 128}};
    curves.targeted[4] = {{0, 128}, {255, 129}};
    curves.qualifier.enabled = true;
    curves.qualifier.hue_min = 338.0;
    curves.qualifier.hue_max = 24.0;
    curves.qualifier.hue_softness = 8.0;
    curves.qualifier.saturation_min = 12.0;
    curves.qualifier.saturation_max = 95.0;
    curves.qualifier.saturation_softness = 5.0;
    curves.qualifier.luminance_min = 8.0;
    curves.qualifier.luminance_max = 94.0;
    curves.qualifier.luminance_softness = 4.0;
    auto all_controls = adjustments;
    all_controls.push_back(expanded);
    effects::ColorAdjustmentParameters curve_adjustment;
    curve_adjustment.video_curves = curves;
    all_controls.push_back(curve_adjustment);
    for (const auto& parameters : all_controls) {
        require(effects::applyColorAdjustment(adjusted, parameters) ==
                    effects::ProcessingResult::Completed,
                "CPU reference adjustment failed");
    }
    CompositionLayer layer{&source};
    layer.gpu_color_adjustments = all_controls;
    const auto cpu = FrameCompositor::compose(source.width, source.height, {{&adjusted}});
    OpenGlCompositionTimings timings;
    const auto output = gpu.compose(source.width, source.height, {layer}, {}, &timings);
    require(cpu && output.status == OpenGlCompositionStatus::Complete && output.frame,
        "GPU color adjustment composition failed: " + output.operation + ": " + output.cause);
    for (std::size_t prefix = 0; prefix < all_controls.size(); ++prefix) {
        auto expected_prefix = source;
        std::vector<effects::ColorAdjustmentParameters> controls(
            all_controls.begin(), all_controls.begin() + static_cast<std::ptrdiff_t>(prefix + 1));
        for (const auto& parameters : controls)
            require(effects::applyColorAdjustment(expected_prefix, parameters) ==
                        effects::ProcessingResult::Completed,
                    "CPU color adjustment prefix failed");
        const auto composed_prefix = FrameCompositor::compose(
            source.width, source.height, {{&expected_prefix}});
        CompositionLayer prefix_layer{&source};
        prefix_layer.gpu_color_adjustments = controls;
        const auto actual_prefix = gpu.compose(source.width, source.height, {prefix_layer});
        require(composed_prefix && bool(actual_prefix.frame),
            "GPU color adjustment prefix failed: " + actual_prefix.cause);
        for (std::size_t i = 0; i < composed_prefix->rgba_pixels.size(); ++i) {
            const int delta = std::abs(int(composed_prefix->rgba_pixels[i]) -
                int(actual_prefix.frame->rgba_pixels[i]));
            if (delta > (i % 4 == 3 ? 0 : 1))
                throw std::runtime_error("GPU color adjustment prefix " +
                    std::to_string(prefix) + " mismatch at pixel " +
                    std::to_string((i / 4) % static_cast<std::size_t>(source.width)) + "," +
                    std::to_string((i / 4) / static_cast<std::size_t>(source.width)) +
                    " channel " + std::to_string(i % 4) + " CPU=" +
                    std::to_string(composed_prefix->rgba_pixels[i]) + " GPU=" +
                    std::to_string(actual_prefix.frame->rgba_pixels[i]));
        }
    }
    for (std::size_t i = 0; i < cpu->rgba_pixels.size(); ++i) {
        const auto delta = std::abs(static_cast<int>(cpu->rgba_pixels[i]) -
                                    static_cast<int>(output.frame->rgba_pixels[i]));
        if (delta > (i % 4 == 3 ? 0 : 1)) {
            throw std::runtime_error("GPU color adjustment mismatch at pixel " +
                std::to_string((i / 4) % static_cast<std::size_t>(source.width)) + "," +
                std::to_string((i / 4) / static_cast<std::size_t>(source.width)) +
                " channel " + std::to_string(i % 4) + " CPU=" +
                std::to_string(cpu->rgba_pixels[i]) + " GPU=" +
                std::to_string(output.frame->rgba_pixels[i]));
        }
    }
    require(timings.color_adjustment_count == all_controls.size() &&
                timings.color_adjustment_submission_nanoseconds > 0,
            "GPU color adjustment work is measured");

    const std::array<std::array<double, 4>, 4> refinement_controls{{
        {30.0, 0.0, 100.0, 0.0},
        {0.0, 12.0, 91.0, -27.0},
        {0.0, 0.0, 100.0, 0.0},
        {65.0, 8.0, 89.0, 23.0}}};
    for (std::size_t index = 0; index < refinement_controls.size(); ++index) {
        auto spatial_refinement = curve_adjustment;
        const auto& controls = refinement_controls[index];
        spatial_refinement.video_curves->qualifier.denoise_percent = controls[0];
        spatial_refinement.video_curves->qualifier.clean_black_percent = controls[1];
        spatial_refinement.video_curves->qualifier.clean_white_percent = controls[2];
        spatial_refinement.video_curves->qualifier.in_out_percent = controls[3];
        spatial_refinement.video_curves->qualifier.blur_radius_pixels =
            index == 2 || index == 3 ? 2.0 : 0.0;
        auto expected = source;
        require(effects::applyColorAdjustment(expected, spatial_refinement) ==
                    effects::ProcessingResult::Completed,
                "CPU qualifier refinement reference failed.");
        OpenGlImageOperation refined_operation;
        refined_operation.kind = OpenGlImageOperationKind::Color;
        refined_operation.background.rgba = &source;
        refined_operation.colors = {spatial_refinement};
        const auto refined_texture = gpu.processImage(refined_operation);
        require(refined_texture.status == OpenGlCompositionStatus::Complete &&
                    refined_texture.frame,
                "OpenGL must process spatial Curves refinement on the GPU: " +
                    refined_texture.cause);
        const auto refined_readback = gpu.readback(refined_texture.frame);
        require(bool(refined_readback.frame),
                "OpenGL qualifier refinement readback failed: " + refined_readback.cause);
        require(expected.width == refined_readback.frame->width &&
                    expected.height == refined_readback.frame->height,
                "OpenGL qualifier refinement dimensions changed.");
        for (int y = 0; y < expected.height; ++y) {
            for (int x = 0; x < expected.width * 4; ++x) {
                const auto expected_value = expected.rgba_pixels[
                    static_cast<std::size_t>(y) * expected.stride + x];
                const auto actual_value = refined_readback.frame->rgba_pixels[
                    static_cast<std::size_t>(y) * refined_readback.frame->stride + x];
                const int difference = std::abs(int(expected_value) - int(actual_value));
                if (difference > (x % 4 == 3 ? 0 : 1)) {
                    throw std::runtime_error("OpenGL qualifier refinement parity at case " +
                        std::to_string(index) + " pixel " +
                        std::to_string(x / 4) + "," + std::to_string(y) +
                        " channel " + std::to_string(x % 4) + " CPU=" +
                        std::to_string(expected_value) + " GPU=" +
                        std::to_string(actual_value));
                }
            }
        }
    }

    auto spatial_refinement = curve_adjustment;
    spatial_refinement.video_curves->qualifier.denoise_percent = 65.0;
    spatial_refinement.video_curves->qualifier.clean_black_percent = 8.0;
    spatial_refinement.video_curves->qualifier.clean_white_percent = 89.0;
    spatial_refinement.video_curves->qualifier.in_out_percent = 23.0;
    spatial_refinement.video_curves->qualifier.blur_radius_pixels = 2.0;
    auto expected_refined_source = source;
    require(effects::applyColorAdjustment(expected_refined_source, spatial_refinement) ==
                effects::ProcessingResult::Completed,
            "CPU composed qualifier refinement reference failed.");
    CompositionLayer refined_layer{&source};
    refined_layer.gpu_color_adjustments = {spatial_refinement};
    const auto refined_output = gpu.compose(source.width, source.height, {refined_layer});
    const auto expected_refined_output = FrameCompositor::compose(
        source.width, source.height, {{&expected_refined_source}});
    require(refined_output.status == OpenGlCompositionStatus::Complete &&
                refined_output.frame && expected_refined_output,
            "OpenGL composition must preprocess a refined Curves node on the GPU: " +
                refined_output.cause);
    for (std::size_t i = 0; i < expected_refined_output->rgba_pixels.size(); ++i) {
        const auto tolerance = i % 4 == 3 ? 0 : 1;
        require(std::abs(int(expected_refined_output->rgba_pixels[i]) -
                         int(refined_output.frame->rgba_pixels[i])) <= tolerance,
                "OpenGL composed qualifier refinement differs from the CPU frame.");
    }

    OpenGlImageOperation cancelled_refinement;
    cancelled_refinement.kind = OpenGlImageOperationKind::Color;
    cancelled_refinement.background.rgba = &source;
    cancelled_refinement.colors = {spatial_refinement};
    int refinement_cancel_checks = 0;
    const auto cancelled_refinement_result = gpu.processImage(cancelled_refinement,
        [&] { return ++refinement_cancel_checks >= 3; });
    require(cancelled_refinement_result.status == OpenGlCompositionStatus::Cancelled &&
                !cancelled_refinement_result.frame,
            "OpenGL matte passes must honor cancellation without publishing a partial frame.");

    int cancellation_checks = 0;
    const auto cancelled = gpu.compose(source.width, source.height, {layer},
        [&] { return ++cancellation_checks >= 5; });
    require(cancelled.status == OpenGlCompositionStatus::Cancelled && !cancelled.frame,
        "cancelled GPU color adjustment does not publish a partial frame");
}

void matteBudgetFallback(QOffscreenSurface* surface) {
    auto source = fixture(4, 4, true);
    auto curves = effects::VideoCurvesParameters{};
    curves.qualifier.enabled = true;
    curves.qualifier.denoise_percent = 30.0;
    effects::ColorAdjustmentParameters adjustment;
    adjustment.video_curves = curves;
    OpenGlImageOperation operation;
    operation.kind = OpenGlImageOperationKind::Color;
    operation.background.rgba = &source;
    operation.colors = {adjustment};
    auto budget = std::make_shared<OpenGlTexturePoolBudget>(128, 1);
    OpenGlFrameCompositor constrained(surface, OpenGlPrecisionPolicy::Automatic,
        nullptr, budget);
    const auto refused = constrained.processImage(operation);
    require(refused.status == OpenGlCompositionStatus::Unsupported &&
                refused.operation == "color-matte-budget" && !refused.frame,
            "A two-buffer R8 matte allocation beyond the GPU budget must be refused cleanly.");
    auto recovered = source;
    require(effects::applyColorAdjustment(recovered, adjustment) ==
                effects::ProcessingResult::Completed,
            "The same matte adjustment must remain available through CPU recovery.");
}

void primaryGradeStackParity(OpenGlFrameCompositor& gpu) {
    auto source = fixture(93, 57, false);
    auto brightness = effects::makeDefaultInstance("video.brightness");
    auto contrast = effects::makeDefaultInstance("video.contrast");
    auto grade = effects::makeDefaultInstance("video.primary_grade");
    auto curve_node = effects::makeDefaultInstance("video.curves");
    curve_node.video_curves->rgb.points = {{0, 0}, {92, 124}, {255, 255}};
    curve_node.video_curves->targeted[0] = {{0, 129}, {255, 127}};
    curve_node.video_curves->targeted[1] = {{0, 128}, {255, 129}};
    curve_node.video_curves->targeted[2] = {{0, 128}, {255, 127}};
    curve_node.video_curves->targeted[3] = {{0, 129}, {255, 128}};
    curve_node.video_curves->targeted[4] = {{0, 128}, {255, 129}};
    curve_node.video_curves->qualifier.enabled = true;
    curve_node.video_curves->qualifier.hue_min = 338.0;
    curve_node.video_curves->qualifier.hue_max = 24.0;
    curve_node.video_curves->qualifier.hue_softness = 8.0;
    curve_node.video_curves->qualifier.saturation_min = 12.0;
    curve_node.video_curves->qualifier.saturation_max = 95.0;
    curve_node.video_curves->qualifier.saturation_softness = 5.0;
    curve_node.video_curves->qualifier.luminance_min = 8.0;
    curve_node.video_curves->qualifier.luminance_max = 94.0;
    curve_node.video_curves->qualifier.luminance_softness = 4.0;
    const auto neutral_targeted = effects::VideoCurvesParameters{}.targeted;
    const std::array<effects::CurvesPointList, 5> targeted_stress{{
        {{0, 143}, {255, 114}}, {{0, 128}, {255, 203}},
        {{0, 128}, {255, 76}}, {{0, 151}, {255, 128}},
        {{0, 128}, {255, 171}}}};
    for (std::size_t targeted_index = 0; targeted_index < curve_node.video_curves->targeted.size();
         ++targeted_index) {
        auto isolated_curves_node = curve_node;
        isolated_curves_node.video_curves->rgb = effects::CurvesParameters{};
        isolated_curves_node.video_curves->targeted = neutral_targeted;
        isolated_curves_node.video_curves->targeted[targeted_index] =
            targeted_stress[targeted_index];
        isolated_curves_node.video_curves->qualifier.enabled = false;
        const std::array<effects::EffectInstance, 1> isolated_stack{isolated_curves_node};
        CompositionLayer isolated_layer{&source};
        isolated_layer.effect_stack = isolated_stack;
        const auto isolated_expected = FrameCompositor::compose(93, 57, {isolated_layer});
        const auto isolated_actual = gpu.compose(93, 57, {isolated_layer});
        require(isolated_expected && isolated_actual.frame,
            "An isolated targeted Curves GPU pass failed.");
        for (std::size_t i = 0; i < isolated_expected->rgba_pixels.size(); ++i) {
            const int cpu_value = isolated_expected->rgba_pixels[i];
            const int gpu_value = isolated_actual.frame->rgba_pixels[i];
            if (std::abs(cpu_value - gpu_value) > (i % 4 == 3 ? 0 : 1))
            {
                const auto pixel = i / 4;
                const auto* original = source.rgba_pixels.data() +
                    (pixel / static_cast<std::size_t>(source.width)) * source.stride +
                    (pixel % static_cast<std::size_t>(source.width)) * 4;
                throw std::runtime_error("Targeted Curves family " +
                    std::to_string(targeted_index) + " mismatch at pixel " +
                    std::to_string((i / 4) % static_cast<std::size_t>(source.width)) + "," +
                    std::to_string((i / 4) / static_cast<std::size_t>(source.width)) +
                    " channel " + std::to_string(i % 4) + " CPU=" +
                    std::to_string(cpu_value) + " GPU=" + std::to_string(gpu_value) +
                    " source=" + std::to_string(original[0]) + "," +
                    std::to_string(original[1]) + "," + std::to_string(original[2]));
            }
        }
    }
    curve_node.video_curves->qualifier.enabled = true;
    const std::array<effects::EffectInstance, 1> curves_only_stack{curve_node};
    const auto curves_only_passes = effects::colorAdjustmentPasses(curves_only_stack);
    require(curves_only_passes && curves_only_passes->size() == 1 &&
                curves_only_passes->front().video_curves,
            "The Curves graph node must create one typed GPU color pass.");
    CompositionLayer direct_curves_only{&source};
    direct_curves_only.gpu_color_adjustments = *curves_only_passes;
    CompositionLayer curves_only{&source};
    curves_only.effect_stack = curves_only_stack;
    const auto curves_only_expected = FrameCompositor::compose(93, 57, {curves_only});
    const auto direct_curves_result = gpu.compose(93, 57, {direct_curves_only});
    require(curves_only_expected && direct_curves_result.frame,
            "Direct Curves-only GPU pass failed.");
    for (std::size_t i = 0; i < curves_only_expected->rgba_pixels.size(); ++i) {
        const int cpu_value = curves_only_expected->rgba_pixels[i];
        const int gpu_value = direct_curves_result.frame->rgba_pixels[i];
        const int delta = std::abs(cpu_value - gpu_value);
        if (delta > (i % 4 == 3 ? 0 : 1)) {
            const auto pixel = i / 4;
            const auto* source_pixel = source.rgba_pixels.data() +
                (pixel / static_cast<std::size_t>(source.width)) * source.stride +
                (pixel % static_cast<std::size_t>(source.width)) * 4;
            throw std::runtime_error("Direct Curves-only mismatch at pixel " +
                std::to_string(pixel % source.width) + "," +
                std::to_string(pixel / source.width) + " channel " +
                std::to_string(i % 4) + " CPU=" + std::to_string(cpu_value) +
                " GPU=" + std::to_string(gpu_value) + " source=" +
                std::to_string(source_pixel[0]) + "," +
                std::to_string(source_pixel[1]) + "," +
                std::to_string(source_pixel[2]));
        }
    }
    compare(gpu, 93, 57, {curves_only}, "Curves-only clip node parity");
    require(effects::setParameterValue(brightness, "amount", 19) &&
        effects::setParameterValue(contrast, "amount", 132) &&
        effects::setParameterValue(grade, "red_gain", 145) &&
        effects::setParameterValue(grade, "green_gain", 82) &&
        effects::setParameterValue(grade, "blue_gain", 112) &&
        effects::setParameterValue(grade, "log_shadows_red", 26) &&
        effects::setParameterValue(grade, "log_midtones_green", -17) &&
        effects::setParameterValue(grade, "log_highlights_blue", 31), "grade stack parameters");
    std::vector<effects::EffectInstance> stack{brightness, grade, curve_node, contrast};
    CompositionLayer clip{&source}; clip.effect_stack = stack;
    compare(gpu, 93, 57, {clip}, "ordered Primary Grade clip effects and alpha");
    auto reference = source;
    require(effects::applyStack(reference, stack), "CPU grade stack reference");
    const auto prepared = FrameCompositor::compose(93, 57, {{&reference}});
    const auto fallback = FrameCompositor::compose(93, 57, {clip});
    require(prepared && fallback && prepared->rgba_pixels == fallback->rgba_pixels,
        "CPU recovery must apply the original grade stack once");
    effects::ColorAdjustmentParameters adjustment{15, 125, 85, 145, 82, 115};
    adjustment.log_shadows_red = 26;
    adjustment.log_midtones_green = -17;
    adjustment.log_highlights_blue = 31;
    auto next_adjustment = adjustment;
    next_adjustment.log_shadows_blue = -34;
    next_adjustment.log_midtones_red = 19;
    next_adjustment.log_highlights_green = 28;
    effects::VideoCurvesParameters curves;
    curves.rgb.points = {{0, 0}, {104, 137}, {255, 255}};
    curves.targeted[0] = {{0, 137}, {255, 119}};
    curves.targeted[1] = {{0, 128}, {255, 211}};
    curves.targeted[2] = {{0, 128}, {255, 82}};
    curves.targeted[3] = {{0, 145}, {255, 128}};
    curves.targeted[4] = {{0, 128}, {255, 165}};
    curves.qualifier.enabled = true;
    curves.qualifier.hue_min = 340.0;
    curves.qualifier.hue_max = 20.0;
    curves.qualifier.hue_softness = 6.0;
    curves.qualifier.saturation_min = 8.0;
    curves.qualifier.saturation_max = 98.0;
    curves.qualifier.luminance_min = 5.0;
    curves.qualifier.luminance_max = 97.0;
    auto opaque = fixture(93, 57, false);
    auto adjusted = opaque;
    require(effects::applyColorAdjustment(adjusted, adjustment) ==
        effects::ProcessingResult::Completed &&
        effects::applyColorAdjustment(adjusted, next_adjustment) ==
        effects::ProcessingResult::Completed, "CPU adjustment reference failed");
    OpenGlImageOperation operation;
    operation.kind = OpenGlImageOperationKind::Adjustment;
    operation.background.rgba = &opaque;
    operation.foreground.rgba = &opaque;
    effects::ColorAdjustmentParameters curve_adjustment;
    curve_adjustment.video_curves = curves;
    require(effects::applyColorAdjustment(adjusted, curve_adjustment) ==
        effects::ProcessingResult::Completed, "CPU Curves adjustment reference failed");
    operation.colors = {adjustment, next_adjustment, curve_adjustment};
    const auto gpu_adjustment = gpu.processImage(operation);
    require(gpu_adjustment.frame && gpu_adjustment.status == OpenGlCompositionStatus::Complete,
        "OpenGL Primary Grade adjustment operation failed: " + gpu_adjustment.cause);
    const auto readback = gpu.readback(gpu_adjustment.frame);
    require(bool(readback.frame), "OpenGL Primary Grade adjustment readback failed: " + readback.cause);
    for (std::size_t i = 0; i < adjusted.rgba_pixels.size(); ++i)
        require(std::abs(int(adjusted.rgba_pixels[i]) - int(readback.frame->rgba_pixels[i])) <=
            (i % 4 == 3 ? 0 : 2), "OpenGL Primary Grade adjustment parity failed");
}

void referenceBlur(media::RgbaFrame& frame, double sigma) {
    const int radius = static_cast<int>(std::lround(sigma));
    if (radius <= 0) return;
    for (int y = 0; y < frame.height; ++y) {
        auto* row = frame.rgba_pixels.data() + static_cast<std::size_t>(y) * frame.stride;
        for (int x = 0; x < frame.width; ++x) {
            auto* pixel = row + static_cast<std::size_t>(x) * 4;
            for (int channel = 0; channel < 3; ++channel)
                pixel[channel] = static_cast<std::uint8_t>(
                    (static_cast<unsigned>(pixel[channel]) * pixel[3] + 127U) / 255U);
        }
    }
    auto scratch = frame;
    const int window = radius * 2 + 1;
    for (int pass = 0; pass < 3; ++pass) {
        for (const bool horizontal : {true, false}) {
            for (int y = 0; y < frame.height; ++y) {
                for (int x = 0; x < frame.width; ++x) {
                    auto* output = (horizontal ? scratch : frame).rgba_pixels.data() +
                        static_cast<std::size_t>(y) * frame.stride + x * 4;
                    std::array<unsigned, 4> sum{};
                    for (int offset = -radius; offset <= radius; ++offset) {
                        const int sx = horizontal ? std::clamp(x + offset, 0, frame.width - 1) : x;
                        const int sy = horizontal ? y : std::clamp(y + offset, 0, frame.height - 1);
                        const auto* input = (horizontal ? frame : scratch).rgba_pixels.data() +
                            static_cast<std::size_t>(sy) * frame.stride + sx * 4;
                        for (int channel = 0; channel < 4; ++channel) sum[channel] += input[channel];
                    }
                    for (int channel = 0; channel < 4; ++channel)
                        output[channel] = static_cast<std::uint8_t>((sum[channel] + window / 2) / window);
                }
            }
        }
    }
    for (int y = 0; y < frame.height; ++y) {
        auto* row = frame.rgba_pixels.data() + static_cast<std::size_t>(y) * frame.stride;
        for (int x = 0; x < frame.width; ++x) {
            auto* pixel = row + static_cast<std::size_t>(x) * 4;
            if (pixel[3] == 0) pixel[0] = pixel[1] = pixel[2] = 0;
            else for (int channel = 0; channel < 3; ++channel)
                pixel[channel] = static_cast<std::uint8_t>(std::min(255U,
                    (static_cast<unsigned>(pixel[channel]) * 255U + pixel[3] / 2U) / pixel[3]));
        }
    }
}

void orderedEffectParity(OpenGlFrameCompositor& gpu) {
    auto source = fixture(23, 13, true, 7);
    const std::vector<std::vector<GpuCompositionEffect>> stacks{
        {effects::ColorAdjustmentParameters{18.0, 132.0, 74.0},
         GpuGaussianBlurParameters{1.0},
         effects::ColorAdjustmentParameters{-7.0, 83.0, 145.0}},
        {GpuGaussianBlurParameters{2.5},
         effects::ColorAdjustmentParameters{2.0, 105.0, 91.0},
         GpuGaussianBlurParameters{4.0}},
        {GpuGaussianBlurParameters{0.0}}};
    for (std::size_t stack_index = 0; stack_index < stacks.size(); ++stack_index) {
        auto expected = source;
        for (const auto& effect : stacks[stack_index]) {
            if (const auto* adjustment = std::get_if<effects::ColorAdjustmentParameters>(&effect)) {
                require(effects::applyColorAdjustment(expected, *adjustment) ==
                            effects::ProcessingResult::Completed,
                        "CPU reference ordered adjustment failed");
            } else {
                referenceBlur(expected, std::get<GpuGaussianBlurParameters>(effect).radius_pixels);
            }
        }
        const auto cpu_composition = FrameCompositor::compose(
            source.width, source.height, {{&expected}});
        require(cpu_composition.has_value(),
            "CPU reference ordered composition failed");
        CompositionLayer layer{&source};
        layer.gpu_effects = stacks[stack_index];
        OpenGlCompositionTimings timings;
        const auto output = gpu.compose(source.width, source.height, {layer}, {}, &timings);
        require(output.status == OpenGlCompositionStatus::Complete && output.frame,
            "Ordered GPU effects failed: " + output.operation + ": " + output.cause);
        for (int y = 0; y < expected.height; ++y) {
            for (int x = 0; x < expected.width; ++x) {
                for (int channel = 0; channel < 4; ++channel) {
                    const auto cpu_value = cpu_composition->rgba_pixels[
                        static_cast<std::size_t>(y) * cpu_composition->stride + x * 4 + channel];
                    const auto gpu_value = output.frame->rgba_pixels[
                        static_cast<std::size_t>(y) * output.frame->stride + x * 4 + channel];
                    const int delta = std::abs(static_cast<int>(cpu_value) -
                                               static_cast<int>(gpu_value));
                    require(delta <= (channel == 3 ? 0 : 1),
                        "Ordered GPU effects mismatch at pixel " + std::to_string(x) + "," +
                        std::to_string(y) + " channel " + std::to_string(channel) +
                        " CPU=" + std::to_string(cpu_value) + " GPU=" +
                        std::to_string(gpu_value));
                }
            }
        if (stack_index == 0)
            require(timings.gaussian_blur_count == 1 && timings.color_adjustment_count == 2 &&
                    timings.gaussian_blur_submission_nanoseconds > 0,
                "Ordered effect counters and submission timings are recorded");
        if (stack_index == 2)
            require(timings.gaussian_blur_count == 0,
                "A zero-radius blur skips GPU work");
        }
    }

    CompositionLayer layer{&source};
    layer.gpu_effects = {GpuGaussianBlurParameters{3.0}};
    int checks = 0;
    const auto cancelled = gpu.compose(source.width, source.height, {layer},
        [&] { return ++checks >= 5; });
    require(cancelled.status == OpenGlCompositionStatus::Cancelled && !cancelled.frame,
        "Cancellation during blur discards the partial frame");

    media::RgbaFrame over_budget{4096, 4097, 4096 * 4,
        std::vector<std::uint8_t>(static_cast<std::size_t>(4096) * 4097 * 4, 0)};
    CompositionLayer oversized_effect{&over_budget};
    oversized_effect.gpu_effects = {GpuGaussianBlurParameters{1.0}};
    const auto over_budget_result = gpu.compose(16, 12, {oversized_effect});
    require(over_budget_result.status == OpenGlCompositionStatus::Unsupported &&
                over_budget_result.operation == "check-effect-memory" &&
                !over_budget_result.frame,
        "Gaussian Blur rejects a scratch target beyond the 64 MiB worker limit");
}
void cancellationAndLimits(OpenGlFrameCompositor& gpu) {
    auto f = fixture(16, 12, true);
    const std::vector<CompositionLayer> layers{{&f}, {&f}};
    // Predicate checkpoints cover preflight, layers, upload, and readback.
    for (int checkpoint = 1; checkpoint <= 9; ++checkpoint) {
        int calls = 0;
        auto output = gpu.compose(32, 24, layers, [&] { return ++calls >= checkpoint; });
        require(output.status == OpenGlCompositionStatus::Cancelled && !output.frame,
            "cancelled request published a partial frame at checkpoint " + std::to_string(checkpoint));
        compare(gpu, 32, 24, layers, "reuse after cancellation");
    }
    for (const auto dimensions : {std::pair{0, 10}, {-1, 10},
             {std::numeric_limits<int>::max(), 10}, {16384, 16384}}) {
        auto output = gpu.compose(dimensions.first, dimensions.second, layers);
        require(output.status == OpenGlCompositionStatus::Unsupported && !output.frame,
            "invalid/oversized canvas was not rejected");
    }
    auto huge = fixture(65537, 1, false);
    require(gpu.compose(16, 12, {{&huge}}).status == OpenGlCompositionStatus::Unsupported,
        "source device limit was not respected");
    compare(gpu, 16, 12, {{&f}}, "valid request after limits");
}
void benchmark(OpenGlFrameCompositor& gpu) {
    auto background = fixture(1920, 1080, false);
    auto overlay = fixture(1280, 720, true);
    animation::Transform2D scaled; scaled.scale = .72; scaled.opacity = .6;
    animation::Transform2D rotated = scaled; rotated.rotation_degrees = 23.;
    const std::vector<CompositionLayer> layers{{&background}, {&overlay, scaled}, {&overlay, rotated}};
    for (int workload = 0; workload < 2; ++workload) {
        const auto inputs = workload == 0 ? layers : std::vector<CompositionLayer>{{&background}};
        for (const auto size : {std::pair{1920, 1080}, {960, 540}, {480, 270}}) {
            (void)FrameCompositor::compose(size.first, size.second, inputs);
            (void)gpu.compose(size.first, size.second, inputs);
            double cpu_ms = 0, gpu_ms = 0;
            OpenGlCompositionTimings totals;
            constexpr int repeats = 5;
            for (int i = 0; i < repeats; ++i) {
                auto start = std::chrono::steady_clock::now();
                auto cpu = FrameCompositor::compose(size.first, size.second, inputs);
                cpu_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                OpenGlCompositionTimings timings;
                start = std::chrono::steady_clock::now();
                auto output = gpu.compose(size.first, size.second, inputs, {}, &timings);
                gpu_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                require(cpu && output.frame, "benchmark composition failed");
                totals.upload_nanoseconds += timings.upload_nanoseconds;
                totals.draw_submission_nanoseconds += timings.draw_submission_nanoseconds;
                totals.readback_nanoseconds += timings.readback_nanoseconds;
                totals.uploaded_bytes = timings.uploaded_bytes;
                totals.readback_bytes = timings.readback_bytes;
            }
            std::cout << "benchmark " << (workload == 0 ? "three_layers " : "opaque_identity ")
                << size.first << 'x' << size.second
                << " CPU_ms=" << cpu_ms / repeats << " GPU_total_ms=" << gpu_ms / repeats
                << " upload_ms=" << totals.upload_nanoseconds / (repeats * 1e6)
                << " submit_ms=" << totals.draw_submission_nanoseconds / (repeats * 1e6)
                << " readback_ms=" << totals.readback_nanoseconds / (repeats * 1e6)
                << " upload_bytes=" << totals.uploaded_bytes << " readback_bytes=" << totals.readback_bytes << '\n';
        }
    }
}

void trackMatteCapacity(QOffscreenSurface* surface) {
    const std::uint64_t bytes=16*12*4;
    auto budget=std::make_shared<OpenGlTexturePoolBudget>(bytes*2);
    {
        OpenGlFrameCompositor limited(surface,OpenGlPrecisionPolicy::Automatic,QOpenGLContext::globalShareContext(),budget);
        auto target=fixture(16,12,true),matte=fixture(16,12,true);
        OpenGlImageOperation op;op.kind=OpenGlImageOperationKind::ApplyTrackMatte;op.background={&target};op.mask={&matte};
        const auto refused=limited.processImage(op);
        require(refused.status==OpenGlCompositionStatus::Unsupported&&refused.operation=="track-matte-upload-budget","Matte inputs must share the bounded pool");
        auto larger=fixture(16,24,true);op={};op.background={&larger};
        require(limited.processImage(op).frame!=nullptr,"Normal image resize after matte budget refusal must free unused uploads before allocating");
    }
    require(budget->bytes()==0&&budget->targets()==0,"Matte upload reservations leaked after worker cleanup");
    budget=std::make_shared<OpenGlTexturePoolBudget>(bytes*5);
    {
        OpenGlFrameCompositor retiring(surface,OpenGlPrecisionPolicy::Automatic,QOpenGLContext::globalShareContext(),budget);
        auto pixels=fixture(16,12,true);OpenGlImageOperation op;op.kind=OpenGlImageOperationKind::ApplyTrackMatte;op.background={&pixels};op.mask={&pixels};
        const auto output=retiring.processImage(op);require(output.frame&&budget->bytes()==bytes*3,"Matte output and two uploads accounted");
        retiring.retireTextureFrames();require(retiring.collectReleasedTextureFrames().status==OpenGlCompositionStatus::Complete&&budget->bytes()==bytes,"Retirement frees matte uploads while retaining displayed output");
        require(retiring.readback(output.frame).frame.has_value(),"Retired matte output remains readable");
    }
    require(budget->bytes()==0,"Retired matte reservation leaked");
}

void textureLeases(QOffscreenSurface* surface) {
    auto retirement_budget=std::make_shared<OpenGlTexturePoolBudget>(16*12*4*3);
    {
        OpenGlFrameCompositor old(surface,OpenGlPrecisionPolicy::Automatic,QOpenGLContext::globalShareContext(),retirement_budget);
        auto source=fixture(16,12,true);CompositionLayer layer{&source};layer.blend_mode=BlendMode::Screen;
        const auto lease=old.composeTexture(16,12,{layer});require(lease.frame&&retirement_budget->bytes()==16*12*4*2,"Blend output/backdrop accounted");
        old.retireTextureFrames();require(old.collectReleasedTextureFrames().status==OpenGlCompositionStatus::Complete&&retirement_budget->bytes()==16*12*4,"Retirement must free the unleased backdrop");
        require(old.readback(lease.frame).frame.has_value(),"Retired blended lease remains readable");
    }
    require(retirement_budget->bytes()==0,"Retired backdrop reservation leaked");
    {
        auto two_frames=std::make_shared<OpenGlTexturePoolBudget>(16*12*4*2);
        OpenGlFrameCompositor stream(surface,OpenGlPrecisionPolicy::Automatic,QOpenGLContext::globalShareContext(),two_frames);
        auto source=fixture(16,12,true);CompositionLayer layer{&source};layer.blend_mode=BlendMode::Screen;
        auto displayed=stream.composeTexture(16,12,{layer});require(displayed.frame!=nullptr,"Single blended output fits the two-frame budget");
        const auto next=stream.composeTexture(16,12,{layer});
        require(next.status==OpenGlCompositionStatus::Unsupported&&next.operation=="blend-output-budget","Backdrop pressure must recover, not stall forever on a displayed lease");
        displayed.frame.reset();layer.blend_mode=BlendMode::Normal;
        require(stream.composeTexture(16,12,{layer}).frame!=nullptr,"Normal recovery after backdrop pressure");
    }
    auto constrained=std::make_shared<OpenGlTexturePoolBudget>(16*12*4);
    {
        OpenGlFrameCompositor limited(surface,OpenGlPrecisionPolicy::Automatic,QOpenGLContext::globalShareContext(),constrained);
        auto pixels=fixture(16,12,true);CompositionLayer layer{&pixels};layer.blend_mode=BlendMode::Overlay;
        const auto rejected=limited.composeTexture(16,12,{layer});
        require(rejected.status==OpenGlCompositionStatus::Unsupported&&rejected.operation=="blend-backdrop-budget"&&!rejected.frame,"Backdrop must share the output reservation budget");
        layer.blend_mode=BlendMode::Normal;const auto recovered=limited.composeTexture(16,12,{layer});
        require(recovered.frame&&limited.readback(recovered.frame).frame,"Budget refusal must preserve Normal recovery");
    }
    require(constrained->bytes()==0&&constrained->targets()==0,"Backdrop/output reservations must retire");

    auto budget = std::make_shared<OpenGlTexturePoolBudget>();
    auto backend = std::make_unique<OpenGlFrameCompositor>(surface,
        OpenGlPrecisionPolicy::Automatic, QOpenGLContext::globalShareContext(), budget);
    std::vector<OpenGlTextureFramePtr> leased;
    for (int i = 0; i < 3; ++i) {
        auto result = backend->composeTexture(16 + i, 12, {});
        require(result.frame && result.status == OpenGlCompositionStatus::Complete, "Pool allocation failed.");
        require(backend->readback(result.frame).frame.has_value(), "Lease readback failed.");
        leased.push_back(std::move(result.frame));
    }
    require(backend->texturePoolOccupancy() == 3 && budget->targets() == 3 &&
        budget->bytes() == (16 + 17 + 18) * 12 * 4, "Pool accounting is incorrect.");
    require(backend->composeTexture(19, 12, {}).status == OpenGlCompositionStatus::Busy,
        "Leased output was overwritten instead of returning Busy.");
    const auto original_texture = leased[0]->texture();
    QOpenGLContext consumer;
    consumer.setFormat(surface->format());
    consumer.setShareContext(QOpenGLContext::globalShareContext());
    require(consumer.create() && consumer.makeCurrent(surface), "Shared consumer unavailable.");
    std::string cause; std::int64_t code = 0;
    require(!leased[0]->beginUse(nullptr, cause, code), "A texture accepted a missing consumer context.");
    require(leased[0]->beginUse(&consumer, cause, code) && leased[0]->endUse(&consumer, cause, code),
        "Cross-context fences failed: " + cause);
    consumer.doneCurrent();
    // Final reference return may happen on a thread with no OpenGL context.
    std::thread release([frame = std::move(leased[0])]() mutable { frame.reset(); });
    release.join();
    OpenGlTextureCompositionResult replacement;
    for (int attempt = 0; attempt < 100 && !replacement.frame; ++attempt) {
        replacement = backend->composeTexture(16, 12, {});
        if (!replacement.frame) QThread::msleep(1);
    }
    require(replacement.frame && replacement.frame->texture() == original_texture,
        "Returned and consumed buffer was not reused.");
    require(backend->readback(replacement.frame, [] { return true; }).status == OpenGlCompositionStatus::Cancelled,
        "Cancelled recovery published pixels.");
    OpenGlFrameCompositor retired(surface, OpenGlPrecisionPolicy::Automatic,
        QOpenGLContext::globalShareContext(), budget);
    require(retired.composeTexture(16, 12, {}).status == OpenGlCompositionStatus::Busy,
        "Retired and active pools exceeded the shared target budget.");
    backend.reset();
    require(!replacement.frame->valid() && !leased[1]->valid() && budget->bytes() == 0 && budget->targets() == 0,
        "Worker shutdown did not revoke leases and release reservations.");
    replacement.frame.reset(); leased.clear();
    auto large = retired.composeTexture(4096, 2048, {});
    require(large.frame && budget->bytes() == 32ULL * 1024 * 1024, "Large pool target accounting failed.");
    auto second = retired.composeTexture(4096, 2048, {});
    require(second.frame && budget->bytes() == OpenGlTexturePoolBudget::maximum_bytes,
        "64 MiB pool budget was not accounted.");
    require(retired.composeTexture(16, 12, {}).status == OpenGlCompositionStatus::Busy,
        "Shared pool exceeded 64 MiB.");
    require(retired.composeTexture(4096, 4097, {}).status == OpenGlCompositionStatus::Unsupported,
        "A target larger than the pool budget was accepted.");
    OpenGlFrameCompositor isolated(surface);
    require(isolated.composeTexture(16, 12, {}).status == OpenGlCompositionStatus::Unsupported,
        "An unshared worker published a texture.");
    require(isolated.readback(large.frame).status == OpenGlCompositionStatus::Unsupported,
        "A foreign session read a leased texture.");
    for (int checkpoint = 1; checkpoint <= 5; ++checkpoint) {
        int calls = 0;
        auto cancelled = isolated.composeTexture(16, 12, {}, [&] { return ++calls >= checkpoint; });
        require(!cancelled.frame, "An unavailable/cancelled texture was published.");
    }
}

void directCancellation(OpenGlFrameCompositor& gpu) {
    auto source = fixture(16, 12, true, 3);
    std::vector<CompositionLayer> layers{{&source}, {&source}};
    layers.back().blend_mode=BlendMode::Overlay;
    int checkpoints = 0;
    auto probe = gpu.composeTexture(32, 24, layers, [&] { ++checkpoints; return false; });
    require(probe.frame && gpu.readback(probe.frame).frame, "Direct cancellation fixture unavailable.");
    for (int checkpoint = 1; checkpoint <= 4; ++checkpoint) {
        int calls = 0;
        auto read = gpu.readback(probe.frame, [&] { return ++calls >= checkpoint; });
        require(read.status == OpenGlCompositionStatus::Cancelled && !read.frame,
            "Cancelled texture recovery published pixels.");
    }
    probe.frame.reset();
    for (int checkpoint = 1; checkpoint <= checkpoints; ++checkpoint) {
        int calls = 0;
        auto output = gpu.composeTexture(32, 24, layers, [&] { return ++calls >= checkpoint; });
        require(output.status == OpenGlCompositionStatus::Cancelled && !output.frame,
            "Cancelled direct composition published a lease.");
        require(gpu.compose(32, 24, layers).frame.has_value(), "Cancelled direct output damaged RGBA fallback.");
    }
}

void retirementCapacity(QOffscreenSurface* surface) {
    auto budget = std::make_shared<OpenGlTexturePoolBudget>();
    OpenGlFrameCompositor old(surface, OpenGlPrecisionPolicy::Automatic,
        QOpenGLContext::globalShareContext(), budget);
    std::vector<OpenGlTextureFramePtr> frames;
    for (int i = 0; i < 3; ++i) {
        auto result = old.composeTexture(16, 12, {});
        require(result.frame && old.readback(result.frame).frame, "Retirement fixture failed.");
        frames.push_back(std::move(result.frame));
    }
    frames[1].reset(); frames[2].reset();
    old.retireTextureFrames();
    require(old.composeTexture(16, 12, {}).status == OpenGlCompositionStatus::Unsupported,
        "A retired session continued publishing textures.");
    require(old.collectReleasedTextureFrames().status == OpenGlCompositionStatus::Complete &&
        budget->targets() == 1 && frames[0]->valid(), "Unused retired targets retained the pool capacity.");
    require(old.readback(frames[0]).frame.has_value(), "Retirement prevented recovery of a displayed lease.");
    OpenGlFrameCompositor fresh(surface, OpenGlPrecisionPolicy::Automatic,
        QOpenGLContext::globalShareContext(), budget);
    auto next = fresh.composeTexture(16, 12, {});
    require(next.frame && budget->targets() == 2 && frames[0]->valid(),
        "A held previous preview starved direct delivery after reactivation.");
}

void highResolution(OpenGlFrameCompositor& gpu) {
    auto opaque = fixture(29, 17, false, 3);
    auto alpha = fixture(19, 13, true, 12);
    animation::Transform2D transform; transform.position_x = .42; transform.scale = .71; transform.opacity = .6;
    for (const auto size : {std::pair{1920, 1080}, {2560, 1440}, {3840, 2160}, {2160, 3840}})
        compare(gpu, size.first, size.second, {{&opaque}, {&alpha, transform}}, "high resolution split lookup");
    const auto usage = gpu.resourceUsage();
    require(usage.geometry_buffer_bytes == 36864 && usage.texture_bytes > 0 &&
        usage.peak_known_bytes >= usage.texture_bytes + usage.geometry_buffer_bytes,
        "GPU resource accounting omitted split lookup buffers: geometry=" +
            std::to_string(usage.geometry_buffer_bytes) + " texture=" +
            std::to_string(usage.texture_bytes) + " peak=" +
            std::to_string(usage.peak_known_bytes));
    require(gpu.compose(4097, 1, {{&opaque}}).status == OpenGlCompositionStatus::Unsupported &&
        gpu.compose(1, 4097, {{&opaque}}).status == OpenGlCompositionStatus::Unsupported,
        "Axis lookup limit was not enforced independently.");
}
} // namespace

int main(int argc, char** argv) {
    QSurfaceFormat format; format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(3, 2); format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(format);
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QGuiApplication app(argc, argv);
    try {
        OpenGlFrameCompositor unavailable(nullptr);
        require(unavailable.compose(16, 12, {}).status == OpenGlCompositionStatus::Failed,
            "missing context was not reported");
        require(unavailable.compose(16, 12, {}, [] { return true; }).status ==
            OpenGlCompositionStatus::Cancelled, "preflight cancellation attempted context creation");
        auto surface = OpenGlFrameCompositor::createSurface();
        if (!surface || !QOpenGLContext::supportsThreadedOpenGL()) {
            std::cout << "SKIP: native threaded OpenGL context unavailable; driver acceptance pending.\n";
            return 77;
        }
        QOpenGLContext info;
        info.setFormat(surface->format());
        if (info.create() && info.makeCurrent(surface.get())) {
            auto* gl = info.functions();
            std::cout << "vendor=" << gl->glGetString(GL_VENDOR)
                << " renderer=" << gl->glGetString(GL_RENDERER)
                << " version=" << gl->glGetString(GL_VERSION) << '\n';
            info.doneCurrent();
        }
        std::exception_ptr failure;
        bool context_unavailable = false;
        const bool measure = argc > 1 && std::string(argv[1]) == "--benchmark";
        const bool color_only = argc > 1 && std::string(argv[1]) == "--color-only";
        const bool glow_only = argc > 1 && std::string(argv[1]) == "--glow-only";
        // GUI-created surface; worker creates/releases everything else. Repeat
        // activation and teardown to exercise resource lifetime with native Qt.
        for (int activation = 0; activation < (color_only || glow_only ? 1 : 2); ++activation) {
            std::unique_ptr<QThread> worker(QThread::create([&] {
                try {
                    OpenGlFrameCompositor gpu(surface.get(), OpenGlPrecisionPolicy::Automatic,
                        QOpenGLContext::globalShareContext());
                    const auto initial = gpu.compose(16, 12, {});
                    if (initial.status == OpenGlCompositionStatus::Failed &&
                        (initial.operation == "create-context" || initial.operation == "check-context")) {
                        context_unavailable = true; return;
                    }
                    require(initial.status == OpenGlCompositionStatus::Complete,
                        "native initialization failed: " + initial.operation + ": " + initial.cause);
                    if (color_only) {
                        colorAdjustmentParity(gpu);
                        primaryGradeStackParity(gpu);
                        return;
                    }
                    if (glow_only) {
                        glowParity(gpu);
                        OpenGlFrameCompositor baseline(surface.get(), OpenGlPrecisionPolicy::CoreOnly,
                            QOpenGLContext::globalShareContext());
                        glowParity(baseline, true);
                        return;
                    }
                    parity(gpu);
                    primaryGradeStackParity(gpu);
                    colorAdjustmentParity(gpu);
                    matteBudgetFallback(surface.get());
                    trackMatteParity(gpu);
                    glowParity(gpu);
                    {
                        auto source = fixture(93, 57, true);
                        auto brightness = effects::makeDefaultInstance("video.brightness");
                        auto contrast = effects::makeDefaultInstance("video.contrast");
                        auto saturation = effects::makeDefaultInstance("video.saturation");
                        auto gray = effects::makeDefaultInstance("video.grayscale");
                        auto grade = effects::makeDefaultInstance("video.primary_grade");
                        require(effects::setParameterValue(brightness, "amount", 19) &&
                            effects::setParameterValue(contrast, "amount", 132) &&
                            effects::setParameterValue(saturation, "amount", 143) &&
                            effects::setParameterValue(gray, "amount", 37) &&
                            effects::setParameterValue(grade, "red_gain", 145) &&
                            effects::setParameterValue(grade, "green_gain", 82) &&
                            effects::setParameterValue(grade, "blue_gain", 112), "clip effect parameters");
                        auto disabled = brightness; disabled.enabled = false;
                        std::vector<effects::EffectInstance> stack{brightness, grade, contrast, disabled, saturation, gray};
                        CompositionLayer clip{&source}; clip.effect_stack = stack;
                        compare(gpu, 93, 57, {clip}, "ordered built-in clip effects and alpha");
                        auto reference = source;
                        require(effects::applyStack(reference, stack), "CPU effect reference");
                        const auto prepared = FrameCompositor::compose(93, 57, {{&reference}});
                        const auto fallback = FrameCompositor::compose(93, 57, {clip});
                        require(prepared && fallback && prepared->rgba_pixels == fallback->rgba_pixels,
                            "CPU recovery must apply the original effect stack once");
                    }
                    orderedEffectParity(gpu);
                    if (activation == 0) highResolution(gpu);
                    cancellationAndLimits(gpu);
                    directCancellation(gpu);
                    trackMatteCapacity(surface.get());
                    textureLeases(surface.get());
                    retirementCapacity(surface.get());
                    asynchronousReadbackParity(gpu);
                    {
        OpenGlFrameCompositor baseline(surface.get(), OpenGlPrecisionPolicy::CoreOnly, QOpenGLContext::globalShareContext());
                        auto f = fixture(13, 7, true, 3);
                        compare(baseline, 53, 39, {{&f}}, "OpenGL 3.2 without precision extensions");
                        trackMatteParity(baseline);
                        glowParity(baseline, true);
                        CompositionLayer blended{&f};blended.blend_mode=BlendMode::Overlay;
                        compare(baseline,53,39,{{&f},blended},"OpenGL 3.2 blend without precision extensions");
                        animation::Transform2D rotated; rotated.rotation_degrees = 90;
                        const auto limited = baseline.compose(53, 39, {{&f, rotated}});
                        require(limited.status == OpenGlCompositionStatus::Unsupported &&
                            limited.operation == "check-precision" && !limited.frame,
                            "Core-only rotation did not request CPU fallback.");
                        CompositionLayer affine{&f};affine.source_to_canvas=AffineTransform2D{};
                        require(baseline.compose(53,39,{affine}).status==OpenGlCompositionStatus::Unsupported,
                            "Core-only affine geometry did not request CPU fallback.");
                        compare(baseline, 53, 39, {{&f}}, "GPU reuse after precision fallback");
                    }
                    if (measure && activation == 0) benchmark(gpu);
                } catch (...) { failure = std::current_exception(); }
            }));
            worker->start(); worker->wait();
            if (failure) std::rethrow_exception(failure);
            if (context_unavailable) return 77;
        }
        surface.reset();
        std::cout << "OpenGL composition parity and worker lifecycle passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
