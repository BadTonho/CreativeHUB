#include "main_window/main_window.h"
#include "main_window_support.h"

#include "logging/logger.h"
#include "ui/preview/preview_widget.h"
#include "project/project_file.h"
#include "rendering/preview_performance_metrics.h"
#include "timeline/timeline_widget.h"
#include "ui/media_browser/media_browser_list_widget.h"

#include <QAction>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QDesktopServices>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QAbstractItemView>
#include <QListWidget>
#include <QListWidgetItem>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMetaObject>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QScrollArea>
#include <QSlider>
#include <QStatusBar>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <iterator>
#include <limits>
#include <sstream>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>


using namespace main_window_detail;

namespace {

using rendering::PreviewPerformanceSnapshot;
using rendering::PreviewTimingSnapshot;

void appendTimingContext(
    logging::Context& context,
    const char* name,
    const PreviewTimingSnapshot& timing) {
    const std::string prefix(name);
    context.emplace_back(prefix + "_count", std::to_string(timing.count));
    context.emplace_back(
        prefix + "_avg_ms", std::to_string(timing.averageMilliseconds()));
    context.emplace_back(
        prefix + "_max_ms", std::to_string(timing.maximumMilliseconds()));
    context.emplace_back(
        prefix + "_p95_ms", std::to_string(timing.percentile95Milliseconds()));
    context.emplace_back(
        prefix + "_p99_ms", std::to_string(timing.percentile99Milliseconds()));
}

void appendOptionalDoubleContext(
    logging::Context& context,
    const char* name,
    const std::optional<double>& value) {
    context.emplace_back(
        name,
        value.has_value() ? std::to_string(*value) : "N/A");
}

void appendOptionalUint64Context(
    logging::Context& context,
    const char* name,
    const std::optional<std::uint64_t>& value) {
    context.emplace_back(
        name,
        value.has_value() ? std::to_string(*value) : "N/A");
}

void appendPerformanceContext(
    logging::Context& context,
    const PreviewPerformanceSnapshot& snapshot) {
    context.emplace_back("metrics_schema_version", "5");
    context.emplace_back(
        "timeline_fps_numerator",
        std::to_string(snapshot.timeline_frame_rate_numerator));
    context.emplace_back(
        "timeline_fps_denominator",
        std::to_string(snapshot.timeline_frame_rate_denominator));
    context.emplace_back(
        "decoded_frames", std::to_string(snapshot.decoded_frames));
    context.emplace_back(
        "decode_discarded_frames",
        std::to_string(snapshot.decode_discarded_frames));
    context.emplace_back(
        "stale_frames_discarded",
        std::to_string(snapshot.stale_frames_discarded));
    context.emplace_back(
        "decoded_cache_hits", std::to_string(snapshot.decoded_cache_hits));
    context.emplace_back(
        "decoded_cache_entries",
        std::to_string(snapshot.decoded_cache_entries));
    context.emplace_back(
        "decoded_cache_bytes",
        std::to_string(snapshot.decoded_cache_bytes));
    context.emplace_back(
        "text_cache_hits", std::to_string(snapshot.text_cache_hits));
    context.emplace_back(
        "text_composition_fast_path_hits",
        std::to_string(snapshot.text_composition_fast_path_hits));
    context.emplace_back(
        "activation_events", std::to_string(snapshot.activation_events));
    context.emplace_back(
        "playback_start_events",
        std::to_string(snapshot.playback_start_events));
    context.emplace_back(
        "seek_requests", std::to_string(snapshot.seek_requests));
    context.emplace_back(
        "seek_operations", std::to_string(snapshot.seek_operations));
    context.emplace_back(
        "composed_frames", std::to_string(snapshot.composed_frames));
    context.emplace_back(
        "composition_cache_hits", std::to_string(snapshot.composition_cache_hits));
    context.emplace_back(
        "emitted_frames", std::to_string(snapshot.emitted_frames));
    context.emplace_back(
        "received_frames", std::to_string(snapshot.received_frames));
    context.emplace_back(
        "submitted_frames", std::to_string(snapshot.submitted_frames));
    context.emplace_back(
        "cpu_presented_frames",
        std::to_string(snapshot.cpu_presented_frames));
    context.emplace_back(
        "gpu_presented_frames", std::to_string(snapshot.gpu_presented_frames));
    context.emplace_back(
        "overwritten_frames", std::to_string(snapshot.overwritten_frames));
    context.emplace_back(
        "decode_failures", std::to_string(snapshot.decode_failures));
    context.emplace_back(
        "seek_failures", std::to_string(snapshot.seek_failures));
    context.emplace_back(
        "composition_failures",
        std::to_string(snapshot.composition_failures));
    context.emplace_back(
        "gpu_failures", std::to_string(snapshot.gpu_failures));
    context.emplace_back(
        "playback_ticks", std::to_string(snapshot.playback_ticks));
    context.emplace_back(
        "pacing_skipped_frames",
        std::to_string(snapshot.pacing_skipped_frames));
    context.emplace_back(
        "pacing_audio_catchup_frames",
        std::to_string(snapshot.pacing_audio_catchup_frames));
    context.emplace_back(
        "pacing_deadline_catchup_frames",
        std::to_string(snapshot.pacing_deadline_catchup_frames));
    context.emplace_back(
        "pacing_coalesced_frames",
        std::to_string(snapshot.pacing_coalesced_frames));
    context.emplace_back(
        "audio_clock_drift_samples",
        std::to_string(snapshot.audio_clock_drift_samples));
    context.emplace_back(
        "last_frame_width", std::to_string(snapshot.last_frame_width));
    context.emplace_back(
        "last_frame_height", std::to_string(snapshot.last_frame_height));
    context.emplace_back(
        "composition_layer_count",
        std::to_string(snapshot.composition_layer_count));
    context.emplace_back(
        "composition_text_layer_count",
        std::to_string(snapshot.composition_text_layer_count));
    context.emplace_back(
        "composition_transition_count",
        std::to_string(snapshot.composition_transition_count));
    context.emplace_back(
        "composition_enabled",
        snapshot.composition_enabled ? "true" : "false");
    context.emplace_back(
        "audio_enabled",
        snapshot.audio_enabled ? "true" : "false");

    const auto window_seconds = static_cast<double>(
        snapshot.preview_window_elapsed_nanoseconds) / 1'000'000'000.0;
    const auto active_seconds = static_cast<double>(
        snapshot.playback_active_nanoseconds) / 1'000'000'000.0;
    const auto target_fps = static_cast<double>(snapshot.target_frame_rate_milli) /
        1000.0;
    const auto presented_frames = snapshot.cpu_presented_frames +
        snapshot.gpu_presented_frames;
    const auto expected_frames = target_fps > 0.0 && active_seconds > 0.0
        ? static_cast<std::uint64_t>(std::llround(target_fps * active_seconds))
        : 0U;
    context.emplace_back(
        "preview_window_ms",
        std::to_string(window_seconds * 1000.0));
    context.emplace_back(
        "playback_active_ms",
        std::to_string(active_seconds * 1000.0));
    appendOptionalDoubleContext(
        context,
        "target_fps",
        target_fps > 0.0 ? std::optional<double>(target_fps) : std::nullopt);
    context.emplace_back(
        "expected_frames", std::to_string(expected_frames));
    appendOptionalDoubleContext(
        context,
        "frame_budget_ms",
        target_fps > 0.0
            ? std::optional<double>(1000.0 / target_fps)
            : std::nullopt);
    appendOptionalDoubleContext(
        context,
        "emitted_fps",
        window_seconds > 0.0
            ? std::optional<double>(static_cast<double>(snapshot.emitted_frames) /
                window_seconds)
            : std::nullopt);
    appendOptionalDoubleContext(
        context,
        "received_fps",
        window_seconds > 0.0
            ? std::optional<double>(static_cast<double>(snapshot.received_frames) /
                window_seconds)
            : std::nullopt);
    appendOptionalDoubleContext(
        context,
        "presented_fps",
        window_seconds > 0.0
            ? std::optional<double>(static_cast<double>(presented_frames) /
                window_seconds)
            : std::nullopt);
    appendOptionalDoubleContext(
        context,
        "presentation_ratio_percent",
        expected_frames > 0
            ? std::optional<double>(static_cast<double>(presented_frames) * 100.0 /
                static_cast<double>(expected_frames))
            : std::nullopt);
    appendOptionalDoubleContext(
        context,
        "first_frame_ms",
        snapshot.first_frame_nanoseconds > 0
            ? std::optional<double>(static_cast<double>(snapshot.first_frame_nanoseconds) /
                1'000'000.0)
            : std::nullopt);
    appendOptionalDoubleContext(
        context,
        "audio_clock_drift_avg_ms",
        snapshot.audio_clock_drift_samples > 0
            ? std::optional<double>(
                static_cast<double>(snapshot.audio_clock_drift_total_nanoseconds) /
                static_cast<double>(snapshot.audio_clock_drift_samples) /
                1'000'000.0)
            : std::nullopt);
    appendOptionalDoubleContext(
        context,
        "audio_clock_drift_max_abs_ms",
        snapshot.audio_clock_drift_samples > 0
            ? std::optional<double>(
                static_cast<double>(snapshot.audio_clock_drift_max_abs_nanoseconds) /
                1'000'000.0)
            : std::nullopt);
    appendOptionalDoubleContext(
        context,
        "audio_buffered_ms",
        snapshot.audio_buffered_usecs.has_value()
            ? std::optional<double>(
                static_cast<double>(*snapshot.audio_buffered_usecs) / 1000.0)
            : std::nullopt);
    appendTimingContext(context, "decode", snapshot.decode);
    appendTimingContext(context, "decode_packet", snapshot.decode_packet);
    appendTimingContext(context, "decode_receive", snapshot.decode_receive);
    appendTimingContext(context, "pixel_conversion", snapshot.pixel_conversion);
    appendTimingContext(context, "frame_cache_copy", snapshot.frame_cache_copy);
    appendTimingContext(
        context,
        "text_rasterization",
        snapshot.text_rasterization);
    appendTimingContext(context, "seek", snapshot.seek);
    appendTimingContext(context, "composition", snapshot.composition);
    appendTimingContext(context, "payload", snapshot.payload);
    appendTimingContext(context, "ui_callback", snapshot.ui_callback);
    appendTimingContext(context, "preview_submit", snapshot.preview_submit);
    appendTimingContext(context, "cpu_surface", snapshot.cpu_surface);
    appendTimingContext(context, "gpu_upload", snapshot.gpu_upload);
    appendTimingContext(context, "gpu_paint", snapshot.gpu_paint);
    appendTimingContext(context, "pacing_lag", snapshot.pacing_lag);
    appendTimingContext(context, "media_open", snapshot.media_open);
    appendTimingContext(context, "audio_setup", snapshot.audio_setup);
    appendTimingContext(
        context,
        "composition_setup",
        snapshot.composition_setup);
    appendTimingContext(
        context,
        "activation_to_presentation",
        snapshot.activation_to_presentation);
    appendTimingContext(
        context,
        "playback_start_to_presentation",
        snapshot.playback_start_to_presentation);
    appendTimingContext(
        context,
        "seek_to_presentation",
        snapshot.seek_to_presentation);
}

const char* slowFrameLayerKindName(rendering::SlowFrameLayerKind kind) noexcept {
    switch (kind) {
    case rendering::SlowFrameLayerKind::Video: return "video";
    case rendering::SlowFrameLayerKind::Image: return "image";
    case rendering::SlowFrameLayerKind::Text: return "text";
    }
    return "unknown";
}

const char* slowFrameDecodePathName(
    rendering::SlowFrameDecodePath path) noexcept {
    switch (path) {
    case rendering::SlowFrameDecodePath::None: return "none";
    case rendering::SlowFrameDecodePath::Forward: return "forward";
    case rendering::SlowFrameDecodePath::FrameAt: return "frame_at";
    case rendering::SlowFrameDecodePath::ForwardFallbackFrameAt:
        return "forward_fallback_frame_at";
    case rendering::SlowFrameDecodePath::StaticFrame: return "static_frame";
    case rendering::SlowFrameDecodePath::TextCache: return "text_cache";
    case rendering::SlowFrameDecodePath::TextRasterization:
        return "text_rasterization";
    }
    return "unknown";
}

const char* compositionRasterPathName(
    rendering::CompositionRasterPath path) noexcept {
    using Path = rendering::CompositionRasterPath;
    switch (path) {
    case Path::Unprocessed: return "unprocessed";
    case Path::FullFrameCopy: return "full_frame_copy";
    case Path::AlphaCoverage: return "alpha_coverage";
    case Path::AxisAligned: return "axis_aligned";
    case Path::Rotated: return "rotated";
    case Path::GeneralFallback: return "general_fallback";
    }
    return "unknown";
}

std::string diagnosticDouble(double value) {
    std::ostringstream formatted;
    formatted << std::setprecision(std::numeric_limits<double>::max_digits10)
              << value;
    return formatted.str();
}

const char* deliveryStageName(
    rendering::PreviewFrameDeliveryStage stage) noexcept {
    using Stage = rendering::PreviewFrameDeliveryStage;
    switch (stage) {
    case Stage::WorkerEmitted: return "worker_emitted";
    case Stage::MailboxPublished: return "mailbox_published";
    case Stage::ControllerDelivered: return "controller_delivered";
    case Stage::WindowReceived: return "window_received";
    case Stage::PreviewSubmitted: return "preview_submitted";
    case Stage::GpuUploaded: return "gpu_uploaded";
    case Stage::GpuDrawn: return "gpu_drawn";
    case Stage::QtFrameSwapped: return "qt_frame_swapped";
    case Stage::CpuPainted: return "cpu_painted";
    case Stage::Count: break;
    }
    return "unknown";
}

const char* deliveryDropReasonName(
    rendering::PreviewFrameDeliveryDropReason reason) noexcept {
    using Reason = rendering::PreviewFrameDeliveryDropReason;
    switch (reason) {
    case Reason::None: return "none";
    case Reason::MailboxCoalesced: return "mailbox_coalesced";
    case Reason::StaleGeneration: return "stale_generation";
    case Reason::TimelineBehind: return "timeline_behind";
    case Reason::PreviewOverwritten: return "preview_overwritten";
    case Reason::InvalidFrame: return "invalid_frame";
    case Reason::NoActiveClip: return "no_active_clip";
    case Reason::GpuFailure: return "gpu_failure";
    case Reason::Shutdown: return "shutdown";
    case Reason::Count: break;
    }
    return "unknown";
}

void appendFrameDeliveryContext(
    logging::Context& context,
    const rendering::PreviewFrameDeliverySnapshot& snapshot) {
    context.emplace_back("diagnostic_schema_version", "1");
    context.emplace_back("thread_role", "ui_logger");
    context.emplace_back("sample_origin_thread_role", "playback_worker");
    context.emplace_back("trace_capacity", "512");
    context.emplace_back("trace_evictions", std::to_string(snapshot.trace_evictions));
    context.emplace_back(
        "unknown_trace_updates", std::to_string(snapshot.unknown_trace_updates));
    context.emplace_back(
        "incomplete_trace_count",
        std::to_string(snapshot.incomplete_trace_count));

    constexpr std::array stage_names{
        "worker_emitted", "mailbox_published", "controller_delivered",
        "window_received", "preview_submitted", "gpu_uploaded", "gpu_drawn",
        "qt_frame_swapped", "cpu_painted"};
    for (std::size_t index = 0; index < stage_names.size(); ++index) {
        context.emplace_back(
            std::string(stage_names[index]) + "_count",
            std::to_string(snapshot.stage_counts[index]));
    }

    constexpr std::array drop_names{
        "none", "mailbox_coalesced", "stale_generation", "timeline_behind",
        "preview_overwritten", "invalid_frame", "no_active_clip", "gpu_failure",
        "shutdown"};
    for (std::size_t index = 1; index < drop_names.size(); ++index) {
        context.emplace_back(
            std::string("drop_") + drop_names[index] + "_count",
            std::to_string(snapshot.drop_counts[index]));
    }

    constexpr std::array timing_names{
        "worker_to_mailbox", "mailbox_wait", "controller_to_window",
        "window_to_preview", "preview_to_gpu_upload", "gpu_upload_to_draw",
        "draw_to_qt_swap", "preview_to_cpu_paint", "worker_to_qt_swap",
        "worker_to_cpu_paint"};
    for (std::size_t index = 0; index < timing_names.size(); ++index) {
        appendTimingContext(context, timing_names[index], snapshot.timings[index]);
    }

    const auto milliseconds = [](std::uint64_t nanoseconds) {
        return std::to_string(static_cast<double>(nanoseconds) / 1'000'000.0);
    };
    context.emplace_back("sample_count", std::to_string(snapshot.sample_count));
    for (std::size_t index = 0; index < snapshot.sample_count; ++index) {
        const auto prefix = "sample_" + std::to_string(index) + "_";
        const auto& sample = snapshot.samples[index];
        context.emplace_back(prefix + "trace_id", std::to_string(sample.trace_id));
        context.emplace_back(
            prefix + "playback_generation",
            std::to_string(sample.playback_generation));
        context.emplace_back(
            prefix + "timeline_frame", std::to_string(sample.timeline_frame));
        context.emplace_back(
            prefix + "last_stage", deliveryStageName(sample.last_stage));
        context.emplace_back(
            prefix + "drop_reason", deliveryDropReasonName(sample.drop_reason));
        context.emplace_back(
            prefix + "completion_status",
            sample.drop_reason != rendering::PreviewFrameDeliveryDropReason::None
                ? "dropped"
                : (sample.complete ? "completed" : "incomplete"));
        context.emplace_back(prefix + "age_ms", milliseconds(sample.age_nanoseconds));
        context.emplace_back(
            prefix + "end_to_end_ms",
            milliseconds(sample.end_to_end_nanoseconds));
    }
}

bool hasFrameDeliveryActivity(
    const rendering::PreviewFrameDeliverySnapshot& snapshot) noexcept {
    if (snapshot.trace_evictions > 0 || snapshot.unknown_trace_updates > 0 ||
        snapshot.incomplete_trace_count > 0 || snapshot.sample_count > 0) {
        return true;
    }
    for (const auto count : snapshot.stage_counts) {
        if (count > 0) return true;
    }
    for (const auto count : snapshot.drop_counts) {
        if (count > 0) return true;
    }
    for (const auto& timing : snapshot.timings) {
        if (timing.count > 0) return true;
    }
    return false;
}

void appendSlowFrameContext(
    logging::Context& context,
    const rendering::PreviewPerformanceSnapshot& snapshot) {
    if (!snapshot.worst_slow_frame.has_value()) return;
    const auto& frame = *snapshot.worst_slow_frame;
    context.emplace_back("diagnostic_schema_version", "5");
    context.emplace_back("thread_role", "ui_logger");
    context.emplace_back("sample_origin_thread_role", "playback_worker");
    context.emplace_back(
        "playback_worker_thread_id",
        std::to_string(snapshot.playback_worker_thread_id));
    context.emplace_back(
        "slow_frame_count", std::to_string(snapshot.slow_frame_count));
    context.emplace_back(
        "playback_generation", std::to_string(frame.playback_generation));
    context.emplace_back("timeline_frame", std::to_string(frame.timeline_frame));
    context.emplace_back(
        "timeline_fps_numerator",
        std::to_string(frame.timeline_frame_rate_numerator));
    context.emplace_back(
        "timeline_fps_denominator",
        std::to_string(frame.timeline_frame_rate_denominator));
    context.emplace_back(
        "target_fps",
        std::to_string(static_cast<double>(frame.frame_rate_milli) / 1000.0));
    const auto milliseconds = [](std::uint64_t nanoseconds) {
        return std::to_string(static_cast<double>(nanoseconds) / 1'000'000.0);
    };
    context.emplace_back(
        "frame_budget_ms", milliseconds(frame.frame_budget_nanoseconds));
    context.emplace_back(
        "processing_ms", milliseconds(frame.processing_nanoseconds));
    context.emplace_back("decode_ms", milliseconds(frame.decode_nanoseconds));
    context.emplace_back(
        "composition_ms", milliseconds(frame.composition_nanoseconds));
    context.emplace_back("payload_ms", milliseconds(frame.payload_nanoseconds));
    context.emplace_back(
        "composition_adapter_ms",
        milliseconds(frame.composition_adapter_nanoseconds));
    context.emplace_back(
        "output_buffer_create_ms",
        milliseconds(frame.output_buffer_create_nanoseconds));
    context.emplace_back(
        "output_background_fill_ms",
        milliseconds(frame.output_background_fill_nanoseconds));
    context.emplace_back(
        "composition_layer_setup_ms",
        milliseconds(frame.composition_layer_setup_nanoseconds));
    context.emplace_back(
        "composition_raster_blend_ms",
        milliseconds(frame.composition_raster_blend_nanoseconds));
    context.emplace_back(
        "composition_fast_path_copy_ms",
        milliseconds(frame.composition_fast_path_copy_nanoseconds));
    context.emplace_back(
        "composition_canvas_width",
        std::to_string(frame.composition_canvas_width));
    context.emplace_back(
        "composition_canvas_height",
        std::to_string(frame.composition_canvas_height));
    context.emplace_back(
        "active_layer_count", std::to_string(frame.active_layer_count));
    context.emplace_back(
        "slow_layer_count", std::to_string(frame.slow_layer_count));
    for (std::size_t index = 0; index < frame.slow_layer_count; ++index) {
        const auto prefix = "slow_layer_" + std::to_string(index) + "_";
        const auto& layer = frame.slow_layers[index];
        context.emplace_back(prefix + "track_id", std::to_string(layer.track_id));
        context.emplace_back(prefix + "clip_id", std::to_string(layer.clip_id));
        context.emplace_back(
            prefix + "track_index", std::to_string(layer.track_index));
        context.emplace_back(
            prefix + "clip_index", std::to_string(layer.clip_index));
        context.emplace_back(
            prefix + "source_frame", std::to_string(layer.source_frame));
        context.emplace_back(
            prefix + "source_width", std::to_string(layer.source_width));
        context.emplace_back(
            prefix + "source_height", std::to_string(layer.source_height));
        context.emplace_back(
            prefix + "source_stride", std::to_string(layer.source_stride));
        context.emplace_back(
            prefix + "transform_position_x",
            diagnosticDouble(layer.transform_position_x));
        context.emplace_back(
            prefix + "transform_position_y",
            diagnosticDouble(layer.transform_position_y));
        context.emplace_back(
            prefix + "transform_scale",
            diagnosticDouble(layer.transform_scale));
        context.emplace_back(
            prefix + "transform_rotation_degrees",
            diagnosticDouble(layer.transform_rotation_degrees));
        context.emplace_back(
            prefix + "transform_opacity",
            diagnosticDouble(layer.transform_opacity));
        context.emplace_back(
            prefix + "kind", slowFrameLayerKindName(layer.kind));
        context.emplace_back(
            prefix + "decode_path", slowFrameDecodePathName(layer.decode_path));
        context.emplace_back(
            prefix + "composition_path",
            compositionRasterPathName(layer.composition_path));
        context.emplace_back(
            prefix + "full_copy_source_dimensions_match",
            layer.full_frame_copy_eligibility.source_dimensions_match
                ? "true"
                : "false");
        context.emplace_back(
            prefix + "full_copy_source_stride_matches",
            layer.full_frame_copy_eligibility.source_stride_matches
                ? "true"
                : "false");
        context.emplace_back(
            prefix + "full_copy_position_x_centered",
            layer.full_frame_copy_eligibility.position_x_centered
                ? "true"
                : "false");
        context.emplace_back(
            prefix + "full_copy_position_y_centered",
            layer.full_frame_copy_eligibility.position_y_centered
                ? "true"
                : "false");
        context.emplace_back(
            prefix + "full_copy_scale_is_one",
            layer.full_frame_copy_eligibility.scale_is_one ? "true" : "false");
        context.emplace_back(
            prefix + "full_copy_rotation_is_zero",
            layer.full_frame_copy_eligibility.rotation_is_zero
                ? "true"
                : "false");
        context.emplace_back(
            prefix + "full_copy_opacity_is_one",
            layer.full_frame_copy_eligibility.opacity_is_one ? "true" : "false");
        context.emplace_back(
            prefix + "full_copy_alpha_check_performed",
            layer.full_frame_copy_eligibility.alpha_check_performed
                ? "true"
                : "false");
        context.emplace_back(
            prefix + "full_copy_source_pixels_opaque",
            layer.full_frame_copy_eligibility.source_pixels_opaque
                ? "true"
                : "false");
        context.emplace_back(
            prefix + "decode_ms", milliseconds(layer.decode_nanoseconds));
        context.emplace_back(
            prefix + "composition_ms",
            milliseconds(layer.composition_nanoseconds));
        context.emplace_back(
            prefix + "composition_setup_ms",
            milliseconds(layer.composition_setup_nanoseconds));
        context.emplace_back(
            prefix + "raster_blend_ms",
            milliseconds(layer.raster_blend_nanoseconds));
        context.emplace_back(
            prefix + "fast_path_copy_ms",
            milliseconds(layer.fast_path_copy_nanoseconds));
        context.emplace_back(
            prefix + "forward_decode_collected",
            layer.forward_decode_collected ? "true" : "false");
        if (layer.forward_decode_collected) {
            context.emplace_back(
                prefix + "forward_decode_completed",
                layer.forward_decode_completed ? "true" : "false");
            context.emplace_back(
                prefix + "forward_decode_cancelled",
                layer.forward_decode_cancelled ? "true" : "false");
            context.emplace_back(
                prefix + "forward_decode_start_frame",
                std::to_string(layer.forward_decode_start_frame));
            context.emplace_back(
                prefix + "forward_decode_requested_frame",
                std::to_string(layer.forward_decode_requested_frame));
            context.emplace_back(
                prefix + "forward_decode_discarded_frames",
                std::to_string(layer.forward_decode_discarded_frames));
            context.emplace_back(
                prefix + "forward_decode_ms",
                milliseconds(layer.forward_decode_elapsed_nanoseconds));
            context.emplace_back(
                prefix + "forward_packet_io_ms",
                milliseconds(layer.forward_decode_packet_io_nanoseconds));
            context.emplace_back(
                prefix + "forward_receive_ms",
                milliseconds(layer.forward_decode_receive_nanoseconds));
            context.emplace_back(
                prefix + "forward_pixel_conversion_ms",
                milliseconds(layer.forward_decode_pixel_conversion_nanoseconds));
            const auto forward_substage_nanoseconds =
                layer.forward_decode_packet_io_nanoseconds +
                layer.forward_decode_receive_nanoseconds +
                layer.forward_decode_pixel_conversion_nanoseconds;
            context.emplace_back(
                prefix + "forward_other_ms",
                milliseconds(layer.forward_decode_elapsed_nanoseconds >
                        forward_substage_nanoseconds
                    ? layer.forward_decode_elapsed_nanoseconds -
                        forward_substage_nanoseconds
                    : 0U));
        }
    }
}

} // namespace

