#include "ui/viewer/pen_tool.h"
#include "ui/inspector/trim_paths_inspector.h"
#include "ui/translation_helpers.h"
#include "ui/inspector/mask_inspector_widget.h"
#include "main_window.h"
#include "main_window_support.h"

#include "ui/viewer/composition_viewer.h"
#include "ui/inspector/inspector_widget.h"
#include "ui/media_pool/media_pool_widget.h"
#include "ui/timeline/graph_editor/property_curve_editor.h"
#include "rendering/preview_renderer.h"
#include "ui/timeline/timeline_navigator.h"
#include "ui/workspace/motion_workspace.h"

#include <creative_suite/diagnostics/logger.h>
#include <creative_suite/composition/opengl_frame_compositor.h>
#include <creative_suite/media/media_importer.h>
#include <creative_suite/media/media_library.h>
#include "settings/gpu_composition_preferences.h"
#include <QOpenGLContext>

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QOffscreenSurface>
#include <QProgressDialog>
#include <QPushButton>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <filesystem>
#include <utility>
#include <vector>

namespace motion::ui {
using creative_suite::animation::CubicBezierEasing;
using creative_suite::animation::TransformProperty;
using detail::pathFromQString;

void MainWindow::createWorkspace()
{
    const bool was_maximized = isMaximized();
    const bool was_full_screen = isFullScreen();
    const auto previous_geometry = geometry();

    media_pool_ = new MediaPoolWidget(this);
    media_pool_->setMinimumWidth(220);
    media_pool_->setImportRequestedHandler([this] { openMedia(); });
    media_pool_->setSelectionChangedHandler([this] { updateMediaDetails(); });
    media_pool_->setContentChangedHandler([this] { finishPendingTransformEdit(); updateDocumentState(); });
    viewer_ = new CompositionViewer(this);
    viewer_->setObjectName(QStringLiteral("motion-composition-viewer"));
    viewer_->setScaleDragHandlers({[this](model::ScaleDragControl control,QPointF lever) { return beginScaleDrag(control,lever); },
        [this](QPointF delta) { return previewScaleDrag(delta); },
        [this] { commitScaleDrag(); },[this] { cancelScaleDrag(); }});
    viewer_->setRotationDragHandlers({[this](QPointF press,double scale) { return beginRotationDrag(press,scale); },
        [this](QPointF pointer) { return previewRotationDrag(pointer); },
        [this] { commitRotationDrag(); },[this] { cancelRotationDrag(); }});
    viewer_->setPositionDragHandlers({[this] { return beginPositionDrag(); },
        [this](QPointF delta) { return previewPositionDrag(delta); },
        [this] { commitPositionDrag(); },[this] { cancelPositionDrag(); }});
    viewer_->setPanBehindHandlers([this] { return beginPanBehind(); },
        [this](const QPointF& delta) { return previewPanBehind(delta); },
        [this] { commitPanBehind(); }, [this] { cancelPanBehind(); });
    viewer_->setLayerContextMenuHandler([this](const QPoint& global_position) {
        if (!document_ || selected_layer_id_ == 0) return;
        const auto selected = std::find_if(
            document_->layers().begin(), document_->layers().end(), [this](const auto& layer) {
                return layer.id == selected_layer_id_;
            });
        if (selected != document_->layers().end() &&
            selected->kind == model::LayerKind::Image) {
            showLayerContextMenu(selected->id, global_position);
        }
    });
    inspector_ = new InspectorWidget(this);
    trim_paths_inspector_=new TrimPathsInspector(inspector_);
    inspector_->addTab(trim_paths_inspector_,MotionUiTranslation::tr("Trim Paths"));
    trim_paths_inspector_->setHandlers([this](auto edit){editTrimPaths(std::move(edit));},
        [this](model::LayerId id,model::TrimPathProperty property,std::int64_t frame){selectTrimCurveSegment(id,property,frame);});
    inspector_->masksInspector()->setContextChangedHandler([this]{finishPendingTransformEdit();if(pen_tool_)pen_tool_->sync();});
    connect(inspector_,&InspectorWidget::scaleValueEdited,this,[this](int axis){editSelectedScaleAxis(static_cast<std::size_t>(axis));});
    connect(inspector_,&InspectorWidget::scaleKeyframeToggleRequested,this,[this](int axis){toggleSelectedScaleAxisKeyframe(static_cast<std::size_t>(axis));});
    connect(inspector_,&InspectorWidget::scaleLinkChanged,this,&MainWindow::setSelectedScaleLinked);
    connect(inspector_,&InspectorWidget::anchorValueEdited,this,[this](int axis){editSelectedLayerAnchor(static_cast<std::size_t>(axis));});
    connect(inspector_,&InspectorWidget::anchorKeyframeToggleRequested,this,[this](int axis){toggleSelectedAnchorKeyframe(static_cast<std::size_t>(axis));});
    connect(inspector_,&InspectorWidget::anchorRepositionRequested,this,&MainWindow::repositionSelectedAnchor);
    connect(inspector_, &InspectorWidget::parentLayerChanged, this,
            [this](qulonglong parent) { changeSelectedLayerParent(static_cast<model::LayerId>(parent)); });
    inspector_->masksInspector()->setHandlers(
        [this](MaskInspectorWidget::Edit edit) {
            finishPendingTransformEdit();if(!document_)return;
            try {
                auto before=captureEditState();
                if(edit(*document_)){(void)recordCompositionEdit(std::move(before));updateDocumentState();refreshTimeline();}
                syncTransformInspector();refreshCurveEditor();requestPreview();
            } catch(const std::exception& e){reportDocumentError("edit_mask",document_path_.value_or(std::filesystem::path{}),e);}
        },
        [this](model::LayerId layer,int target,bool enabled){finishPendingTransformEdit();mask_view_layer_=enabled?layer:0;mask_view_target_=target;requestPreview();},
        [this](model::MaskId mask,std::size_t property,std::int64_t frame){
            curve_selection_=CurveSelection{selected_layer_id_,creative_suite::animation::TransformProperty::PositionX,frame,mask,property};refreshCurveEditor();
        });
    connect(inspector_, &InspectorWidget::layerContentEdited,
            this, &MainWindow::editSelectedLayerContent);
    connect(inspector_, &InspectorWidget::layerContentEditFinished,
            this, &MainWindow::finishPendingContentEdit);
    connect(inspector_, &InspectorWidget::layerColorSelectionRequested,
            this, &MainWindow::chooseSelectedLayerColor);
    connect(inspector_, &InspectorWidget::blendModeChanged,this,&MainWindow::changeSelectedLayerBlendMode);
    connect(inspector_, &InspectorWidget::trackMatteChanged,this,&MainWindow::changeSelectedLayerTrackMatte);
    connect(inspector_, &InspectorWidget::transformValueEdited, this,
            [this](int index) { editSelectedLayerTransform(static_cast<std::size_t>(index)); });
    connect(inspector_, &InspectorWidget::transformEditFinished,
            this, &MainWindow::finishPendingTransformEdit);
    connect(inspector_, &InspectorWidget::keyframeToggleRequested, this,
            [this](int index) { toggleSelectedLayerKeyframe(static_cast<std::size_t>(index)); });
    connect(inspector_, &InspectorWidget::effectAddRequested,
            this, &MainWindow::addLayerEffect);
    connect(inspector_, &InspectorWidget::effectMoveRequested,
            this, &MainWindow::moveSelectedEffect);
    connect(inspector_, &InspectorWidget::effectRemoveRequested,
            this, &MainWindow::removeSelectedEffect);
    connect(inspector_, &InspectorWidget::effectRowSelected,
            this, &MainWindow::selectEffectRow);
    connect(inspector_,&InspectorWidget::shadowValueEdited,this,&MainWindow::editSelectedShadowValue);
    connect(inspector_,&InspectorWidget::shadowColorRequested,this,&MainWindow::chooseShadowColor);
    connect(inspector_,&InspectorWidget::glowColorRequested,this,&MainWindow::chooseGlowColor);
    connect(inspector_, &InspectorWidget::effectParametersEdited,
            this, &MainWindow::editSelectedEffectParameters);
    connect(inspector_, &InspectorWidget::effectParametersEditFinished,
            this, &MainWindow::finishPendingEffectEdit);
    connect(inspector_, &InspectorWidget::effectKeyframeToggleRequested,
            this, &MainWindow::toggleSelectedEffectKeyframe);
    connect(inspector_, &InspectorWidget::effectReorderRequested, this,
            [this](qulonglong source, int insertion) {
                reorderEffectsFromList(static_cast<std::size_t>(source), insertion);
            });
    connect(inspector_, &InspectorWidget::effectEnabledChanged, this,
            [this](int row, bool enabled) {
                if (!document_ || selected_layer_id_ == 0 || row < 0) return;
                const auto found = std::find_if(document_->layers().begin(),
                    document_->layers().end(), [this](const auto& layer) {
                        return layer.id == selected_layer_id_;
                    });
                if (found == document_->layers().end() ||
                    static_cast<std::size_t>(row) >= found->effects.size()) return;
                auto effects = found->effects;
                std::visit([enabled](auto& effect) { effect.enabled = enabled; },
                           effects[static_cast<std::size_t>(row)]);
                applySelectedEffectStack(std::move(effects));
            });
    timeline_ = new TimelineNavigator(this);
    timeline_->setTrimKeyframeHandlers([this](model::LayerId id,model::TrimPathProperty property,std::int64_t frame){selectLayer(id);selectTrimCurveSegment(id,property,frame);},
        [this](model::LayerId id,model::TrimPathProperty property,std::int64_t from,std::int64_t to){
            bool changed=false;editTrimPaths([&](auto& doc){changed=doc.moveLayerTrimKeyframe(id,property,from,to);return changed;});
            if(changed)selectTrimCurveSegment(id,property,to);return changed;});
    timeline_->setParentPickWhipHandlers({
        [this](model::LayerId child) { return beginParentPickWhip(child); },
        [this](model::LayerId child,model::LayerId parent) { commitParentPickWhip(child,parent); },
        [this] { cancelParentPickWhip(); }});
    // These run before TimelineNavigator's action handlers, including a seek
    // that stays at the same frame and therefore emits no frame-change signal.
    for (auto* action : {play_pause_action_, previous_frame_action_, next_frame_action_})
        connect(action, &QAction::triggered, this, [this] { finishPendingTransformEdit(); });
    timeline_->setShortcutActions(
        play_pause_action_, previous_frame_action_, next_frame_action_, loop_action_,
        zoom_in_action_, zoom_out_action_);
    timeline_->setMediaDropHandler([this](const std::filesystem::path& path,
                                          std::int64_t frame,
                                          model::LayerId before) {
        handleMediaDrop(path, frame, before);
    });
    timeline_->setLayerSelectedHandler([this](model::LayerId id) { selectLayer(id); });
    timeline_->setLayerContextMenuHandler(
        [this](model::LayerId id, const QPoint& global_position) {
            showLayerContextMenu(id, global_position);
        });
    timeline_->setKeyframeSelectedHandler(
        [this](model::LayerId id, TransformProperty property, std::int64_t local_frame) {
            if (!document_ || timeline_ == nullptr) return;
            const auto found = std::find_if(document_->layers().begin(), document_->layers().end(),
                [id](const auto& layer) { return layer.id == id; });
            if (found == document_->layers().end() || local_frame < 0 ||
                local_frame >= found->duration_frames) return;
            selectLayer(id);
            selectCurveSegment(id, property, local_frame);
            const auto composition_frame = found->timeline_start_frame + local_frame;
            timeline_->setCurrentFrame(composition_frame);
        });
    timeline_->setCurveSegmentSelectedHandler(
        [this](model::LayerId id, TransformProperty property, std::int64_t local_frame) {
            selectLayer(id);
            selectCurveSegment(id, property, local_frame);
        });
    timeline_->setKeyframeMoveHandler(
        [this](model::LayerId id, TransformProperty property,
               std::int64_t from_local_frame, std::int64_t to_local_frame) {
            finishPendingTransformEdit();
            if (!document_) return false;
            auto before = captureEditState();
            if (!document_ || !document_->moveLayerKeyframe(
                    id, property, from_local_frame, to_local_frame)) {
                if (statusBar() != nullptr) {
                    statusBar()->showMessage(
                        MotionUiTranslation::tr("A keyframe already exists at that frame."), 4000);
                }
                return false;
            }
            (void)recordCompositionEdit(std::move(before));
            updateDocumentState();
            refreshTimeline();
            syncTransformInspector();
            requestPreview();
            return true;
        });
    timeline_->setAnchorKeyframeSelectedHandler([this](model::LayerId id,model::AnchorProperty property,std::int64_t frame) {
        if(!document_ || !timeline_)return;
        const auto found=std::find_if(document_->layers().begin(),document_->layers().end(),[id](const auto& l){return l.id==id;});
        if(found==document_->layers().end() || frame<0 || frame>=found->duration_frames)return;
        const auto composition_frame=found->timeline_start_frame+frame;
        selectLayer(id);selectAnchorCurveSegment(id,property,frame);timeline_->setCurrentFrame(composition_frame);
    });
    timeline_->setAnchorCurveSegmentSelectedHandler([this](model::LayerId id,model::AnchorProperty property,std::int64_t frame) {
        selectLayer(id);selectAnchorCurveSegment(id,property,frame);
    });
    timeline_->setAnchorKeyframeMoveHandler([this](model::LayerId id,model::AnchorProperty property,std::int64_t from,std::int64_t to) {
        finishPendingTransformEdit();if(!document_)return false;
        try {
            auto before=captureEditState();
            if(!document_->moveLayerAnchorKeyframe(id,property,from,to))return false;
            (void)recordCompositionEdit(std::move(before));
            selectAnchorCurveSegment(id,property,to);
            updateDocumentState();refreshTimeline();syncTransformInspector();requestPreview();return true;
        }catch(const std::exception& error){reportDocumentError("move_anchor_keyframe",document_path_.value_or(std::filesystem::path{}),error);return false;}
    });
    timeline_->setScaleAxisKeyframeSelectedHandler([this](model::LayerId id,model::ScaleAxis property,std::int64_t frame) {
        if(!document_ || !timeline_)return;
        const auto found=std::find_if(document_->layers().begin(),document_->layers().end(),[id](const auto& l){return l.id==id;});
        if(found==document_->layers().end() || frame<0 || frame>=found->duration_frames)return;
        const auto composition_frame=found->timeline_start_frame+frame;
        selectLayer(id);selectScaleAxisCurveSegment(id,property,frame);timeline_->setCurrentFrame(composition_frame);
    });
    timeline_->setScaleAxisCurveSegmentSelectedHandler([this](model::LayerId id,model::ScaleAxis property,std::int64_t frame) {
        selectLayer(id);selectScaleAxisCurveSegment(id,property,frame);
    });
    timeline_->setScaleAxisKeyframeMoveHandler([this](model::LayerId id,model::ScaleAxis property,std::int64_t from,std::int64_t to) {
        finishPendingTransformEdit();if(!document_)return false;
        try {
            auto before=captureEditState();
            if(!document_->moveLayerScaleAxisKeyframe(id,property,from,to))return false;
            (void)recordCompositionEdit(std::move(before));
            selectScaleAxisCurveSegment(id,property,to);
            updateDocumentState();refreshTimeline();syncTransformInspector();requestPreview();return true;
        }catch(const std::exception& error){reportDocumentError("move_scale_keyframe",document_path_.value_or(std::filesystem::path{}),error);return false;}
    });
    timeline_->setEffectKeyframeSelectedHandler(
        [this](model::LayerId id,
               std::size_t effect_index,
               model::EffectParameterProperty property,
               std::int64_t local_frame) {
            if (!document_ || timeline_ == nullptr) return;
            const auto found = std::find_if(document_->layers().begin(), document_->layers().end(),
                [id](const auto& layer) { return layer.id == id; });
            if (found == document_->layers().end() || local_frame < 0 ||
                local_frame >= found->duration_frames) return;
            selectLayer(id);
            selectEffectRow(static_cast<int>(effect_index));
            selectEffectCurveSegment(id, effect_index, property, local_frame);
            const auto composition_frame = found->timeline_start_frame + local_frame;
            timeline_->setCurrentFrame(composition_frame);
        });
    timeline_->setEffectCurveSegmentSelectedHandler(
        [this](model::LayerId id,
               std::size_t effect_index,
               model::EffectParameterProperty property,
               std::int64_t local_frame) {
            selectLayer(id);
            selectEffectRow(static_cast<int>(effect_index));
            selectEffectCurveSegment(id, effect_index, property, local_frame);
        });
    timeline_->setEffectKeyframeMoveHandler(
        [this](model::LayerId id,
               std::size_t effect_index,
               model::EffectParameterProperty property,
               std::int64_t from_local_frame,
               std::int64_t to_local_frame) {
            finishPendingTransformEdit();
            if (!document_ || export_worker_ || audio_keyframe_worker_) return false;
            auto before = captureEditState();
            if (!document_ || !document_->moveEffectKeyframe(
                    id, effect_index, property, from_local_frame, to_local_frame)) {
                if (statusBar() != nullptr) {
                    statusBar()->showMessage(
                        MotionUiTranslation::tr("A keyframe already exists at that frame."), 4000);
                }
                return false;
            }
            (void)recordCompositionEdit(std::move(before));
            updateDocumentState();
            refreshTimeline();
            syncTransformInspector();
            requestPreview();
            return true;
        });
    timeline_->setLayerMoveHandler([this](model::LayerId id, std::int64_t frame) {
        finishPendingTransformEdit();
        if (!document_) return;
        auto before = captureEditState();
        if (document_ && document_->moveLayerInTimeline(id, frame)) {
            (void)recordCompositionEdit(std::move(before));
            updateDocumentState();
            refreshTimeline();
            requestPreview();
        }
    });
    timeline_->setLayerResizeHandler([this](model::LayerId id, std::int64_t duration) {
        finishPendingTransformEdit();
        if (!document_) return;
        auto before = captureEditState();
        if (document_ && document_->resizeLayerDuration(id, duration)) {
            (void)recordCompositionEdit(std::move(before));
            updateDocumentState();
            refreshTimeline();
            requestPreview();
        }
    });
    timeline_->setLayerReorderHandler([this](model::LayerId id, std::size_t front_index) {
        finishPendingTransformEdit();
        if (!document_ || front_index >= document_->layers().size()) return;
        auto before = captureEditState();
        const auto model_index = document_->layers().size() - 1 - front_index;
        if (document_->moveLayer(id, model_index)) {
            (void)recordCompositionEdit(std::move(before));
            updateDocumentState();
            refreshTimeline();
            requestPreview();
        }
    });
    timeline_->setLayerVisibilityHandler([this](model::LayerId id, bool visible) {
        finishPendingTransformEdit();
        if (!document_) return;
        auto before = captureEditState();
        if (document_ && document_->setLayerVisible(id, visible)) {
            (void)recordCompositionEdit(std::move(before));
            updateDocumentState();
            refreshTimeline();
            if (selected_layer_id_ == id) syncTransformInspector();
            requestPreview();
        }
    });
    timeline_->setLayerRemoveHandler([this](model::LayerId id) {
        removeCompositionLayer(id);
    });
    connect(timeline_, &TimelineNavigator::currentFrameChanged,
            this, [this] {
                finishPendingTransformEdit();
                syncTransformInspector();
                requestPreview(timeline_ != nullptr && timeline_->isPlaying());
            });
    curve_editor_panel_ = new QWidget(this);
    curve_editor_panel_->setObjectName(QStringLiteral("motion-graph-editor-panel"));
    curve_editor_panel_->setMinimumHeight(168);
    auto* curve_panel_layout = new QVBoxLayout(curve_editor_panel_);
    curve_panel_layout->setContentsMargins(6, 4, 6, 4);
    curve_panel_layout->setSpacing(3);
    auto* curve_panel_controls = new QHBoxLayout();
    auto* curve_panel_title = new QLabel(MotionUiTranslation::tr("Graph Editor"), curve_editor_panel_);
    curve_panel_title->setObjectName(QStringLiteral("motion-graph-editor-title"));
    curve_panel_controls->addWidget(curve_panel_title);
    curve_panel_controls->addStretch(1);
    curve_custom_label_ = new QLabel(MotionUiTranslation::tr("Custom Bézier"), curve_editor_panel_);
    curve_custom_label_->setObjectName(QStringLiteral("motion-curve-editor-custom-status"));
    curve_custom_label_->hide();
    curve_panel_controls->addWidget(curve_custom_label_);
    curve_preset_combo_ = new QComboBox(curve_editor_panel_);
    curve_preset_combo_->setObjectName(QStringLiteral("motion-curve-editor-preset"));
    curve_preset_combo_->addItem(MotionUiTranslation::tr("Linear"));
    curve_preset_combo_->addItem(MotionUiTranslation::tr("Ease In"));
    curve_preset_combo_->addItem(MotionUiTranslation::tr("Ease Out"));
    curve_preset_combo_->addItem(MotionUiTranslation::tr("Ease In/Out"));
    curve_preset_combo_->setToolTip(
        MotionUiTranslation::tr("Set interpolation for the selected keyframe segment"));
    curve_panel_controls->addWidget(curve_preset_combo_);
    curve_panel_layout->addLayout(curve_panel_controls);
    curve_editor_ = new PropertyCurveEditor(curve_editor_panel_);
    curve_editor_->setSelectionHandler([this](std::int64_t local_start_frame) {
        if (!curve_selection_) return;
        curve_selection_->segment_start_frame = local_start_frame;
        refreshCurveEditor();
    });
    curve_editor_->setEasingEditedHandler(
        [this](std::int64_t local_start_frame, CubicBezierEasing easing) {
            if (!curve_selection_) return;
            curve_selection_->segment_start_frame = local_start_frame;
            applyCurveEasing(local_start_frame, easing);
        });
    connect(curve_preset_combo_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int index) { applyCurvePreset(index); });
    curve_panel_layout->addWidget(curve_editor_, 1);

