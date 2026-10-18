#include "main_window.h"
#include "ui/translation_helpers.h"
#include "ui/inspector/inspector_widget.h"
#include "ui/timeline/timeline_navigator.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>

#include <algorithm>
#include <stdexcept>

namespace motion::ui {
namespace a=creative_suite::animation;
namespace {
bool keyAt(const a::ScalarKeyframes& keys,std::int64_t frame)
{ return std::any_of(keys.begin(),keys.end(),[frame](const auto& key){return key.frame==frame;}); }
}

void MainWindow::editSelectedLayerAnchor(std::size_t axis)
{
    if(!document_ || !timeline_ || axis>=2 || !selected_layer_id_)return;
    const auto value=inspector_->anchorFieldValue(axis);
    const auto identity=std::pair{selected_layer_id_,axis+5};
    const bool coalescing=active_transform_edit_==identity;
    if(!coalescing)finishPendingTransformEdit();
    try {
        auto found=std::find_if(document_->layers().begin(),document_->layers().end(),[this](const auto& l){return l.id==selected_layer_id_;});
        if(found==document_->layers().end())return;
        const auto local=timeline_->currentFrame()-found->timeline_start_frame;
        const auto& keys=found->anchor_keyframes[axis];
        std::optional<CompositionEditState> before;
        if(!coalescing)before=captureEditState();
        bool changed=false;
        if(keys.empty()) {
            auto point=found->anchor_point;
            (axis==0?point.x_pixels:point.y_pixels)=value;
            changed=document_->setLayerAnchorPoint(selected_layer_id_,point);
        } else if(local>=0 && local<found->duration_frames && keyAt(keys,local))
            changed=document_->setLayerAnchorKeyframe(selected_layer_id_,static_cast<model::AnchorProperty>(axis),local,value);
        if(changed) {
            if(!coalescing){composition_history_.beginCoalescedEdit(std::move(*before));active_transform_edit_=identity;updateHistoryActions();}
            updateDocumentState();refreshTimeline();requestPreview();
        }
    } catch(const std::exception& error){reportDocumentError("edit_anchor",document_path_.value_or(std::filesystem::path{}),error);}
    syncTransformInspector();
}

void MainWindow::toggleSelectedAnchorKeyframe(std::size_t axis)
{
    finishPendingTransformEdit();
    if(!document_ || !timeline_ || axis>=2)return;
    const auto found=std::find_if(document_->layers().begin(),document_->layers().end(),[this](const auto& l){return l.id==selected_layer_id_;});
    if(found==document_->layers().end())return;
    const auto local=timeline_->currentFrame()-found->timeline_start_frame;
    if(local<0 || local>=found->duration_frames)return;
    try {
        auto before=captureEditState();
        const auto property=static_cast<model::AnchorProperty>(axis);
        const auto point=model::evaluateLayerAnchor(*found,local);
        const bool changed=keyAt(found->anchor_keyframes[axis],local)
            ?document_->removeLayerAnchorKeyframe(selected_layer_id_,property,local)
            :document_->setLayerAnchorKeyframe(selected_layer_id_,property,local,axis==0?point.x_pixels:point.y_pixels);
        if(!changed)return;
        (void)recordCompositionEdit(std::move(before));
        timeline_->setLayerExpanded(selected_layer_id_,true);timeline_->setTransformGroupExpanded(selected_layer_id_,true);
        updateDocumentState();refreshTimeline();syncTransformInspector();requestPreview();
    }catch(const std::exception& error){reportDocumentError("toggle_anchor_keyframe",document_path_.value_or(std::filesystem::path{}),error);}
}

void MainWindow::repositionSelectedAnchor()
{
    finishPendingTransformEdit();
    if(!document_ || !timeline_)return;
    const auto id=selected_layer_id_;const auto frame=timeline_->currentFrame();
    timeline_->setCurrentFrame(frame); // Existing seek operation pauses playback.
    const auto found=std::find_if(document_->layers().begin(),document_->layers().end(),[id](const auto& l){return l.id==id;});
    if(found==document_->layers().end() || frame<found->timeline_start_frame || frame-found->timeline_start_frame>=found->duration_frames)return;
    const auto point=model::evaluateLayerAnchor(*found,frame-found->timeline_start_frame);
    QDialog dialog(this);dialog.setObjectName(QStringLiteral("motion-reposition-anchor-dialog"));
    dialog.setWindowTitle(MotionUiTranslation::tr("Reposition Anchor Without Moving"));
    auto* layout=new QVBoxLayout(&dialog);
    auto* help=new QLabel(MotionUiTranslation::tr("Keeps the layer and its children in place at the current frame. Animated affected properties receive keyframes at this frame."),&dialog);
    help->setWordWrap(true);layout->addWidget(help);
    auto* form=new QFormLayout;
    std::array<QDoubleSpinBox*,2> fields{};
    std::array<double,2> initial_values{};
    for(std::size_t axis=0;axis<2;++axis){auto* field=new QDoubleSpinBox(&dialog);field->setDecimals(6);field->setRange(-1e9,1e9);field->setKeyboardTracking(false);
        field->setObjectName(axis==0?QStringLiteral("motion-reposition-anchor-x"):QStringLiteral("motion-reposition-anchor-y"));
        field->setValue(axis==0?point.x_pixels:point.y_pixels);fields[axis]=field;initial_values[axis]=field->value();
        form->addRow(axis==0?MotionUiTranslation::tr("Anchor X (px)"):MotionUiTranslation::tr("Anchor Y (px)"),field);}
    layout->addLayout(form);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);
    buttons->setObjectName(QStringLiteral("motion-reposition-anchor-buttons"));layout->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if(dialog.exec()!=QDialog::Accepted)return;
    // Keep untouched axes exact despite display rounding or UI range limits.
    const model::AnchorPoint requested{
        fields[0]->value()==initial_values[0]?point.x_pixels:fields[0]->value(),
        fields[1]->value()==initial_values[1]?point.y_pixels:fields[1]->value()};
    try {
        commitAnchorReposition(id,requested,frame);
    }catch(const std::exception& error){reportDocumentError("reposition_anchor",document_path_.value_or(std::filesystem::path{}),
        std::runtime_error("Layer "+std::to_string(id)+", frame "+std::to_string(frame)+": "+error.what()));}
}