void MainWindow::configurePreviewPerformanceMetrics(bool enabled) {
    auto& metrics = rendering::PreviewPerformanceMetrics::instance();
    metrics.setEnabled(false);
    metrics.reset();
    performance_sampler_.reset();

    if (!enabled) {
        if (preview_metrics_timer_ != nullptr) preview_metrics_timer_->stop();
        return;
    }

    if (preview_metrics_timer_ == nullptr) {
        preview_metrics_timer_ = new QTimer(this);
        preview_metrics_timer_->setInterval(1000);
        connect(
            preview_metrics_timer_,
            &QTimer::timeout,
            this,
            &MainWindow::flushPreviewPerformanceMetrics);
    }

    metrics.setEnabled(true);
    metrics.setPlaybackActive(playback_is_playing_);
    preview_metrics_timer_->start();
}

void MainWindow::flushPreviewPerformanceMetrics() {
    auto& metrics = rendering::PreviewPerformanceMetrics::instance();
    if (!metrics.isEnabled()) return;

    const auto snapshot = metrics.takeSnapshotAndReset();
    const bool has_delivery_activity = hasFrameDeliveryActivity(snapshot.frame_delivery);
    if (snapshot.playback_ticks == 0 && snapshot.emitted_frames == 0 &&
        snapshot.received_frames == 0 && snapshot.submitted_frames == 0 &&
        snapshot.playback_active_nanoseconds == 0 &&
        snapshot.activation_events == 0 &&
        snapshot.playback_start_events == 0 &&
        snapshot.seek_requests == 0 &&
        snapshot.seek_operations == 0 &&
        snapshot.decode_failures == 0 &&
        snapshot.seek_failures == 0 &&
        snapshot.composition_failures == 0 &&
        snapshot.gpu_failures == 0 &&
        snapshot.media_open.count == 0 &&
        snapshot.audio_setup.count == 0 &&
        snapshot.composition_setup.count == 0 &&
        snapshot.activation_to_presentation.count == 0 &&
        snapshot.playback_start_to_presentation.count == 0 &&
        snapshot.seek_to_presentation.count == 0 &&
        snapshot.audio_clock_drift_samples == 0 &&
        snapshot.slow_frame_count == 0 && !has_delivery_activity) {
        return;
    }

    logging::Context context;
    appendPerformanceContext(context, snapshot);
    const auto resources = performance_sampler_.sample();
    appendOptionalDoubleContext(
        context,
        "process_cpu_percent",
        resources.process_cpu_percent);
    appendOptionalUint64Context(
        context,
        "process_working_set_bytes",
        resources.process_working_set_bytes);
    appendOptionalUint64Context(
        context,
        "process_private_usage_bytes",
        resources.process_private_usage_bytes);
    appendOptionalUint64Context(
        context,
        "system_total_bytes",
        resources.system_total_bytes);
    appendOptionalUint64Context(
        context,
        "system_available_bytes",
        resources.system_available_bytes);
    appendOptionalDoubleContext(
        context,
        "gpu_utilization_percent",
        resources.gpu_utilization_percent);
    appendOptionalUint64Context(
        context,
        "gpu_memory_used_bytes",
        resources.gpu_memory_used_bytes);
    context.emplace_back(
        "preview_backend",
        preview_widget_ == nullptr
            ? "N/A"
            : preview_widget_->usesGpuPreview() ? "opengl" : "cpu_fallback");

    const auto source_kind_index = context.size();
    context.emplace_back("source_kind", "N/A");
    const auto source_width_index = context.size();
    context.emplace_back("source_width", "N/A");
    const auto source_height_index = context.size();
    context.emplace_back("source_height", "N/A");
    const auto source_fps_index = context.size();
    context.emplace_back("source_fps", "N/A");
    const auto source_codec_index = context.size();
    context.emplace_back("source_video_codec", "N/A");
    const auto source_container_index = context.size();
    context.emplace_back("source_container_format", "N/A");
    const auto source_audio_index = context.size();
    context.emplace_back("source_has_audio", "N/A");
    if (active_timeline_track_index_cache_.has_value() &&
        active_timeline_clip_index_cache_.has_value() &&
        *active_timeline_track_index_cache_ < timeline_model_.trackCount() &&
        *active_timeline_clip_index_cache_ < timeline_model_.clipCount(
            *active_timeline_track_index_cache_)) {
        const auto& clip = timeline_model_.tracks()[
            *active_timeline_track_index_cache_].clips[*active_timeline_clip_index_cache_];
        if (clip.kind == timeline::ClipKind::Text) {
            context[source_kind_index].second = "text";
        } else {
            context[source_kind_index].second = clip.kind == timeline::ClipKind::Image
                ? "image" : "video";
            const auto media_item = std::find_if(
                media_items_.begin(),
                media_items_.end(),
                [&clip](const ImportedMedia& item) {
                    return normalizedPath(item.metadata.source_path) ==
                        normalizedPath(clip.source_path);
                });
            if (media_item != media_items_.end()) {
                const auto& metadata = media_item->metadata;
                context[source_width_index].second = std::to_string(metadata.width);
                context[source_height_index].second = std::to_string(metadata.height);
                context[source_fps_index].second = metadata.frame_rate.has_value()
                    ? std::to_string(*metadata.frame_rate)
                    : "N/A";
                context[source_codec_index].second = metadata.video_codec;
                context[source_container_index].second = metadata.container_format;
                context[source_audio_index].second = metadata.audio.has_value()
                    ? "true"
                    : "false";
            }
        }
    }
    context.emplace_back("thread_role", "ui_logger");
    context.emplace_back(
        "active_track_index",
        active_timeline_track_index_cache_.has_value()
            ? std::to_string(*active_timeline_track_index_cache_)
            : "-1");
    context.emplace_back(
        "active_clip_index",
        active_timeline_clip_index_cache_.has_value()
            ? std::to_string(*active_timeline_clip_index_cache_)
            : "-1");
    context.emplace_back(
        "playback_frame_index",
        std::to_string(static_cast<long long>(playback_frame_index_)));
    context.emplace_back(
        "playback_worker_thread_id",
        std::to_string(snapshot.playback_worker_thread_id));
    logging::Logger::instance().log(
        logging::Level::Info,
        "preview",
        "performance_metrics",
        "Preview performance sample.",
        context);
    if (snapshot.slow_frame_count > 0 && snapshot.worst_slow_frame.has_value()) {
        logging::Context slow_frame_context;
        appendSlowFrameContext(slow_frame_context, snapshot);
        logging::Logger::instance().log(
            logging::Level::Info,
            "playback",
            "slow_frame",
            "Playback composition exceeded its frame budget.",
            slow_frame_context);
    }
    if (has_delivery_activity) {
        logging::Context delivery_context;
        appendFrameDeliveryContext(delivery_context, snapshot.frame_delivery);
        delivery_context.emplace_back(
            "playback_worker_thread_id",
            std::to_string(snapshot.playback_worker_thread_id));
        logging::Logger::instance().log(
            logging::Level::Info,
            "playback",
            "frame_delivery",
            "Playback frame delivery sample.",
            delivery_context);
    }
}

