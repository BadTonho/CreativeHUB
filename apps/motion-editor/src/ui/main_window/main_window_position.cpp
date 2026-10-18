#include "main_window.h"
#include "model/layer_position.h"
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
bool MainWindow::beginPositionDrag()
{
    finishPendingTransformEdit();
    if (!panBehindAvailable()) return false;
    const auto id=selected_layer_id_;const auto frame=timeline_->currentFrame();
    timeline_->setCurrentFrame(frame);
    try {
        auto request=buildPreviewRequest();
        const auto hierarchy=request.sceneHierarchy();
        if(!hierarchy)return false;
        const auto& layer=*std::find_if(document_->layers().begin(),document_->layers().end(),[id](const auto& l){return l.id==id;});
        const auto local=frame-layer.timeline_start_frame;
        const auto initial=creative_suite::animation::evaluateTransform(layer.transform,layer.keyframes,local);
        if(!model::validLayerPosition(initial.position_x,initial.position_y))return false;
        const auto resolved=hierarchy->evaluate(frame);
        const auto parent=layer.parent_id?resolved.at(layer.parent_id).world:model::AffineTransform{};
        auto outer=creative_suite::composition::composeAffine(parent,
            layer.parent_affine.value_or(model::affineOffset(layer.parent_offset)));
        outer.tx=outer.ty=0;
        const auto inverse=creative_suite::composition::requireInverseAffine(outer);
        const auto pivot=model::resolvedAnchorPosition(resolved.at(id),model::evaluateLayerAnchor(layer,local),document_->canvasSize());
        const std::array values{initial.position_x,initial.position_y};
        position_drag_=PositionDragGesture{id,frame,document_->canvasSize(),values,values,inverse,
            QPointF(pivot[0],pivot[1]),std::make_shared<const PreviewRequest>(std::move(request)),++viewer_gesture_session_};
        inspector_->previewLayerPosition(values[0],values[1]);updateHistoryActions();return true;
    }catch(const std::exception& error){logPositionDragFailure("position_drag_begin",id,frame,error);return false;}
}
bool MainWindow::previewPositionDrag(const QPointF& delta)
{
    if(!position_drag_)return false;
    const auto& g=*position_drag_;
    if(!panBehindAvailable()||selected_layer_id_!=g.id||timeline_->currentFrame()!=g.frame||document_->canvasSize()!=g.canvas) {
        cancelPositionDrag();return false;
    }
    try {
        const auto values=model::positionFromCanvasDelta(g.initial[0],g.initial[1],g.canvas,g.inverse_linear,delta.x(),delta.y());
        if(!model::validLayerPosition(values[0],values[1])){cancelPositionDrag();return false;}
        const bool changed=values!=g.preview;
        position_drag_->preview=values;
        inspector_->previewLayerPosition(values[0],values[1]);
        viewer_->setSelectedLayerAnchor(g.initial_pivot+QPointF(delta.x()/g.canvas.width,delta.y()/g.canvas.height));
        if(changed)requestPreview();return true;
    }catch(const std::exception& error) {
        const auto id=g.id;const auto frame=g.frame;cancelPositionDrag();
        logPositionDragFailure("position_drag_preview",id,frame,error);return false;
    }
}
void MainWindow::cancelPositionDrag()
{
    if(!position_drag_)return;
    const bool submitted=position_drag_->submitted;
    position_drag_.reset();
    if(viewer_)viewer_->cancelPositionDrag();
    syncTransformInspector();updateHistoryActions();
    if(submitted)requestPreview();
}
void MainWindow::commitPositionDrag()
{
    if(!position_drag_)return;
    const auto g=*std::exchange(position_drag_,std::nullopt);
    updateHistoryActions();
    if(panBehindAvailable()&&selected_layer_id_==g.id&&timeline_->currentFrame()==g.frame&&document_->canvasSize()==g.canvas) {
        try {
            if(g.preview!=g.initial) {
                auto before=captureEditState();
                if(document_->setLayerPositionAtFrame(g.id,g.preview[0],g.preview[1],g.frame)) {
                    (void)recordCompositionEdit(std::move(before));refreshTimeline();updateDocumentState();
                }
            }
        }catch(const std::exception& error){logPositionDragFailure("position_drag_commit",g.id,g.frame,error);}
    }
    syncTransformInspector();
    if(g.submitted||g.preview!=g.initial)requestPreview();
}
void MainWindow::logPositionDragFailure(const char* operation,model::LayerId id,std::int64_t frame,const std::exception& error)
{
    creative_suite::diagnostics::Logger::instance().log(creative_suite::diagnostics::Level::Error,
        "motion_document",operation,error.what(),{{"layer_id",std::to_string(id)},{"frame",std::to_string(frame)}});
    statusBar()->showMessage(MotionUiTranslation::tr("The layer could not be moved. Check the Motion Studio log for details."),5000);
}
}
