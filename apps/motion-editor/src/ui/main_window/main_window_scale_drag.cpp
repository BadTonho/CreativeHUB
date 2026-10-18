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
void MainWindow::syncScaleGuideAxes(const model::AffineTransform& axes,std::array<double,2> scale)
{
    if(!viewer_)return;
    if(!model::validDragScale(scale[0])||!model::validDragScale(scale[1])){viewer_->setScaleDirections({});return;}
    const auto directions=model::scaleDragDirections(axes);
    viewer_->setScaleDirections(std::array{QPointF(directions[0][0],directions[0][1]),
        QPointF(directions[1][0],directions[1][1]),QPointF(directions[2][0],directions[2][1])});
}
bool MainWindow::beginScaleDrag(model::ScaleDragControl control,QPointF lever)
{
    finishPendingTransformEdit();
    if(!panBehindAvailable())return false;
    const auto id=selected_layer_id_;const auto frame=timeline_->currentFrame();timeline_->setCurrentFrame(frame);
    try {
        auto request=buildPreviewRequest();const auto hierarchy=request.sceneHierarchy();if(!hierarchy)return false;
        const auto& layer=*std::find_if(document_->layers().begin(),document_->layers().end(),[id](const auto& l){return l.id==id;});
        const auto local=frame-layer.timeline_start_frame;
        const auto initial=model::evaluateLayerScale(layer,local);
        if(!model::validDragScale(initial[0])||!model::validDragScale(initial[1]))return false;
        const auto resolved=hierarchy->evaluate(frame);
        const auto parent=layer.parent_id?resolved.at(layer.parent_id).world:model::AffineTransform{};
        auto outer=creative_suite::composition::composeAffine(parent,layer.parent_affine.value_or(model::affineOffset(layer.parent_offset)));
        outer.tx=outer.ty=0;
        const auto angle=creative_suite::animation::evaluateTransform(layer.transform,layer.keyframes,local).rotation_degrees;
        const auto axes=creative_suite::composition::composeAffine(outer,model::affineOffset({0,0,1,angle}));
        model::ScaleDragCalculation calculation(control,layer.scale_linked,initial,axes,{lever.x(),lever.y()});
        scale_drag_=ScaleDragGesture{id,frame,document_->canvasSize(),initial,initial,std::move(calculation),
            std::make_shared<const PreviewRequest>(std::move(request)),++viewer_gesture_session_};
        inspector_->previewLayerScale(initial[0],initial[1]);updateHistoryActions();return true;
    }catch(const std::exception& error){logScaleDragFailure("scale_drag_begin",id,frame,error);return false;}
}
bool MainWindow::previewScaleDrag(QPointF delta)
{
    if(!scale_drag_)return false;
    const auto& g=*scale_drag_;
    if(!panBehindAvailable()||selected_layer_id_!=g.id||timeline_->currentFrame()!=g.frame||document_->canvasSize()!=g.canvas) {
        cancelScaleDrag();return false;
    }
    try {
        const auto values=g.calculation.evaluate(delta.x(),delta.y());
        const auto indicator=g.calculation.indicator(values);
        const bool changed=values!=g.preview;scale_drag_->preview=values;
        inspector_->previewLayerScale(values[0],values[1]);viewer_->setScaleIndicator(QPointF(indicator[0],indicator[1]));
        if(changed)requestPreview();return true;
    }catch(const std::exception& error){
        const auto id=g.id;const auto frame=g.frame;cancelScaleDrag();
        logScaleDragFailure("scale_drag_preview",id,frame,error);return false;
    }
}
void MainWindow::cancelScaleDrag()
{
    if(!scale_drag_)return;
    const bool submitted=scale_drag_->submitted;scale_drag_.reset();
    if(viewer_){viewer_->cancelScaleDrag();viewer_->setScaleIndicator({});}
    syncTransformInspector();updateHistoryActions();if(submitted)requestPreview();
}
void MainWindow::commitScaleDrag()
{
    if(!scale_drag_)return;
    const auto g=*std::exchange(scale_drag_,std::nullopt);updateHistoryActions();
    if(viewer_)viewer_->setScaleIndicator({});
    if(panBehindAvailable()&&selected_layer_id_==g.id&&timeline_->currentFrame()==g.frame&&document_->canvasSize()==g.canvas) {
        try {
            if(g.preview!=g.initial) {
                auto before=captureEditState();
                if(document_->setLayerScaleAtFrame(g.id,g.preview[0],g.preview[1],g.frame)) {
                    (void)recordCompositionEdit(std::move(before));refreshTimeline();updateDocumentState();
                }
            }
        }catch(const std::exception& error){logScaleDragFailure("scale_drag_commit",g.id,g.frame,error);}
    }
    syncTransformInspector();if(g.submitted||g.preview!=g.initial)requestPreview();
}
void MainWindow::logScaleDragFailure(const char* operation,model::LayerId id,std::int64_t frame,const std::exception& error)
{
    creative_suite::diagnostics::Logger::instance().log(creative_suite::diagnostics::Level::Error,
        "motion_document",operation,error.what(),{{"layer_id",std::to_string(id)},{"frame",std::to_string(frame)}});
    statusBar()->showMessage(MotionUiTranslation::tr("The layer could not be scaled. Check the Motion Studio log for details."),5000);
}
}