void MainWindow::initializePlayback() {
    playback_controller_ = std::make_unique<playback::PlaybackController>(
        editor_session_);
    playback_controller_->setPreviewQuality(playback_preview_quality_);
    playback_controller_->setEventHandler(
        [this](const playback::PlaybackControllerEvent& event) {
            handlePlaybackEvent(event);
        });
    if (editUi().monitor_volume != nullptr) {
        applyMonitorVolumePercent(editUi().monitor_volume->value());
    }
}

void MainWindow::shutdownPlayback() {
    if (playback_controller_ == nullptr) return;
    playback_controller_->shutdown();
    playback_controller_.reset();
}

void MainWindow::refreshPlaybackComposition() {
    if (playback_controller_ != nullptr) {
        playback_controller_->refreshComposition();
    }
}

void MainWindow::activateTimelineClip(
    std::size_t clip_index,
    std::int64_t target_frame,
    bool resume_playback) {
    activateTimelineClipAt(0, clip_index, target_frame, resume_playback);
}

void MainWindow::activateTimelineClipAt(
    std::size_t track_index,
    std::size_t clip_index,
    std::int64_t target_frame,
    bool resume_playback,
    bool preserve_timeline_playhead) {
    if (playback_controller_ == nullptr || !playback_controller_->available() ||
        track_index >= timeline_model_.trackCount() ||
        clip_index >= timeline_model_.clipCount(track_index)) {
        return;
    }

    const auto& clip = timeline_model_.tracks()[track_index].clips[clip_index];
    if (clip.kind == timeline::ClipKind::Text ||
        clip.kind == timeline::ClipKind::Image) {
        target_frame = std::clamp<std::int64_t>(
            target_frame, 0, std::max<std::int64_t>(
                0, clip.timeline_duration_frames - 1));
    }

    const auto result = playback_controller_->activateClip(
        clip.clip_id, target_frame, resume_playback,
        preserve_timeline_playhead);
    if (result == playback::PlaybackCommandResult::Pending) {
        statusBar()->showMessage("Loading timeline clip...");
    } else if (result == playback::PlaybackCommandResult::Rejected) {
        statusBar()->showMessage("Could not activate the timeline clip.");
    } else if (result == playback::PlaybackCommandResult::Unavailable) {
        statusBar()->showMessage("Playback is unavailable.");
    }
}

