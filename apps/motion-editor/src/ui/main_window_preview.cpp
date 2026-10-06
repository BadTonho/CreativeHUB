#include "main_window.h"

#include "../settings/autosave_preferences.h"
#include "rendering/layer_effect_worker_pool.h"
#include "media_pool_widget.h"
#include "timeline/timeline_navigator.h"

#include "rendering/preview_renderer.h"

#include <creative_suite/diagnostics/logger.h>

#include <QTimer>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <utility>

namespace motion::ui {
void MainWindow::configurePreviewPerformanceMetrics()
{
    const bool enabled = settings::previewPerformanceMetricsEnabled();
    diagnostics::PerformanceMetrics::instance().setEnabled(enabled);
    performance_sampler_.reset();
    if (performance_metrics_timer_ == nullptr) return;
    if (enabled) performance_metrics_timer_->start(1000);
    else performance_metrics_timer_->stop();
}
void MainWindow::flushPreviewPerformanceMetrics()
{
    auto& metrics = diagnostics::PerformanceMetrics::instance();
    const auto snapshot = metrics.takeSnapshotAndReset();
    if (!snapshot.has_value()) {
        performance_sampler_.reset();
        return;
    }

    const auto resources = performance_sampler_.sample();
    diagnostics::PreviewLogMetadata metadata;
    if (document_.has_value()) {
        const auto canvas = document_->canvasSize();
        const auto rate = document_->frameRate();
        metadata.canvas_width = canvas.width;
        metadata.canvas_height = canvas.height;
        metadata.frame_rate_numerator = rate.numerator;
        metadata.frame_rate_denominator = rate.denominator;
        metadata.layer_count = document_->layers().size();
        for (const auto& layer : document_->layers())
            metadata.effect_count += layer.effects.size();
    }
    metadata.effect_worker_count =
        motion::ui::detail::configuredLayerEffectWorkerCount();
    const auto context = diagnostics::makePreviewPerformanceContext(
        *snapshot, resources, metadata);

    creative_suite::diagnostics::Logger::instance().log(
        creative_suite::diagnostics::Level::Info,
        "motion_performance", "preview_sample",
        "Motion Studio preview performance interval.", context);
}
void MainWindow::requestPreview(bool playback_tick)
{
    if (!document_ || !preview_renderer_ || !media_pool_ || !timeline_) return;
    PreviewRequest request;
    request.canvas_size = document_->canvasSize();
    request.frame_rate = document_->frameRate();
    const auto frame = timeline_->currentFrame();
    for (const auto& layer : document_->layers()) {
        if (!layer.visible || layer.duration_frames <= 0 || frame < layer.timeline_start_frame ||
            frame - layer.timeline_start_frame >= layer.duration_frames) {
            continue;
        }
        PreviewLayerSnapshot snapshot;
        snapshot.id = layer.id;
        snapshot.kind = layer.kind;
        snapshot.source_path = layer.source_path;
        snapshot.local_frame = frame - layer.timeline_start_frame;
        snapshot.source_frame_count = layer.source_frame_count;
        snapshot.source_frame_rate = layer.source_frame_rate;
        snapshot.transform = layer.transform;
        snapshot.keyframes = layer.keyframes;
        snapshot.content = layer.content;
        snapshot.effects = layer.effects;
        if (layer.kind == model::LayerKind::Image) {
            snapshot.still_frame = media_pool_->sharedFirstFrameForPath(layer.source_path);
        }
        request.layers.push_back(std::move(snapshot));
    }
    (void)preview_renderer_->submit(
        std::move(request),
        playback_tick ? PreviewRequestMode::Playback : PreviewRequestMode::Interactive);
}

} // namespace motion::ui
