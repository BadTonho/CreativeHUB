#include "playback/playback_worker.h"
#include <creative_suite/composition/vulkan_frame_compositor.h>
#include "rendering/graph_generators.h"
#include "rendering/adjustment_composition.h"
#include "rendering/offline_export_renderer.h"
#include "rendering/preview_performance_metrics.h"
#include "logging/logger.h"
#include "workspaces/fusion/nodes/evaluation/node_graph_evaluator.h"
#ifdef CREATIVE_SUITE_TEST_IMAGE_EDITOR_MASKS
#include "image_document_session.h"
#endif

#include <QGuiApplication>
#include <QImage>
#include <QOffscreenSurface>
#include <QThread>
#include <QTemporaryDir>
#include <QFileInfo>
#include <QEventLoop>
#include <QTimer>
#include <QOpenGLContext>
#include <QSurfaceFormat>

#include <cmath>
#include <exception>
#include <iostream>
#include <stdexcept>

using namespace creative_suite::composition;
namespace {
void fusionParity(GpuFrameCompositor& gpu, QOffscreenSurface* surface) {
    using namespace fusion::nodes;
    auto source = std::make_shared<media::VideoFrame>(media::VideoFrame{37, 23, 148, std::vector<std::uint8_t>(148 * 23)});
    for (std::size_t i = 0; i < source->rgba_pixels.size(); ++i)
        source->rgba_pixels[i] = static_cast<std::uint8_t>(i * 31 + i / 7);
    auto foreground = std::make_shared<media::VideoFrame>(media::VideoFrame{13, 17, 52, std::vector<std::uint8_t>(52 * 17)});
    for (std::size_t i = 0; i < foreground->rgba_pixels.size(); ++i)
        foreground->rgba_pixels[i] = static_cast<std::uint8_t>(i * 11 + i / 3);
    NodeGraph graph;
    graph.nodes = {{1, NodeType::Input}, {2, NodeType::Input}, {3, NodeType::Transform},
        {4, NodeType::Color}, {5, NodeType::Effect}, {6, NodeType::Merge}, {7, NodeType::Output}};
    graph.next_id = 8;
    graph.nodes[2].transform.position_x = 0.47;
    graph.nodes[2].transform.position_y = 0.63;
    graph.nodes[2].transform.scale = 0.79;
    graph.nodes[2].transform.opacity = 0.37;
    graph.nodes[3].color = {12, 131, 83};
    graph.nodes[4].effect = creative_suite::effects::makeDefaultInstance("video.grayscale");
    graph.connections = {{1, 3, 0}, {3, 4, 0}, {4, 5, 0}, {5, 6, 0}, {2, 6, 1}, {6, 7, 0}};
    const InputFrames inputs{{1, source}, {2, foreground}};
    for (const double rotation : {0.0, 17.0, -43.0}) {
        graph.nodes[2].transform.rotation_degrees = rotation;
        bool used = false;
        const auto cpu = evaluate(graph, inputs, EvaluationContext{9});
        const auto output = evaluate(graph, inputs, EvaluationContext{9, &gpu, &used});
        if (!cpu || !output || !used) throw std::runtime_error("Fusion parity did not execute the native GPU graph.");
        if (cpu->width != output->width || cpu->height != output->height)
            throw std::runtime_error("Fusion GPU geometry changed.");
        for (std::size_t i = 0; i < cpu->rgba_pixels.size(); ++i)
            if (std::abs(int(cpu->rgba_pixels[i]) - int(output->rgba_pixels[i])) > (i % 4 == 3 ? 0 : 2))
                throw std::runtime_error("Fusion GPU intermediate alpha or RGB parity failed at byte " + std::to_string(i));
    }
    auto generated = makeCompositionGraph(); generated.nodes[0].background_color = {70, 80, 90, 128};
    Node text; text.id = 3; text.type = NodeType::Text; text.text.content = "Fusion";
    Node merge; merge.id = 4; merge.type = NodeType::Merge;
    generated.nodes.push_back(text); generated.nodes.push_back(merge); generated.next_id = 5;
    generated.connections = {{1, 4, 0}, {3, 4, 1}, {4, 2, 0}};
    InputFrames generators; rendering::prepareGraphFrames(generated, generators, 320, 180, true);
    EvaluationContext cpu_context; cpu_context.canvas_width = 320; cpu_context.canvas_height = 180;
    auto gpu_context = cpu_context; bool generated_gpu_used = false;
    gpu_context.gpu = &gpu; gpu_context.gpu_used = &generated_gpu_used;
    const auto generated_cpu = evaluate(generated, generators, cpu_context);
    const auto generated_gpu = evaluate(generated, generators, gpu_context);
    if (!generated_cpu || !generated_gpu || !generated_gpu_used)
        throw std::runtime_error("Fusion generators did not execute the native GPU graph.");
    for (std::size_t byte = 0; byte < generated_cpu->rgba_pixels.size(); ++byte)
        if (std::abs(int(generated_cpu->rgba_pixels[byte]) - int(generated_gpu->rgba_pixels[byte])) > 2)
            throw std::runtime_error("Fusion generator CPU/GPU parity failed.");
    for (const auto mask_type : {NodeType::RectangleMask,NodeType::EllipseMask})
    for (const auto receiver : {NodeType::Input,NodeType::Output,NodeType::Transform,NodeType::Color,NodeType::Effect,NodeType::Merge}) {
        auto masked=makePassthroughGraph(); Node mask;mask.id=3;mask.type=mask_type;
        mask.mask.rotation=31;mask.mask.soft_edge=0.2;mask.mask.invert=receiver==NodeType::Transform;
        masked.nodes.push_back(mask);masked.next_id=4;
        if (receiver==NodeType::Input || receiver==NodeType::Output) masked.connections.push_back({3,receiver==NodeType::Input ? 1ULL : 2ULL,2});
        else {
            Node n;n.id=4;n.type=receiver;n.color.brightness=27;n.transform.position_x=0.75;
            n.effect=creative_suite::effects::makeDefaultInstance("video.grayscale");
            masked.nodes.push_back(n);masked.next_id=5;masked.connections={{1,4,0},{4,2,0},{3,4,2}};
            if (receiver==NodeType::Merge) {Node fg;fg.id=5;masked.nodes.push_back(fg);masked.next_id=6;masked.connections.push_back({5,4,1});}
        }
        InputFrames frames{{1,source},{5,foreground}};
        bool used=false; const auto cpu=evaluate(masked,frames,EvaluationContext{9});
        const auto output=evaluate(masked,frames,EvaluationContext{9,&gpu,&used});
        if (!cpu || !output || !used) throw std::runtime_error("Masked graph did not execute the native GPU receiver " + std::to_string(int(receiver)));
        for (std::size_t i=0;i<cpu->rgba_pixels.size();++i)
            if (std::abs(int(cpu->rgba_pixels[i])-int(output->rgba_pixels[i]))>2)
                throw std::runtime_error("Mask CPU/GPU parity failed at byte "+std::to_string(i));
        auto viewed=evaluate(masked,frames,3,EvaluationContext{9,&gpu,&used});
        if (!viewed || !used) throw std::runtime_error("Mask VIEW did not execute on GPU.");
    }
    auto limited_graph=makePassthroughGraph();Node limited_mask;limited_mask.id=3;limited_mask.type=NodeType::RectangleMask;
    limited_graph.nodes.push_back(limited_mask);limited_graph.next_id=4;limited_graph.connections.push_back({3,2,2});
    auto budget=std::make_shared<OpenGlTexturePoolBudget>(64ULL*1024*1024,1);
    auto limited_gpu=createGpuFrameCompositor(surface,OpenGlPrecisionPolicy::Automatic,nullptr,budget);
    bool limited_used=true;auto fallback=evaluate(limited_graph,inputs,EvaluationContext{0,limited_gpu.get(),&limited_used});
    auto reference=evaluate(limited_graph,inputs);
    if (!fallback || !reference || limited_used || fallback->rgba_pixels!=reference->rgba_pixels)
        throw std::runtime_error("Bounded masked graph did not recover the complete CPU result.");
    bool used = false;
    const auto cancelled = evaluate(graph, inputs, EvaluationContext{0, &gpu, &used, [] { return true; }});
    if (cancelled || used) throw std::runtime_error("A cancelled Fusion GPU graph published a frame.");
}
void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}
media::VideoFramePtr raster(const QImage& image) {
    const auto rgba = image.convertToFormat(QImage::Format_RGBA8888);
    auto frame = std::make_shared<media::VideoFrame>();
    frame->width = rgba.width(); frame->height = rgba.height(); frame->stride = rgba.bytesPerLine();
    frame->rgba_pixels.assign(rgba.constBits(), rgba.constBits() + rgba.sizeInBytes());
    return frame;
}
void compare(const playback::VideoFramePtr& cpu, const playback::VideoFramePtr& gpu) {
    require(cpu && gpu && cpu->width == gpu->width && cpu->height == gpu->height,
        "Timeline GPU output geometry differs from CPU.");
    for (std::size_t i = 0; i < cpu->rgba_pixels.size(); ++i)
        if (std::abs(int(cpu->rgba_pixels[i]) - int(gpu->rgba_pixels[i])) > (i % 4 == 3 ? 0 : 2)) require(false,
            "Timeline CPU/GPU pixel mismatch at " + std::to_string(i));
}