void MainWindow::commitTimelineClipActivation(
    timeline::ClipId clip_id,
    std::int64_t frame_index,
    bool show_cached_frame,
    bool preserve_timeline_playhead) {
    const auto location = timeline_model_.locateClip(clip_id);
    if (!location.has_value()) return;

    const auto& track = timeline_model_.tracks()[location->track_index];
    const auto& clip = track.clips[location->clip_index];
    setActiveTimelineSelection(location.value());
    playback_frame_index_ = frame_index;
    if (!preserve_timeline_playhead) {
        preserved_timeline_playhead_frame_.reset();
    }

    if (timeline::isMediaClipKind(clip.kind) && media_list_ != nullptr) {
        const auto& library = media_controller_.library();
        if (library.contains(clip.source_path)) {
            const auto media_index = library.indexForPath(clip.source_path);
            const QSignalBlocker blocker(media_list_);
            media_list_->setCurrentRow(static_cast<int>(media_index));
            if (show_cached_frame && media_index < library.size()) {
                preview_widget_->setFrame(library.items()[media_index].first_frame);
            }
        }
    }
    updateTimelineState();
    updatePlaybackControls();
    updatePlaybackStatus();
}

void MainWindow::sendPlaybackCommand(playback::PlaybackCommand command) {
    if (playback_controller_ == nullptr || !playback_controller_->available()) return;
    const auto result = playback_controller_->execute(command);
    switch (result) {
    case playback::PlaybackCommandResult::Gap:
        statusBar()->showMessage("Gap in timeline.");
        break;
    case playback::PlaybackCommandResult::NoClip:
        statusBar()->showMessage("No timeline clip at the playhead.");
        break;
    case playback::PlaybackCommandResult::Beginning:
        statusBar()->showMessage("Already at the beginning of the timeline.");
        break;
    case playback::PlaybackCommandResult::End:
        statusBar()->showMessage("Already at the end of the timeline.");
        break;
    case playback::PlaybackCommandResult::Rejected:
        statusBar()->showMessage("The selected timeline media is unavailable.");
        break;
    case playback::PlaybackCommandResult::Pending:
    case playback::PlaybackCommandResult::Unavailable:
    case playback::PlaybackCommandResult::Applied:
        break;
    }
}

