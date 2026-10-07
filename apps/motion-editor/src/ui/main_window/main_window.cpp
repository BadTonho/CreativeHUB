#include "main_window.h"
#include "main_window_support.h"

#include "ui/workers/audio_keyframe_generation.h"
#include "export/motion_video_export.h"
#include "rendering/preview_renderer.h"

#include <creative_suite/diagnostics/logger.h>

#include <QAction>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QOffscreenSurface>
#include <QPushButton>
#include <QStatusBar>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <utility>

namespace motion::ui {
using detail::pathForLog;
using detail::pathForDisplay;

MainWindow::MainWindow(QWidget* parent,
                       std::filesystem::path recovery_root,
                       std::string recovery_session_id)
    : QMainWindow(parent),
      recovery_store_(std::move(recovery_root), std::move(recovery_session_id))
{
    setWindowTitle(QStringLiteral("Motion Studio"));
    setWindowState(windowState() | Qt::WindowMaximized);

    auto* empty_state_container = new QWidget(this);
    empty_state_container->setObjectName(QStringLiteral("motion-empty-state-container"));
    auto* empty_state_layout = new QVBoxLayout(empty_state_container);
    empty_state_layout->setContentsMargins(16, 16, 16, 16);
    empty_state_layout->setSpacing(10);
    empty_state_layout->addStretch(1);

    empty_state_ = new QLabel(QStringLiteral("No composition open"), empty_state_container);
    empty_state_->setObjectName(QStringLiteral("motion-empty-state"));
    empty_state_->setAlignment(Qt::AlignCenter);
    empty_state_layout->addWidget(empty_state_, 0, Qt::AlignHCenter);

    empty_state_new_composition_button_ = new QPushButton(
        QStringLiteral("New Composition..."), empty_state_container);
    empty_state_new_composition_button_->setObjectName(
        QStringLiteral("motion-empty-new-composition-button"));
    empty_state_layout->addWidget(empty_state_new_composition_button_, 0, Qt::AlignHCenter);
    empty_state_layout->addStretch(1);
    setCentralWidget(empty_state_container);

    QMenu* file_menu = menuBar()->addMenu(QStringLiteral("File"));
    new_composition_action_ = file_menu->addAction(QStringLiteral("New Composition..."));
    new_composition_action_->setObjectName(QStringLiteral("motion-new-composition-action"));
    new_composition_action_->setShortcut(QKeySequence::New);
    new_composition_action_->setShortcutContext(Qt::WindowShortcut);
    addAction(new_composition_action_);
    shortcut_manager_.registerAction(
        QStringLiteral("file.new_composition"), QStringLiteral("New Composition"),
        new_composition_action_);
    connect(new_composition_action_, &QAction::triggered, this, [this] {
        createNewComposition();
    });
    connect(empty_state_new_composition_button_, &QPushButton::clicked,
            new_composition_action_, &QAction::trigger);

    open_composition_action_ = file_menu->addAction(QStringLiteral("Open Composition..."));
    open_composition_action_->setObjectName(QStringLiteral("motion-open-composition-action"));
    open_composition_action_->setShortcut(QKeySequence::Open);
    open_composition_action_->setShortcutContext(Qt::WindowShortcut);
    addAction(open_composition_action_);
    shortcut_manager_.registerAction(
        QStringLiteral("file.open_composition"), QStringLiteral("Open Composition"),
        open_composition_action_);
    connect(open_composition_action_, &QAction::triggered, this, [this] {
        openComposition();
    });

    save_composition_action_ = file_menu->addAction(QStringLiteral("Save"));
    save_composition_action_->setObjectName(QStringLiteral("motion-save-composition-action"));
    save_composition_action_->setShortcut(QKeySequence::Save);
    save_composition_action_->setShortcutContext(Qt::WindowShortcut);
    save_composition_action_->setEnabled(false);
    addAction(save_composition_action_);
    shortcut_manager_.registerAction(
        QStringLiteral("file.save_composition"), QStringLiteral("Save Composition"),
        save_composition_action_);
    connect(save_composition_action_, &QAction::triggered, this, [this] {
        (void)saveComposition();
    });

    save_composition_as_action_ = file_menu->addAction(QStringLiteral("Save As..."));
    save_composition_as_action_->setObjectName(
        QStringLiteral("motion-save-composition-as-action"));
#if defined(Q_OS_MACOS)
    save_composition_as_action_->setShortcut(
        QKeySequence(Qt::META | Qt::SHIFT | Qt::Key_S));
#else
    save_composition_as_action_->setShortcut(
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
#endif
    save_composition_as_action_->setShortcutContext(Qt::WindowShortcut);
    save_composition_as_action_->setEnabled(false);
    addAction(save_composition_as_action_);
    shortcut_manager_.registerAction(
        QStringLiteral("file.save_composition_as"), QStringLiteral("Save Composition As"),
        save_composition_as_action_);
    connect(save_composition_as_action_, &QAction::triggered, this, [this] {
        (void)saveCompositionAs();
    });

    export_video_action_ = file_menu->addAction(QStringLiteral("Export Video..."));
    export_video_action_->setObjectName(QStringLiteral("motion-export-video-action"));
    export_video_action_->setEnabled(false);
    connect(export_video_action_, &QAction::triggered,
            this, [this] { startVideoExport(); });

    file_menu->addSeparator();
    import_media_action_ = file_menu->addAction(QStringLiteral("Import Media..."));
    import_media_action_->setObjectName(QStringLiteral("motion-import-media-action"));
    import_media_action_->setEnabled(false);
    const auto import_sequence =
#if defined(Q_OS_MACOS)
        QKeySequence(Qt::META | Qt::Key_I);
#else
        QKeySequence(Qt::CTRL | Qt::Key_I);
#endif
    import_media_action_->setShortcut(import_sequence);
    import_media_action_->setShortcutContext(Qt::WindowShortcut);
    addAction(import_media_action_);
    shortcut_manager_.registerAction(
        QStringLiteral("media.import"), QStringLiteral("Import Media"),
        import_media_action_);
    connect(import_media_action_, &QAction::triggered, this, [this] { openMedia(); });

    auto* layer_menu = menuBar()->addMenu(QStringLiteral("Layer"));
    new_text_layer_action_ = layer_menu->addAction(QStringLiteral("New Text"));
    new_text_layer_action_->setObjectName(QStringLiteral("motion-new-text-layer-action"));
    new_text_layer_action_->setEnabled(false);
    connect(new_text_layer_action_, &QAction::triggered, this, [this] {
        createContentLayer(model::LayerKind::Text, model::ShapeKind::Rectangle);
    });
    new_rectangle_layer_action_ = layer_menu->addAction(QStringLiteral("New Rectangle"));
    new_rectangle_layer_action_->setObjectName(
        QStringLiteral("motion-new-rectangle-layer-action"));
    new_rectangle_layer_action_->setEnabled(false);
    connect(new_rectangle_layer_action_, &QAction::triggered, this, [this] {
        createContentLayer(model::LayerKind::Shape, model::ShapeKind::Rectangle);
    });
    new_ellipse_layer_action_ = layer_menu->addAction(QStringLiteral("New Ellipse"));
    new_ellipse_layer_action_->setObjectName(
        QStringLiteral("motion-new-ellipse-layer-action"));
    new_ellipse_layer_action_->setEnabled(false);
    connect(new_ellipse_layer_action_, &QAction::triggered, this, [this] {
        createContentLayer(model::LayerKind::Shape, model::ShapeKind::Ellipse);
    });
    layer_menu->addSeparator();
    generate_audio_keyframes_action_ = layer_menu->addAction(
        QStringLiteral("Generate Keyframes from Audio..."));
    generate_audio_keyframes_action_->setObjectName(
        QStringLiteral("motion-generate-audio-keyframes-action"));
    generate_audio_keyframes_action_->setEnabled(false);
    connect(generate_audio_keyframes_action_, &QAction::triggered,
            this, [this] { generateKeyframesFromAudio(); });

    auto* edit_menu = menuBar()->addMenu(QStringLiteral("Edit"));
    undo_action_ = edit_menu->addAction(QStringLiteral("Undo"));
    undo_action_->setObjectName(QStringLiteral("motion-undo-action"));
    undo_action_->setShortcut(QKeySequence::Undo);
    undo_action_->setShortcutContext(Qt::WindowShortcut);
    undo_action_->setEnabled(false);
    addAction(undo_action_);
    shortcut_manager_.registerAction(
        QStringLiteral("edit.undo"), QStringLiteral("Undo"), undo_action_);
    connect(undo_action_, &QAction::triggered, this, [this] { undoComposition(); });

    redo_action_ = edit_menu->addAction(QStringLiteral("Redo"));
    redo_action_->setObjectName(QStringLiteral("motion-redo-action"));
    redo_action_->setShortcut(QKeySequence::Redo);
    redo_action_->setShortcutContext(Qt::WindowShortcut);
    redo_action_->setEnabled(false);
    addAction(redo_action_);
    shortcut_manager_.registerAction(
        QStringLiteral("edit.redo"), QStringLiteral("Redo"), redo_action_);
    connect(redo_action_, &QAction::triggered, this, [this] { redoComposition(); });

    view_menu_ = menuBar()->addMenu(QStringLiteral("View"));
    view_menu_->setObjectName(QStringLiteral("motion-view-menu"));
    reset_panel_layout_action_ = view_menu_->addAction(
        QStringLiteral("Reset Panel Layout"));
    reset_panel_layout_action_->setObjectName(
        QStringLiteral("motion-reset-panel-layout-action"));
    reset_panel_layout_action_->setEnabled(false);
    connect(reset_panel_layout_action_, &QAction::triggered,
            this, [this] { restoreDefaultPanelLayout(); });

    auto* settings_menu = menuBar()->addMenu(QStringLiteral("Settings"));
    settings_action_ = settings_menu->addAction(QStringLiteral("Keyboard Shortcuts..."));
    settings_action_->setObjectName(QStringLiteral("motion-shortcut-settings-action"));
    connect(settings_action_, &QAction::triggered,
            this, [this] { openShortcutSettings(); });
    autosave_settings_action_ = settings_menu->addAction(
        QStringLiteral("Autosave & Recovery..."));
    autosave_settings_action_->setObjectName(
        QStringLiteral("motion-autosave-recovery-action"));
    connect(autosave_settings_action_, &QAction::triggered,
            this, [this] { openAutosaveRecoverySettings(); });
    general_settings_action_ = settings_menu->addAction(
        QStringLiteral("General..."));
    general_settings_action_->setObjectName(
        QStringLiteral("motion-general-settings-action"));
    connect(general_settings_action_, &QAction::triggered,
            this, [this] { openGeneralSettings(); });

    auto* help_menu = menuBar()->addMenu(QStringLiteral("&Help"));
    auto* system_action = help_menu->addAction(QStringLiteral("&System"));
    connect(system_action, &QAction::triggered, this, [this] {
        const auto version = QCoreApplication::applicationVersion();
        const auto executable_path = QCoreApplication::applicationFilePath();
        QMessageBox::information(
            this,
            QStringLiteral("System"),
            QStringLiteral("Motion Studio\n\nVersion: %1\nExecutable: %2")
                .arg(version.isEmpty() ? QStringLiteral("Beta 0.1.0") : version,
                     executable_path.isEmpty()
                         ? QStringLiteral("N/A")
                         : executable_path));
    });

    auto* open_log_folder_action = help_menu->addAction(
        QStringLiteral("Open &Log Folder"));
    connect(open_log_folder_action, &QAction::triggered, this, [this] {
        auto& logger = creative_suite::diagnostics::Logger::instance();
        const auto directory = logger.log_directory();
        const auto directory_text = pathForLog(directory);
        const bool opened = !directory.empty() && QDesktopServices::openUrl(
            QUrl::fromLocalFile(pathForDisplay(directory)));
        if (!opened) {
            logger.log(
                creative_suite::diagnostics::Level::Warning,
                "ui", "open_log_folder", "Could not open the log directory.",
                {{"path", directory_text}});
            statusBar()->showMessage(QStringLiteral("Could not open the log folder."));
            return;
        }
        logger.log(
            creative_suite::diagnostics::Level::Info,
            "ui", "open_log_folder", "Opened the log directory.",
            {{"path", directory_text}});
        statusBar()->showMessage(QStringLiteral("Log folder opened."));
    });

    help_menu->addSeparator();
    auto* about_action = help_menu->addAction(QStringLiteral("&About Motion Studio"));
    about_action->setObjectName(QStringLiteral("motion-about-action"));
    connect(about_action, &QAction::triggered, this, [this] {
        QMessageBox::about(this, QStringLiteral("About Motion Studio"),
            QStringLiteral("Motion Studio application shell\n\n"
                           "This is an early open-source creative suite workspace."));
    });

    const auto register_timeline_action = [this](
        QAction*& action, const QString& object_name, const QString& id,
        const QString& label, const QKeySequence& default_sequence) {
        action = new QAction(label, this);
        action->setObjectName(object_name);
        action->setShortcut(default_sequence);
        action->setShortcutContext(Qt::WindowShortcut);
        addAction(action);
        shortcut_manager_.registerAction(id, label, action);
    };
    register_timeline_action(
        play_pause_action_, QStringLiteral("motion-play-pause-action"),
        QStringLiteral("timeline.play_pause"), QStringLiteral("Play/Pause"),
        QKeySequence(Qt::Key_Space));
    register_timeline_action(
        previous_frame_action_, QStringLiteral("motion-previous-frame-action"),
        QStringLiteral("timeline.previous_frame"), QStringLiteral("Previous frame"),
        QKeySequence(Qt::Key_Left));
    register_timeline_action(
        next_frame_action_, QStringLiteral("motion-next-frame-action"),
        QStringLiteral("timeline.next_frame"), QStringLiteral("Next frame"),
        QKeySequence(Qt::Key_Right));
    register_timeline_action(
        loop_action_, QStringLiteral("motion-loop-action"),
        QStringLiteral("timeline.toggle_loop"), QStringLiteral("Loop"), QKeySequence{});
    loop_action_->setCheckable(true);
    register_timeline_action(
        zoom_in_action_, QStringLiteral("motion-zoom-in-action"),
        QStringLiteral("timeline.zoom_in"), QStringLiteral("Zoom In"), QKeySequence{});
    register_timeline_action(
        zoom_out_action_, QStringLiteral("motion-zoom-out-action"),
        QStringLiteral("timeline.zoom_out"), QStringLiteral("Zoom Out"), QKeySequence{});
    previous_frame_action_->setEnabled(false);
    next_frame_action_->setEnabled(false);
    play_pause_action_->setEnabled(false);
    loop_action_->setEnabled(false);
    zoom_in_action_->setEnabled(false);
    zoom_out_action_->setEnabled(false);

    QString shortcut_error;
    if (!shortcut_manager_.load(&shortcut_error)) {
        const auto detail = shortcut_error.toStdString();
        creative_suite::diagnostics::Logger::instance().log(
            creative_suite::diagnostics::Level::Error,
            "motion_settings", "load_shortcuts", detail,
            {{"settings_group", shortcut_manager_.settingsGroup().toStdString()}});
    }

    autosave_timer_ = new QTimer(this);
    autosave_timer_->setObjectName(QStringLiteral("motion-autosave-timer"));
    connect(autosave_timer_, &QTimer::timeout,
            this, [this] { autosaveProject(); });
    configureAutosaveTimer();
    performance_metrics_timer_ = new QTimer(this);
    performance_metrics_timer_->setObjectName(
        QStringLiteral("motion-performance-metrics-timer"));
    performance_metrics_timer_->setInterval(1000);
    connect(performance_metrics_timer_, &QTimer::timeout,
            this, [this] { flushPreviewPerformanceMetrics(); });
    configurePreviewPerformanceMetrics();
    QTimer::singleShot(0, this, [this] { maybeOfferUnsavedRecovery(); });
}
MainWindow::~MainWindow()
{
    if (autosave_timer_ != nullptr) autosave_timer_->stop();
    if (performance_metrics_timer_ != nullptr) performance_metrics_timer_->stop();
    if (audio_keyframe_worker_) audio_keyframe_worker_->cancelAndWait();
    if (export_worker_) export_worker_->cancelAndWait();
    if (preview_renderer_) preview_renderer_->stopAndWait();
    preview_renderer_.reset();
    gpu_composition_surface_.reset();
}
const model::CompositionDocument* MainWindow::compositionDocument() const noexcept
{
    return document_.has_value() ? &*document_ : nullptr;
}
MediaPoolWidget* MainWindow::mediaPoolWidget() const noexcept
{
    return media_pool_;
}

} // namespace motion::ui