void adjustmentParity(QOffscreenSurface* surface, GpuFrameCompositor& gpu) {
    creative_suite::media::RgbaFrame lower{17, 11, 68, std::vector<std::uint8_t>(17 * 11 * 4)};
    creative_suite::media::RgbaFrame upper{17, 11, 68, std::vector<std::uint8_t>(17 * 11 * 4)};
    for (int y = 0; y < lower.height; ++y) for (int x = 0; x < lower.width; ++x) {
        const auto offset = std::size_t(y * lower.stride + x * 4);
        lower.rgba_pixels[offset] = std::uint8_t((x * 23 + y * 7) % 256);
        lower.rgba_pixels[offset + 1] = std::uint8_t((x * 3 + y * 29) % 256);
        lower.rgba_pixels[offset + 2] = std::uint8_t((x * 17 + y * 13) % 256);
        lower.rgba_pixels[offset + 3] = 255;
        upper.rgba_pixels[offset] = std::uint8_t((230 - x * 5 + y * 2) % 256);
        upper.rgba_pixels[offset + 1] = std::uint8_t((30 + x * 9 + y * 4) % 256);
        upper.rgba_pixels[offset + 2] = std::uint8_t((80 + x * 2 + y * 11) % 256);
        upper.rgba_pixels[offset + 3] = 255;
    }
    CompositionLayer lower_layer; lower_layer.frame = &lower;
    CompositionLayer upper_layer; upper_layer.frame = &upper;
    auto brightness = creative_suite::effects::makeDefaultInstance("video.brightness");
    auto contrast = creative_suite::effects::makeDefaultInstance("video.contrast");
    auto saturation = creative_suite::effects::makeDefaultInstance("video.saturation");
    auto grayscale = creative_suite::effects::makeDefaultInstance("video.grayscale");
    require(creative_suite::effects::setParameterValue(brightness, "amount", 18) &&
        creative_suite::effects::setParameterValue(contrast, "amount", 125) &&
        creative_suite::effects::setParameterValue(saturation, "amount", 70) &&
        creative_suite::effects::setParameterValue(grayscale, "amount", 45),
        "Adjustment parity effects could not be configured.");
    std::vector<creative_suite::effects::EffectInstance> ordered{brightness, contrast, saturation};
    std::vector<creative_suite::effects::EffectInstance> second_stack{grayscale};
    for (const double opacity : {0.0, 0.35, 1.0}) {
        CompositionLayer first;
        first.transform.position_x = 0.63; first.transform.position_y = 0.42;
        first.transform.scale = 0.74; first.transform.opacity = opacity; first.effect_stack = ordered;
        CompositionLayer second;
        second.transform.position_x = 0.42; second.transform.position_y = 0.58;
        second.transform.scale = 0.86; second.transform.opacity = 0.61; second.effect_stack = second_stack;
        const std::vector<rendering::TimelineCompositionStep> steps{{lower_layer, false}, {first, true},
            {upper_layer, false}, {second, true}};
        const auto cpu = rendering::composeAdjustmentLayers(lower.width, lower.height, steps);
        require(bool(cpu), "CPU Adjustment reference composition failed.");
        OpenGlCompositionTimings timings;
        const auto gpu_result = rendering::composeAdjustmentLayersGpu(lower.width, lower.height,
            steps, gpu, {}, &timings);
        require(gpu_result.status == OpenGlCompositionStatus::Complete && gpu_result.frame,
            "GPU Adjustment operation did not complete: " + gpu_result.cause);
        const auto readback = gpu.readback(gpu_result.frame);
        require(readback.status == OpenGlCompositionStatus::Complete && readback.frame,
            "GPU Adjustment parity readback failed: " + readback.cause);
        require(cpu->rgba_pixels.size() == readback.frame->rgba_pixels.size(),
            "CPU/GPU Adjustment outputs have different sizes.");
        for (std::size_t i = 0; i < cpu->rgba_pixels.size(); ++i)
            if (std::abs(int(cpu->rgba_pixels[i]) - int(readback.frame->rgba_pixels[i])) > 2)
                require(false, "CPU/GPU Adjustment parity failed at byte " + std::to_string(i));
    }
    CompositionLayer canceled_adjustment;
    canceled_adjustment.effect_stack = ordered;
    const std::vector<rendering::TimelineCompositionStep> canceled_steps{{lower_layer, false},
        {canceled_adjustment, true}};
    const auto canceled = rendering::composeAdjustmentLayersGpu(lower.width, lower.height,
        canceled_steps, gpu, [] { return true; });
    require(canceled.status == OpenGlCompositionStatus::Cancelled && !canceled.frame,
        "Canceled GPU Adjustment published a partial texture.");

    auto rotated = CompositionLayer{};
    rotated.transform.position_x = .54; rotated.transform.scale = .79;
    rotated.transform.rotation_degrees = 19; rotated.transform.opacity = .47;
    rotated.effect_stack = ordered;
    const std::vector<rendering::TimelineCompositionStep> rotated_steps{{lower_layer, false}, {rotated, true}};
    const auto rotated_cpu = rendering::composeAdjustmentLayers(lower.width, lower.height, rotated_steps);
    const auto rotated_gpu = rendering::composeAdjustmentLayersGpu(lower.width, lower.height, rotated_steps, gpu);
    require(bool(rotated_cpu), "Rotated CPU Adjustment fallback reference failed.");
    if (rotated_gpu.status == OpenGlCompositionStatus::Complete && rotated_gpu.frame) {
        const auto rotated_readback = gpu.readback(rotated_gpu.frame);
        require(rotated_readback.status == OpenGlCompositionStatus::Complete && rotated_readback.frame,
            "Supported GPU rotation readback failed.");
        for (std::size_t i = 0; i < rotated_cpu->rgba_pixels.size(); ++i)
            if (std::abs(int(rotated_cpu->rgba_pixels[i]) - int(rotated_readback.frame->rgba_pixels[i])) > 2)
                require(false, "Rotated CPU/GPU Adjustment parity failed.");
    } else {
        require(rotated_gpu.status == OpenGlCompositionStatus::Unsupported && !rotated_gpu.frame,
            "A failed rotated Adjustment did not return an explicit fallback status.");
        const auto fallback = rendering::composeAdjustmentLayers(lower.width, lower.height, rotated_steps);
        require(fallback && fallback->rgba_pixels == rotated_cpu->rgba_pixels,
            "Unsupported GPU rotation did not retain the CPU Adjustment result.");
    }

    auto limited_budget = std::make_shared<OpenGlTexturePoolBudget>(64ULL * 1024 * 1024, 1);
    auto limited = createGpuFrameCompositor(surface, OpenGlPrecisionPolicy::Automatic,
        QOpenGLContext::globalShareContext(), limited_budget, gpu.backend());
    auto limited_adjustment = rotated;
    limited_adjustment.transform.rotation_degrees = 0;
    const std::vector<rendering::TimelineCompositionStep> limited_steps{{lower_layer, false},
        {limited_adjustment, true}};
    const auto limited_result = rendering::composeAdjustmentLayersGpu(lower.width, lower.height,
        limited_steps, *limited);
    require((limited_result.status == OpenGlCompositionStatus::Busy ||
        limited_result.status == OpenGlCompositionStatus::Unsupported) && !limited_result.frame,
        "A one-target GPU budget unexpectedly published an Adjustment result.");
    require(bool(rendering::composeAdjustmentLayers(lower.width, lower.height, limited_steps)),
        "CPU Adjustment fallback failed after bounded GPU resource pressure.");
}

