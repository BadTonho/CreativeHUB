#include <creative_suite/composition/opengl_frame_compositor.h>
#include <creative_suite/media/video_encoder.h>
#include <creative_suite/media/video_playback.h>
#include "rendering/offline_export_renderer.h"
#include "playback/playback_worker.h"
#include "workspaces/fusion/nodes/evaluation/node_graph_evaluator.h"
#include "rendering/preview_performance_metrics.h"
#include "logging/logger.h"
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions_3_2_Core>
#include <QOpenGLVersionFunctionsFactory>
#include <QTemporaryDir>
#include <QSurfaceFormat>
#include <QThread>
#include <QFileInfo>
#include <fstream>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>
#include <iostream>
#include <exception>
#include <stdexcept>

namespace csmedia = creative_suite::media;
namespace composition = creative_suite::composition;
namespace effects = creative_suite::effects;
namespace {
void require(bool value, const std::string& cause) { if (!value) throw std::runtime_error(cause); }
void compare(const csmedia::RgbaFrame& a, const csmedia::RgbaFrame& b) {
    require(a.width == b.width && a.height == b.height && a.stride == b.stride, "Interop changed geometry.");
    int maximum = 0;
    int low[3]{0,0,0}, high[3]{0,0,0};
    for (std::size_t i = 0; i < a.rgba_pixels.size(); ++i) {
        const int difference = std::abs(int(a.rgba_pixels[i]) - int(b.rgba_pixels[i]));
        require(i % 4 != 3 || difference == 0, "Interop changed alpha.");
        maximum = std::max(maximum, difference);
        if (i % 4 != 3) { const auto delta = int(b.rgba_pixels[i]) - int(a.rgba_pixels[i]);
            low[i % 4] = std::min(low[i % 4], delta); high[i % 4] = std::max(high[i % 4], delta); }
    }
    require(maximum <= 2, "Interop RGB difference is " + std::to_string(maximum) + " signed R=" +
        std::to_string(low[0]) + "," + std::to_string(high[0]) + " G=" + std::to_string(low[1]) + "," +
        std::to_string(high[1]) + " B=" + std::to_string(low[2]) + "," + std::to_string(high[2]));
}
class FailAfterFusion final : public rendering::ExportGpuCompositor {
public:
    explicit FailAfterFusion(int& calls) : calls_(calls) {}
    composition::OpenGlCompositionResult compose(int, int,
        const std::vector<composition::CompositionLayer>& layers,
        const composition::OpenGlFrameCompositor::CancellationPredicate&,
        composition::OpenGlCompositionTimings*) override {
        ++calls_;
        require(std::any_of(layers.begin(), layers.end(), [](const auto& layer) {
            return layer.texture_frame && layer.texture_frame->valid();
        }), "Export fault did not occur after a completed GPU Fusion graph.");
        return {composition::OpenGlCompositionStatus::Failed, {}, "injected-after-fusion", "Injected composition failure."};
    }
private:
    int& calls_;
};
void pipeline(const std::filesystem::path& path, QOffscreenSurface* surface, const std::string& codec = "h264_nvenc") {
    csmedia::VideoEncodingSettings encoding;
    encoding.output_path = path; encoding.container_name = "matroska";
    encoding.video_encoder_name = codec; encoding.width = 320; encoding.height = 180;
    encoding.frame_rate_numerator = 30;
    csmedia::RgbaFrame source{320, 180, 1280, std::vector<std::uint8_t>(1280 * 180)};
    {
        csmedia::VideoEncoder writer(encoding);
        for (int f = 0; f < 8; ++f) {
            for (int y = 0; y < 180; ++y) for (int x = 0; x < 320; ++x) {
                const auto i = y * 1280 + x * 4;
                source.rgba_pixels[i] = static_cast<std::uint8_t>(x + f * 29);
                source.rgba_pixels[i + 1] = static_cast<std::uint8_t>(y + f * 17);
                source.rgba_pixels[i + 2] = static_cast<std::uint8_t>(x / 3 + y / 2 + f * 23);
                source.rgba_pixels[i + 3] = 255;
            }
            writer.writeVideo(source, f);
        }
        writer.finish();
    }
    auto cpu = csmedia::VideoPlaybackSession::open(path);
    auto native = csmedia::VideoPlaybackSession::open(path, csmedia::DecodeOptions{csmedia::DecodeAcceleration::PreferHardware});
    composition::OpenGlFrameCompositor gpu(surface, composition::OpenGlPrecisionPolicy::Automatic, QOpenGLContext::globalShareContext());
    csmedia::NativeVideoFramePool pool(320, 180);
    auto first = pool.acquire(), second = pool.acquire(), third = pool.acquire();
    require(first && second && third && !pool.acquire(), "Native output pool did not enforce bounded retention.");
    second.reset(); third.reset();
    const auto native_path = path.parent_path() / L"native-encoded.mkv";
    auto native_encoding = encoding;
    native_encoding.output_path = native_path;
    native_encoding.native_frame_template = first;
    csmedia::VideoEncoder native_writer(native_encoding);
    native_encoding.native_frame_template.reset(); first.reset();
    require(native_writer.acceptsNativeFrames(), "NVENC did not open with D3D11 input.");
    int output_index = 0;
    std::vector<csmedia::RgbaFrame> encoded_references;
    for (const auto index : {0, 7, 2, 3}) {
        const auto decoded = native->decode_frame_at_native(index);
        require(decoded && decoded->native, "D3D11 surface missing.");
        csmedia::RgbaFrame geometry{decoded->width(), decoded->height(), decoded->width() * 4};
        composition::CompositionLayer layer{&geometry}; layer.native_frame = decoded->native;
        composition::OpenGlCompositionTimings timing;
        const auto composed = gpu.compose(320, 180, {layer}, {}, &timing);
        require(composed.status == composition::OpenGlCompositionStatus::Complete && composed.frame,
            "Native composition failed: " + composed.operation + ": " + composed.cause);
        require(timing.native_video_imports == 1 && timing.uploaded_layers == 0 &&
            native->acceleration_diagnostics().downloaded_frames == 0,
            "Decode-to-composition path downloaded or uploaded video pixels.");
        compare(**cpu->decode_frame_at(index), *composed.frame);
        const auto imported = gpu.importFrame(decoded->native, {}, &timing);
        require(imported.status == composition::OpenGlCompositionStatus::Complete && imported.frame &&
            timing.readback_bytes == 0 && timing.uploaded_layers == 0 && timing.native_video_imports == 1,
            "Native image import did not retain GPU-only delivery.");
        const auto recovered = gpu.readback(imported.frame);
        require(recovered.frame.has_value(), "Imported frame recovery failed.");
        compare(**cpu->decode_frame_at(index), *recovered.frame);
        const auto destination = pool.acquire();
        require(destination != nullptr, "NVENC retained more frames than its bounded pool.");
        const auto copied = gpu.copyToNative(imported.frame, destination);
        require(copied.status == composition::OpenGlCompositionStatus::Complete,
            "Native encoding copy failed: " + copied.operation + ": " + copied.cause);
        compare(*recovered.frame, *destination->download_rgba());
        encoded_references.push_back(*recovered.frame);
        native_writer.writeVideo(destination, output_index++);
        require(native_writer.uploadedVideoBytes() == 0, "Native NVENC uploaded CPU pixels.");
    }
    native_writer.finish();
    auto encoded = csmedia::VideoPlaybackSession::open(native_path);
    for (int i = 0; i < output_index; ++i) {
        const auto frame = encoded->decode_frame_at(i);
        require(frame && *frame && (*frame)->width == 320 && (*frame)->height == 180,
            "The native NVENC output lost a submitted frame or changed dimensions.");
        double error = 0;
        for (std::size_t pixel = 0; pixel < (*frame)->rgba_pixels.size(); ++pixel)
            if (pixel % 4 != 3) error += std::abs(int((*frame)->rgba_pixels[pixel]) - int(encoded_references[i].rgba_pixels[pixel]));
        require(error / (320 * 180 * 3) < 12,
            "Native NVENC changed frame order or exceeded the separate lossy-encoding tolerance.");
    }
    require(native->acceleration_diagnostics().hardware_frames > 0 &&
        native->acceleration_diagnostics().downloaded_frames == 0,
        "Native bridge silently used a decoded CPU frame.");
    std::cout << "D3D11 decode -> GPU conversion -> OpenGL composition/import -> D3D11 NVENC passed with zero decoded-frame downloads.\n";

    playback::CompositionLayerSpec clip;
    clip.source_path = QString::fromStdWString(path.wstring());
    clip.segment_frame_count = clip.source_duration_frames = 8;
    clip.frame_rate = 30;
    clip.track_index = clip.clip_index = 0;
    clip.effects = {effects::makeDefaultInstance("video.grayscale"), effects::makeDefaultInstance("video.brightness")};
    fusion::nodes::NodeGraph graph;
    graph.nodes = {{1, fusion::nodes::NodeType::Input}, {2, fusion::nodes::NodeType::Color}, {3, fusion::nodes::NodeType::Output}};
    graph.nodes[1].color = {7, 110, 90}; graph.next_id = 4;
    graph.connections = {{1, 2, 0}, {2, 3, 0}};
    clip.node_graph = graph;
    bool lose_decode_resource = false;
    csmedia::DecodeOptions graph_decode_options{csmedia::DecodeAcceleration::PreferHardware};
    graph_decode_options.hardware_frame_guard = [&](std::int64_t) {
        if (lose_decode_resource) throw csmedia::MediaError("Injected lost Fusion input transfer resource.", -1);
    };
    auto graph_session = csmedia::VideoPlaybackSession::open(path, graph_decode_options);
    const auto graph_native = graph_session->decode_frame_at_native(7);
    require(graph_native && graph_native->native, "Fusion recovery fixture did not decode a native input.");
    lose_decode_resource = true;
    fusion::nodes::NativeInputFrames graph_native_inputs{{1, graph_native->native}};
    fusion::nodes::EvaluationContext graph_recovery_context{7, nullptr, nullptr, {}, &graph_native_inputs};
    int graph_recoveries = 0;
    graph_recovery_context.recover_native_input = [&](fusion::nodes::NodeId id) -> csmedia::RgbaFramePtr {
        require(id == 1, "Fusion recovery requested a different source."); ++graph_recoveries;
        const auto result = graph_session->decode_frame_at(7);
        return result ? *result : csmedia::RgbaFramePtr{};
    };
    const auto recovered_graph = fusion::nodes::evaluateFrame(graph, {}, graph_recovery_context);
    const auto reference_graph = fusion::nodes::evaluateFrame(graph, {{1, *cpu->decode_frame_at(7)}}, {});
    require(recovered_graph && recovered_graph->rgba && reference_graph && reference_graph->rgba && graph_recoveries == 1 &&
        graph_session->acceleration_diagnostics().backend == csmedia::DecodeBackend::Software &&
        graph_session->acceleration_diagnostics().recovery_count == 1,
        "Fusion input recovery did not reopen software decoding at the requested frame.");
    compare(*reference_graph->rgba, *recovered_graph->rgba);
    playback::PlaybackWorker preview_cpu, preview_gpu;
    auto& preview_metrics = rendering::PreviewPerformanceMetrics::instance();
    preview_metrics.setEnabled(true); preview_metrics.reset();
    playback::VideoFramePtr expected, actual;
    int errors = 0;
    QObject::connect(&preview_cpu, &playback::PlaybackWorker::frameReady, [&](auto frame, auto, auto, auto) { expected = std::move(frame); });
    QObject::connect(&preview_gpu, &playback::PlaybackWorker::frameReady, [&](auto frame, auto, auto, auto) { actual = std::move(frame); });
    QObject::connect(&preview_gpu, &playback::PlaybackWorker::playbackError, [&](auto, auto, auto) { ++errors; });
    preview_gpu.setHardwareDecodingEnabled(true);
    preview_gpu.setGpuCompositionEnabled(true, surface);
    preview_cpu.setComposition({clip}, {}, 81); preview_gpu.setComposition({clip}, {}, 81);
    for (const auto index : {0, 7, 2, 3}) {
        preview_cpu.renderCompositionFrame(index, index, 81); preview_gpu.renderCompositionFrame(index, index, 81);
        require(expected && actual && errors == 0, "Native preview did not produce a frame.");
        compare(*expected, *actual);
    }
    const auto preview_diagnostic = preview_metrics.takeSnapshotAndReset();
    require(preview_diagnostic.gpu_composition_frames == 4 && preview_diagnostic.gpu_composition_fallbacks == 0 &&
        preview_diagnostic.native_video_imports >= 4, "Preview/Fusion did not retain native GPU execution.");
    for (const auto quality : {playback::PreviewQuality::Half, playback::PreviewQuality::Quarter}) {
        preview_cpu.setPreviewQuality(quality); preview_gpu.setPreviewQuality(quality);
        expected.reset(); actual.reset();
        preview_cpu.renderCompositionFrame(3, 3, 81); preview_gpu.renderCompositionFrame(3, 3, 81);
        require(expected && actual && errors == 0 && actual->width ==
            (quality == playback::PreviewQuality::Half ? 960 : 480), "Native preview quality did not resize the same frame.");
        compare(*expected, *actual);
    }
    preview_cpu.setPreviewQuality(playback::PreviewQuality::Full);
    preview_gpu.setPreviewQuality(playback::PreviewQuality::Full);
    int composition_failures = 0;
    playback::PlaybackWorker* failing_preview_ptr = nullptr;
    playback::PlaybackWorker failing_preview(nullptr, [&](int, int, const auto& layers, const auto&, auto*) {
        ++composition_failures;
        require(layers.size() == 1 && layers[0].texture_frame && layers[0].texture_frame->valid(),
            "Preview fault did not occur after GPU Fusion.");
        auto retired = layers[0].texture_frame;
        failing_preview_ptr->setGpuCompositionEnabled(true, nullptr);
        require(!retired->valid(), "Injected preview failure did not invalidate the graph lease.");
        return composition::OpenGlCompositionResult{composition::OpenGlCompositionStatus::Failed, {},
            "injected-after-fusion", "Injected loss of the graph context."};
    });
    failing_preview_ptr = &failing_preview;
    QObject::connect(&failing_preview, &playback::PlaybackWorker::frameReady,
        [&](auto frame, auto, auto, auto) { actual = std::move(frame); });
    QObject::connect(&failing_preview, &playback::PlaybackWorker::playbackError, [&](auto, auto, auto) { ++errors; });
    failing_preview.setHardwareDecodingEnabled(true);
    failing_preview.setGpuCompositionEnabled(true, surface);
    failing_preview.setComposition({clip}, {}, 81);
    for (const auto index : {7, 2}) {
        expected.reset(); actual.reset();
        preview_cpu.renderCompositionFrame(index, index, 81);
        failing_preview.renderCompositionFrame(index, index, 81);
        require(expected && actual && errors == 0, "CPU recovery depended on a lost Fusion texture.");
        compare(*expected, *actual);
    }
    require(composition_failures == 1, "Failed preview composition was retried without recovery.");
    rendering::RenderJob job; job.id = 91;
    job.settings.output_path = QString::fromStdWString((path.parent_path() / L"application-export.mkv").wstring());
    job.settings.container_name = "matroska"; job.settings.video_encoder_name = QString::fromStdString(codec);
    job.settings.width = 320; job.settings.height = 180; job.settings.export_audio = true;
    job.settings.audio_encoder_name = "aac";
    job.settings.hardware_decoding_enabled = job.settings.gpu_composition_enabled = true;
    project::ProjectClip exported; exported.source_path = path; exported.duration_frames = exported.source_duration_frames = 8;
    exported.effects = clip.effects; exported.node_graph = graph;
    project::ProjectTrack track; track.name = "Native pipeline"; track.clips = {exported};
    job.project_snapshot.timeline_tracks = {track, track, track};
    for (std::size_t i = 0; i < job.project_snapshot.timeline_tracks.size(); ++i) {
        auto& item = job.project_snapshot.timeline_tracks[i]; item.track_id = i + 1;
        item.clips[0].clip_id = i + 1; item.clips[0].transform.opacity = .3 + .2 * i;
    }
    rendering::OfflineExportMetrics metrics;
    rendering::OfflineExportOptions options; options.gpu_surface = surface;
    options.metrics_callback = [&](const auto& value) { metrics = value; };
    std::atomic_bool cancel{false};
    rendering::OfflineExportRenderer::render(job, cancel, {}, options);
    require(metrics.native_encoded_frames == 8 && metrics.gpu_frames == 8 && metrics.cpu_frames == 0 &&
        metrics.readback_bytes == 0 && metrics.decoded_hardware_frames > 0 && metrics.decoded_downloaded_frames == 0 &&
        metrics.decoded_downloaded_bytes == 0 && metrics.encoding_uploaded_bytes == 0 &&
        metrics.decoder_reserved_gpu_bytes > 0 && metrics.graph_peak_gpu_bytes > 0 && metrics.encoder_reserved_gpu_bytes > 0,
        "Integrated native export: encoded=" + std::to_string(metrics.native_encoded_frames) +
        " gpu=" + std::to_string(metrics.gpu_frames) + " cpu=" + std::to_string(metrics.cpu_frames) +
        " readback=" + std::to_string(metrics.readback_bytes) + " hardware=" + std::to_string(metrics.decoded_hardware_frames) +
        " downloads=" + std::to_string(metrics.decoded_downloaded_frames));
    const auto destination_path = std::filesystem::path(job.settings.output_path.toStdWString());
    const auto contents = [&] { std::ifstream file(destination_path, std::ios::binary);
        return std::vector<char>(std::istreambuf_iterator<char>(file), {}); };
    const auto preserved = contents();
    bool canceled = false;
    try { rendering::OfflineExportRenderer::render(job, cancel, [&](int progress) { if (progress >= 10) cancel.store(true); }, options); }
    catch (const rendering::ExportCanceled&) { canceled = true; }
    require(canceled && contents() == preserved, "Canceling native export changed the existing destination.");
    cancel.store(false);
    bool unknown_failure = false;
    try { rendering::OfflineExportRenderer::render(job, cancel, [](int) { throw 42; }, options); }
    catch (int value) { unknown_failure = value == 42; }
    require(unknown_failure && contents() == preserved, "Unknown callback failure changed the previous output.");
    for (const auto& entry : std::filesystem::directory_iterator(path.parent_path()))
        require(entry.path().filename().string().find(".rendering-") == std::string::npos,
            "Unknown callback failure retained a temporary output.");
    auto invalid = job; invalid.settings.video_encoder_name = "missing_nvenc";
    bool failed = false;
    try { rendering::OfflineExportRenderer::render(invalid, cancel, {}, options); }
    catch (const std::exception&) { failed = true; }
    require(failed && contents() == preserved, "Encoder failure changed the existing destination or silently changed encoder.");
    auto missing_context = options; missing_context.gpu_surface = nullptr;
    rendering::OfflineExportRenderer::render(job, cancel, {}, missing_context);
    require(metrics.cpu_frames == 8 && metrics.native_encoded_frames == 0 && metrics.gpu_frames == 0 &&
        metrics.decoded_hardware_frames > 0 && metrics.decoded_downloaded_frames > 0,
        "Missing interoperability did not expose RGBA/CPU recovery with the selected hardware encoder.");
    auto reference_job = job;
    reference_job.settings.output_path = QString::fromStdWString((path.parent_path() / L"cpu-reference.mkv").wstring());
    reference_job.settings.hardware_decoding_enabled = reference_job.settings.gpu_composition_enabled = false;
    rendering::OfflineExportRenderer::render(reference_job, cancel);
    int export_failures = 0;
    auto fail_after_graph = options;
    fail_after_graph.gpu_factory = [&](auto*) { return std::make_unique<FailAfterFusion>(export_failures); };
    rendering::OfflineExportRenderer::render(job, cancel, {}, fail_after_graph);
    require(export_failures == 1 && metrics.cpu_frames == 8 && metrics.fallback_frames == 8 &&
        metrics.gpu_frames == 0 && metrics.readback_bytes == 0 && metrics.graph_peak_gpu_bytes > 0,
        "Export did not reevaluate the whole Fusion graph through CPU after composition failure.");
    {
        auto reference_output = csmedia::VideoPlaybackSession::open(std::filesystem::path(reference_job.settings.output_path.toStdWString()));
        auto recovered_output = csmedia::VideoPlaybackSession::open(destination_path);
        for (int i = 0; i < 8; ++i) compare(**reference_output->decode_frame_at(i), **recovered_output->decode_frame_at(i));
    }
    rendering::OfflineExportRenderer::render(job, cancel, {}, options);
    require(metrics.native_encoded_frames == 8 && metrics.readback_bytes == 0,
        "Retry after encoder failure did not create fresh native export resources.");
    std::cout << "Native preview/Fusion/effects and integrated NVENC export passed.\n";
}

void benchmark(const std::filesystem::path& root, QOffscreenSurface* surface) {
    for (const auto size : {std::pair{1920,1080}, std::pair{2560,1440}, std::pair{3840,2160}}) {
        const auto [width, height] = size;
        const auto source_path = root / ("source-" + std::to_string(width) + ".mkv");
        csmedia::VideoEncodingSettings settings;
        settings.output_path = source_path; settings.container_name = "matroska";
        settings.video_encoder_name = "h264_nvenc"; settings.width = width; settings.height = height;
        settings.frame_rate_numerator = 30; settings.video_bitrate_mbps = 20;
        csmedia::RgbaFrame source{width, height, width * 4, std::vector<std::uint8_t>(std::size_t(width) * height * 4)};
        for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
            const auto i = std::size_t(y) * source.stride + x * 4;
            source.rgba_pixels[i] = std::uint8_t(x / 8); source.rgba_pixels[i + 1] = std::uint8_t(y / 8);
            source.rgba_pixels[i + 2] = std::uint8_t((x + y) / 16); source.rgba_pixels[i + 3] = 255;
        }
        { csmedia::VideoEncoder writer(settings); for (int i = 0; i < 30; ++i) writer.writeVideo(source, i); writer.finish(); }
        source.rgba_pixels.clear(); source.rgba_pixels.shrink_to_fit();
        { auto reference = csmedia::VideoPlaybackSession::open(source_path);
          auto decoder = csmedia::VideoPlaybackSession::open(source_path, csmedia::DecodeOptions{csmedia::DecodeAcceleration::PreferHardware});
          const auto native_frame = decoder->decode_frame_at_native(17);
          require(native_frame && native_frame->native, "Resolution parity requires a native decoded frame.");
          composition::OpenGlFrameCompositor converter(surface, composition::OpenGlPrecisionPolicy::Automatic,
              QOpenGLContext::globalShareContext());
          csmedia::RgbaFrame geometry{width, height, width * 4};
          composition::CompositionLayer layer{&geometry}; layer.native_frame = native_frame->native;
          const auto result = converter.compose(width, height, {layer});
          require(result.frame.has_value(), "Resolution conversion parity failed.");
          compare(**reference->decode_frame_at(17), *result.frame); }
        rendering::RenderJob job; job.id = width;
        job.settings.container_name = "matroska"; job.settings.video_encoder_name = "h264_nvenc";
        job.settings.width = width; job.settings.height = height; job.settings.export_audio = false;
        job.settings.video_bitrate_mbps = 20;
        project::ProjectClip clip; clip.source_path = source_path; clip.duration_frames = clip.source_duration_frames = 30;
        clip.effects = {effects::makeDefaultInstance("video.grayscale"), effects::makeDefaultInstance("video.brightness")};
        fusion::nodes::NodeGraph graph;
        graph.nodes = {{1, fusion::nodes::NodeType::Input}, {2, fusion::nodes::NodeType::Color}, {3, fusion::nodes::NodeType::Output}};
        graph.next_id = 4; graph.nodes[1].color = {5, 110, 90}; graph.connections = {{1,2,0}, {2,3,0}};
        clip.node_graph = graph;
        for (int i = 0; i < 3; ++i) {
            project::ProjectTrack track; track.track_id = i + 1; track.name = "Synthetic " + std::to_string(i);
            clip.clip_id = i + 1; clip.transform.opacity = .3 + .3 * i;
            track.clips = {clip}; job.project_snapshot.timeline_tracks.push_back(track);
        }
        std::atomic_bool canceled{false};
        for (const bool native : {false, true}) {
            job.settings.gpu_composition_enabled = job.settings.hardware_decoding_enabled = native;
            job.settings.output_path = QString::fromStdWString((root / (std::to_string(width) + (native ? "-gpu.mkv" : "-cpu.mkv"))).wstring());
            rendering::OfflineExportMetrics metrics;
            rendering::OfflineExportOptions options; options.gpu_surface = surface;
            options.metrics_callback = [&](const auto& value) { metrics = value; };
            rendering::OfflineExportRenderer::render(job, canceled, {}, options);
            require(metrics.encoded_frames == 30 && (!native || (metrics.native_encoded_frames == 30 &&
                metrics.decoded_hardware_frames >= 90 && metrics.readback_bytes == 0 && metrics.decoded_downloaded_frames == 0)),
                "The native benchmark silently recovered to CPU or downloaded video frames.");
            std::cout << "pipeline_benchmark resolution=" << width << 'x' << height << " backend=" << (native ? "native" : "cpu")
                << " frames=" << metrics.encoded_frames << " total_ms=" << metrics.total_nanoseconds / 1e6
                << " preparation_ms=" << metrics.preparation_nanoseconds / 1e6 << " composition_ms=" << metrics.composition_nanoseconds / 1e6
                << " conversion_ms=" << metrics.native_conversion_nanoseconds / 1e6 << " encode_ms=" << metrics.encoding_nanoseconds / 1e6
                << " decode_packet_ms=" << metrics.decode_packet_nanoseconds / 1e6 << " decode_receive_ms=" << metrics.decode_receive_nanoseconds / 1e6
                << " decode_conversion_ms=" << metrics.decode_conversion_nanoseconds / 1e6 << " graph_ms=" << metrics.graph_nanoseconds / 1e6
                << " upload_bytes=" << metrics.uploaded_bytes << " readback_bytes=" << metrics.readback_bytes
                << " hardware_frames=" << metrics.decoded_hardware_frames << " software_frames=" << metrics.decoded_software_frames
                << " native_encoded_frames=" << metrics.native_encoded_frames << '\n';
        }
    }
}

