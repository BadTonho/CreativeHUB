#include "ui/viewer/pen_tool.h"
#include "main_window.h"
#include "ui/inspector/inspector_widget.h"
#include "ui/timeline/timeline_navigator.h"
#include "ui/translation_helpers.h"
#include "ui/viewer/composition_viewer.h"

#include <creative_suite/diagnostics/logger.h>
#include <QStatusBar>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace motion::ui {
namespace {
bool withinEditingLimits(const model::AnchorRepositionValues& value)
{
    return model::validAnchorPoint(value.anchor) &&
        std::isfinite(value.position_x) && std::isfinite(value.position_y) &&
        std::abs(value.anchor.x_pixels) <= 1e9 && std::abs(value.anchor.y_pixels) <= 1e9 &&
        std::abs(value.position_x) <= 1e6 && std::abs(value.position_y) <= 1e6;
}
}

bool MainWindow::panBehindAvailable() const
{
    if ((pen_tool_&&pen_tool_->mode()) || !document_ || !timeline_ || !selected_layer_id_ || export_worker_ || audio_keyframe_worker_) return false;
    const auto frame = timeline_->currentFrame();
    return std::any_of(document_->layers().begin(), document_->layers().end(), [this, frame](const auto& layer) {
        return layer.id == selected_layer_id_ && layer.visible && frame >= layer.timeline_start_frame &&
            frame - layer.timeline_start_frame < layer.duration_frames;
    });
}

bool MainWindow::beginPanBehind()
{
    finishPendingTransformEdit();
    if (!panBehindAvailable()) return false;
    const auto frame = timeline_->currentFrame();
    timeline_->setCurrentFrame(frame);
    const auto id = selected_layer_id_;
    try {
        const auto& layer = *std::find_if(document_->layers().begin(), document_->layers().end(),
            [id](const auto& value) { return value.id == id; });
        const auto local_frame = frame - layer.timeline_start_frame;
        const auto local = creative_suite::animation::evaluateTransform(layer.transform, layer.keyframes, local_frame);
        const auto anchor = model::evaluateLayerAnchor(layer, local_frame);
        const model::AnchorRepositionValues initial{anchor, local.position_x, local.position_y};
        if (!withinEditingLimits(initial)) return false;
        if (!hierarchy_snapshot_ || !hierarchy_snapshot_->matches(document_->canvasSize(), document_->layers()))
            hierarchy_snapshot_ = std::make_shared<const model::LayerTransformHierarchy>(document_->canvasSize(), document_->layers());
        const auto world = hierarchy_snapshot_->evaluate(frame).at(id).world;
        auto linear = world;
        linear.tx = linear.ty = 0;
        const auto inverse = creative_suite::composition::requireInverseAffine(linear);
        pan_behind_ = PanBehindGesture{id, frame, document_->canvasSize(), local,
            model::evaluateLayerScale(layer, local_frame)[1], anchor, world, inverse, initial};
        inspector_->previewAnchorReposition(initial);
        updateHistoryActions();
        return true;
    } catch (const std::exception& error) {
        logPanBehindFailure("pan_behind_begin", id, frame, error);
        return false;
    }
}

bool MainWindow::previewPanBehind(const QPointF& delta)
{
    if (!pan_behind_) return false;
    const auto& gesture = *pan_behind_;
    if (!panBehindAvailable() || selected_layer_id_ != gesture.layer_id ||
        timeline_->currentFrame() != gesture.frame || document_->canvasSize() != gesture.canvas) {
        cancelPanBehind();
        return false;
    }
    try {
        const auto local_delta = gesture.inverse_linear.map(delta.x(), delta.y());
        const model::AnchorPoint anchor{gesture.initial_anchor.x_pixels + local_delta[0],
            gesture.initial_anchor.y_pixels + local_delta[1]};
        if (model::validAnchorPoint(anchor) &&
            (std::abs(anchor.x_pixels) > 1e9 || std::abs(anchor.y_pixels) > 1e9)) {
            cancelPanBehind();
            return false;
        }
        const auto values = model::compensatedAnchorValues(gesture.local, gesture.scale_y,
            gesture.canvas, gesture.initial_anchor, anchor);
        if (!withinEditingLimits(values)) { cancelPanBehind(); return false; }
        const auto pivot = gesture.world.map(anchor.x_pixels, anchor.y_pixels);
        if (!std::isfinite(pivot[0]) || !std::isfinite(pivot[1]))
            throw std::runtime_error("Pan Behind pivot exceeds finite composition limits.");
        pan_behind_->preview = values;
        inspector_->previewAnchorReposition(values);
        viewer_->setSelectedLayerAnchor(QPointF(pivot[0] / gesture.canvas.width, pivot[1] / gesture.canvas.height));
        return true;
    } catch (const std::exception& error) {
        const auto id = gesture.layer_id;
        const auto frame = gesture.frame;
        cancelPanBehind();
        logPanBehindFailure("pan_behind_preview", id, frame, error);
        return false;
    }
}

void MainWindow::cancelPanBehind()
{
    if (!pan_behind_) return;
    pan_behind_.reset();
    if (viewer_) viewer_->cancelPanBehind();
    syncTransformInspector();
    updateHistoryActions();
}

void MainWindow::commitPanBehind()
{
    if (!pan_behind_) return;
    const auto gesture = *std::exchange(pan_behind_, std::nullopt);
    updateHistoryActions();
    if (panBehindAvailable() && selected_layer_id_ == gesture.layer_id &&
        timeline_->currentFrame() == gesture.frame && document_->canvasSize() == gesture.canvas) {
        try {
            commitAnchorReposition(gesture.layer_id, gesture.preview.anchor, gesture.frame);
        } catch (const std::exception& error) {
            logPanBehindFailure("pan_behind_commit", gesture.layer_id, gesture.frame, error);
        }
    }
    syncTransformInspector();
}

void MainWindow::commitAnchorReposition(model::LayerId id, model::AnchorPoint anchor, std::int64_t frame)
{
    auto before = captureEditState();
    if (!document_->repositionLayerAnchorKeepingPose(id, anchor, frame)) return;
    (void)recordCompositionEdit(std::move(before));
    updateDocumentState();
    refreshTimeline();
    syncTransformInspector();
    requestPreview();
}

void MainWindow::logPanBehindFailure(const char* operation, model::LayerId id, std::int64_t frame,
    const std::exception& error)
{
    creative_suite::diagnostics::Logger::instance().log(creative_suite::diagnostics::Level::Error,
        "motion_document", operation, error.what(), {{"layer_id", std::to_string(id)}, {"frame", std::to_string(frame)}});
    statusBar()->showMessage(MotionUiTranslation::tr("The pivot could not be repositioned. Check the Motion Studio log for details."), 5000);
}
} // namespace motion::ui