    pen_tool_=new PenTool(*this);
    timeline_->setPathKeyframeHandlers([this](model::LayerId layer,model::PathTarget target,std::int64_t frame){selectLayer(layer);pen_tool_->selectCurve(target,frame);},
        [this](model::LayerId layer,model::PathTarget target,std::int64_t from,std::int64_t to){finishPendingTransformEdit();if(!document_)return false;try{auto before=captureEditState();if(!document_->movePathKeyframe(target,from,to))return false;(void)recordCompositionEdit(std::move(before));selectLayer(layer);pen_tool_->selectCurve(target,to);updateDocumentState();refreshTimeline();syncTransformInspector();requestPreview();return true;}catch(const std::exception& e){reportDocumentError("move_path_keyframe",document_path_.value_or(std::filesystem::path{}),e);return false;}});
    auto* viewer_container=new QWidget(this);auto* viewer_layout=new QVBoxLayout(viewer_container);viewer_layout->setContentsMargins(0,0,0,0);viewer_layout->addWidget(pen_tool_->controls());viewer_layout->addWidget(viewer_,1);
    workspace_ = new MotionWorkspace(
        this, viewer_container, media_pool_, inspector_, timeline_, curve_editor_panel_, timeline_);
    media_pool_dock_ = workspace_->mediaPoolDock();
    inspector_dock_ = workspace_->inspectorDock();
    timeline_dock_ = workspace_->timelineDock();
    graph_editor_dock_ = workspace_->graphEditorDock();
    workspace_->setGraphEditorRefreshHandler([this] { refreshCurveEditor(); });

