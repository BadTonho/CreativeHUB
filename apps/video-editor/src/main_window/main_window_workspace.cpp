#include "main_window/main_window.h"
#include "main_window_support.h"

#include "logging/logger.h"
#include "ui/preview/preview_widget.h"
#include "project/project_file.h"
#include "settings/settings_dialog.h"
#include "settings/shortcut_manager.h"
#include "timeline/timeline_widget.h"
#include "ui/functions/function_palette.h"
#include "ui/media_browser/media_browser_list_widget.h"
#include "ui/timeline/timeline_end_buttons.h"
#include "ui/workspace/workspace_host.h"
#include "ui/workspace/workspace_transition_controller.h"
#include "ui/workspace/pages/fusion/fusion_workspace.h"
#include "ui/workspace/pages/render/render_workspace.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QDesktopServices>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QAbstractItemView>
#include <QListWidget>
#include <QListWidgetItem>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMetaObject>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QScrollArea>
#include <QSettings>
#include <QStackedWidget>
#include <QSlider>
#include <QStatusBar>
#include <QToolBar>
#include <QTextEdit>
#include <QUrl>
#include <QWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <iterator>
#include <limits>
#include <string_view>
#include <system_error>
#include <utility>


using namespace main_window_detail;

void MainWindow::createWorkspace() {
    preview_widget_ = new PreviewWidget(this);
    edit_workspace_ = new ui::EditWorkspace(
        editor_session_, timeline_command_service_, preview_widget_, this);
    connect(
        edit_workspace_->controller(),
        &ui::EditWorkspaceController::historyStateChanged,
        this,
        [this](bool can_undo, bool can_redo) {
            if (undo_action_ != nullptr) undo_action_->setEnabled(can_undo);
            if (redo_action_ != nullptr) redo_action_->setEnabled(can_redo);
        });
    connect(
        edit_workspace_->controller(),
        &ui::EditWorkspaceController::statusMessageRequested,
        this,
        [this](const QString& message) {
            statusBar()->showMessage(message);
        });
    connect(
        edit_workspace_->controller(),
        &ui::EditWorkspaceController::warningMessageRequested,
        this,
        [this](const QString& title, const QString& message) {
            QMessageBox::warning(this, title, message);
        });
    connect(
        edit_workspace_->controller(),
        &ui::EditWorkspaceController::projectDirtyStateUpdateRequested,
        this,
        &MainWindow::updateProjectDirtyState);
    connect(
        edit_workspace_->controller(),
        &ui::EditWorkspaceController::playbackInvalidateRequested,
        this,
        [this](bool stop_playback) {
            if (playback_controller_ != nullptr) {
                playback_controller_->invalidate(stop_playback);
            }
            playback_is_playing_ = false;
            updatePlaybackControls();
            updatePlaybackStatus();
        });
    connect(
        edit_workspace_->controller(),
        &ui::EditWorkspaceController::refreshPlaybackCompositionRequested,
        this,
        &MainWindow::refreshPlaybackComposition);
    connect(
        edit_workspace_->controller(),
        &ui::EditWorkspaceController::renderCompositionFrameRequested,
        this,
        [this](qint64 timeline_frame, qint64 clip_frame) {
            if (playback_controller_ != nullptr) {
                playback_controller_->renderCompositionFrame(
                    timeline_frame, clip_frame);
            }
        });
    connect(
        edit_workspace_->controller(),
        &ui::EditWorkspaceController::seekActiveClipRequested,
        this,
        [this](qint64 clip_frame) {
            if (playback_controller_ != nullptr) {
                playback_controller_->seekActiveClip(clip_frame);
            }
        });
    connect(
        edit_workspace_->controller(),
        &ui::EditWorkspaceController::playbackAudioParametersRequested,
        this,
        &MainWindow::updatePlaybackAudioParameters);
    connect(
        edit_workspace_->controller(),
        &ui::EditWorkspaceController::playbackCommandRequested,
        this,
        &MainWindow::sendPlaybackCommand);
    connect(
        edit_workspace_->controller(),
        &ui::EditWorkspaceController::monitorVolumeChangedRequested,
        this,
        &MainWindow::applyMonitorVolumePercent);
    connect(
        edit_workspace_->controller(),
        &ui::EditWorkspaceController::razorToolStateChanged,
        this,
        [this](bool enabled) {
            if (razor_tool_action_ != nullptr &&
                razor_tool_action_->isChecked() != enabled) {
                const QSignalBlocker blocker(razor_tool_action_);
                razor_tool_action_->setChecked(enabled);
            }
        });
    connect(
        edit_workspace_->controller(),
        &ui::EditWorkspaceController::timelineImageClipEditRequested,
        this,
        &MainWindow::editTimelineImageClip);
    connect(
        edit_workspace_->controller(),
        &ui::EditWorkspaceController::timelineFusionClipOpenRequested,
        this,
        [this](timeline::ClipId clip_id) {
            if (editor_session_.selection().active_clip_id != clip_id) return;
            const auto location = editor_session_.timeline().locateClip(clip_id);
            if (!location.has_value()) return;
            const auto& track = editor_session_.timeline().tracks()[location->track_index];
            if (location->clip_index >= track.clips.size()) return;
            const auto kind = track.clips[location->clip_index].kind;
            if (kind != timeline::ClipKind::Video &&
                kind != timeline::ClipKind::Image) return;
            refreshFusionSelection();
            setWorkspacePage(ui::WorkspacePageId::Fusion);
        });
    connect(
        edit_workspace_->controller(),
        &ui::EditWorkspaceController::timelineMediaDropRequested,
        this,
        &MainWindow::handleMediaDropAt);
    connect(
        edit_workspace_->controller(),
        &ui::EditWorkspaceController::timelineExternalFilesDropRequested,
        this,
        &MainWindow::handleExternalTimelineFilesDrop);
    connect(
        edit_workspace_->controller(),
        &ui::EditWorkspaceController::seekTimelineRequested,
        this,
        [this](qint64 global_frame) {
            const auto result = playback_controller_ != nullptr
                ? playback_controller_->seekTimeline(global_frame)
                : playback::PlaybackCommandResult::Unavailable;
            edit_workspace_->controller()->handleTimelineSeekResult(
                global_frame, result);
        });
    connect(
        edit_workspace_->controller(),
        &ui::EditWorkspaceController::clearPreviewRequested,
        this,
        [this](const QString& message) {
            if (preview_widget_ != nullptr) preview_widget_->clearFrame(message);
        });
    connect(
        edit_workspace_->controller(),
        &ui::EditWorkspaceController::selectMediaBrowserClipRequested,
        this,
        [this](timeline::ClipId clip_id) {
            const auto location = timeline_model_.locateClip(clip_id);
            if (!location.has_value()) return;
            const auto& clip = timeline_model_.tracks()[location->track_index]
                .clips[location->clip_index];
            populateMediaBrowser(clip.source_path);
        });
    connect(
        edit_workspace_->controller(),
        &ui::EditWorkspaceController::activateTimelineClipRequested,
        this,
        [this](timeline::ClipId clip_id,
               qint64 clip_frame,
               bool resume_playback,
               bool preserve_timeline_playhead) {
            const auto location = timeline_model_.locateClip(clip_id);
            if (!location.has_value()) return;
            activateTimelineClipAt(
                location->track_index,
                location->clip_index,
                clip_frame,
                resume_playback,
                preserve_timeline_playhead);
        });
    connect(
        edit_workspace_->controller(),
        &ui::EditWorkspaceController::showTimelineClipPreviewRequested,
        this,
        [this](timeline::ClipId clip_id) {
            const auto location = timeline_model_.locateClip(clip_id);
            if (!location.has_value()) return;
            const auto& clip = timeline_model_.tracks()[location->track_index]
                .clips[location->clip_index];
            populateMediaBrowser(clip.source_path);
            const auto media = std::find_if(
                media_items_.begin(), media_items_.end(),
                [&clip](const ImportedMedia& item) {
                    return normalizedPath(item.metadata.source_path) ==
                        normalizedPath(clip.source_path);
                });
            if (media == media_items_.end()) return;
            if (media->offline) {
                preview_widget_->clearFrame(
                    "Preview area\n\nThe selected media is offline.");
            } else if (clip.kind == timeline::ClipKind::Audio) {
                preview_widget_->clearFrame(
                    "Audio clip\n\nAudio is mixed during timeline playback.");
            } else {
                preview_widget_->setFrame(media->first_frame);
            }
        });
    connect(
        edit_workspace_->controller(),
        &ui::EditWorkspaceController::clearMediaBrowserSelectionRequested,
        this,
        [this]() {
            if (media_list_ == nullptr) return;
            const QSignalBlocker blocker(media_list_);
            media_list_->clearSelection();
            media_list_->setCurrentRow(-1);
        });
    connect(
        edit_workspace_->controller(),
        &ui::EditWorkspaceController::timelineSelectionPresentationChanged,
        this,
        &MainWindow::synchronizeActiveTimelineSelection);
    connect(
        edit_workspace_->controller(),
        &ui::EditWorkspaceController::timelineSelectionPresentationChanged,
        this,
        &MainWindow::updateAttributeClipboardActions);
    connect(
        edit_workspace_->controller(),
        &ui::EditWorkspaceController::refreshPlaybackUiRequested,
        this,
        [this]() {
            updatePlaybackControls();
            updatePlaybackStatus();
        });
    connect(
        edit_workspace_->controller(),
        &ui::EditWorkspaceController::timelineEditCommitted,
        this,
        [this](const application::TimelineEditResult& result,
               bool stop_playback,
               bool refresh_composition,
               const QString& status_message) {
            applyTimelineEditResult(result, stop_playback);
            updateTimelineState();
            updatePlaybackControls();
            updatePlaybackStatus();
            if (refresh_composition) refreshPlaybackComposition();
            if (!status_message.isEmpty()) {
                statusBar()->showMessage(status_message);
            }
        });
    connect(
        preview_widget_,
        &PreviewWidget::gpuFallbackRequested,
        this,
        [this](const QString& reason, qint64 error_code) {
            logging::Context context{{"cause", reason.toUtf8().toStdString()}};
            const auto payload = preview_widget_->currentPayload();
            context.emplace_back("delivery_epoch", std::to_string(payload.delivery_epoch));
            context.emplace_back("generation", std::to_string(payload.playback_generation));
            context.emplace_back("timeline_frame", std::to_string(payload.timeline_frame));
            context.emplace_back("gpu_session", payload.gpu ? std::to_string(payload.gpu->session()) : "none");
            if (error_code != 0) {
                context.emplace_back("error_code", std::to_string(error_code));
            }
            logging::Logger::instance().log(
                logging::Level::Error,
                "rendering",
                "gpu_preview",
                reason.toUtf8().toStdString(),
                context);
            if (playback_controller_ && preview_widget_->currentPayload().gpu)
                playback_controller_->recoverPreviewFrame(preview_widget_->currentPayload());
            statusBar()->showMessage(preview_widget_->usesGpuPreview()
                ? "Direct GPU preview unavailable; using RGBA delivery."
                : "GPU preview unavailable; using CPU preview.", 5000);
        });

    bins_dock_ = createDock(
        "Bins",
        "binsDock",
        createMediaBins());
    media_dock_ = createDock(
        "Media",
        "mediaDock",
        createMediaPanel());
    addDockWidget(Qt::LeftDockWidgetArea, bins_dock_);
    addDockWidget(Qt::LeftDockWidgetArea, media_dock_);
    splitDockWidget(bins_dock_, media_dock_, Qt::Vertical);
    resizeDocks({bins_dock_, media_dock_}, {300, 700}, Qt::Vertical);

    toolbox_dock_ = createDock(
        "Toolbox",
        "toolboxDock",
        createEffectsToolbox());
    effects_dock_ = createDock(
        "Effects",
        "effectsDock",
        createEffectsPanel());
    favorites_dock_ = createDock(
        "Favorites",
        "favoritesDock",
        createEffectsFavorites());
    toolbox_dock_->setMinimumWidth(20);
    favorites_dock_->setMinimumWidth(20);
    effects_dock_->setMinimumWidth(30);
    addDockWidget(Qt::LeftDockWidgetArea, toolbox_dock_);
    addDockWidget(Qt::LeftDockWidgetArea, effects_dock_);
    splitDockWidget(toolbox_dock_, effects_dock_, Qt::Horizontal);
    resizeDocks({toolbox_dock_, effects_dock_}, {180, 420}, Qt::Horizontal);
    addDockWidget(Qt::LeftDockWidgetArea, favorites_dock_);
    splitDockWidget(toolbox_dock_, favorites_dock_, Qt::Vertical);
    resizeDocks({toolbox_dock_, favorites_dock_}, {300, 300}, Qt::Vertical);
    toolbox_dock_->hide();
    favorites_dock_->hide();
    effects_dock_->hide();

    populateMediaBrowser();
    updateTimelineState();
    edit_workspace_->createPanels(this);
    fusion_workspace_ = new ui::FusionWorkspace(this);
    fusion_workspace_->createPanels(this);
    auto* edit_controller = edit_workspace_->controller();
    connect(fusion_workspace_, &ui::FusionWorkspace::graphEditRequested,
        edit_controller, [this, edit_controller](timeline::ClipId clip_id,
            const fusion::nodes::NodeGraph& graph) {
            edit_controller->applyFusionNodeGraph(clip_id, graph);
            refreshFusionSelection();
        });
    connect(fusion_workspace_, &ui::FusionWorkspace::nodePreviewRequested,
        this, [this](timeline::ClipId, fusion::nodes::NodeId) {
            refreshFusionNodePreviewTarget();
        });
    connect(edit_controller, &ui::EditWorkspaceController::timelineSelectionPresentationChanged,
        this, &MainWindow::refreshFusionSelection);
    connect(edit_controller, &ui::EditWorkspaceController::timelineEditCommitted,
        this, [this](const application::TimelineEditResult&,
            bool, bool, const QString&) { refreshFusionSelection(); });
    refreshFusionSelection();
    render_workspace_ = new ui::RenderWorkspace(
        [edit_controller](bool active) {
            if (edit_controller != nullptr) {
                edit_controller->setTimelineReadOnly(active);
            }
        },
        [this] { return currentProjectDocument(); },
        [this] {
            return editor_session_.timeline().frameRate().asDouble();
        },
        this);
    render_workspace_->createPanels(this);
    applyMonitorVolumePercent(edit_workspace_->ui().monitor_volume->value());

    const auto workspace_buttons = ui::createTimelineEndButtons(this);
    workspace_buttons_container_ = workspace_buttons.container;
    edit_workspace_button_ = workspace_buttons.edit;
    fusion_workspace_button_ = workspace_buttons.fusion;
    render_workspace_button_ = workspace_buttons.render;
    workspace_host_ = new ui::WorkspaceHost(
        edit_workspace_, fusion_workspace_, render_workspace_, this);
    setCentralWidget(workspace_host_);
    statusBar()->setVisible(false);

    inspector_dock_ = createDock(
        "Inspector",
        "inspectorDock",
        workspace_host_->inspectorPanel());
    addDockWidget(Qt::RightDockWidgetArea, inspector_dock_);

    timeline_dock_ = createDock(
        "Timeline",
        "timelineDock",
        workspace_host_->lowerWorkspacePanel());
    addDockWidget(Qt::BottomDockWidgetArea, timeline_dock_);

    workspace_transition_controller_ =
        new ui::WorkspaceTransitionController(
            workspace_host_,
            {
                bins_dock_,
                media_dock_,
                toolbox_dock_,
                favorites_dock_,
                effects_dock_,
                inspector_dock_,
                timeline_dock_},
            {
                edit_workspace_button_,
                fusion_workspace_button_,
                render_workspace_button_},
            this);
    workspace_transition_controller_->setPageChangedHandler(
        [this](ui::WorkspacePageId page) { handleWorkspacePageChanged(page); });

    function_palette_ = new ui::FunctionPalette(this, *shortcut_manager_);
    connect(function_palette_, &ui::FunctionPalette::effectAddRequested,
            edit_workspace_->controller(),
            &ui::EditWorkspaceController::addEffectToSelectedClip);
    connect(edit_workspace_->controller(),
            &ui::EditWorkspaceController::effectTargetAvailabilityChanged,
            function_palette_, &ui::FunctionPalette::setEffectTargetAvailable);
    connect(edit_workspace_->controller(),
            &ui::EditWorkspaceController::effectTargetAvailabilityChanged,
            this, &MainWindow::updateAttributeClipboardActions);
    function_palette_->setEffectTargetAvailable(
        edit_workspace_->controller()->selectedClipSupportsEffects());

    restoreWorkspaceLayout();
    setWorkspacePage(ui::WorkspacePageId::Edit);
}

