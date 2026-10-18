#include "main_window.h"
#include "ui/timeline/timeline_navigator.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace motion::ui {
void MainWindow::changeSelectedLayerParent(model::LayerId parent)
{
    if (timeline_) changeLayerParent(selected_layer_id_, parent, timeline_->currentFrame());
}

void MainWindow::changeLayerParent(model::LayerId child, model::LayerId parent, std::int64_t frame)
{
    finishPendingTransformEdit();
    if (!document_ || !timeline_) return;
    const auto& layers = document_->layers();
    const auto layer = std::find_if(layers.begin(), layers.end(), [child](const auto& item) { return item.id == child; });
    if (layer != layers.end() && layer->parent_id == parent) return;
    try {
        auto before = captureEditState();
        if (document_->setLayerParent(child, parent, frame)) {
            (void)recordCompositionEdit(std::move(before));
            refreshTimeline();
            updateDocumentState();
            requestPreview();
        }
    } catch (const std::exception& error) {
        reportDocumentError("set_layer_parent",document_path_.value_or(std::filesystem::path{}),
            std::runtime_error("Layer "+std::to_string(child)+", parent "+std::to_string(parent)+
                ", frame "+std::to_string(frame)+": "+error.what()));
    }
    syncTransformInspector();
}

std::optional<std::vector<model::LayerId>> MainWindow::beginParentPickWhip(model::LayerId child)
{
    finishPendingTransformEdit();
    if (!document_ || !timeline_ || export_worker_ || audio_keyframe_worker_ ||
        !document_->canSetLayerParent(child,0)) return std::nullopt;
    const auto frame = timeline_->currentFrame();
    timeline_->setCurrentFrame(frame);
    selectLayer(child);
    try {
        auto candidates = document_->eligibleLayerParents(child);
        parent_pick_whip_ = ParentPickWhipGesture{child,frame};
        updateHistoryActions();
        return candidates;
    } catch (const std::exception& error) {
        reportDocumentError("begin_parent_pick_whip",document_path_.value_or(std::filesystem::path{}),
            std::runtime_error("Layer "+std::to_string(child)+", frame "+std::to_string(frame)+": "+error.what()));
        return std::nullopt;
    }
}

void MainWindow::cancelParentPickWhip()
{
    if (!parent_pick_whip_) return;
    parent_pick_whip_.reset();
    if (timeline_) timeline_->cancelParentPickWhip();
    updateHistoryActions();
}

void MainWindow::commitParentPickWhip(model::LayerId child, model::LayerId parent)
{
    if (!parent_pick_whip_) return;
    const auto gesture = *std::exchange(parent_pick_whip_, std::nullopt);
    updateHistoryActions();
    if (!document_ || !timeline_ || child != gesture.child || selected_layer_id_ != child ||
        timeline_->currentFrame() != gesture.frame || export_worker_ || audio_keyframe_worker_ ||
        !document_->canSetLayerParent(child,parent)) return;
    changeLayerParent(child,parent,gesture.frame);
}
} // namespace motion::ui
