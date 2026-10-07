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
            return layer.transform.rotation_degrees != 0;
        }), "Unexpected precision fallback.");
        std::cout << "LIMITED: " << name << ": rotated GPU parity pending; " << output.cause << '\n';
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
    auto opaque = fixture(16, 12, false);
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
void colorAdjustmentParity(OpenGlFrameCompositor& gpu) {
    auto source = fixture(23, 13, true, 7);
    auto adjusted = source;
    const std::vector<effects::ColorAdjustmentParameters> adjustments{
        {18.0, 132.0, 74.0}, {-7.0, 83.0, 145.0}, {2.0, 105.0, 91.0}};
    for (const auto& parameters : adjustments) {
        require(effects::applyColorAdjustment(adjusted, parameters) ==
                    effects::ProcessingResult::Completed,
                "CPU reference adjustment failed");
    }
    CompositionLayer layer{&source};
    layer.gpu_color_adjustments = adjustments;
    const auto cpu = FrameCompositor::compose(source.width, source.height, {{&adjusted}});
    OpenGlCompositionTimings timings;
    const auto output = gpu.compose(source.width, source.height, {layer}, {}, &timings);
    require(cpu && output.status == OpenGlCompositionStatus::Complete && output.frame,
        "GPU color adjustment composition failed: " + output.operation + ": " + output.cause);
    for (std::size_t i = 0; i < cpu->rgba_pixels.size(); ++i) {
        const auto delta = std::abs(static_cast<int>(cpu->rgba_pixels[i]) -
                                    static_cast<int>(output.frame->rgba_pixels[i]));
        require(delta <= (i % 4 == 3 ? 0 : 1),
            "GPU color adjustment exceeded the one-channel RGB tolerance or changed alpha");
    }
    require(timings.color_adjustment_count == adjustments.size() &&
                timings.color_adjustment_submission_nanoseconds > 0,
            "GPU color adjustment work is measured");

    int cancellation_checks = 0;
    const auto cancelled = gpu.compose(source.width, source.height, {layer},
        [&] { return ++cancellation_checks >= 5; });
    require(cancelled.status == OpenGlCompositionStatus::Cancelled && !cancelled.frame,
        "cancelled GPU color adjustment does not publish a partial frame");
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
        CompositionLayer layer{&source};
        layer.gpu_effects = stacks[stack_index];
        OpenGlCompositionTimings timings;
        const auto output = gpu.compose(source.width, source.height, {layer}, {}, &timings);
        require(output.status == OpenGlCompositionStatus::Complete && output.frame,
            "Ordered GPU effects failed: " + output.operation + ": " + output.cause);
        for (int y = 0; y < expected.height; ++y) {
            for (int x = 0; x < expected.width; ++x) {
                for (int channel = 0; channel < 4; ++channel) {
                    const auto cpu_value = expected.rgba_pixels[
                        static_cast<std::size_t>(y) * expected.stride + x * 4 + channel];
                    const auto gpu_value = output.frame->rgba_pixels[
                        static_cast<std::size_t>(y) * output.frame->stride + x * 4 + channel];
                    const int delta = std::abs(static_cast<int>(cpu_value) -
                                               static_cast<int>(gpu_value));
                    require(delta <= (channel == 3 ? 0 : 1),
                        "Ordered GPU effects exceeded one RGB level or changed alpha");
                }
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

void textureLeases(QOffscreenSurface* surface) {
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
    require(usage.geometry_buffer_bytes == 32768 && usage.texture_bytes > 0 &&
        usage.peak_known_bytes >= usage.texture_bytes + usage.geometry_buffer_bytes, "GPU resource accounting omitted split lookup buffers.");
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
        // GUI-created surface; worker creates/releases everything else. Repeat
        // activation and teardown to exercise resource lifetime with native Qt.
        for (int activation = 0; activation < 2; ++activation) {
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
    parity(gpu);
    colorAdjustmentParity(gpu);
    orderedEffectParity(gpu);
                    if (activation == 0) highResolution(gpu);
                    cancellationAndLimits(gpu);
                    directCancellation(gpu);
                    textureLeases(surface.get());
                    retirementCapacity(surface.get());
                    {
        OpenGlFrameCompositor baseline(surface.get(), OpenGlPrecisionPolicy::CoreOnly, QOpenGLContext::globalShareContext());
                        auto f = fixture(13, 7, true, 3);
                        compare(baseline, 53, 39, {{&f}}, "OpenGL 3.2 without precision extensions");
                        animation::Transform2D rotated; rotated.rotation_degrees = 90;
                        const auto limited = baseline.compose(53, 39, {{&f, rotated}});
                        require(limited.status == OpenGlCompositionStatus::Unsupported &&
                            limited.operation == "check-precision" && !limited.frame,
                            "Core-only rotation did not request CPU fallback.");
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