void directWorker(QOffscreenSurface* surface, playback::CompositionLayerSpec image,
    playback::CompositionLayerSpec incoming, playback::CompositionLayerSpec text,
    media::VideoFramePtr published, media::VideoFramePtr refreshed) {
    playback::PlaybackWorker cpu, direct;
    media::VideoFramePtr expected;
    rendering::PreviewFramePayload delivered;
    int errors = 0;
    QObject::connect(&cpu, &playback::PlaybackWorker::frameReady,
        [&](media::VideoFramePtr frame, qint64, quint64, quint64) { expected = std::move(frame); });
    QObject::connect(&direct, &playback::PlaybackWorker::previewFrameReady,
        [&](rendering::PreviewFramePayload frame, qint64, quint64, quint64) { delivered = std::move(frame); });
    QObject::connect(&direct, &playback::PlaybackWorker::playbackError, [&](QString, qint64, quint64) { ++errors; });
    auto& metrics = rendering::PreviewPerformanceMetrics::instance();
    quint64 epoch = 1;
    auto enable = [&] {
        direct.setGpuCompositionEnabled(true, surface);
        direct.setGpuTextureDelivery(true, epoch, QOpenGLContext::globalShareContext());
    };
    enable();
    for (const auto quality : {playback::PreviewQuality::Full, playback::PreviewQuality::Half, playback::PreviewQuality::Quarter}) {
        cpu.setPreviewQuality(quality); direct.setPreviewQuality(quality);
        for (const auto kind : {timeline::TransitionKind::CrossDissolve, timeline::TransitionKind::FadeToBlack}) {
            QVector<playback::CompositionTransitionSpec> transitions{{0, 0, 1, 60, 10, kind}};
            cpu.setComposition({image, incoming, text}, transitions, 30);
            direct.setComposition({image, incoming, text}, transitions, 30);
            for (qint64 frame : {0, 49, 50, 55, 59, 60, 65, 75}) {
                metrics.reset();
                cpu.renderCompositionFrame(frame, frame, 30);
                direct.renderCompositionFrame(frame, frame, 30);
                require(delivered.gpu && !delivered.rgba && delivered.delivery_epoch == epoch,
                    "Native timeline did not use direct delivery.");
                auto cached = delivered.gpu;
                direct.renderCompositionFrame(frame, frame, 30);
                require(delivered.gpu == cached, "Direct final frame cache lost its lease.");
                auto normal = metrics.takeSnapshotAndReset();
                require(normal.gpu_composition_readback_bytes == 0 && normal.gpu_composition_frames == 1 &&
                    normal.composition_cache_hits == 1, "Normal direct composition read pixels or missed the cache.");
                direct.recoverPreviewFrame(delivered, frame, 30, epoch);
                compare(expected, delivered.rgba);
                cached.reset(); delivered = {};
                direct.setGpuCompositionEnabled(false, surface);
                // Allow nonblocking retirement and producer fence collection.
                QThread::msleep(6); QCoreApplication::processEvents();
                ++epoch; enable();
            }
        }
    }
    image.still_frame = published;
    direct.setComposition({image}, {}, 31); direct.renderCompositionFrame(0, 0, 31);
    require(bool(delivered.gpu), "Published PNG did not use direct delivery.");
    direct.recoverPreviewFrame(delivered, 0, 31, epoch);
    const auto old = delivered.rgba;
    delivered = {}; direct.setGpuCompositionEnabled(false, surface);
    QThread::msleep(6); QCoreApplication::processEvents(); ++epoch; enable();
    image.still_frame = refreshed;
    direct.setComposition({image}, {}, 32); direct.renderCompositionFrame(0, 0, 32);
    require(bool(delivered.gpu), "Refreshed PNG did not use direct delivery.");
    direct.recoverPreviewFrame(delivered, 0, 32, epoch);
    require(old && delivered.rgba && old->rgba_pixels != delivered.rgba->rgba_pixels,
        "Direct delivery cached the previous published PNG.");
    delivered = {}; direct.setGpuCompositionEnabled(false, surface);
    QThread::msleep(6); QCoreApplication::processEvents(); ++epoch; enable();
    auto outgoing = image; outgoing.track_index = 2; outgoing.clip_index = 0;
    outgoing.timeline_start_frame = 0; outgoing.segment_frame_count = 60;
    auto transitioning = incoming; transitioning.track_index = 2; transitioning.clip_index = 1;
    transitioning.timeline_start_frame = 30; transitioning.segment_frame_count = 60;
    auto adjustment = image; adjustment.kind = timeline::ClipKind::Adjustment;
    adjustment.still_frame.reset(); adjustment.track_index = 1; adjustment.clip_index = 2;
    adjustment.timeline_start_frame = 0; adjustment.segment_frame_count = 90;
    adjustment.transform.position_x = .57; adjustment.transform.scale = .82;
    adjustment.transform.opacity = .65;
    adjustment.effects = {creative_suite::effects::makeDefaultInstance("video.grayscale"),
        creative_suite::effects::makeDefaultInstance("video.brightness")};
    require(creative_suite::effects::setParameterValue(adjustment.effects[1], "amount", 8),
        "Preview Adjustment effect could not be configured.");
    auto upper_text = text; upper_text.track_index = 0; upper_text.clip_index = 3;
    const QVector<playback::CompositionTransitionSpec> adjustment_transition{{0, 0, 1, 30, 10,
        timeline::TransitionKind::CrossDissolve}};
    cpu.setComposition({outgoing, transitioning, adjustment, upper_text}, adjustment_transition, 34);
    direct.setComposition({outgoing, transitioning, adjustment, upper_text}, adjustment_transition, 34);
    cpu.renderCompositionFrame(34, 34, 34); direct.renderCompositionFrame(34, 34, 34);
    require(delivered.gpu && !delivered.rgba,
        "Adjustment Preview did not preserve direct GPU texture delivery.");
    direct.recoverPreviewFrame(delivered, 34, 34, epoch);
    compare(expected, delivered.rgba);
    std::vector<rendering::PreviewFramePayload> held_adjustment_textures;
    bool adjustment_busy_fell_back_to_cpu = false;
    for (qint64 frame = 35; frame < 52; ++frame) {
        cpu.renderCompositionFrame(frame, frame, 34);
        direct.renderCompositionFrame(frame, frame, 34);
        if (!delivered.gpu) {
            require(bool(delivered.rgba), "GPU Adjustment resource pressure returned no Preview frame.");
            compare(expected, delivered.rgba);
            adjustment_busy_fell_back_to_cpu = true;
            break;
        }
        held_adjustment_textures.push_back(delivered);
    }
    require(adjustment_busy_fell_back_to_cpu,
        "GPU Adjustment Preview did not recover through CPU under bounded texture pressure.");
    held_adjustment_textures.clear();
    delivered = {}; direct.setGpuCompositionEnabled(false, surface);
    QThread::msleep(6); QCoreApplication::processEvents(); ++epoch; enable();
    direct.setPreviewQuality(playback::PreviewQuality::Quarter);
    direct.setComposition({image}, {}, 33);
    std::vector<rendering::PreviewFramePayload> held;
    for (qint64 frame : {0, 1, 2}) {
        direct.renderCompositionFrame(frame, frame, 33);
        require(bool(delivered.gpu), "Direct pool warmup failed."); held.push_back(delivered);
    }
    auto before = delivered.gpu;
    direct.renderCompositionFrame(3, 3, 33);
    direct.renderCompositionFrame(4, 4, 33);
    require(delivered.gpu == before, "Busy request overwrote a leased frame.");
    held[0] = {};
    QEventLoop retry;
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &retry, [&] { if (delivered.timeline_frame == 4) retry.quit(); });
    poll.start(5); QTimer::singleShot(2000, &retry, &QEventLoop::quit); retry.exec();
    require(delivered.timeline_frame == 4 && delivered.gpu && errors == 0,
        "Paused Busy retry did not coalesce to the latest request.");
    direct.setGpuTextureDelivery(false, ++epoch, QOpenGLContext::globalShareContext());
    direct.renderCompositionFrame(4, 4, 33);
    require(delivered.rgba && !delivered.gpu, "Disabling direct presentation did not retain GPU RGBA fallback.");
    held.clear(); delivered = {}; before.reset();
    direct.setGpuCompositionEnabled(false, surface);
    QThread::msleep(6); QCoreApplication::processEvents(); ++epoch; enable();
    direct.renderCompositionFrame(5, 5, 33);
    require(bool(delivered.gpu), "Direct delivery did not retry after off/on.");
    std::string cause; std::int64_t code = 0;
    if (sessionGraphicsBackend() == GraphicsBackend::Vulkan)
        direct.recoverPreviewFrame(delivered, 5, 33, epoch);
    else
        require(!delivered.gpu->endUse(nullptr, cause, code), "Lost consumer context was not rejected.");
    QThread::msleep(6); QCoreApplication::processEvents();
    direct.renderCompositionFrame(6, 6, 33);
    require(delivered.rgba && !delivered.gpu && errors == 0,
        "A consumer/fence failure did not disable direct delivery and preserve RGBA fallback.");
    direct.renderCompositionFrame(7, 7, 33);
    require(delivered.rgba && !delivered.gpu, "Direct failure unexpectedly retried within the same activation.");
    delivered = {};
}