void MainWindow::updatePlaybackControls() {
    if (edit_workspace_ != nullptr && edit_workspace_->controller() != nullptr) {
        edit_workspace_->controller()->setPlaybackPresentation(
            playback_is_playing_,
            playback_activation_loading_,
            playback_controller_ != nullptr && playback_controller_->available());
    }
    if (delete_clip_action_ != nullptr) {
        delete_clip_action_->setEnabled(
            canPlaybackSelectedMedia() && !playback_activation_loading_);
    }
}

void MainWindow::updatePlaybackStatus() {
    if (editUi().playback_status == nullptr) return;

    if (playback_activation_loading_) {
        editUi().playback_status->setText("Loading timeline clip...");
        return;
    }

    const auto selected_index = selectedMediaIndex();
    if (!selected_index.has_value()) {
        if (canPlaybackSelectedMedia() &&
            active_timeline_track_index_cache_.has_value() &&
            active_timeline_clip_index_cache_.has_value() &&
            *active_timeline_track_index_cache_ < timeline_model_.trackCount() &&
            *active_timeline_clip_index_cache_ < timeline_model_.clipCount(
                *active_timeline_track_index_cache_)) {
            const auto& clip = timeline_model_.tracks()[*active_timeline_track_index_cache_]
                .clips[*active_timeline_clip_index_cache_];
            const QString state = playback_is_playing_ ? "Playing" : "Paused";
            editUi().playback_status->setText(
                QString("%1 - Frame %2 / %3")
                    .arg(state)
                    .arg(playback_frame_index_ + 1)
                    .arg(clip.timeline_duration_frames));
            return;
        }
        editUi().playback_status->setText("No media selected.");
        return;
    }

    if (!canPreviewSelectedMedia()) {
        editUi().playback_status->setText("Select the timeline media to play.");
        return;
    }

    const auto& metadata = media_items_[*selected_index].metadata;
    QString total = metadata.frame_count.has_value()
        ? QString::number(*metadata.frame_count)
        : "?";
    if (active_timeline_track_index_cache_.has_value() &&
        active_timeline_clip_index_cache_.has_value() &&
        *active_timeline_track_index_cache_ < timeline_model_.trackCount() &&
        *active_timeline_clip_index_cache_ < timeline_model_.clipCount(
            *active_timeline_track_index_cache_)) {
        total = QString::number(
            timeline_model_.tracks()[*active_timeline_track_index_cache_]
                .clips[*active_timeline_clip_index_cache_]
                .timeline_duration_frames);
    }
    const QString state = playback_is_playing_ ? "Playing" : "Paused";
    editUi().playback_status->setText(
        QString("%1 - Frame %2 / %3")
            .arg(state)
            .arg(playback_frame_index_ + 1)
            .arg(total));
}