void MainWindow::refreshFusionSelection() {
    if (fusion_workspace_ == nullptr || edit_workspace_ == nullptr) return;
    std::vector<ui::FusionWorkspace::MediaChoice> choices;
    for (const auto& item : editor_session_.mediaLibrary().items()) {
        if (item.offline || (item.metadata.kind != media::MediaKind::Video &&
                             item.metadata.kind != media::MediaKind::Image)) continue;
        choices.push_back({
            QString::fromUtf8(item.display_name.data(),
                              static_cast<qsizetype>(item.display_name.size())),
            item.metadata.source_path,
            item.metadata.frame_rate.value_or(30.0),
            item.metadata.frame_count.value_or(0),
            item.metadata.kind == media::MediaKind::Image});
    }
    const timeline::TimelineClip* selected = nullptr;
    if (const auto location = edit_workspace_->controller()->selectedTimelineClipLocation();
        location.has_value()) {
        const auto& model = editor_session_.timeline();
        if (location->track_index < model.tracks().size() &&
            location->clip_index < model.tracks()[location->track_index].clips.size())
            selected = &model.tracks()[location->track_index].clips[location->clip_index];
    }
    fusion_workspace_->setSelection(selected, std::move(choices));
    if (workspace_host_ != nullptr &&
        workspace_host_->currentPage() == ui::WorkspacePageId::Fusion)
        refreshFusionNodePreviewTarget();
}

