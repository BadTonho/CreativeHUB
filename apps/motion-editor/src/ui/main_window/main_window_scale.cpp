#include "main_window.h"
#include "ui/translation_helpers.h"
#include "ui/inspector/inspector_widget.h"
#include "ui/timeline/timeline_navigator.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace motion::ui {
namespace a=creative_suite::animation;
void MainWindow::editSelectedScaleAxis(std::size_t axis) {
    if(!document_||!timeline_||!inspector_||axis>=2||!selected_layer_id_)return;
    const auto identity=std::pair{selected_layer_id_,axis+7};
    const bool coalescing=active_transform_edit_==identity;
    if(!coalescing)finishPendingTransformEdit();
    try {
        const auto found=std::find_if(document_->layers().begin(),document_->layers().end(),[this](const auto& l){return l.id==selected_layer_id_;});
        if(found==document_->layers().end())return;
        const auto frame=timeline_->currentFrame();
        const auto values=model::evaluateLayerScale(*found,std::max<std::int64_t>(0,frame-found->timeline_start_frame));
        if(!coalescing)active_scale_ratio_=values[1]/values[0];
        const auto value=inspector_->scaleFieldValue(axis);
        const auto other=axis==0?value*active_scale_ratio_:value/active_scale_ratio_;
        if(found->scale_linked && (!std::isfinite(other)||other<.000001||other>1'000'000.)) {syncTransformInspector();return;}
        std::optional<CompositionEditState> before;if(!coalescing)before=captureEditState();
        if(document_->editLayerScaleAxis(selected_layer_id_,static_cast<model::ScaleAxis>(axis),value,frame,active_scale_ratio_)) {
            if(!coalescing){composition_history_.beginCoalescedEdit(std::move(*before));active_transform_edit_=identity;updateHistoryActions();}
            updateDocumentState();refreshTimeline();requestPreview();
        }
    }catch(const std::exception& error){reportDocumentError("edit_scale_axes",document_path_.value_or(std::filesystem::path{}),std::runtime_error("Layer "+std::to_string(selected_layer_id_)+": "+error.what()));}
    syncTransformInspector();
}
void MainWindow::setSelectedScaleLinked(bool linked) {
    finishPendingTransformEdit();if(!document_||!selected_layer_id_)return;
    auto before=captureEditState();if(!document_->setLayerScaleLinked(selected_layer_id_,linked))return;
    (void)recordCompositionEdit(std::move(before));updateDocumentState();syncTransformInspector();
}
void MainWindow::toggleSelectedScaleAxisKeyframe(std::size_t axis) {
    finishPendingTransformEdit();if(!document_||!timeline_||axis>=2)return;
    const auto found=std::find_if(document_->layers().begin(),document_->layers().end(),[this](const auto& l){return l.id==selected_layer_id_;});
    if(found==document_->layers().end())return;
    const auto local=timeline_->currentFrame()-found->timeline_start_frame;
    if(local<0||local>=found->duration_frames)return;
    try {
        const auto property=static_cast<model::ScaleAxis>(axis);const auto& keys=model::layerScaleKeys(*found,property);
        const bool here=std::any_of(keys.begin(),keys.end(),[local](const auto& k){return k.frame==local;});
        const auto value=model::evaluateLayerScale(*found,local)[axis];auto before=captureEditState();
        const bool changed=here?document_->removeLayerScaleAxisKeyframe(selected_layer_id_,property,local)
            :document_->setLayerScaleAxisKeyframe(selected_layer_id_,property,local,value);
        if(!changed)return;(void)recordCompositionEdit(std::move(before));
        timeline_->setLayerExpanded(selected_layer_id_,true);timeline_->setTransformGroupExpanded(selected_layer_id_,true);
        updateDocumentState();refreshTimeline();syncTransformInspector();requestPreview();
    }catch(const std::exception& error){reportDocumentError("toggle_scale_keyframe",document_path_.value_or(std::filesystem::path{}),std::runtime_error("Layer "+std::to_string(selected_layer_id_)+": "+error.what()));}
}
void MainWindow::selectScaleAxisCurveSegment(model::LayerId id,model::ScaleAxis axis,std::int64_t frame) {
    if(!document_||static_cast<std::size_t>(axis)>=2)return;
    const auto found=std::find_if(document_->layers().begin(),document_->layers().end(),[id](const auto& l){return l.id==id;});
    if(found==document_->layers().end())return;
    const auto& keys=model::layerScaleKeys(*found,axis);std::int64_t segment=0;
    if(keys.size()>=2){auto upper=std::upper_bound(keys.begin(),keys.end(),frame,[](auto f,const auto& k){return f<k.frame;});
        const auto index=upper==keys.begin()?std::size_t{0}:std::min(static_cast<std::size_t>(upper-keys.begin()-1),keys.size()-2);segment=keys[index].frame;}
    CurveSelection selection;selection.layer_id=id;selection.segment_start_frame=segment;selection.scale_axis=axis;
    curve_selection_=selection;refreshCurveEditor();
}
} // namespace motion::ui