void MainWindow::handlePlaybackEvent(
    const playback::PlaybackControllerEvent& event) {
    std::visit([this](const auto& value) {
        using Event = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Event, playback::PlaybackActivationEvent>) {
            handlePlaybackActivation(value);
        } else if constexpr (std::is_same_v<Event, playback::PlaybackFrameEvent>) {
            handlePlaybackFrame(value);
        } else if constexpr (std::is_same_v<Event, playback::PlaybackPositionEvent>) {
            handlePlaybackPosition(value);
        } else if constexpr (std::is_same_v<Event, playback::PlaybackStateEvent>) {
            handlePlaybackStateChanged(value.playing);
        } else if constexpr (std::is_same_v<Event, playback::PlaybackFinishedEvent>) {
            handlePlaybackFinished(value.during_playback, value.gap);
        } else if constexpr (std::is_same_v<Event, playback::PlaybackErrorEvent>) {
            handlePlaybackError(value);
        } else if constexpr (std::is_same_v<Event, playback::PlaybackAudioWarningEvent>) {
            statusBar()->showMessage(
                "Audio unavailable; continuing with video playback.");
            if (editUi().playback_status != nullptr) {
                editUi().playback_status->setText(
                    "Audio unavailable; video fallback is active.");
            }
        }
    }, event);
}