void MainWindow::setWorkspacePage(ui::WorkspacePageId page) {
    if (workspace_transition_controller_ != nullptr) {
        workspace_transition_controller_->setPage(page);
    }
}

void MainWindow::handleWorkspacePageChanged(ui::WorkspacePageId page) {
    if (playback_controller_ == nullptr) return;
    if (page != ui::WorkspacePageId::Fusion) {
        playback_controller_->setFusionNodePreviewTarget(std::nullopt);
        return;
    }

    if (fusion_workspace_ == nullptr || fusion_workspace_->selectedClipId() == 0) {
        playback_controller_->setFusionNodePreviewTarget(std::nullopt);
        return;
    }

    const auto clip_id = fusion_workspace_->selectedClipId();
    playback_controller_->pause();
    playback_controller_->setFusionNodePreviewTarget(
        playback::FusionNodePreviewTarget{clip_id, fusion_workspace_->previewNodeId()});
    static_cast<void>(playback_controller_->activateClip(clip_id, 0, false));
}

void MainWindow::refreshFusionNodePreviewTarget() {
    if (playback_controller_ == nullptr) return;
    if (workspace_host_ == nullptr ||
        workspace_host_->currentPage() != ui::WorkspacePageId::Fusion ||
        fusion_workspace_ == nullptr || fusion_workspace_->selectedClipId() == 0) {
        playback_controller_->setFusionNodePreviewTarget(std::nullopt);
        return;
    }
    playback_controller_->setFusionNodePreviewTarget(
        playback::FusionNodePreviewTarget{
            fusion_workspace_->selectedClipId(), fusion_workspace_->previewNodeId()});
}