void adjustmentExport(QOffscreenSurface* surface, const QString& directory,
    const media::VideoFramePtr& source_frame) {
    const auto source_path = directory + "/gpu-adjustment-source.png";
    QImage source_image(source_frame->rgba_pixels.data(), source_frame->width, source_frame->height,
        source_frame->stride, QImage::Format_RGBA8888);
    require(source_image.save(source_path), "GPU Adjustment export fixture could not be saved.");

    rendering::RenderJob job;
    job.settings.output_path = directory + "/gpu-adjustment-export.mkv";
    job.settings.container_name = "matroska";
    job.settings.video_encoder_name = "ffv1";
    job.settings.width = source_frame->width;
    job.settings.height = source_frame->height;
    job.settings.frame_rate = 30;
    job.settings.export_audio = false;
    job.settings.gpu_composition_enabled = true;
    job.settings.graphics_backend = sessionGraphicsBackend();
    auto& document = job.project_snapshot;
    document.canvas_width = source_frame->width;
    document.canvas_height = source_frame->height;
    document.timeline_frame_rate = {30, 1};
    const auto source_file = QFileInfo(source_path).filesystemFilePath();
    document.media.push_back({source_file, "Adjustment Fixture", "Unsorted", false, media::MediaKind::Image});

    project::ProjectClip lower_clip;
    lower_clip.kind = timeline::ClipKind::Image;
    lower_clip.source_path = source_file;
    lower_clip.clip_id = 10;
    lower_clip.duration_frames = lower_clip.source_duration_frames = 4;
    project::ProjectTrack lower_track;
    lower_track.name = "Lower"; lower_track.track_id = 1; lower_track.clips = {lower_clip};

    project::ProjectClip adjustment_clip;
    adjustment_clip.kind = timeline::ClipKind::Adjustment;
    adjustment_clip.clip_id = 11;
    adjustment_clip.duration_frames = 4;
    adjustment_clip.transform.position_x = .56;
    adjustment_clip.transform.scale = .83;
    adjustment_clip.transform.opacity = .62;
    adjustment_clip.effects = {creative_suite::effects::makeDefaultInstance("video.grayscale"),
        creative_suite::effects::makeDefaultInstance("video.brightness")};
    require(creative_suite::effects::setParameterValue(adjustment_clip.effects[1], "amount", 9),
        "Export Adjustment effect could not be configured.");
    project::ProjectTrack adjustment_track;
    adjustment_track.name = "Adjustment"; adjustment_track.track_id = 2;
    adjustment_track.clips = {adjustment_clip};
    document.timeline_tracks = {adjustment_track, lower_track};

    CompositionLayer source_layer; source_layer.frame = source_frame.get();
    CompositionLayer operation; operation.transform = adjustment_clip.transform;
    operation.effect_stack = adjustment_clip.effects;
    const std::vector<rendering::TimelineCompositionStep> steps{{source_layer, false}, {operation, true}};
    const auto expected = rendering::composeAdjustmentLayers(source_frame->width, source_frame->height, steps);
    require(bool(expected), "GPU Adjustment export CPU reference failed.");

    std::atomic_bool canceled{false};
    rendering::OfflineExportOptions options;
    options.gpu_surface = surface;
    int prepared = 0;
    options.prepared_frame_callback = [&](const creative_suite::media::RgbaFrame& frame, std::int64_t) {
        require(frame.rgba_pixels.size() == expected->rgba_pixels.size(),
            "GPU Adjustment export output size changed.");
        for (std::size_t i = 0; i < frame.rgba_pixels.size(); ++i)
            if (std::abs(int(frame.rgba_pixels[i]) - int(expected->rgba_pixels[i])) > 2)
                require(false, "GPU Adjustment export parity failed at byte " + std::to_string(i));
        ++prepared;
    };
    rendering::OfflineExportMetrics metrics;
    options.metrics_callback = [&](const auto& completed) { metrics = completed; };
    rendering::OfflineExportRenderer::render(job, canceled, {}, options);
    require(prepared == 4 && metrics.gpu_frames == 4 && metrics.cpu_frames == 0,
        "Offline export did not keep Adjustment processing on the selected GPU backend.");
}
} // namespace