void MainWindow::handlePlaybackActivation(
    const playback::PlaybackActivationEvent& event) {
    switch (event.phase) {
    case playback::PlaybackActivationPhase::Pending:
        playback_activation_loading_ = true;
        playback_is_playing_ = playback_controller_ != nullptr &&
            playback_controller_->isPlaying();
        if (const auto location = timeline_model_.locateClip(event.clip_id);
            location.has_value()) {
            setActiveTimelineSelection(*location);
        }
        updateTimelineState();
        statusBar()->showMessage("Loading timeline clip...");
        updatePlaybackControls();
        updatePlaybackStatus();
        break;
    case playback::PlaybackActivationPhase::Committed:
        playback_activation_loading_ = false;
        commitTimelineClipActivation(
            event.clip_id,
            event.frame_index,
            event.show_cached_frame,
            event.preserve_timeline_playhead);
        break;
    case playback::PlaybackActivationPhase::Discarded:
        playback_activation_loading_ = false;
        if (event.clear_selection && active_timeline_clip_id_ == event.clip_id) {
            clearActiveTimelineSelection();
        }
        playback_is_playing_ = playback_controller_ != nullptr &&
            playback_controller_->isPlaying();
        updateTimelineState();
        updatePlaybackControls();
        updatePlaybackStatus();
        break;
    }
}

