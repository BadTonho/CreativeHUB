#include "main_window.h"
#include "main_window_support.h"

#include "logging/logger.h"
#include "preview_widget.h"
#include "project/project_file.h"
#include "settings/settings_dialog.h"
#include "settings/shortcut_manager.h"
#include "timeline/timeline_widget.h"
#include "ui/media_browser_list_widget.h"

#include <QAction>
#include <QCheckBox>
#include <QCloseEvent>
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
#include <QMenu>
#include <QMenuBar>
#include <QMetaObject>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QScrollArea>
#include <QSettings>
#include <QSlider>
#include <QStatusBar>
#include <QToolBar>
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
    setCentralWidget(preview_widget_);
    connect(
        preview_widget_,
        &PreviewWidget::gpuFallbackRequested,
        this,
        [this](const QString& reason, qint64 error_code) {
            logging::Context context{{"cause", reason.toUtf8().toStdString()}};
            if (error_code != 0) {
                context.emplace_back("error_code", std::to_string(error_code));
            }
            logging::Logger::instance().log(
                logging::Level::Error,
                "rendering",
                "gpu_preview",
                reason.toUtf8().toStdString(),
                context);
            statusBar()->showMessage("GPU preview unavailable; using CPU preview.");
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

    inspector_dock_ = createDock(
        "Inspector",
        "inspectorDock",
        createInspector());
    addDockWidget(Qt::RightDockWidgetArea, inspector_dock_);

    timeline_dock_ = createDock(
        "Timeline",
        "timelineDock",
        createTimeline());
    addDockWidget(Qt::BottomDockWidgetArea, timeline_dock_);

    restoreWorkspaceLayout();
}
void MainWindow::showSettingsDialog() {
    settings::SettingsDialog dialog(this, *shortcut_manager_);
    dialog.exec();
}
void MainWindow::createMenus() {
    const auto register_shortcut =
        [this](const QString& id, const QString& label, QAction* action) {
            shortcut_manager_->registerAction(id, label, action);
        };

    auto* file_menu = menuBar()->addMenu("&File");
    new_project_action_ = file_menu->addAction("&New Project");
    new_project_action_->setShortcut(QKeySequence("Ctrl+N"));
    new_project_action_->setShortcutContext(Qt::WindowShortcut);
    register_shortcut(
        QStringLiteral("file.new_project"), QStringLiteral("New Project"),
        new_project_action_);
    connect(new_project_action_, &QAction::triggered, this, &MainWindow::newProject);
    auto* open_media_action = file_menu->addAction("Open &Media...");
    connect(open_media_action, &QAction::triggered, this, &MainWindow::openMedia);
    open_project_action_ = file_menu->addAction("&Open Project...");
    open_project_action_->setShortcut(QKeySequence("Ctrl+O"));
    open_project_action_->setShortcutContext(Qt::WindowShortcut);
    register_shortcut(
        QStringLiteral("file.open_project"), QStringLiteral("Open Project"),
        open_project_action_);
    connect(open_project_action_, &QAction::triggered, this, &MainWindow::openProject);
    save_project_action_ = file_menu->addAction("&Save Project");
    save_project_action_->setShortcut(QKeySequence("Ctrl+S"));
    save_project_action_->setShortcutContext(Qt::WindowShortcut);
    register_shortcut(
        QStringLiteral("file.save_project"), QStringLiteral("Save Project"),
        save_project_action_);
    connect(save_project_action_, &QAction::triggered, this, &MainWindow::saveProject);
    save_project_as_action_ = file_menu->addAction("Save Project &As...");
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
    undo_action->setShortcut(QKeySequence::Undo);
    undo_action->setShortcutContext(Qt::WindowShortcut);
    undo_action_ = undo_action;
    register_shortcut(
        QStringLiteral("edit.undo"), QStringLiteral("Undo"), undo_action_);
    connect(undo_action_, &QAction::triggered, this, &MainWindow::undoTimelineEdit);
    auto* redo_action = edit_menu->addAction("&Redo");
    redo_action->setShortcut(QKeySequence::Redo);
    redo_action->setShortcutContext(Qt::WindowShortcut);
    redo_action_ = redo_action;
    register_shortcut(
        QStringLiteral("edit.redo"), QStringLiteral("Redo"), redo_action_);
    connect(redo_action_, &QAction::triggered, this, &MainWindow::redoTimelineEdit);
    edit_menu->addSeparator();
    auto* delete_clip_action = edit_menu->addAction("Delete Selected Clip");
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
        this,
        &MainWindow::deleteActiveTimelineClip);
    auto* split_clip_action = edit_menu->addAction("Split Clip at Playhead");
    split_clip_action->setShortcut(QKeySequence("Ctrl+K"));
    split_clip_action->setShortcutContext(Qt::WindowShortcut);
    register_shortcut(
        QStringLiteral("edit.split_clip"),
        QStringLiteral("Split Clip at Playhead"), split_clip_action);
    connect(
        split_clip_action,
        &QAction::triggered,
        this,
        &MainWindow::splitActiveClipAtPlayhead);
    edit_menu->addSeparator();
    add_video_track_action_ = edit_menu->addAction("Add Video Track");
    connect(add_video_track_action_, &QAction::triggered,
            this, &MainWindow::addVideoTrack);
    rename_track_action_ = edit_menu->addAction("Rename Track");
    connect(rename_track_action_, &QAction::triggered,
            this, &MainWindow::renameActiveTrack);
    move_track_up_action_ = edit_menu->addAction("Move Track Up");
    connect(move_track_up_action_, &QAction::triggered,
            this, [this]() { moveActiveTrack(-1); });
    move_track_down_action_ = edit_menu->addAction("Move Track Down");
    connect(move_track_down_action_, &QAction::triggered,
            this, [this]() { moveActiveTrack(1); });
    remove_track_action_ = edit_menu->addAction("Remove Track");
    connect(remove_track_action_, &QAction::triggered,
            this, &MainWindow::removeActiveTrack);
    auto* razor_tool_action = edit_menu->addAction("Blade Tool");
    razor_tool_action->setCheckable(true);
    razor_tool_action_ = razor_tool_action;
    connect(razor_tool_action_, &QAction::toggled, this, [this](bool enabled) {
        if (razor_button_ != nullptr && razor_button_->isChecked() != enabled) {
            razor_button_->setChecked(enabled);
        }
        if (selection_button_ != nullptr &&
            selection_button_->isChecked() == enabled) {
            selection_button_->setChecked(!enabled);
        }
        if (timeline_widget_ != nullptr) timeline_widget_->setRazorMode(enabled);
    });
    edit_menu->addSeparator();
    require_alt_to_move_action_ = edit_menu->addAction("Require Alt to Move Clips");
    require_alt_to_move_action_->setCheckable(true);
    QSettings settings;
    const bool require_alt_to_move = settings.value(
        "timeline/require_alt_to_move", false).toBool();
    require_alt_to_move_action_->setChecked(require_alt_to_move);
    if (timeline_widget_ != nullptr) {
        timeline_widget_->setMoveRequiresAlt(require_alt_to_move);
    }
    connect(require_alt_to_move_action_, &QAction::toggled, this, [this](bool enabled) {
        QSettings settings;
        settings.setValue("timeline/require_alt_to_move", enabled);
        if (timeline_widget_ != nullptr) timeline_widget_->setMoveRequiresAlt(enabled);
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
        [](bool enabled) {
            QSettings settings;
            settings.setValue(
                "timeline/move_playhead_on_clip_selection", enabled);
        });

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
    auto* about_action = help_menu->addAction("&About Main Editor");
    connect(about_action, &QAction::triggered, this, [this]() {
        QMessageBox::about(
            this,
            "About Main Editor",
            "Main Editor application shell\n\n"
            "This is an early open-source creative suite workspace.");
    });

    auto* play_action = new QAction(this);
    play_action->setShortcut(QKeySequence(Qt::Key_Space));
    play_action->setShortcutContext(Qt::WindowShortcut);
    register_shortcut(
        QStringLiteral("playback.play_pause"),
        QStringLiteral("Play or Pause"), play_action);
    connect(play_action, &QAction::triggered, this, [this]() {
        sendPlaybackCommand(playback_is_playing_ ? "pause" : "play");
    });
    addAction(play_action);

    auto* previous_frame_action = new QAction(this);
    previous_frame_action->setShortcut(QKeySequence(Qt::Key_Left));
    previous_frame_action->setShortcutContext(Qt::WindowShortcut);
    register_shortcut(
        QStringLiteral("playback.previous_frame"),
        QStringLiteral("Previous Frame"), previous_frame_action);
    connect(previous_frame_action, &QAction::triggered, this, [this]() {
        sendPlaybackCommand("stepBackward");
    });
    addAction(previous_frame_action);

    auto* next_frame_action = new QAction(this);
    next_frame_action->setShortcut(QKeySequence(Qt::Key_Right));
    next_frame_action->setShortcutContext(Qt::WindowShortcut);
    register_shortcut(
        QStringLiteral("playback.next_frame"),
        QStringLiteral("Next Frame"), next_frame_action);
    connect(next_frame_action, &QAction::triggered, this, [this]() {
        sendPlaybackCommand("stepForward");
    });
    addAction(next_frame_action);

    auto* move_left_action = new QAction(this);
    move_left_action->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Left));
    move_left_action->setShortcutContext(Qt::WindowShortcut);
    register_shortcut(
        QStringLiteral("timeline.nudge_left"),
        QStringLiteral("Nudge Clip Left"), move_left_action);
    connect(move_left_action, &QAction::triggered, this, [this]() {
        moveActiveTimelineClip(-1);
    });
    addAction(move_left_action);

    auto* move_right_action = new QAction(this);
    move_right_action->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Right));
    move_right_action->setShortcutContext(Qt::WindowShortcut);
    register_shortcut(
        QStringLiteral("timeline.nudge_right"),
        QStringLiteral("Nudge Clip Right"), move_right_action);
    connect(move_right_action, &QAction::triggered, this, [this]() {
        moveActiveTimelineClip(1);
    });
    addAction(move_right_action);

    shortcut_manager_->load();
    updateHistoryActions();
}

void MainWindow::restoreWorkspaceLayout() {
    QSettings settings;
    const auto saved_state = settings.value(
        "workspace/dock_layout_state").toByteArray();
    if (!saved_state.isEmpty() && restoreState(saved_state, 6)) return;

    restoreDefaultLayout();
}

void MainWindow::saveWorkspaceLayout() {
    QSettings settings;
    settings.setValue("workspace/dock_layout_state", saveState(6));
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