int main(int argc, char** argv) {
    QSurfaceFormat format; format.setVersion(3, 2); format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(format);
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QGuiApplication app(argc, argv);
    const bool vulkan = app.arguments().contains("--vulkan");
    std::shared_ptr<VulkanDevice> vulkan_device;
    if (vulkan) {
        std::string cause; vulkan_device = VulkanDevice::create(cause);
        if (!vulkan_device) { std::cerr << cause << '\n'; return app.arguments().contains("--require-gpu") ? 1 : 77; }
        setSessionVulkanDevice(vulkan_device); setSessionGraphicsBackend(GraphicsBackend::Vulkan);
    }
    try {
        QTemporaryDir temp;
        require(temp.isValid(), "Temporary fixture unavailable.");
        require(logging::Logger::instance().initialize(QFileInfo(temp.path()).filesystemFilePath()),
            "GPU test log unavailable.");
        auto surface = OpenGlFrameCompositor::createSurface();
        if (!surface) { std::cout << "SKIP: native OpenGL unavailable.\n"; return 77; }
        playback::CompositionLayerSpec image;
        image.kind = timeline::ClipKind::Image; image.segment_frame_count = 60;
        image.track_index = 0; image.clip_index = 0;
        QImage source(16, 16, QImage::Format_RGBA8888); source.fill(QColor(190, 70, 30, 180));
        image.still_frame = raster(source);
        image.keyframes.position_x = {{0, .3}, {59, .7}};
        auto incoming = image; incoming.clip_index = 1; incoming.timeline_start_frame = 50;
        source.fill(QColor(20, 150, 80, 210)); incoming.still_frame = raster(source);
        playback::CompositionLayerSpec text;
        text.kind = timeline::ClipKind::Text; text.segment_frame_count = 110;
        text.track_index = 1; text.clip_index = 0;
        text.text.content = "Native timeline GPU"; text.text.font_size_pixels = 32;
        text.transform.scale = .6; text.transform.opacity = .7;
        const auto published_path = temp.filePath("published.png");
#ifdef CREATIVE_SUITE_TEST_IMAGE_EDITOR_MASKS
        const auto imported_path = temp.filePath("imported.png");
        source.fill(QColor(150, 90, 230, 255)); require(source.save(imported_path), "Fixture save failed.");
        image_editor::ImageDocumentSession producer;
        QString error;
        require(producer.createCanvas(QSize(16, 16), Qt::transparent, &error) &&
            producer.importRasterImages(image_editor::prepareRasterImport({imported_path}).images, {}, &error),
            "Image producer setup failed: " + error.toStdString());
        const auto rasterLayerId = producer.selectedLayerId();
        require(producer.addLayerMask(rasterLayerId), "Image producer mask setup failed.");
        auto curvesId = producer.addCurvesAdjustment();
        auto curves = creative_suite::effects::CurvesParameters{};
        curves.channels[0] = {{0, 0}, {96, 220}, {255, 255}};
        curves.channels[1] = {{0, 0}, {160, 60}, {255, 255}};
        require(!curvesId.isEmpty() && producer.setCurves(curvesId, curves) &&
            producer.selectLayer(rasterLayerId) &&
            producer.applyLayerMaskEraseStroke({QPointF(8, 8)}, 4, &error) &&
            producer.exportImage(published_path, &error), "Masked PNG producer failed: " + error.toStdString());
        const auto published = raster(QImage(published_path));
        require(producer.applyLayerMaskEraseStroke({QPointF(3, 3)}, 4, &error) &&
            producer.exportImage(published_path, &error), "PNG producer refresh failed.");
        const auto refreshed = raster(QImage(published_path));
#else
        const auto published = image.still_frame;
        const auto refreshed = incoming.still_frame;
#endif
        std::exception_ptr failure;
        bool unsupported_context = false;
        std::unique_ptr<QThread> thread(QThread::create([&] {
            try {
                auto probe = createGpuFrameCompositor(surface.get());
                const auto initial = probe->compose(16, 16, {});
                if (initial.status == OpenGlCompositionStatus::Failed &&
                    (initial.operation == "create-context" || initial.operation == "check-context")) {
                    unsupported_context = true; return;
                }
                require(initial.frame.has_value(), "GPU initialization failed: " + initial.cause);
                auto& metrics = rendering::PreviewPerformanceMetrics::instance();
                metrics.setEnabled(true); metrics.reset();
                adjustmentParity(surface.get(), *probe);
                playback::PlaybackWorker cpu, gpu;
                playback::VideoFramePtr cpu_frame, gpu_frame;
                int failures = 0, warnings = 0;
                QObject::connect(&cpu, &playback::PlaybackWorker::frameReady,
                    [&](playback::VideoFramePtr f, qint64, quint64, quint64) { cpu_frame = std::move(f); });
                QObject::connect(&gpu, &playback::PlaybackWorker::frameReady,
                    [&](playback::VideoFramePtr f, qint64, quint64, quint64) { gpu_frame = std::move(f); });
                QObject::connect(&gpu, &playback::PlaybackWorker::playbackError,
                    [&](QString, qint64, quint64) { ++failures; });
                QObject::connect(&gpu, &playback::PlaybackWorker::compositionWarning,
                    [&](QString, qint64, quint64) { ++warnings; });
                gpu.setGpuCompositionEnabled(true, surface.get());
                cpu.setActiveCompositionClip(0, 0); gpu.setActiveCompositionClip(0, 0);
                for (const auto quality : {playback::PreviewQuality::Full, playback::PreviewQuality::Half,
                     playback::PreviewQuality::Quarter}) {
                    cpu.setPreviewQuality(quality); gpu.setPreviewQuality(quality);
                    for (const auto kind : {timeline::TransitionKind::CrossDissolve, timeline::TransitionKind::FadeToBlack}) {
                        const QVector<playback::CompositionTransitionSpec> transitions{{0, 0, 1, 60, 10, kind}};
                        cpu.setComposition({image, incoming, text}, transitions, 10);
                        gpu.setComposition({image, incoming, text}, transitions, 10);
                        for (qint64 frame : {0, 49, 50, 55, 59, 60, 65, 75}) {
                            cpu.renderCompositionFrame(frame, frame, 10);
                            gpu.renderCompositionFrame(frame, frame, 10);
                            compare(cpu_frame, gpu_frame);
                        }
                    }
                }
                image.still_frame = published;
                gpu.setComposition({image}, {}, 11); gpu.renderCompositionFrame(0, 0, 11);
                const auto old = gpu_frame;
                image.still_frame = refreshed;
                gpu.setComposition({image}, {}, 12); gpu.renderCompositionFrame(0, 0, 12);
                require(old->rgba_pixels != gpu_frame->rgba_pixels, "GPU kept stale published PNG pixels.");
                cpu.setComposition({image}, {}, 12); cpu.renderCompositionFrame(0, 0, 12);
                compare(cpu_frame, gpu_frame);
                const auto snapshot = metrics.takeSnapshotAndReset();
                require(failures == 0 && warnings == 0 && snapshot.gpu_composition_frames == 50 &&
                    snapshot.gpu_composition_fallbacks == 0 && snapshot.gpu_composition_readback_bytes > 0,
                    "Native timeline coverage silently used CPU fallback.");
                // Change the backend from a queued worker operation during real playback.
                QEventLoop loop;
                bool toggled_off = false, toggled_on = false;
                QObject::connect(&gpu, &playback::PlaybackWorker::frameReady,
                    [&](playback::VideoFramePtr, qint64 frame, quint64, quint64) {
                        if (!toggled_off && frame >= 1) {
                            toggled_off = true;
                            QMetaObject::invokeMethod(&gpu, [&] { gpu.setGpuCompositionEnabled(false, surface.get()); }, Qt::QueuedConnection);
                        } else if (toggled_off && !toggled_on && frame >= 3) {
                            toggled_on = true;
                            QMetaObject::invokeMethod(&gpu, [&] { gpu.setGpuCompositionEnabled(true, surface.get()); }, Qt::QueuedConnection);
                        } else if (toggled_on && frame >= 5) loop.quit();
                    });
                QTimer::singleShot(2000, &loop, &QEventLoop::quit);
                gpu.play(); loop.exec(); gpu.pause();
                require(toggled_off && toggled_on && failures == 0 && warnings == 0,
                    "Native toggle interrupted playback.");
                directWorker(surface.get(), image, incoming, text, published, refreshed);
                auto graph_gpu = createGpuFrameCompositor(surface.get(), OpenGlPrecisionPolicy::Automatic, nullptr, std::make_shared<OpenGlTexturePoolBudget>(256ULL*1024*1024,8));
                adjustmentExport(surface.get(), temp.path(), image.still_frame);
                fusionParity(*graph_gpu,surface.get());
                metrics.setEnabled(false);
            } catch (...) { failure = std::current_exception(); }
        }));
        thread->start(); thread->wait();
        if (failure) std::rethrow_exception(failure);
        if (unsupported_context) return 77;
        surface.reset();
        std::cout << "Native GPU timeline, transitions, quality, playback and PNG refresh passed.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
