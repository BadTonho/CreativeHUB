#include "rendering/composition_frame_renderer.h"
#include "rendering/preview_renderer.h"
#include "rendering/preview_scene_builder.h"
#include "diagnostics/performance_metrics.h"
#include "settings/gpu_composition_preferences.h"

#include <creative_suite/composition/opengl_frame_compositor.h>
#include <creative_suite/diagnostics/logger.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QTemporaryDir>
#include <QThread>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <utility>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

bool waitFor(const std::function<bool()>& condition)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < 10000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        QThread::msleep(2);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    return condition();
}

creative_suite::media::RgbaFramePtr imageFrame()
{
    auto frame = std::make_shared<creative_suite::media::RgbaFrame>();
    frame->width = 2;
    frame->height = 2;
    frame->stride = 8;
    frame->rgba_pixels = {
        40, 100, 200, 255, 40, 100, 200, 255,
        40, 100, 200, 255, 40, 100, 200, 255};
    return frame;
}

motion::ui::PreviewRequest request()
{
    motion::ui::PreviewLayerSnapshot image;
    image.id = 1;
    image.kind = motion::model::LayerKind::Image;
    image.still_frame = imageFrame();

    motion::ui::PreviewLayerSnapshot shape;
    shape.id = 2;
    shape.kind = motion::model::LayerKind::Shape;
    motion::model::ShapeLayerContent content;
    content.width = 24;
    content.height = 24;
    content.fill_color = {220, 30, 70, 128};
    shape.content = content;

    motion::ui::PreviewLayerSnapshot text;
    text.id = 3;
    text.kind = motion::model::LayerKind::Text;
    motion::model::TextLayerContent text_content;
    text_content.text = "Motion";
    text_content.font_size_pixels = 9;
    text_content.box_width = 28;
    text_content.box_height = 14;
    text_content.color = {240, 220, 40, 180};
    text.content = text_content;

    motion::ui::PreviewRequest result{{32, 32}, {24, 1},
            {std::move(image), std::move(shape), std::move(text)}};
    std::vector<motion::model::CompositionLayer> hierarchy;
    for (auto& layer : result.layers) {
        layer.anchor_point={3.25,-2.5};layer.anchor_keyframes[0]={{0,3.25},{24,8}};
        motion::model::CompositionLayer node{layer.id,layer.kind,"Layer"};
        layer.scale_y=motion::model::ScaleChannel{.6,{{0,.6},{24,.9}}};layer.transform.rotation_degrees=27;
        node.transform=layer.transform;node.keyframes=layer.keyframes;node.scale_y=layer.scale_y;
        node.anchor_point=layer.anchor_point;node.anchor_keyframes=layer.anchor_keyframes;
        node.parent_id=10;hierarchy.push_back(std::move(node));
    }
    motion::model::CompositionLayer parent{10,motion::model::LayerKind::Null,"Control"};
    parent.transform={.1,.05,.8,12,1};parent.visible=false;
    parent.anchor_point={-1,4};parent.scale_y=motion::model::ScaleChannel{1.3,{}};
    hierarchy.push_back(parent);
    result.hierarchy=std::make_shared<const motion::model::LayerTransformHierarchy>(result.canvas_size,hierarchy);
    return result;
}

} // namespace