void MainWindow::showSettingsDialog() {
    settings::SettingsDialog dialog(this, *shortcut_manager_);
    connect(&dialog, &settings::SettingsDialog::gpuCompositionEnabledChanged,
        this, [this](bool enabled) {
            if (playback_controller_) playback_controller_->setGpuCompositionEnabled(enabled);
        });
    connect(&dialog, &settings::SettingsDialog::audioWaveformStereoModeChanged,
        this, [this](bool enabled) {
            if (editUi().timeline != nullptr) {
                editUi().timeline->setStereoWaveformDisplayEnabled(enabled);
            }
        });
    dialog.setAutosaveSnapshots(autosaveSnapshotsForSettings());
    connect(
        &dialog,
        &settings::SettingsDialog::previewPerformanceMetricsEnabledChanged,
        this,
        &MainWindow::configurePreviewPerformanceMetrics);
    connect(
        &dialog,
        &settings::SettingsDialog::projectAutosaveSettingsChanged,
        this,
        [this](bool enabled, int interval_seconds, int) {
            configureProjectAutosave(enabled, interval_seconds);
        });
    connect(
        &dialog,
        &settings::SettingsDialog::autosaveRefreshRequested,
        &dialog,
        [this, &dialog]() {
            dialog.setAutosaveSnapshots(autosaveSnapshotsForSettings());
        });
    connect(
        &dialog,
        &settings::SettingsDialog::autosaveRestoreRequested,
        &dialog,
        [this, &dialog](const QString& snapshot_path,
                        const QString& project_path) {
            if (restoreAutosaveSnapshot(snapshot_path, project_path)) {
                dialog.accept();
            }
        });
    connect(
        &dialog,
        &settings::SettingsDialog::autosaveDeleteRequested,
        &dialog,
        [this, &dialog](const QString& snapshot_path) {
            deleteAutosaveSnapshot(snapshot_path);
            dialog.setAutosaveSnapshots(autosaveSnapshotsForSettings());
        });
    connect(
        &dialog,
        &settings::SettingsDialog::autosaveOpenFolderRequested,
        this,
        &MainWindow::openAutosaveFolder);
    dialog.exec();
}
void MainWindow::createMenus() {
    const auto disableDuringProjectLoad = [](QAction* action) {
        if (action != nullptr) {
            action->setProperty("disabledDuringProjectLoad", true);
        }
    };
    const auto register_shortcut =
        [this](const QString& id, const QString& label, QAction* action) {
            shortcut_manager_->registerAction(id, label, action);
        };

    auto* file_menu = menuBar()->addMenu("&File");
    new_project_action_ = file_menu->addAction("&New Project");
    disableDuringProjectLoad(new_project_action_);
    new_project_action_->setShortcut(QKeySequence("Ctrl+N"));
    new_project_action_->setShortcutContext(Qt::WindowShortcut);
    register_shortcut(
        QStringLiteral("file.new_project"), QStringLiteral("New Project"),
        new_project_action_);
    connect(new_project_action_, &QAction::triggered, this, &MainWindow::newProject);
    project_settings_action_ = file_menu->addAction("Project Settings...");
    project_settings_action_->setObjectName(QStringLiteral("projectSettingsAction"));
    disableDuringProjectLoad(project_settings_action_);
    connect(project_settings_action_, &QAction::triggered,
            this, &MainWindow::showProjectSettingsDialog);
    auto* open_media_action = file_menu->addAction("Open &Media...");
    disableDuringProjectLoad(open_media_action);
    connect(open_media_action, &QAction::triggered, this, &MainWindow::openMedia);
    open_project_action_ = file_menu->addAction("&Open Project...");
    disableDuringProjectLoad(open_project_action_);
    open_project_action_->setShortcut(QKeySequence("Ctrl+O"));
    open_project_action_->setShortcutContext(Qt::WindowShortcut);
    register_shortcut(
        QStringLiteral("file.open_project"), QStringLiteral("Open Project"),
        open_project_action_);
    connect(open_project_action_, &QAction::triggered, this, &MainWindow::openProject);
    save_project_action_ = file_menu->addAction("&Save Project");
    disableDuringProjectLoad(save_project_action_);
    save_project_action_->setShortcut(QKeySequence("Ctrl+S"));
    save_project_action_->setShortcutContext(Qt::WindowShortcut);
    register_shortcut(
        QStringLiteral("file.save_project"), QStringLiteral("Save Project"),
        save_project_action_);
    connect(save_project_action_, &QAction::triggered, this, &MainWindow::saveProject);
    save_project_as_action_ = file_menu->addAction("Save Project &As...");
    disableDuringProjectLoad(save_project_as_action_);
    save_project_as_action_->setShortcut(QKeySequence("Ctrl+Shift+S"));
    save_project_as_action_->setShortcutContext(Qt::WindowShortcut);
    register_shortcut(
        QStringLiteral("file.save_project_as"),
        QStringLiteral("Save Project As"), save_project_as_action_);
    connect(save_project_as_action_, &QAction::triggered, this, &MainWindow::saveProjectAs);
    file_menu->addSeparator();
    auto* exit_action = file_menu->addAction("E&xit");
    connect(exit_action, &QAction::triggered, this, &QWidget::close);

    auto* edit_menu = menuBar()->addMenu("&Edit");
    auto* undo_action = edit_menu->addAction("&Undo");
    disableDuringProjectLoad(undo_action);
    undo_action->setShortcut(QKeySequence::Undo);
    undo_action->setShortcutContext(Qt::WindowShortcut);
    undo_action_ = undo_action;
    register_shortcut(
        QStringLiteral("edit.undo"), QStringLiteral("Undo"), undo_action_);
    connect(undo_action_, &QAction::triggered, this, [this]() {
        static_cast<void>(edit_workspace_->controller()->undo());
    });
    auto* redo_action = edit_menu->addAction("&Redo");
    disableDuringProjectLoad(redo_action);
    redo_action->setShortcut(QKeySequence::Redo);
    redo_action->setShortcutContext(Qt::WindowShortcut);
    redo_action_ = redo_action;
    register_shortcut(
        QStringLiteral("edit.redo"), QStringLiteral("Redo"), redo_action_);
    connect(redo_action_, &QAction::triggered, this, [this]() {
        static_cast<void>(edit_workspace_->controller()->redo());
    });
    edit_menu->addSeparator();
    auto* delete_clip_action = edit_menu->addAction("Delete Selected Clip");
    disableDuringProjectLoad(delete_clip_action);
    delete_clip_action->setShortcut(QKeySequence(Qt::Key_Delete));
    delete_clip_action->setShortcutContext(Qt::WindowShortcut);
    delete_clip_action_ = delete_clip_action;
    register_shortcut(
        QStringLiteral("edit.delete_clip"),
        QStringLiteral("Delete Selected Clip"), delete_clip_action_);
    delete_clip_action_->setEnabled(false);
    connect(
        delete_clip_action_,
        &QAction::triggered,
        edit_workspace_->controller(),
        &ui::EditWorkspaceController::deleteActiveTimelineClip);
    ripple_delete_clip_action_ = edit_menu->addAction("Ripple Delete Selected Clip");
    disableDuringProjectLoad(ripple_delete_clip_action_);
    ripple_delete_clip_action_->setObjectName(
        QStringLiteral("edit.ripple_delete_clip"));
    ripple_delete_clip_action_->setShortcut(QKeySequence("Shift+Delete"));
    ripple_delete_clip_action_->setShortcutContext(Qt::WindowShortcut);
    register_shortcut(
        QStringLiteral("edit.ripple_delete_clip"),
        QStringLiteral("Ripple Delete Selected Clip"),
        ripple_delete_clip_action_);
    ripple_delete_clip_action_->setEnabled(false);
    connect(ripple_delete_clip_action_, &QAction::triggered, this, [this]() {
        auto* focus = QApplication::focusWidget();
        if (auto* line_edit = qobject_cast<QLineEdit*>(focus)) {
            line_edit->cut();
            return;
        }
        if (auto* text_edit = qobject_cast<QTextEdit*>(focus)) {
            text_edit->cut();
            return;
        }
        if (auto* plain_text_edit = qobject_cast<QPlainTextEdit*>(focus)) {
            plain_text_edit->cut();
            return;
        }
        if (edit_workspace_ != nullptr &&
            edit_workspace_->controller() != nullptr) {
            edit_workspace_->controller()->rippleDeleteActiveTimelineClip();
        }
    });
    auto* split_clip_action = edit_menu->addAction("Split Clip at Playhead");
    disableDuringProjectLoad(split_clip_action);
    split_clip_action->setShortcut(QKeySequence("Ctrl+K"));
    split_clip_action->setShortcutContext(Qt::WindowShortcut);
    register_shortcut(
        QStringLiteral("edit.split_clip"),
        QStringLiteral("Split Clip at Playhead"), split_clip_action);
    connect(
        split_clip_action,
        &QAction::triggered,
        edit_workspace_->controller(),
        &ui::EditWorkspaceController::splitActiveClipAtPlayhead);
    copy_attributes_action_ = edit_menu->addAction("Copy Attributes");
    disableDuringProjectLoad(copy_attributes_action_);
    copy_attributes_action_->setObjectName(QStringLiteral("edit.copy_attributes"));
    copy_attributes_action_->setShortcut(QKeySequence("Ctrl+C"));
    copy_attributes_action_->setShortcutContext(Qt::WindowShortcut);
    register_shortcut(
        QStringLiteral("edit.copy_attributes"), QStringLiteral("Copy Attributes"),
        copy_attributes_action_);
    connect(copy_attributes_action_, &QAction::triggered, this, [this]() {
        auto* focus = QApplication::focusWidget();
        if (auto* line_edit = qobject_cast<QLineEdit*>(focus)) {
            line_edit->copy();
            return;
        }
        if (auto* text_edit = qobject_cast<QTextEdit*>(focus)) {
            text_edit->copy();
            return;
        }
        if (auto* plain_text_edit = qobject_cast<QPlainTextEdit*>(focus)) {
            plain_text_edit->copy();
            return;
        }
        if (edit_workspace_ == nullptr || edit_workspace_->controller() == nullptr) return;
        edit_workspace_->controller()->copySelectedClipAttributes();
        updateAttributeClipboardActions();
    });
    paste_attributes_action_ = edit_menu->addAction("Paste Attributes");
    disableDuringProjectLoad(paste_attributes_action_);
    paste_attributes_action_->setObjectName(QStringLiteral("edit.paste_attributes"));
    paste_attributes_action_->setShortcut(QKeySequence("Ctrl+Shift+V"));
    paste_attributes_action_->setShortcutContext(Qt::WindowShortcut);
    register_shortcut(
        QStringLiteral("edit.paste_attributes"), QStringLiteral("Paste Attributes"),
        paste_attributes_action_);
    connect(paste_attributes_action_, &QAction::triggered, this, [this]() {
        if (edit_workspace_ == nullptr || edit_workspace_->controller() == nullptr) return;
        edit_workspace_->controller()->showPasteCopiedClipAttributesDialog(this);
        updateAttributeClipboardActions();
    });
    edit_menu->addSeparator();
    add_video_track_action_ = edit_menu->addAction("Add Video Track");
    disableDuringProjectLoad(add_video_track_action_);
    connect(add_video_track_action_, &QAction::triggered, this, [this]() {
        edit_workspace_->controller()->promptAddVideoTrack(this);
    });
    rename_track_action_ = edit_menu->addAction("Rename Track");
    disableDuringProjectLoad(rename_track_action_);
    connect(rename_track_action_, &QAction::triggered, this, [this]() {
        edit_workspace_->controller()->promptRenameActiveTrack(this);
    });
    move_track_up_action_ = edit_menu->addAction("Move Track Up");
    disableDuringProjectLoad(move_track_up_action_);
    connect(move_track_up_action_, &QAction::triggered, this, [this]() {
        edit_workspace_->controller()->moveActiveTrack(-1);
    });
    move_track_down_action_ = edit_menu->addAction("Move Track Down");
    disableDuringProjectLoad(move_track_down_action_);
    connect(move_track_down_action_, &QAction::triggered, this, [this]() {
        edit_workspace_->controller()->moveActiveTrack(1);
    });
    remove_track_action_ = edit_menu->addAction("Remove Track");
    disableDuringProjectLoad(remove_track_action_);
    connect(remove_track_action_, &QAction::triggered, this, [this]() {
        edit_workspace_->controller()->removeActiveTrack();
    });
    auto* razor_tool_action = edit_menu->addAction("Blade Tool");
    razor_tool_action->setCheckable(true);
    razor_tool_action_ = razor_tool_action;
    connect(razor_tool_action_, &QAction::toggled, this, [this](bool enabled) {
        if (edit_workspace_ != nullptr && edit_workspace_->controller() != nullptr) {
            edit_workspace_->controller()->setRazorMode(enabled);
        }
    });
    edit_menu->addSeparator();
    require_alt_to_move_action_ = edit_menu->addAction("Require Alt to Move Clips");
    require_alt_to_move_action_->setCheckable(true);
    QSettings settings;
    const bool require_alt_to_move = settings.value(
        "timeline/require_alt_to_move", false).toBool();
    require_alt_to_move_action_->setChecked(require_alt_to_move);
    if (editUi().timeline != nullptr) {
        editUi().timeline->setMoveRequiresAlt(require_alt_to_move);
    }
    connect(require_alt_to_move_action_, &QAction::toggled, this, [this](bool enabled) {
        QSettings settings;
        settings.setValue("timeline/require_alt_to_move", enabled);
        if (editUi().timeline != nullptr) editUi().timeline->setMoveRequiresAlt(enabled);
        statusBar()->showMessage(enabled
            ? "Alt is required to move timeline clips."
            : "Timeline clips can be moved by dragging.");
    });

    move_playhead_on_clip_selection_action_ = edit_menu->addAction(
        "Move Playhead to Selected Clip Start");
    move_playhead_on_clip_selection_action_->setCheckable(true);
    QSettings timeline_selection_settings;
    const bool move_playhead_on_selection = timeline_selection_settings.value(
        "timeline/move_playhead_on_clip_selection", false).toBool();
    move_playhead_on_clip_selection_action_->setChecked(
        move_playhead_on_selection);
    connect(
        move_playhead_on_clip_selection_action_,
        &QAction::toggled,
        this,
        [this](bool enabled) {
            QSettings settings;
            settings.setValue(
                "timeline/move_playhead_on_clip_selection", enabled);
            if (edit_workspace_ != nullptr && edit_workspace_->controller() != nullptr) {
                edit_workspace_->controller()->setMovePlayheadOnClipSelection(enabled);
            }
        });
    edit_workspace_->controller()->setMovePlayheadOnClipSelection(
        move_playhead_on_clip_selection_action_->isChecked());

    auto* view_menu = menuBar()->addMenu("&View");
    auto* media_pool_menu = view_menu->addMenu("Media Pool");
    media_pool_menu->addAction(bins_dock_->toggleViewAction());
    media_pool_menu->addAction(media_dock_->toggleViewAction());
    auto* effects_menu = view_menu->addMenu("Effects");
    effects_menu->addAction(toolbox_dock_->toggleViewAction());
    effects_menu->addAction(favorites_dock_->toggleViewAction());
    effects_menu->addAction(effects_dock_->toggleViewAction());
    view_menu->addAction(inspector_dock_->toggleViewAction());
    view_menu->addAction(timeline_dock_->toggleViewAction());
    view_menu->addSeparator();
    auto* grayscale_action = view_menu->addAction("Grayscale Preview");
    grayscale_action->setCheckable(true);
    grayscale_action->setChecked(false);
    connect(grayscale_action, &QAction::toggled, this, [this](bool enabled) {
        if (preview_widget_ != nullptr) preview_widget_->setGrayscaleEnabled(enabled);
    });
    auto* playback_quality_menu = view_menu->addMenu("Playback Preview Quality");
    playback_quality_menu->setObjectName("playbackPreviewQualityMenu");
    auto* playback_quality_group = new QActionGroup(playback_quality_menu);
    playback_quality_group->setExclusive(true);
    const auto add_playback_quality_action =
        [this, playback_quality_menu, playback_quality_group](
            const QString& label,
            const QString& object_name,
            playback::PreviewQuality quality) {
            auto* action = playback_quality_menu->addAction(label);
            action->setObjectName(object_name);
            action->setCheckable(true);
            action->setChecked(playback_preview_quality_ == quality);
            playback_quality_group->addAction(action);
            connect(action, &QAction::triggered, this, [this, quality]() {
                playback_preview_quality_ = quality;
                QSettings settings;
                settings.setValue(
                    "preview/playback_quality",
                    static_cast<int>(quality));
                if (playback_controller_ != nullptr) {
                    playback_controller_->setPreviewQuality(quality);
                }
            });
        };
    add_playback_quality_action(
        "Full (Project Canvas)",
        "playbackPreviewQualityFull",
        playback::PreviewQuality::Full);
    add_playback_quality_action(
        "Half (Project Canvas)",
        "playbackPreviewQualityHalf",
        playback::PreviewQuality::Half);
    add_playback_quality_action(
        "Quarter (Project Canvas)",
        "playbackPreviewQualityQuarter",
        playback::PreviewQuality::Quarter);
    view_menu->addSeparator();
    auto* restore_layout_action = view_menu->addAction("Restore &Default Layout");
    connect(restore_layout_action, &QAction::triggered, this, &MainWindow::restoreDefaultLayout);

    auto* media_pool_toolbar = new QToolBar("Media Pool", this);
    media_pool_toolbar->setObjectName("mediaPoolToolbar");
    media_pool_toolbar->setMovable(false);
    media_pool_toolbar->setFloatable(false);
    media_pool_toolbar->setAllowedAreas(Qt::TopToolBarArea);
    media_pool_toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    media_pool_toolbar->setStyleSheet(
        "QToolButton {"
        " color: #f2f2f2;"
        " background-color: #343434;"
        " border: 1px solid #5c5c5c;"
        " border-radius: 3px;"
        " padding: 3px 10px;"
        " font-weight: 600;"
        "}"
        "QToolButton:hover {"
        " background-color: #414141;"
        "}"
        "QToolButton:checked {"
        " color: #ffffff;"
        " background-color: #2878b8;"
        " border-color: #66b7ed;"
        "}"
        "QToolButton:pressed {"
        " background-color: #1f5f91;"
        "}");
    addToolBar(Qt::TopToolBarArea, media_pool_toolbar);
    media_pool_action_ = media_pool_toolbar->addAction("Media Pool");
    media_pool_action_->setCheckable(true);
    media_pool_action_->setToolTip("Show the Media Pool docks");
    effects_action_ = media_pool_toolbar->addAction("Effects");
    effects_action_->setCheckable(true);
    effects_action_->setToolTip("Show the Effects docks");
    auto* workspace_toolbar_spacer = new QWidget(media_pool_toolbar);
    workspace_toolbar_spacer->setSizePolicy(
        QSizePolicy::Expanding, QSizePolicy::Preferred);
    media_pool_toolbar->addWidget(workspace_toolbar_spacer);
    if (workspace_buttons_container_ != nullptr) {
        media_pool_toolbar->addWidget(workspace_buttons_container_);
    }
    connect(media_pool_action_, &QAction::triggered,
            this, [this](bool) { activateMediaPoolGroup(); });
    connect(bins_dock_, &QDockWidget::visibilityChanged, this,
            [this](bool) {
                updateMediaPoolActionState();
                updateEffectsActionState();
            });
    connect(media_dock_, &QDockWidget::visibilityChanged, this,
            [this](bool) {
                updateMediaPoolActionState();
                updateEffectsActionState();
            });
    updateMediaPoolActionState();

    connect(effects_action_, &QAction::triggered,
            this, [this](bool) { activateEffectsGroup(); });
    connect(toolbox_dock_, &QDockWidget::visibilityChanged, this,
            [this](bool) {
                updateMediaPoolActionState();
                updateEffectsActionState();
            });
    connect(favorites_dock_, &QDockWidget::visibilityChanged, this,
            [this](bool) {
                updateMediaPoolActionState();
                updateEffectsActionState();
            });
    connect(effects_dock_, &QDockWidget::visibilityChanged, this,
            [this](bool) {
                updateMediaPoolActionState();
                updateEffectsActionState();
            });
    updateEffectsActionState();

    auto* settings_action = menuBar()->addAction("&Settings");
    settings_action->setToolTip("Open editor settings");
    connect(settings_action, &QAction::triggered,
            this, &MainWindow::showSettingsDialog);

    auto* help_menu = menuBar()->addMenu("&Help");
    auto* system_action = help_menu->addAction("&System");
    connect(system_action, &QAction::triggered, this, [this]() {
        const auto version = QCoreApplication::applicationVersion();
        const auto executable_path = QCoreApplication::applicationFilePath();
        QMessageBox::information(
            this,
            "System",
            QString("Video Editor\n\nVersion: %1\nExecutable: %2")
                .arg(version.isEmpty() ? QStringLiteral("Beta 0.1.0") : version,
                     executable_path.isEmpty()
                         ? QStringLiteral("N/A")
                         : executable_path));
    });
    auto* open_log_folder_action = help_menu->addAction("Open &Log Folder");
    connect(open_log_folder_action, &QAction::triggered, this, [this]() {
        auto& logger = logging::Logger::instance();
        const auto directory = logger.log_directory();
        const auto directory_text = pathToUtf8(directory);
        const bool opened = !directory.empty() &&
            QDesktopServices::openUrl(QUrl::fromLocalFile(fromUtf8(directory_text)));
        if (!opened) {
            logger.log(
                logging::Level::Warning,
                "ui",
                "open_log_folder",
                "Could not open the log directory.",
                {{"path", directory_text}});
            statusBar()->showMessage("Could not open the log folder.");
            return;
        }

        logger.log(
            logging::Level::Info,
            "ui",
            "open_log_folder",
            "Opened the log directory.",
            {{"path", directory_text}});
        statusBar()->showMessage("Log folder opened.");
    });
    help_menu->addSeparator();
    auto* about_action = help_menu->addAction("&About Video Editor");
    connect(about_action, &QAction::triggered, this, [this]() {
        QMessageBox::about(
            this,
            "About Video Editor",
            "Video Editor application shell\n\n"
            "This is an early open-source creative suite workspace.");
    });

    auto* play_action = new QAction(this);
    play_action->setShortcut(QKeySequence(Qt::Key_Space));
    play_action->setShortcutContext(Qt::WindowShortcut);
    register_shortcut(
        QStringLiteral("playback.play_pause"),
        QStringLiteral("Play or Pause"), play_action);
    connect(play_action, &QAction::triggered, this, [this]() {
        edit_workspace_->controller()->togglePlayback();
    });
    addAction(play_action);

    auto* previous_frame_action = new QAction(this);
    previous_frame_action->setShortcut(QKeySequence(Qt::Key_Left));
    previous_frame_action->setShortcutContext(Qt::WindowShortcut);
    register_shortcut(
        QStringLiteral("playback.previous_frame"),
        QStringLiteral("Previous Frame"), previous_frame_action);
    connect(previous_frame_action, &QAction::triggered, this, [this]() {
        edit_workspace_->controller()->requestPlaybackCommand(
            playback::PlaybackCommand::StepBackward);
    });
    addAction(previous_frame_action);

    auto* next_frame_action = new QAction(this);
    next_frame_action->setShortcut(QKeySequence(Qt::Key_Right));
    next_frame_action->setShortcutContext(Qt::WindowShortcut);
    register_shortcut(
        QStringLiteral("playback.next_frame"),
        QStringLiteral("Next Frame"), next_frame_action);
    connect(next_frame_action, &QAction::triggered, this, [this]() {
        edit_workspace_->controller()->requestPlaybackCommand(
            playback::PlaybackCommand::StepForward);
    });
    addAction(next_frame_action);

    auto* move_left_action = new QAction(this);
    disableDuringProjectLoad(move_left_action);
    move_left_action->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Left));
    move_left_action->setShortcutContext(Qt::WindowShortcut);
    register_shortcut(
        QStringLiteral("timeline.nudge_left"),
        QStringLiteral("Nudge Clip Left"), move_left_action);
    connect(move_left_action, &QAction::triggered, this, [this]() {
        edit_workspace_->controller()->moveActiveTimelineClip(-1);
    });
    addAction(move_left_action);

    auto* move_right_action = new QAction(this);
    disableDuringProjectLoad(move_right_action);
    move_right_action->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Right));
    move_right_action->setShortcutContext(Qt::WindowShortcut);
    register_shortcut(
        QStringLiteral("timeline.nudge_right"),
        QStringLiteral("Nudge Clip Right"), move_right_action);
    connect(move_right_action, &QAction::triggered, this, [this]() {
        edit_workspace_->controller()->moveActiveTimelineClip(1);
    });
    addAction(move_right_action);

    shortcut_manager_->load();
    updateHistoryActions();
    updateAttributeClipboardActions();
}