    const auto add_panel_action = [this](QDockWidget* dock,
                                         const QString& action_object_name) {
        auto* action = dock->toggleViewAction();
        action->setObjectName(action_object_name);
        view_menu_->addAction(action);
    };
    add_panel_action(media_pool_dock_, QStringLiteral("motion-view-media-pool-action"));
    add_panel_action(inspector_dock_, QStringLiteral("motion-view-inspector-action"));
    add_panel_action(timeline_dock_, QStringLiteral("motion-view-timeline-action"));
    add_panel_action(graph_editor_dock_, QStringLiteral("motion-view-graph-editor-action"));
    connect(graph_editor_dock_->toggleViewAction(), &QAction::triggered,
            this, [this](bool visible) {
                if (visible && graph_editor_dock_ != nullptr) {
                    graph_editor_dock_->show();
                    graph_editor_dock_->raise();
                    timeline_->setGraphEditorOpen(true);
                    refreshCurveEditor();
                }
            });
    view_menu_->addSeparator();
    reset_panel_layout_action_->setEnabled(true);
    restoreWorkspaceLayout();
    empty_state_ = nullptr;
    empty_state_new_composition_button_ = nullptr;

    bool gpu_composition_enabled = settings::gpuCompositionEnabled();
    if (gpu_composition_enabled) {
        gpu_composition_surface_ =
            creative_suite::composition::OpenGlFrameCompositor::createSurface();
        if (!gpu_composition_surface_) {
            gpu_composition_enabled = false;
            diagnostics::PerformanceMetrics::instance().recordGpuComposition(
                false, true, 0, 0, 0, 0, 0);
            creative_suite::diagnostics::Logger::instance().log(
                creative_suite::diagnostics::Level::Error,
                "motion_preview", "create_gpu_surface",
                "Could not create the OpenGL offscreen surface; using CPU composition",
                {{"setting", "CREATIVE_SUITE_MOTION_GPU_COMPOSITION=1"}});
        }
    }
    auto* share = gpu_composition_enabled ? QOpenGLContext::globalShareContext() : nullptr;
    if (share) {
        viewer_->enableTexturePresentation([this](bool use_cpu) {
            if (!preview_renderer_) return;
            preview_renderer_->recoverPresentation(use_cpu);
            requestPreview();
        });
    }
    viewer_->setFrameValidator([this](const PreviewFrame& frame) {
        return preview_renderer_ && preview_renderer_->canPresentFrame(frame);
    });
    preview_renderer_ = std::make_unique<PreviewRenderer>(this,
        PreviewRenderer::ResultHandler{}, PreviewRenderer::RenderFunction{}, nullptr,
        gpu_composition_enabled, gpu_composition_surface_.get(),
        [this](PreviewFrame frame) {
            const bool may_present = preview_renderer_ && preview_renderer_->canPresentFrame(frame) &&
                (!frame.playback || (timeline_ && timeline_->isPlaying()));
            if (may_present && viewer_) {
                if(frame.render_failed&&pen_tool_&&pen_tool_->active()&&frame.gesture_session){pen_tool_->renderFailed();return;}
                if(frame.render_failed&&scale_drag_&&frame.gesture_session==scale_drag_->session) {
                    const auto id=scale_drag_->id;const auto current=scale_drag_->frame;cancelScaleDrag();
                    logScaleDragFailure("scale_drag_render",id,current,std::runtime_error("Gesture frame rendering failed."));return;
                }
                if (frame.render_failed && rotation_drag_ && frame.gesture_session==rotation_drag_->session) {
                    const auto id=rotation_drag_->id;const auto current=rotation_drag_->frame;
                    cancelRotationDrag();
                    logRotationDragFailure("rotation_drag_render",id,current,std::runtime_error("Gesture frame rendering failed."));
                    return;
                }
                if (frame.render_failed && position_drag_ && frame.gesture_session==position_drag_->session) {
                    const auto id=position_drag_->id;const auto current=position_drag_->frame;
                    cancelPositionDrag();
                    logPositionDragFailure("position_drag_render",id,current,std::runtime_error("Gesture frame rendering failed."));
                    return;
                }
                if(frame.render_failed)statusBar()->showMessage(MotionUiTranslation::tr("The preview could not be rendered. Check the Motion Studio log for details."),5000);
                if (frame.valid()) viewer_->setPreviewFrame(std::move(frame));
                else diagnostics::PerformanceMetrics::instance().discardRequest(frame.generation);
            } else diagnostics::PerformanceMetrics::instance().recordStaleResult(frame.generation);
        }, share);
    if (!was_maximized && !was_full_screen) setGeometry(previous_geometry);
}
void MainWindow::restoreWorkspaceLayout()
{
    if (workspace_ != nullptr) workspace_->restoreLayout();
}
void MainWindow::saveWorkspaceLayout()
{
    if (workspace_ != nullptr) workspace_->saveLayout();
}
void MainWindow::restoreDefaultPanelLayout()
{
    if (workspace_ != nullptr) workspace_->restoreDefaultLayout();
}
void MainWindow::openMedia()
{
    if (!document_.has_value() || media_pool_ == nullptr) return;
    QFileDialog dialog(this, MotionUiTranslation::tr("Import Media"));
    dialog.setObjectName(QStringLiteral("motion-import-media-dialog"));
    dialog.setFileMode(QFileDialog::ExistingFiles);
    dialog.setNameFilters({
        MotionUiTranslation::tr("Supported media (*.avi *.mkv *.mov *.mp4 *.mxf *.webm *.png *.jpg *.jpeg *.bmp *.webp *.tif *.tiff)"),
        MotionUiTranslation::tr("Video files (*.avi *.mkv *.mov *.mp4 *.mxf *.webm)"),
        MotionUiTranslation::tr("Image files (*.png *.jpg *.jpeg *.bmp *.webp *.tif *.tiff)"),
        MotionUiTranslation::tr("All files (*)"),
    });
    if (dialog.exec() != QDialog::Accepted) return;
    const auto files = dialog.selectedFiles();
    std::vector<std::filesystem::path> paths;
    paths.reserve(static_cast<std::size_t>(files.size()));
    for (const auto& file : files) paths.push_back(pathFromQString(file));
    media_pool_->importFiles(std::move(paths));
}
void MainWindow::updateMediaDetails()
{
    if (media_pool_ == nullptr || inspector_ == nullptr) return;
    inspector_->setMedia(media_pool_->selectedMedia());
    inspector_->selectMediaTab();
}
void MainWindow::refreshTimeline()
{
    if (timeline_ == nullptr || !document_) return;
    timeline_->setLayers(document_->layers(),document_->masks());
    timeline_->setSelectedLayerId(selected_layer_id_);
    refreshCurveEditor();
}

} // namespace motion::ui