int main(int argc, char* argv[])
{
    QGuiApplication application(argc, argv);
    qunsetenv("CREATIVE_SUITE_MOTION_GPU_COMPOSITION");
    require(!motion::settings::gpuCompositionEnabled(),
            "GPU composition is disabled by default when the setting is absent");
    require(!motion::settings::resolveGpuCompositionEnabled(nullptr) &&
                !motion::settings::resolveGpuCompositionEnabled("") &&
                !motion::settings::resolveGpuCompositionEnabled("0") &&
                !motion::settings::resolveGpuCompositionEnabled("true") &&
                motion::settings::resolveGpuCompositionEnabled("1"),
            "GPU preview requires the exact environment opt-in");

    QTemporaryDir log_directory;
    require(log_directory.isValid(), "a temporary diagnostic directory is available");
    auto& logger = creative_suite::diagnostics::Logger::instance();
    const auto log_path = std::filesystem::path(log_directory.path().toStdString());
    require(logger.initialize(log_path), "GPU fallback diagnostics can be captured");

    auto& metrics = motion::diagnostics::PerformanceMetrics::instance();
    metrics.setEnabled(true);
    metrics.reset();

    QObject fallback_receiver;
    std::uint64_t fallback_generation = 0;
    creative_suite::media::RgbaFramePtr fallback_frame;
    motion::ui::PreviewRenderer fallback_renderer(
        &fallback_receiver,
        [&](std::uint64_t generation, motion::ui::PreviewRequestMode,
            std::uint64_t, creative_suite::media::RgbaFramePtr frame) {
            fallback_generation = generation;
            fallback_frame = std::move(frame);
        }, {}, &metrics, true, nullptr);
    auto fallback_request = request();
    fallback_request.layers.front().effects = {
        motion::model::GaussianBlurEffect{true, 2.5},
        motion::model::ColorAdjustmentEffect{true, 18.0, 132.0, 74.0},
        motion::model::ColorAdjustmentEffect{true, -7.0, 83.0, 145.0}};
    const auto failed_context_generation = fallback_renderer.submit(fallback_request);
    require(waitFor([&] { return fallback_generation == failed_context_generation; }) &&
                fallback_frame != nullptr && fallback_frame->width == 32 &&
                fallback_frame->height == 32,
            "an unavailable OpenGL context still returns the CPU preview frame");
    motion::ui::CompositionFrameRenderer fallback_cpu_renderer(false);
    const auto fallback_cpu_frame = fallback_cpu_renderer.render(fallback_request);
    require(fallback_cpu_frame != nullptr &&
                fallback_frame->rgba_pixels == fallback_cpu_frame->rgba_pixels,
            "failed GPU initialization preserves byte-identical CPU output");
    creative_suite::media::RgbaFramePtr subsequent_fallback_frame;
    const auto subsequent_fallback_generation = fallback_renderer.submit(fallback_request);
    require(waitFor([&] {
                return fallback_generation == subsequent_fallback_generation;
            }) && fallback_frame != nullptr,
            "the worker continues rendering after its OpenGL backend fails");
    motion::ui::CompositionFrameRenderer subsequent_cpu_renderer(false);
    subsequent_fallback_frame = subsequent_cpu_renderer.render(fallback_request);
    require(subsequent_fallback_frame != nullptr &&
                subsequent_fallback_frame->rgba_pixels == fallback_frame->rgba_pixels,
            "subsequent requests remain byte-identical to CPU after disabling the worker GPU path");
    fallback_renderer.stopAndWait();
    auto fallback_snapshot = metrics.takeSnapshotAndReset();
    require(fallback_snapshot.has_value() &&
                fallback_snapshot->gpu_composition_fallbacks == 1 &&
                fallback_snapshot->gpu_composition_failures == 1 &&
                fallback_snapshot->gpu_color_adjustment_fallbacks == 4 &&
                fallback_snapshot->gpu_color_adjustment_failures == 1 &&
                fallback_snapshot->gpu_gaussian_blur_fallbacks == 2 &&
                fallback_snapshot->gpu_gaussian_blur_failures == 1,
            "context failure falls back for ordered effects and later worker requests stay CPU");

    std::ifstream log_file(logger.log_path(), std::ios::binary);
    const std::string log_contents(
        (std::istreambuf_iterator<char>(log_file)), std::istreambuf_iterator<char>());
    require(log_contents.find("gpu_composition_fallback") != std::string::npos &&
                log_contents.find("canvas_width=\"32\"") != std::string::npos,
            "GPU fallback logs the operation and canvas context");
    auto surface = creative_suite::composition::OpenGlFrameCompositor::createSurface();
    if (argc > 1 && std::string(argv[1]) == "--require-gpu")
        require(surface && surface->isValid(), "Native Motion Studio acceptance requires a valid OpenGL surface.");
    if (surface) {
        QObject gpu_receiver;
        std::uint64_t gpu_generation = 0;
        creative_suite::media::RgbaFramePtr gpu_frame;
        motion::ui::PreviewRenderer gpu_renderer(
            &gpu_receiver,
            [&](std::uint64_t generation, motion::ui::PreviewRequestMode,
                std::uint64_t, creative_suite::media::RgbaFramePtr frame) {
                gpu_generation = generation;
                gpu_frame = std::move(frame);
            }, {}, &metrics, true, surface.get());
        const auto composition = request();
        const auto generation = gpu_renderer.submit(composition);
        require(waitFor([&] { return gpu_generation == generation; }) &&
                    gpu_frame != nullptr && gpu_frame->width == 32 && gpu_frame->height == 32,
                "GPU preview produces an RGBA frame or falls back to CPU");

        motion::ui::CompositionFrameRenderer cpu_renderer(false);
        const auto cpu_frame = cpu_renderer.render(composition);
        require(cpu_frame != nullptr &&
                    cpu_frame->rgba_pixels.size() == gpu_frame->rgba_pixels.size(),
                "GPU preview retains the current CPU RGBA output contract");
        for (std::size_t index = 0; index < cpu_frame->rgba_pixels.size(); ++index) {
            const auto cpu_value = static_cast<int>(cpu_frame->rgba_pixels[index]);
            const auto gpu_value = static_cast<int>(gpu_frame->rgba_pixels[index]);
            require(std::abs(cpu_value - gpu_value) <= 2,
                    "layer order and alpha match CPU output within two channel values");
        }
        gpu_renderer.stopAndWait();
        const auto gpu_snapshot = metrics.takeSnapshotAndReset();
        require(gpu_snapshot.has_value() &&
                    gpu_snapshot->gpu_composition_frames +
                        gpu_snapshot->gpu_composition_fallbacks == 1,
                "preview metrics record the successful backend or its fallback");
        std::cout << "gpu_composition_frames=" << gpu_snapshot->gpu_composition_frames
                  << " gpu_composition_fallbacks="
                  << gpu_snapshot->gpu_composition_fallbacks << '\n';

        const bool gpu_backend_available = gpu_snapshot->gpu_composition_frames == 1;
        if (argc > 1 && std::string(argv[1]) == "--require-gpu")
            require(gpu_backend_available, "Native Motion Studio acceptance requires an actual GPU composition frame.");
        motion::ui::PreviewLayerSnapshot color_layer;
        color_layer.id = 10;
        color_layer.kind = motion::model::LayerKind::Image;
        color_layer.still_frame = imageFrame();
        color_layer.effects = {
            motion::model::ColorAdjustmentEffect{true, 18.0, 132.0, 74.0},
            motion::model::ColorAdjustmentEffect{false, 100.0, 0.0, 0.0},
            motion::model::ColorAdjustmentEffect{true, -7.0, 83.0, 145.0},
            motion::model::ColorAdjustmentEffect{true, 2.0, 105.0, 91.0}};
        if (gpu_backend_available) {
            const motion::ui::PreviewRequest color_request{{2, 2}, {24, 1}, {color_layer}};
            metrics.reset();
            QObject color_receiver;
            std::uint64_t color_generation = 0;
            creative_suite::media::RgbaFramePtr color_gpu_frame;
            motion::ui::PreviewRenderer color_renderer(
                &color_receiver,
                [&](std::uint64_t generation, motion::ui::PreviewRequestMode,
                    std::uint64_t, creative_suite::media::RgbaFramePtr frame) {
                    color_generation = generation;
                    color_gpu_frame = std::move(frame);
                }, {}, &metrics, true, surface.get());
            const auto color_expected_generation = color_renderer.submit(color_request);
            require(waitFor([&] { return color_generation == color_expected_generation; }) &&
                        color_gpu_frame != nullptr,
                    "GPU Color Adjustment preview produces a frame");
            motion::ui::CompositionFrameRenderer color_cpu_renderer(false);
            const auto color_cpu_frame = color_cpu_renderer.render(color_request);
            require(color_cpu_frame != nullptr &&
                        color_cpu_frame->rgba_pixels.size() == color_gpu_frame->rgba_pixels.size(),
                    "CPU Color Adjustment reference produces a frame");
            for (std::size_t index = 0; index < color_cpu_frame->rgba_pixels.size(); ++index) {
                const auto delta = std::abs(static_cast<int>(color_cpu_frame->rgba_pixels[index]) -
                                            static_cast<int>(color_gpu_frame->rgba_pixels[index]));
                require(delta <= (index % 4 == 3 ? 0 : 1),
                        "Motion GPU Color Adjustment matches CPU within one RGB level and exact alpha");
            }
            color_renderer.stopAndWait();
            const auto color_snapshot = metrics.takeSnapshotAndReset();
            require(color_snapshot.has_value() &&
                        color_snapshot->gpu_color_adjustment_effects == 3 &&
                        color_snapshot->gpu_color_adjustment_fallbacks == 0 &&
                        color_snapshot->gpu_color_adjustment_failures == 0,
                    "Motion GPU Color Adjustment records enabled effect counts without fallback");
        }

        auto mixed_request = request();
        mixed_request.layers.front().effects = {
            motion::model::ColorAdjustmentEffect{true, 18.0, 132.0, 74.0},
            motion::model::GaussianBlurEffect{true, 1.0},
            motion::model::ColorAdjustmentEffect{false, 100.0, 0.0, 0.0},
            motion::model::GaussianBlurEffect{true, 2.5},
            motion::model::ColorAdjustmentEffect{true, -7.0, 83.0, 145.0}};
        metrics.reset();
        QObject mixed_receiver;
        std::uint64_t mixed_generation = 0;
        creative_suite::media::RgbaFramePtr mixed_frame;
        motion::ui::PreviewRenderer mixed_renderer(
            &mixed_receiver,
            [&](std::uint64_t generation, motion::ui::PreviewRequestMode,
                std::uint64_t, creative_suite::media::RgbaFramePtr frame) {
                mixed_generation = generation;
                mixed_frame = std::move(frame);
            }, {}, &metrics, true, surface.get());
        const auto mixed_expected_generation = mixed_renderer.submit(mixed_request);
        require(waitFor([&] { return mixed_generation == mixed_expected_generation; }) &&
                    mixed_frame != nullptr,
                "mixed ordered Gaussian Blur stack renders through the preview");
        motion::ui::CompositionFrameRenderer mixed_cpu_renderer(false);
        const auto mixed_cpu_frame = mixed_cpu_renderer.render(mixed_request);
        require(mixed_cpu_frame != nullptr && mixed_cpu_frame->rgba_pixels.size() ==
                    mixed_frame->rgba_pixels.size(),
                "mixed effect CPU reference and GPU preview have matching dimensions");
        for (std::size_t index = 0; index < mixed_cpu_frame->rgba_pixels.size(); ++index) {
            const auto delta = std::abs(static_cast<int>(mixed_cpu_frame->rgba_pixels[index]) -
                                        static_cast<int>(mixed_frame->rgba_pixels[index]));
            require(delta <= (index % 4 == 3 ? 0 : 1),
                    "ordered GPU effects match CPU within one RGB level and exact alpha");
        }
        mixed_renderer.stopAndWait();
        const auto mixed_snapshot = metrics.takeSnapshotAndReset();
        require(mixed_snapshot.has_value() &&
                    mixed_snapshot->gpu_color_adjustment_effects ==
                        (gpu_backend_available ? 2U : 0U) &&
                    mixed_snapshot->gpu_color_adjustment_fallbacks ==
                        (gpu_backend_available ? 0U : 2U) &&
                    mixed_snapshot->gpu_gaussian_blur_effects ==
                        (gpu_backend_available ? 2U : 0U) &&
                    mixed_snapshot->gpu_gaussian_blur_fallbacks ==
                        (gpu_backend_available ? 0U : 2U),
                "ordered mixed effects use GPU when available and record CPU fallback otherwise");
        metrics.reset();
        auto masked_request=mixed_request;
        motion::model::Mask mask;mask.id=1;mask.kind=creative_suite::composition::ShapeMaskKind::Ellipse;
        mask.parameters.width=.8;mask.parameters.height=.65;
        masked_request.masks=std::make_shared<const std::vector<motion::model::Mask>>(std::vector{mask});
        masked_request.layers.front().source_mask=1;masked_request.layers.front().transform_mask=1;
        std::get<motion::model::ColorAdjustmentEffect>(masked_request.layers.front().effects.front()).mask=1;
        QObject mask_receiver;creative_suite::media::RgbaFramePtr masked_frame;std::uint64_t mask_generation=0;
        motion::ui::PreviewRenderer masked_renderer(&mask_receiver,
            [&](std::uint64_t generation,motion::ui::PreviewRequestMode,std::uint64_t,creative_suite::media::RgbaFramePtr frame){mask_generation=generation;masked_frame=std::move(frame);},
            {},&metrics,true,surface.get());
        const auto wanted_mask=masked_renderer.submit(masked_request);
        require(waitFor([&]{return mask_generation==wanted_mask;})&&masked_frame,"affine masked GPU preview completes");
        motion::ui::CompositionFrameRenderer masked_cpu(false);const auto masked_reference=masked_cpu.render(masked_request);
        require(masked_reference&&masked_reference->rgba_pixels.size()==masked_frame->rgba_pixels.size(),"affine masked CPU reference");
        for(std::size_t i=0;i<masked_frame->rgba_pixels.size();++i)
            require(std::abs(int(masked_frame->rgba_pixels[i])-int(masked_reference->rgba_pixels[i]))<=(i%4==3?0:2),
                "affine hierarchy with Source/Effect/Transform masks agrees on CPU/GPU");
        masked_renderer.stopAndWait();
        const auto masked_metrics=metrics.takeSnapshotAndReset();
        require(masked_metrics&&(!gpu_backend_available || (masked_metrics->gpu_composition_frames==1&&masked_metrics->gpu_composition_fallbacks==0)),
            "native affine masked preview cannot silently pass through CPU fallback");
        motion::model::CompositionDocument paths(160,90,{30,1});motion::model::LayerId path_id=0;
        motion::model::PathGeometry geometry{{{{-50,-25},{},{20,-10}},{{50,-25},{-20,-10},{}},{{40,25},{},{-15,8}},{{-40,25},{15,8},{}}},true};
        require(paths.createPathLayer(geometry,0,&path_id),"GPU path fixture");
        require(paths.setLayerAnchorPoint(path_id,{10,-5})&&paths.setLayerScaleAtFrame(path_id,1.1,.75,0)&&paths.setLayerRotationAtFrame(path_id,21,0),"GPU path affine geometry");
        require(paths.setLayerEffects(path_id,{motion::model::ColorAdjustmentEffect{true,5,110,90},motion::model::GaussianBlurEffect{true,1.25}}),"GPU path effects");
        motion::model::PathGeometry coverage{{{{.1,.1},{},{.1,-.05}},{{.9,.1},{-.1,-.05},{}},{{.8,.9},{},{}},{{.2,.9},{},{}}},true};
        const auto path_mask=paths.createPathMask(path_id,-2,coverage);
        require(path_mask&&paths.associateMask(path_id,-1,path_mask)&&paths.associateMask(path_id,0,path_mask),"GPU path shared masks");
        auto parameters=paths.findMask(path_mask)->parameters;parameters.soft_edge=.04;require(paths.setMaskParameters(path_mask,parameters),"GPU Bezier feather");
        motion::ui::PreviewRequest path_request;path_request.canvas_size=paths.canvasSize();path_request.frame_rate=paths.frameRate();path_request.hierarchy=std::make_shared<const motion::model::LayerTransformHierarchy>(paths.canvasSize(),paths.layers());path_request.masks=std::make_shared<const std::vector<motion::model::Mask>>(paths.masks());motion::ui::preparePreviewLayers(path_request,paths.layers(),{});path_request.freezeScene();
        auto candidate=geometry;candidate.nodes[0].point.x-=15.5;path_request.path_override=motion::model::PathOverride{{path_id,false},std::make_shared<const motion::model::PathGeometry>(candidate)};
        QObject path_receiver;creative_suite::media::RgbaFramePtr path_frame;std::uint64_t path_generation=0;
        motion::ui::PreviewRenderer path_renderer(&path_receiver,[&](std::uint64_t generation,motion::ui::PreviewRequestMode,std::uint64_t,creative_suite::media::RgbaFramePtr frame){path_generation=generation;path_frame=std::move(frame);},{},&metrics,true,surface.get());
        metrics.reset();path_request.gesture_session=500;const auto wanted_path=path_renderer.submit(path_request,motion::ui::PreviewRequestMode::Gesture);require(waitFor([&]{return path_generation==wanted_path;})&&path_frame,"GPU Bezier override frame completes");
        motion::ui::CompositionFrameRenderer path_cpu(false);const auto path_reference=path_cpu.render(path_request);require(path_reference&&path_reference->rgba_pixels.size()==path_frame->rgba_pixels.size(),"Bezier CPU frame");
        for(std::size_t i=0;i<path_frame->rgba_pixels.size();++i)require(std::abs(int(path_frame->rgba_pixels[i])-int(path_reference->rgba_pixels[i]))<=(i%4==3?0:2),"Bezier GPU/CPU keeps existing mask tolerance");
        path_renderer.stopAndWait();const auto path_metrics=metrics.takeSnapshotAndReset();require(path_metrics&&(!gpu_backend_available||(path_metrics->gpu_composition_frames==1&&path_metrics->gpu_composition_fallbacks==0)),"native Bezier composition cannot be approved by fallback");
        std::cout<<"Bezier GPU composition: native="<<path_metrics->gpu_composition_frames<<" fallback="<<path_metrics->gpu_composition_fallbacks<<'\n';


    }

    return EXIT_SUCCESS;
}