void MainWindow::restoreWorkspaceLayout() {
    QSettings settings;
    const auto saved_state = settings.value(
        "workspace/dock_layout_state").toByteArray();
    if (!saved_state.isEmpty() && restoreState(saved_state, 7)) return;

    restoreDefaultLayout();
}

void MainWindow::saveWorkspaceLayout() {
    QSettings settings;
    settings.setValue("workspace/dock_layout_state", saveState(7));
    settings.sync();
}

void MainWindow::applyInitialWindowLayout() {
    if (!initial_window_layout_pending_) return;
    initial_window_layout_pending_ = false;

    const auto window_width = std::max(1, width());
    const auto window_height = std::max(1, height());
    resizeDocks(
        {bins_dock_},
        {std::max(280, static_cast<int>(std::lround(window_width * 0.18)))},
        Qt::Horizontal);
    resizeDocks(
        {inspector_dock_},
        {std::max(280, static_cast<int>(std::lround(window_width * 0.19)))},
        Qt::Horizontal);
    resizeDocks(
        {timeline_dock_},
        {std::max(260, static_cast<int>(std::lround(window_height * 0.38)))},
        Qt::Vertical);
    resizeDocks({bins_dock_, media_dock_}, {300, 700}, Qt::Vertical);
    saveWorkspaceLayout();
}

