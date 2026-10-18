#include "main_window.h"
#include "rendering/preview_renderer.h"
#include "ui/inspector/inspector_widget.h"
#include "ui/timeline/timeline_navigator.h"
#include "ui/viewer/composition_viewer.h"
#include "ui/translation_helpers.h"
#include <creative_suite/diagnostics/logger.h>
#include <QStatusBar>
#include <algorithm>
#include <utility>

namespace motion::ui {
bool MainWindow::beginRotationDrag(QPointF press, double logical_to_canvas)
{
    finishPendingTransformEdit();
    if (!panBehindAvailable()) return false;
    const auto id=selected_layer_id_;const auto frame=timeline_->currentFrame();
    timeline_->setCurrentFrame(frame);
    try {
        auto request=buildPreviewRequest();
        const auto hierarchy=request.sceneHierarchy();if(!hierarchy)return false;
        const auto& layer=*std::find_if(document_->layers().begin(),document_->layers().end(),[id](const auto& l){return l.id==id;});
        const auto local=frame-layer.timeline_start_frame;
        const auto initial=creative_suite::animation::evaluateTransform(layer.transform,layer.keyframes,local).rotation_degrees;
        if(!model::validLayerRotation(initial))return false;
        const auto resolved=hierarchy->evaluate(frame);
        const auto parent=layer.parent_id?resolved.at(layer.parent_id).world:model::AffineTransform{};
        auto outer=creative_suite::composition::composeAffine(parent,layer.parent_affine.value_or(model::affineOffset(layer.parent_offset)));
        outer.tx=outer.ty=0;
        const auto inverse=creative_suite::composition::requireInverseAffine(outer);
        const auto pivot=model::resolvedAnchorPosition(resolved.at(id),model::evaluateLayerAnchor(layer,local),document_->canvasSize());
        const auto canvas=document_->canvasSize();
        model::RotationDragCalculation calculation(initial,inverse,pivot[0]*canvas.width,pivot[1]*canvas.height,
            press.x(),press.y(),8*logical_to_canvas);
        rotation_drag_=RotationDragGesture{id,frame,canvas,initial,initial,outer,model::evaluateLayerScale(layer,local),std::move(calculation),
            std::make_shared<const PreviewRequest>(std::move(request)),++viewer_gesture_session_};
        inspector_->previewLayerRotation(initial);updateHistoryActions();return true;
    }catch(const std::exception& error){logRotationDragFailure("rotation_drag_begin",id,frame,error);return false;}
}
bool MainWindow::previewRotationDrag(QPointF pointer)
{
    if(!rotation_drag_)return false;
    auto& g=*rotation_drag_;
    if(!panBehindAvailable()||selected_layer_id_!=g.id||timeline_->currentFrame()!=g.frame||document_->canvasSize()!=g.canvas) {
        cancelRotationDrag();return false;
    }
    try {
        const auto value=g.calculation.update(pointer.x(),pointer.y());
        if(!model::validLayerRotation(value)){cancelRotationDrag();return false;}
        const bool changed=value!=g.preview;g.preview=value;
        inspector_->previewLayerRotation(value);
        const auto direction=model::rotationGuideDirection(g.outer,value);
        viewer_->setRotationDirection(QPointF(direction[0],direction[1]));
        syncScaleGuideAxes(creative_suite::composition::composeAffine(g.outer,model::affineOffset({0,0,1,value})),g.scales);
        if(changed)requestPreview();return true;
    }catch(const std::exception& error){
        const auto id=g.id;const auto frame=g.frame;cancelRotationDrag();
        logRotationDragFailure("rotation_drag_preview",id,frame,error);return false;
    }
}
void MainWindow::cancelRotationDrag()
{
    if(!rotation_drag_)return;
    const bool submitted=rotation_drag_->submitted;rotation_drag_.reset();
    if(viewer_)viewer_->cancelRotationDrag();
    syncTransformInspector();updateHistoryActions();if(submitted)requestPreview();
}
void MainWindow::commitRotationDrag()
{
    if(!rotation_drag_)return;
    const auto g=*std::exchange(rotation_drag_,std::nullopt);updateHistoryActions();
    if(panBehindAvailable()&&selected_layer_id_==g.id&&timeline_->currentFrame()==g.frame&&document_->canvasSize()==g.canvas) {
        try {
            if(g.preview!=g.initial) {
                auto before=captureEditState();
                if(document_->setLayerRotationAtFrame(g.id,g.preview,g.frame)) {
                    (void)recordCompositionEdit(std::move(before));refreshTimeline();updateDocumentState();
                }
            }
        }catch(const std::exception& error){logRotationDragFailure("rotation_drag_commit",g.id,g.frame,error);}
    }
    syncTransformInspector();if(g.submitted||g.preview!=g.initial)requestPreview();
}
void MainWindow::logRotationDragFailure(const char* operation,model::LayerId id,std::int64_t frame,const std::exception& error)
{
    creative_suite::diagnostics::Logger::instance().log(creative_suite::diagnostics::Level::Error,
        "motion_document",operation,error.what(),{{"layer_id",std::to_string(id)},{"frame",std::to_string(frame)}});
    statusBar()->showMessage(MotionUiTranslation::tr("The layer could not be rotated. Check the Motion Studio log for details."),5000);
}
}