// Explicit offscreen acceptance run. It does not qualify physical audio/display timing.
void stress(const std::filesystem::path& root, QOffscreenSurface* surface, int seconds) {
    require(seconds >= 1 && seconds <= 900, "Stress duration must be between one and 900 seconds.");
    const int frame_count = seconds * 30;
    const auto path = root / "stress-1080p.mkv";
    csmedia::VideoEncodingSettings settings;
    settings.output_path = path; settings.container_name = "matroska"; settings.video_encoder_name = "h264_nvenc";
    settings.width = 1920; settings.height = 1080; settings.frame_rate_numerator = 30;
    csmedia::RgbaFrame source{1920, 1080, 7680, std::vector<std::uint8_t>(7680 * 1080, 255)};
    { csmedia::VideoEncoder encoder(settings);
      for (int i = 0; i < frame_count; ++i) {
          for (int y = 0; y < source.height; ++y) for (int x = 0; x < 16; ++x)
              source.rgba_pixels[std::size_t(y) * source.stride + x * 4] = std::uint8_t(i);
          encoder.writeVideo(source, i);
          if (i % 3000 == 0) std::cout << "stress_fixture frames=" << i << '/' << frame_count << std::endl;
      }
      encoder.finish(); }
    source.rgba_pixels.clear(); source.rgba_pixels.shrink_to_fit();
    QVector<playback::CompositionLayerSpec> clips;
    for (int i = 0; i < 3; ++i) {
        playback::CompositionLayerSpec clip;
        clip.source_path = QString::fromStdWString(path.wstring()); clip.frame_rate = 30;
        clip.segment_frame_count = clip.source_duration_frames = frame_count;
        clip.track_index = clip.track_id = i + 1; clip.clip_index = 0; clip.clip_id = i + 1;
        clip.transform.opacity = .3 + i * .3;
        clip.effects = {effects::makeDefaultInstance("video.grayscale"), effects::makeDefaultInstance("video.brightness")};
        fusion::nodes::NodeGraph graph;
        graph.nodes = {{1, fusion::nodes::NodeType::Input}, {2, fusion::nodes::NodeType::Color}, {3, fusion::nodes::NodeType::Output}};
        graph.next_id = 4; graph.nodes[1].color = {5, 110, 90}; graph.connections = {{1,2,0}, {2,3,0}};
        clip.node_graph = graph; clips.push_back(clip);
    }
    playback::PlaybackWorker preview;
    preview.setHardwareDecodingEnabled(true); preview.setGpuCompositionEnabled(true, surface);
    preview.setGpuTextureDelivery(true, 1, QOpenGLContext::globalShareContext());
    preview.setCompositionCanvasSize(1920, 1080);
    rendering::PreviewFramePayload delivered;
    int errors = 0;
    QObject::connect(&preview, &playback::PlaybackWorker::previewFrameReady,
        [&](auto frame, auto, auto, auto) { delivered = std::move(frame); });
    QObject::connect(&preview, &playback::PlaybackWorker::playbackError, [&](auto, auto, auto) { ++errors; });
    auto& metrics = rendering::PreviewPerformanceMetrics::instance(); metrics.setEnabled(true); metrics.reset();
    preview.setComposition(clips, {}, 17);
    QOpenGLContext consumer; consumer.setFormat(surface->format()); consumer.setShareContext(QOpenGLContext::globalShareContext());
    require(consumer.create() && consumer.makeCurrent(surface), "Cannot create the offscreen preview consumer.");
    auto* gl = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_3_2_Core>(&consumer);
    require(gl && gl->initializeOpenGLFunctions(), "Cannot initialize the preview consumer.");
    GLuint target = 0, buffers[2]{};
    gl->glGenTextures(1, &target); gl->glBindTexture(GL_TEXTURE_2D, target);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1920, 1080, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    gl->glGenFramebuffers(2, buffers);
    gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, buffers[1]);
    gl->glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target, 0);
    require(gl->glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "Incomplete preview output.");
    consumer.doneCurrent();
    const auto started = std::chrono::steady_clock::now();
    std::uint64_t late_frames = 0; double maximum_ms = 0;
    std::vector<double> frame_latencies; frame_latencies.reserve(frame_count);
    for (int i = 0; i < frame_count; ++i) {
        const auto frame_started = std::chrono::steady_clock::now();
        delivered = {};
        preview.renderCompositionFrame(i, i, 17);
        require(errors == 0 && delivered.gpu && !delivered.rgba && delivered.timeline_frame == i,
            "The prolonged preview recovered to CPU, failed, or delivered an old frame.");
        require(consumer.makeCurrent(surface), "Cannot activate the preview consumer.");
        std::string cause; std::int64_t code = 0;
        require(delivered.gpu->beginUse(&consumer, cause, code), "Preview synchronization failed: " + cause);
        gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, buffers[0]);
        gl->glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, delivered.gpu->texture(), 0);
        gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, buffers[1]);
        gl->glBlitFramebuffer(0, 0, 1920, 1080, 0, 0, 1920, 1080, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        require(gl->glGetError() == GL_NO_ERROR && delivered.gpu->endUse(&consumer, cause, code),
            "Preview consumption failed: " + cause);
        consumer.doneCurrent(); delivered = {};
        const auto elapsed_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frame_started).count();
        maximum_ms = std::max(maximum_ms, elapsed_ms);
        frame_latencies.push_back(elapsed_ms);
        if (elapsed_ms > 1000.0 / 30) ++late_frames;
        std::this_thread::sleep_until(started + std::chrono::nanoseconds(std::int64_t(i + 1) * 1000000000LL / 30));
        if ((i + 1) % 900 == 0) std::cout << "stress_progress frames=" << i + 1 << " late_frames=" << late_frames
            << " max_frame_ms=" << maximum_ms << std::endl;
    }
    require(consumer.makeCurrent(surface), "Cannot release preview consumer resources.");
    gl->glDeleteFramebuffers(2, buffers); gl->glDeleteTextures(1, &target); consumer.doneCurrent();
    const auto result = metrics.takeSnapshotAndReset();
    const double elapsed_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::sort(frame_latencies.begin(), frame_latencies.end());
    require(result.gpu_composition_frames == std::uint64_t(frame_count) && result.gpu_composition_fallbacks == 0 &&
        result.gpu_composition_readback_bytes == 0 && result.native_video_imports >= std::uint64_t(frame_count) * 3,
        "The prolonged preview did not retain the native pipeline for all three tracks.");
    require(elapsed_seconds < seconds * 1.01 + 2, "Prolonged offscreen preview could not sustain the paced 30 fps workload.");
    std::cout << "stress_result frames=" << frame_count << " seconds=" << seconds << " late_frames=" << late_frames
        << " elapsed_seconds=" << elapsed_seconds << " max_frame_ms=" << maximum_ms
        << " p95_frame_ms=" << frame_latencies[std::min(frame_latencies.size()-1, std::size_t(frame_count * .95))]
        << " p99_frame_ms=" << frame_latencies[std::min(frame_latencies.size()-1, std::size_t(frame_count * .99))]
        << " native_imports=" << result.native_video_imports << " readback_bytes=0" << std::endl;
}
}
int main(int argc, char** argv) {
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QSurfaceFormat format; format.setVersion(3, 2); format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(format);
    QGuiApplication application(argc, argv);
    try {
        QTemporaryDir temporary; require(temporary.isValid(), "Cannot create native pipeline fixture directory.");
        require(logging::Logger::instance().initialize(QFileInfo(temporary.path()).filesystemFilePath()), "Cannot initialize native pipeline diagnostics.");
        auto surface = composition::OpenGlFrameCompositor::createSurface();
        std::exception_ptr error;
        std::unique_ptr<QThread> worker(QThread::create([&] {
            try {
                const auto root = std::filesystem::path(temporary.path().toStdWString());
                if (argc > 1 && std::string(argv[1]) == "--benchmark") benchmark(root, surface.get());
                else if (argc > 1 && std::string(argv[1]) == "--stress") stress(root, surface.get(), argc > 2 ? std::stoi(argv[2]) : 900);
                else if (argc > 1 && std::string(argv[1]) == "--native-hevc") pipeline(root / L"native-hevc.mkv", surface.get(), "hevc_nvenc");
                else pipeline(root / L"native.mkv", surface.get());
            }
            catch (...) { error = std::current_exception(); }
        }));
        worker->start(); worker->wait();
        if (error) { temporary.setAutoRemove(false); std::cerr << "Native test diagnostics: " << temporary.path().toStdString() << '\n'; std::rethrow_exception(error); }
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