void MainWindow::saveWindowGeometry() {
    QSettings settings;
    settings.setValue("workspace/window_geometry", saveGeometry());
    settings.setValue("workspace/window_maximized", isMaximized());
    settings.sync();
}

void MainWindow::activateMediaPoolGroup() {
    toolbox_dock_->hide();
    favorites_dock_->hide();
    effects_dock_->hide();
    bins_dock_->show();
    media_dock_->show();
    updateMediaPoolActionState();
    updateEffectsActionState();
}

void MainWindow::activateEffectsGroup() {
    bins_dock_->hide();
    media_dock_->hide();
    toolbox_dock_->show();
    favorites_dock_->show();
    effects_dock_->show();
    updateMediaPoolActionState();
    updateEffectsActionState();
}

void MainWindow::updateMediaPoolActionState() {
    if (media_pool_action_ == nullptr ||
        bins_dock_ == nullptr ||
        media_dock_ == nullptr) {
        return;
    }
    const QSignalBlocker blocker(media_pool_action_);
    media_pool_action_->setChecked(
        bins_dock_->isVisible() && media_dock_->isVisible() &&
        !toolbox_dock_->isVisible() &&
        !favorites_dock_->isVisible() &&
        !effects_dock_->isVisible());
}

void MainWindow::updateEffectsActionState() {
    if (effects_action_ == nullptr ||
        toolbox_dock_ == nullptr ||
        effects_dock_ == nullptr) {
        return;
    }
    const QSignalBlocker blocker(effects_action_);
    effects_action_->setChecked(
        toolbox_dock_->isVisible() &&
        favorites_dock_->isVisible() &&
        effects_dock_->isVisible() &&
        !bins_dock_->isVisible() && !media_dock_->isVisible());
}

