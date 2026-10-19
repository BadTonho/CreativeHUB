#include "rendering/preview_scene_builder.h"
#include "main_window.h"

#include "settings/autosave_preferences.h"
#include "rendering/layer_effect_worker_pool.h"
#include "ui/media_pool/media_pool_widget.h"
#include "ui/timeline/timeline_navigator.h"

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
PreviewRequest MainWindow::buildPreviewRequest()
{
    PreviewRequest request;
    if (!document_ || !media_pool_ || !timeline_) return request;
    request.canvas_size = document_->canvasSize();
    request.frame_rate = document_->frameRate();
    const auto frame = timeline_->currentFrame();
    request.composition_frame=frame;
    try {
        if (!hierarchy_snapshot_ || !hierarchy_snapshot_->matches(document_->canvasSize(), document_->layers()))
            hierarchy_snapshot_ = std::make_shared<const model::LayerTransformHierarchy>(
                document_->canvasSize(), document_->layers());
    } catch (const std::exception& error) {
        creative_suite::diagnostics::Logger::instance().log(creative_suite::diagnostics::Level::Error,
            "motion_preview", "prepare_hierarchy", error.what());
        return request;
    }
    request.hierarchy = hierarchy_snapshot_;
    if(!mask_snapshot_ || *mask_snapshot_!=document_->masks())mask_snapshot_=std::make_shared<const std::vector<model::Mask>>(document_->masks());
    request.masks=mask_snapshot_;
    request.mask_view_layer=mask_view_layer_;request.mask_view_target=mask_view_target_;
    preparePreviewLayers(request,document_->layers(),[&](const model::CompositionLayer& layer) {
        const auto linked=linked_image_frames_.find(layer.id);
        return linked==linked_image_frames_.end()?media_pool_->sharedFirstFrameForPath(layer.source_path):linked->second;
    });
    request.freezeScene();
    return request;
}
void MainWindow::requestPreview(bool playback_tick)
{
    if (!document_ || !preview_renderer_ || !media_pool_ || !timeline_) return;
    if (scale_drag_) {
        auto request=*scale_drag_->snapshot;
        if(request.sceneLayers().empty())return;
        request.gesture_session=scale_drag_->session;
        request.scale_override=model::LayerScaleOverride{scale_drag_->id,scale_drag_->preview[0],scale_drag_->preview[1]};
        scale_drag_->submitted=true;
        (void)preview_renderer_->submit(std::move(request),PreviewRequestMode::Gesture);return;
    }
    if (rotation_drag_) {
        auto request = *rotation_drag_->snapshot;
        if(request.sceneLayers().empty())return;
        request.gesture_session = rotation_drag_->session;
        request.rotation_override = model::LayerRotationOverride{rotation_drag_->id,rotation_drag_->preview};
        rotation_drag_->submitted = true;
        (void)preview_renderer_->submit(std::move(request),PreviewRequestMode::Gesture);
        return;
    }
    if (position_drag_) {
        auto request = *position_drag_->snapshot;
        if (request.sceneLayers().empty()) return; // Null-only scenes need no pixels.
        request.gesture_session = position_drag_->session;
        request.position_override = model::LayerPositionOverride{position_drag_->id,position_drag_->preview[0],position_drag_->preview[1]};
        position_drag_->submitted = true;
        (void)preview_renderer_->submit(std::move(request),PreviewRequestMode::Gesture);
        return;
    }
    auto request = buildPreviewRequest();
    (void)preview_renderer_->submit(
        std::move(request),
        playback_tick ? PreviewRequestMode::Playback : PreviewRequestMode::Interactive);
}

} // namespace motion::ui