void MainWindow::handlePlaybackFrame(
    const playback::PlaybackFrameEvent& event) {
    if (event.frame == nullptr) return;

    auto& metrics = rendering::PreviewPerformanceMetrics::instance();
    rendering::PreviewPerformanceScope timing(
        metrics,
        rendering::PreviewTiming::UiCallback);
    metrics.recordReceivedFrame();
    metrics.recordFrameDeliveryStage(
        event.delivery_trace_id,
        rendering::PreviewFrameDeliveryStage::WindowReceived);
    preview_widget_->setFrame(event.frame, event.delivery_trace_id);

    if ((playback_controller_ == nullptr || !playback_controller_->isPlaying()) &&
        edit_workspace_ != nullptr && edit_workspace_->controller() != nullptr) {
        edit_workspace_->controller()->presentPlaybackFrame(event.frame_index);
    }
    updatePlaybackStatus();
}

void MainWindow::handlePlaybackPosition(
    const playback::PlaybackPositionEvent& event) {
    if (edit_workspace_ != nullptr && edit_workspace_->controller() != nullptr) {
        edit_workspace_->controller()->presentPlaybackPosition(
            static_cast<qint64>(event.timeline_frame),
            static_cast<qint64>(event.clip_frame));
    }
    updatePlaybackStatus();
}

void MainWindow::handlePlaybackStateChanged(bool playing) {
    playback_is_playing_ = playing;
    updatePlaybackControls();
    updatePlaybackStatus();
}

void MainWindow::handlePlaybackFinished(bool during_playback, bool gap) {
    playback_is_playing_ = false;
    updatePlaybackControls();
    if (gap) {
        if (editUi().playback_status != nullptr) {
            editUi().playback_status->setText("Gap in timeline.");
        }
        preview_widget_->clearFrame("Gap in timeline.");
        statusBar()->showMessage("Gap in timeline.");
        return;
    }
    if (editUi().playback_status != nullptr) {
        editUi().playback_status->setText(
            during_playback ? "End of timeline." : "End of media.");
    }
}

void MainWindow::handlePlaybackError(const playback::PlaybackErrorEvent& event) {
    if (event.activation_clip_id.has_value()) {
        logging::Context context{
            {"path", pathToUtf8(event.source_path)},
            {"clip_id", std::to_string(*event.activation_clip_id)},
            {"requested_frame", std::to_string(event.target_frame)},
            {"source_start_frame", std::to_string(event.source_start_frame)},
            {"segment_frame_count", std::to_string(event.segment_frame_count)},
            {"generation", std::to_string(event.generation)}};
        if (event.error_code >= 0) {
            context.emplace_back("error_code", std::to_string(event.error_code));
        }
        logging::Logger::instance().log(
            logging::Level::Error,
            "playback",
            "transition",
            event.message.toUtf8().toStdString(),
            context);
        if (active_timeline_clip_id_ == event.activation_clip_id) {
            clearActiveTimelineSelection();
        }
        statusBar()->showMessage("Could not activate the next timeline clip.");
    }

    playback_activation_loading_ = false;
    playback_is_playing_ = false;
    updateTimelineState();
    updatePlaybackControls();
    if (editUi().playback_status != nullptr) {
        editUi().playback_status->setText("Playback error.");
    }
    if (edit_workspace_ != nullptr && edit_workspace_->controller() != nullptr) {
        edit_workspace_->controller()->refreshTimelinePresentation();
    }
    QMessageBox::warning(this, "Playback error", event.message);
}