void MainWindow::restoreDefaultLayout() {
    bins_dock_->setFloating(false);
    media_dock_->setFloating(false);
    toolbox_dock_->setFloating(false);
    favorites_dock_->setFloating(false);
    effects_dock_->setFloating(false);
    inspector_dock_->setFloating(false);
    timeline_dock_->setFloating(false);

    addDockWidget(Qt::LeftDockWidgetArea, bins_dock_);
    addDockWidget(Qt::LeftDockWidgetArea, media_dock_);
    splitDockWidget(bins_dock_, media_dock_, Qt::Vertical);
    resizeDocks({bins_dock_, media_dock_}, {300, 700}, Qt::Vertical);

    addDockWidget(Qt::LeftDockWidgetArea, toolbox_dock_);
    addDockWidget(Qt::LeftDockWidgetArea, effects_dock_);
    splitDockWidget(toolbox_dock_, effects_dock_, Qt::Horizontal);
    resizeDocks({toolbox_dock_, effects_dock_}, {180, 420}, Qt::Horizontal);
    addDockWidget(Qt::LeftDockWidgetArea, favorites_dock_);
    splitDockWidget(toolbox_dock_, favorites_dock_, Qt::Vertical);
    resizeDocks({toolbox_dock_, favorites_dock_}, {300, 300}, Qt::Vertical);
    addDockWidget(Qt::RightDockWidgetArea, inspector_dock_);
    addDockWidget(Qt::BottomDockWidgetArea, timeline_dock_);

    bins_dock_->show();
    media_dock_->show();
    toolbox_dock_->hide();
    favorites_dock_->hide();
    effects_dock_->hide();
    inspector_dock_->show();
    timeline_dock_->show();
    updateMediaPoolActionState();
    updateEffectsActionState();
    saveWorkspaceLayout();
}