void MainWindow::selectAnchorCurveSegment(model::LayerId id,model::AnchorProperty property,std::int64_t frame)
{
    if(!document_ || static_cast<std::size_t>(property)>=2)return;
    const auto found=std::find_if(document_->layers().begin(),document_->layers().end(),[id](const auto& l){return l.id==id;});
    if(found==document_->layers().end())return;
    const auto& keys=found->anchor_keyframes[static_cast<std::size_t>(property)];
    std::int64_t segment=0;
    if(keys.size()>=2){auto upper=std::upper_bound(keys.begin(),keys.end(),frame,[](auto f,const auto& k){return f<k.frame;});
        const auto index=upper==keys.begin()?std::size_t{0}:std::min(static_cast<std::size_t>(upper-keys.begin()-1),keys.size()-2);segment=keys[index].frame;}
    CurveSelection selection;selection.layer_id=id;selection.segment_start_frame=segment;selection.anchor_property=property;
    curve_selection_=selection;refreshCurveEditor();
}

bool MainWindow::applySelectedCurveInterpolation(std::int64_t frame,a::InterpolationMode mode,const a::CubicBezierEasing& easing)
{
    if(!document_ || !curve_selection_)return false;
    const auto& s=*curve_selection_;
    if(s.scale_axis) {
        try{return document_->setLayerScaleAxisKeyframeInterpolation(s.layer_id,*s.scale_axis,frame,mode,easing);}
        catch(const std::exception& error){reportDocumentError("edit_scale_interpolation",document_path_.value_or(std::filesystem::path{}),error);return false;}
    }
    if(s.anchor_property) {
        try {return document_->setLayerAnchorKeyframeInterpolation(s.layer_id,*s.anchor_property,frame,mode,easing);}
        catch(const std::exception& error){reportDocumentError("edit_anchor_interpolation",document_path_.value_or(std::filesystem::path{}),error);return false;}
    }
    if(s.mask_id)return document_->setMaskKeyframeInterpolation(s.mask_id,s.mask_property,frame,mode,easing);
    if(s.effect_index)return document_->setEffectKeyframeInterpolation(s.layer_id,*s.effect_index,s.effect_property,frame,mode,easing);
    return document_->setLayerKeyframeInterpolation(s.layer_id,s.property,frame,mode,easing);
}
} // namespace motion::ui
