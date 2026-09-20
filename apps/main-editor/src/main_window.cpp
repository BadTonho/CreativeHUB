#include "main_window.h"

#include "logging/logger.h"
#include "preview_widget.h"
#include "timeline/timeline_widget.h"
#include "ui/media_browser_list_widget.h"

#include <QAction>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QAbstractItemView>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMenuBar>
#include <QMetaObject>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <filesystem>
#include <iterator>
#include <string_view>
#include <system_error>
#include <utility>

namespace {

QString fromUtf8(const std::string& value) {
    return QString::fromUtf8(value.data(), static_cast<int>(value.size()));
}

std::string pathToUtf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return std::string(reinterpret_cast<const char*>(value.data()), value.size());
}

QString formatOptionalDouble(const std::optional<double>& value, const QString& suffix) {
    if (!value.has_value()) return "Unknown";
    return QString::number(*value, 'f', 3) + suffix;
}

std::filesystem::path normalizedPath(const std::filesystem::path& path) {
    std::error_code error;
    const auto canonical = std::filesystem::weakly_canonical(path, error);
    if (!error) return canonical;

    const auto absolute = std::filesystem::absolute(path, error);
    if (!error) return absolute.lexically_normal();
    return path.lexically_normal();
}

QString mediaListText(const media::VideoMetadata& metadata) {
    return QString("%1 — %2×%3 — %4 — %5 — %6")
        .arg(fromUtf8(metadata.display_name))
        .arg(metadata.width)
        .arg(metadata.height)
        .arg(formatOptionalDouble(metadata.frame_rate, " FPS"))
        .arg(formatOptionalDouble(metadata.duration_seconds, " s"))
        .arg(fromUtf8(metadata.container_format));
}

QString mediaDetailsText(const media::VideoMetadata& metadata) {
    const QString frame_count = metadata.frame_count.has_value()
        ? QString::number(*metadata.frame_count)
        : "Unknown";

    return QString("Name: %1\n"
                   "Format: %2\n"
                   "Codec: %3\n"
                   "Resolution: %4×%5\n"
                   "Frame rate: %6\n"
                   "Duration: %7\n"
                   "Frames: %8\n"
                   "Path: %9")
        .arg(fromUtf8(metadata.display_name))
        .arg(fromUtf8(metadata.container_format))
        .arg(fromUtf8(metadata.video_codec))
        .arg(metadata.width)
        .arg(metadata.height)
        .arg(formatOptionalDouble(metadata.frame_rate, " FPS"))
        .arg(formatOptionalDouble(metadata.duration_seconds, " s"))
        .arg(frame_count)
        .arg(fromUtf8(pathToUtf8(metadata.source_path)));
}

QWidget* createPlaceholder(const QString& title, const QString& description) {
    auto* container = new QWidget;
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(8);

    auto* title_label = new QLabel(title, container);
    title_label->setStyleSheet("font-weight: 600; font-size: 14px;");
    layout->addWidget(title_label);

    auto* description_label = new QLabel(description, container);
    description_label->setWordWrap(true);
    description_label->setStyleSheet("color: #9aa4b2;");
    layout->addWidget(description_label);
    layout->addStretch();

    return container;
}

QDockWidget* createDock(const QString& title, const QString& object_name, QWidget* content) {
    auto* dock = new QDockWidget(title);
    dock->setObjectName(object_name);
    dock->setAllowedAreas(Qt::AllDockWidgetAreas);
    dock->setFeatures(QDockWidget::DockWidgetClosable |
                      QDockWidget::DockWidgetMovable |
                      QDockWidget::DockWidgetFloatable);
    dock->setWidget(content);
    return dock;
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    setWindowTitle("Main Editor");
    resize(1280, 720);
    setDockOptions(QMainWindow::AnimatedDocks |
                   QMainWindow::AllowNestedDocks |
                   QMainWindow::AllowTabbedDocks |
                   QMainWindow::GroupedDragging);

    createWorkspace();
    createMenus();
    initializePlayback();

    statusBar()->showMessage("Ready");
}

MainWindow::~MainWindow() {
    shutdownPlayback();
}

void MainWindow::createWorkspace() {
    preview_widget_ = new PreviewWidget(this);
    setCentralWidget(preview_widget_);

    media_browser_dock_ = createDock(
        "Media Browser",
        "mediaBrowserDock",
        createMediaBrowser());
    addDockWidget(Qt::LeftDockWidgetArea, media_browser_dock_);

    inspector_dock_ = createDock(
        "Inspector",
        "inspectorDock",
        createPlaceholder("Inspector", "Selected item properties will appear here."));
    addDockWidget(Qt::RightDockWidgetArea, inspector_dock_);

    timeline_dock_ = createDock(
        "Timeline",
        "timelineDock",
        createTimeline());
    addDockWidget(Qt::BottomDockWidgetArea, timeline_dock_);
}

void MainWindow::createMenus() {
    auto* file_menu = menuBar()->addMenu("&File");
    auto* new_project_action = file_menu->addAction("&New Project");
    new_project_action->setEnabled(false);
    auto* open_media_action = file_menu->addAction("Open &Media...");
    connect(open_media_action, &QAction::triggered, this, &MainWindow::openMedia);
    auto* open_project_action = file_menu->addAction("&Open Project...");
    open_project_action->setEnabled(false);
    file_menu->addSeparator();
    auto* exit_action = file_menu->addAction("E&xit");
    connect(exit_action, &QAction::triggered, this, &QWidget::close);

    auto* edit_menu = menuBar()->addMenu("&Edit");
    auto* undo_action = edit_menu->addAction("&Undo");
    undo_action->setEnabled(false);
    auto* redo_action = edit_menu->addAction("&Redo");
    redo_action->setEnabled(false);

    auto* view_menu = menuBar()->addMenu("&View");
    view_menu->addAction(media_browser_dock_->toggleViewAction());
    view_menu->addAction(inspector_dock_->toggleViewAction());
    view_menu->addAction(timeline_dock_->toggleViewAction());
    view_menu->addSeparator();
    auto* restore_layout_action = view_menu->addAction("Restore &Default Layout");
    connect(restore_layout_action, &QAction::triggered, this, &MainWindow::restoreDefaultLayout);

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
    connect(play_action, &QAction::triggered, this, [this]() {
        sendPlaybackCommand(playback_is_playing_ ? "pause" : "play");
    });
    addAction(play_action);

    auto* previous_frame_action = new QAction(this);
    previous_frame_action->setShortcut(QKeySequence(Qt::Key_Left));
    previous_frame_action->setShortcutContext(Qt::WindowShortcut);
    connect(previous_frame_action, &QAction::triggered, this, [this]() {
        sendPlaybackCommand("stepBackward");
    });
    addAction(previous_frame_action);

    auto* next_frame_action = new QAction(this);
    next_frame_action->setShortcut(QKeySequence(Qt::Key_Right));
    next_frame_action->setShortcutContext(Qt::WindowShortcut);
    connect(next_frame_action, &QAction::triggered, this, [this]() {
        sendPlaybackCommand("stepForward");
    });
    addAction(next_frame_action);
}

QWidget* MainWindow::createMediaBrowser() {
    auto* container = new QWidget;
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    auto* title = new QLabel("Media Browser", container);
    title->setStyleSheet("font-weight: 600; font-size: 14px;");
    layout->addWidget(title);

    media_list_ = new MediaBrowserListWidget(container);
    media_list_->setSelectionMode(QAbstractItemView::SingleSelection);
    media_list_->setWordWrap(true);
    connect(media_list_, &QListWidget::currentRowChanged, this, &MainWindow::updateMediaDetails);
    layout->addWidget(media_list_, 1);

    media_details_ = new QLabel("No media imported.", container);
    media_details_->setWordWrap(true);
    media_details_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    media_details_->setStyleSheet("color: #9aa4b2;");
    layout->addWidget(media_details_);

    add_to_timeline_button_ = new QPushButton("Add to Timeline", container);
    connect(add_to_timeline_button_, &QPushButton::clicked, this, [this]() {
        addSelectedMediaToTimeline();
    });
    layout->addWidget(add_to_timeline_button_);

    updateTimelineState();

    return container;
}

QWidget* MainWindow::createTimeline() {
    auto* container = new QWidget;
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    auto* title = new QLabel("Timeline", container);
    title->setStyleSheet("font-weight: 600; font-size: 14px;");
    layout->addWidget(title);

    auto* controls = new QHBoxLayout;
    controls->setSpacing(6);

    previous_frame_button_ = new QPushButton("Previous Frame", container);
    play_pause_button_ = new QPushButton("Play", container);
    next_frame_button_ = new QPushButton("Next Frame", container);
    clear_timeline_button_ = new QPushButton("Clear Timeline", container);
    controls->addWidget(previous_frame_button_);
    controls->addWidget(play_pause_button_);
    controls->addWidget(next_frame_button_);
    controls->addWidget(clear_timeline_button_);
    controls->addStretch();
    layout->addLayout(controls);

    timeline_widget_ = new timeline::TimelineWidget(container);
    layout->addWidget(timeline_widget_);

    playback_status_label_ = new QLabel("No media selected.", container);
    playback_status_label_->setStyleSheet("color: #9aa4b2;");
    layout->addWidget(playback_status_label_);
    layout->addStretch();

    connect(previous_frame_button_, &QPushButton::clicked, this, [this]() {
        sendPlaybackCommand("stepBackward");
    });
    connect(play_pause_button_, &QPushButton::clicked, this, [this]() {
        sendPlaybackCommand(playback_is_playing_ ? "pause" : "play");
    });
    connect(next_frame_button_, &QPushButton::clicked, this, [this]() {
        sendPlaybackCommand("stepForward");
    });
    connect(clear_timeline_button_, &QPushButton::clicked, this, [this]() {
        clearTimeline();
    });
    connect(
        timeline_widget_,
        &timeline::TimelineWidget::clipSelected,
        this,
        &MainWindow::handleTimelineClipSelected);
    connect(
        timeline_widget_,
        &timeline::TimelineWidget::seekStarted,
        this,
        &MainWindow::handleTimelineSeekStarted);
    connect(
        timeline_widget_,
        &timeline::TimelineWidget::seekRequested,
        this,
        &MainWindow::handleTimelineSeek);
    connect(
        timeline_widget_,
        &timeline::TimelineWidget::mediaDropRequested,
        this,
        &MainWindow::handleMediaDrop);

    updateTimelineState();
    updatePlaybackControls();
    return container;
}

void MainWindow::initializePlayback() {
    qRegisterMetaType<playback::VideoFramePtr>();

    playback_worker_ = new playback::PlaybackWorker;
    playback_worker_->moveToThread(&playback_thread_);

    connect(
        &playback_thread_,
        &QThread::finished,
        playback_worker_,
        &QObject::deleteLater);
    connect(
        playback_worker_,
        &playback::PlaybackWorker::frameReady,
        this,
        [this](playback::VideoFramePtr frame, qint64 frame_index, quint64 generation) {
            handlePlaybackFrame(std::move(frame), frame_index, generation);
        },
        Qt::QueuedConnection);
    connect(
        playback_worker_,
        &playback::PlaybackWorker::mediaReady,
        this,
        &MainWindow::handlePlaybackMediaReady,
        Qt::QueuedConnection);
    connect(
        playback_worker_,
        &playback::PlaybackWorker::playbackStateChanged,
        this,
        [this](bool playing, quint64 generation) {
            handlePlaybackStateChanged(playing, generation);
        },
        Qt::QueuedConnection);
    connect(
        playback_worker_,
        &playback::PlaybackWorker::playbackFinished,
        this,
        [this](quint64 generation, bool during_playback) {
            handlePlaybackFinished(generation, during_playback);
        },
        Qt::QueuedConnection);
    connect(
        playback_worker_,
        &playback::PlaybackWorker::playbackError,
        this,
        [this](const QString& message, qint64 error_code, quint64 generation) {
            handlePlaybackError(message, error_code, generation);
        },
        Qt::QueuedConnection);

    playback_thread_.start();
}

void MainWindow::shutdownPlayback() {
    if (playback_worker_ == nullptr) return;

    if (playback_thread_.isRunning()) {
        QMetaObject::invokeMethod(
            playback_worker_,
            "stop",
            Qt::BlockingQueuedConnection);
        playback_thread_.quit();
        playback_thread_.wait();
    }
    playback_worker_ = nullptr;
}

bool MainWindow::hasSelectedMedia() const noexcept {
    return media_list_ != nullptr &&
        media_list_->currentRow() >= 0 &&
        media_list_->currentRow() < static_cast<int>(media_items_.size());
}

std::optional<std::size_t> MainWindow::selectedTimelineClipIndex() const noexcept {
    if (!hasSelectedMedia() || !timeline_model_.hasClip()) return std::nullopt;

    const auto& selected = media_items_[static_cast<std::size_t>(media_list_->currentRow())];
    const auto& clips = timeline_model_.clips();
    for (std::size_t index = 0; index < clips.size(); ++index) {
        if (clips[index].source_path == selected.metadata.source_path) {
            return index;
        }
    }
    return std::nullopt;
}

bool MainWindow::selectedMediaMatchesTimeline() const noexcept {
    return selectedTimelineClipIndex().has_value();
}

bool MainWindow::canPreviewSelectedMedia() const noexcept {
    return hasSelectedMedia() &&
        (!timeline_model_.hasClip() || selectedMediaMatchesTimeline());
}

void MainWindow::updateTimelineState() {
    const bool selected = hasSelectedMedia();
    const bool occupied = timeline_model_.hasClip();

    if (add_to_timeline_button_ != nullptr) {
        add_to_timeline_button_->setEnabled(selected);
        add_to_timeline_button_->setText("Add to Timeline");
    }

    if (clear_timeline_button_ != nullptr) {
        clear_timeline_button_->setEnabled(occupied);
    }

    if (timeline_widget_ != nullptr) {
        timeline_widget_->setClips(timeline_model_.clips());
        timeline_widget_->setActiveClipIndex(active_timeline_clip_index_);
        timeline_widget_->setPlayheadFrame(playback_frame_index_);
    }
}

void MainWindow::addSelectedMediaToTimeline() {
    if (!hasSelectedMedia()) return;

    const auto& selected = media_items_[static_cast<size_t>(media_list_->currentRow())];
    switch (timeline_model_.addClip(selected.metadata)) {
    case timeline::AddClipResult::Added:
        active_timeline_clip_index_ = timeline_model_.clipCount() - 1;
        updateTimelineState();
        updatePlaybackControls();
        updatePlaybackStatus();
        statusBar()->showMessage("Media added to the timeline.");
        break;
    case timeline::AddClipResult::InvalidTimingMetadata: {
        const auto path = pathToUtf8(selected.metadata.source_path);
        logging::Logger::instance().log(
            logging::Level::Error,
            "timeline",
            "add_clip",
            "Media does not contain enough valid timing metadata for timeline placement.",
            {{"path", path}});
        QMessageBox::warning(
            this,
            "Could not add media",
            "This media does not contain enough timing metadata for the timeline.");
        statusBar()->showMessage("Could not add media to the timeline.");
        break;
    }
    }
}

void MainWindow::handleMediaDrop(const QString& source_path) {
    const std::string source_text = source_path.toUtf8().toStdString();
    try {
        const auto dropped_path = normalizedPath(
            QFileInfo(source_path).filesystemFilePath());
        const auto existing = std::find_if(
            media_items_.begin(),
            media_items_.end(),
            [&dropped_path](const ImportedMedia& item) {
                return item.metadata.source_path == dropped_path;
            });

        if (existing == media_items_.end()) {
            statusBar()->showMessage("Import this media before adding it to the timeline.");
            return;
        }

        const int index = static_cast<int>(
            std::distance(media_items_.begin(), existing));
        media_list_->setCurrentRow(index);
        addSelectedMediaToTimeline();
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "ui",
            "timeline_drop",
            error.what(),
            {{"path", source_text}});
        statusBar()->showMessage("Could not add dropped media to the timeline.");
        QMessageBox::warning(
            this,
            "Could not add media",
            "The dropped media could not be added to the timeline.");
    }
}

void MainWindow::handleTimelineClipSelected(qint64 clip_index) {
    if (clip_index < 0 ||
        clip_index >= static_cast<qint64>(timeline_model_.clipCount())) {
        return;
    }

    const bool had_pending_activation = pending_clip_activation_.has_value();
    if (had_pending_activation) {
        pending_clip_activation_.reset();
        ++playback_generation_;
        playback_is_playing_ = false;
        if (playback_worker_ != nullptr) {
            QMetaObject::invokeMethod(playback_worker_, "stop", Qt::QueuedConnection);
        }
    }

    const auto& clip = timeline_model_.clips()[static_cast<std::size_t>(clip_index)];
    const auto media_item = std::find_if(
        media_items_.begin(),
        media_items_.end(),
        [&clip](const ImportedMedia& item) {
            return item.metadata.source_path == clip.source_path;
        });
    if (media_item == media_items_.end()) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "timeline",
            "select_clip",
            "The selected timeline clip has no matching imported media item.",
            {{"path", pathToUtf8(clip.source_path)},
             {"clip_index", std::to_string(clip_index)}});
        statusBar()->showMessage("Could not select the timeline clip.");
        return;
    }

    const int media_index = static_cast<int>(
        std::distance(media_items_.begin(), media_item));
    const bool row_changed = media_list_->currentRow() != media_index;
    if (row_changed) {
        media_list_->setCurrentRow(media_index);
    } else if (had_pending_activation) {
        updateMediaDetails(media_index);
    }

    active_timeline_clip_index_ = static_cast<std::size_t>(clip_index);
    updateTimelineState();
    updatePlaybackControls();
    updatePlaybackStatus();
    statusBar()->showMessage("Timeline clip selected.");
}

void MainWindow::activateTimelineClip(
    std::size_t clip_index,
    std::int64_t target_frame,
    bool resume_playback) {
    if (playback_worker_ == nullptr ||
        clip_index >= timeline_model_.clipCount()) {
        return;
    }

    const auto& clip = timeline_model_.clips()[clip_index];
    const auto media_item = std::find_if(
        media_items_.begin(),
        media_items_.end(),
        [&clip](const ImportedMedia& item) {
            return item.metadata.source_path == clip.source_path;
        });
    if (media_item == media_items_.end() ||
        target_frame < 0 ||
        target_frame >= clip.timeline_duration_frames) {
        logging::Context context{
            {"clip_index", std::to_string(clip_index)},
            {"path", pathToUtf8(clip.source_path)},
            {"requested_frame", std::to_string(target_frame)}};
        logging::Logger::instance().log(
            logging::Level::Error,
            "playback",
            "transition",
            media_item == media_items_.end()
                ? "The timeline clip has no matching imported media item."
                : "The requested timeline transition frame is invalid.",
            context);
        statusBar()->showMessage("Could not activate the timeline clip.");
        QMessageBox::warning(
            this,
            "Playback transition error",
            "The next timeline clip could not be activated.");
        playback_is_playing_ = false;
        updatePlaybackControls();
        updatePlaybackStatus();
        return;
    }

    ++playback_generation_;
    pending_clip_activation_ = PendingClipActivation{
        clip_index,
        static_cast<std::size_t>(std::distance(media_items_.begin(), media_item)),
        target_frame,
        resume_playback,
        playback_generation_};
    playback_is_playing_ = false;
    updatePlaybackControls();
    updatePlaybackStatus();
    statusBar()->showMessage("Loading timeline clip...");

    QMetaObject::invokeMethod(playback_worker_, "stop", Qt::QueuedConnection);
    const auto& item = *media_item;
    QMetaObject::invokeMethod(
        playback_worker_,
        "setMedia",
        Qt::QueuedConnection,
        Q_ARG(QString, fromUtf8(pathToUtf8(item.metadata.source_path))),
        Q_ARG(double, item.metadata.frame_rate.value_or(30.0)),
        Q_ARG(quint64, playback_generation_));
}

void MainWindow::commitTimelineClipActivation(
    std::size_t clip_index,
    std::size_t media_index,
    std::int64_t frame_index,
    bool show_cached_frame) {
    if (clip_index >= timeline_model_.clipCount() ||
        media_index >= media_items_.size()) {
        return;
    }

    active_timeline_clip_index_ = clip_index;
    playback_frame_index_ = frame_index;
    {
        const QSignalBlocker blocker(media_list_);
        media_list_->setCurrentRow(static_cast<int>(media_index));
    }

    const auto& item = media_items_[media_index];
    media_details_->setText(mediaDetailsText(item.metadata));
    if (show_cached_frame) preview_widget_->setFrame(item.first_frame);
    updateTimelineState();
    updatePlaybackControls();
    updatePlaybackStatus();
}

void MainWindow::clearTimeline() {
    if (!timeline_model_.hasClip()) return;

    timeline_model_.clear();
    active_timeline_clip_index_.reset();
    const int selected_row = media_list_ != nullptr ? media_list_->currentRow() : -1;
    if (selected_row >= 0) {
        updateMediaDetails(selected_row);
    } else {
        updateTimelineState();
        updatePlaybackControls();
        updatePlaybackStatus();
    }
    statusBar()->showMessage("Timeline cleared.");
}

void MainWindow::sendPlaybackCommand(const char* command) {
    if (playback_worker_ == nullptr || media_list_ == nullptr ||
        !canPreviewSelectedMedia() || pending_clip_activation_.has_value()) {
        return;
    }

    const auto active_index = active_timeline_clip_index_;
    if (active_index.has_value() &&
        *active_index < timeline_model_.clipCount() &&
        std::string_view(command) == "stepForward") {
        const auto& clip = timeline_model_.clips()[*active_index];
        if (playback_frame_index_ >= clip.timeline_duration_frames - 1) {
            if (*active_index + 1 < timeline_model_.clipCount()) {
                activateTimelineClip(*active_index + 1, 0, false);
            } else {
                statusBar()->showMessage("Already at the end of the timeline.");
            }
            return;
        }
    }

    if (active_index.has_value() &&
        *active_index < timeline_model_.clipCount() &&
        std::string_view(command) == "stepBackward" &&
        playback_frame_index_ <= 0) {
        if (*active_index > 0) {
            const auto& previous_clip = timeline_model_.clips()[*active_index - 1];
            activateTimelineClip(
                *active_index - 1,
                previous_clip.timeline_duration_frames - 1,
                false);
        } else {
            statusBar()->showMessage("Already at the beginning of the timeline.");
        }
        return;
    }

    QMetaObject::invokeMethod(playback_worker_, command, Qt::QueuedConnection);
}

void MainWindow::updatePlaybackControls() {
    const bool has_media = canPreviewSelectedMedia() &&
        !pending_clip_activation_.has_value();
    if (previous_frame_button_ != nullptr) previous_frame_button_->setEnabled(has_media);
    if (play_pause_button_ != nullptr) play_pause_button_->setEnabled(has_media);
    if (next_frame_button_ != nullptr) next_frame_button_->setEnabled(has_media);
    if (play_pause_button_ != nullptr) {
        play_pause_button_->setText(playback_is_playing_ ? "Pause" : "Play");
    }
}

void MainWindow::updatePlaybackStatus() {
    if (playback_status_label_ == nullptr) return;

    if (pending_clip_activation_.has_value()) {
        playback_status_label_->setText("Loading timeline clip...");
        return;
    }

    const int row = media_list_ != nullptr ? media_list_->currentRow() : -1;
    if (row < 0 || row >= static_cast<int>(media_items_.size())) {
        playback_status_label_->setText("No media selected.");
        return;
    }

    if (!canPreviewSelectedMedia()) {
        playback_status_label_->setText("Select the timeline media to play.");
        return;
    }

    const auto& metadata = media_items_[static_cast<size_t>(row)].metadata;
    const QString total = metadata.frame_count.has_value()
        ? QString::number(*metadata.frame_count)
        : "?";
    const QString state = playback_is_playing_ ? "Playing" : "Paused";
    playback_status_label_->setText(
        QString("%1 - Frame %2 / %3")
            .arg(state)
            .arg(playback_frame_index_ + 1)
            .arg(total));
}

void MainWindow::openMedia() {
    const QString selected_file = QFileDialog::getOpenFileName(
        this,
        "Open Media",
        QString(),
        "Video Files (*.avi *.mkv *.mov *.mp4 *.mxf *.webm);;All Files (*)");
    if (selected_file.isEmpty()) return;

    const std::filesystem::path source_path = normalizedPath(
        QFileInfo(selected_file).filesystemFilePath());

    const auto existing = std::find_if(
        media_items_.begin(),
        media_items_.end(),
        [&source_path](const ImportedMedia& item) {
            return item.metadata.source_path == source_path;
        });
    if (existing != media_items_.end()) {
        const auto index = static_cast<int>(std::distance(media_items_.begin(), existing));
        media_list_->setCurrentRow(index);
        updateMediaDetails(index);
        logging::Logger::instance().log(
            logging::Level::Debug,
            "media",
            "import",
            "Media was already imported.",
            {{"path", pathToUtf8(source_path)}});
        statusBar()->showMessage("Media is already imported.");
        return;
    }

    try {
        auto metadata = video_probe_.probe(source_path);
        auto first_frame = video_decoder_.decode_first_frame(source_path);
        addMediaItem(std::move(metadata), std::move(first_frame));
        logging::Logger::instance().log(
            logging::Level::Info,
            "media",
            "import",
            "Media metadata and first preview frame imported.",
            {{"path", pathToUtf8(source_path)},
             {"width", std::to_string(media_items_.back().first_frame.width)},
             {"height", std::to_string(media_items_.back().first_frame.height)}});
        statusBar()->showMessage("Media imported with preview frame.");
    } catch (const media::MediaError& error) {
        const QString message = fromUtf8(error.what());
        QMessageBox::warning(this, "Could not open media", message);
        statusBar()->showMessage("Could not import media.");
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "ui",
            "media_import",
            error.what(),
            {{"path", pathToUtf8(source_path)}});
        const QString message = fromUtf8(error.what());
        QMessageBox::warning(this, "Could not open media", message);
        statusBar()->showMessage("Could not import media.");
    }
}

void MainWindow::updateMediaDetails(int row) {
    pending_clip_activation_.reset();
    ++playback_generation_;
    if (playback_worker_ != nullptr) {
        QMetaObject::invokeMethod(playback_worker_, "stop", Qt::QueuedConnection);
    }
    playback_is_playing_ = false;
    playback_frame_index_ = 0;

    if (row < 0 || row >= static_cast<int>(media_items_.size())) {
        active_timeline_clip_index_.reset();
        media_details_->setText("No media imported.");
        preview_widget_->clearFrame("Preview area\n\nImport media to display its first frame.");
        updateTimelineState();
        updatePlaybackControls();
        updatePlaybackStatus();
        return;
    }

    const auto& item = media_items_[static_cast<size_t>(row)];
    active_timeline_clip_index_ = selectedTimelineClipIndex();
    media_details_->setText(mediaDetailsText(item.metadata));
    preview_widget_->setFrame(item.first_frame);
    updateTimelineState();
    updatePlaybackControls();
    updatePlaybackStatus();

    if (playback_worker_ != nullptr && canPreviewSelectedMedia()) {
        const QString source_path = fromUtf8(pathToUtf8(item.metadata.source_path));
        const double frame_rate = item.metadata.frame_rate.value_or(30.0);
        QMetaObject::invokeMethod(
            playback_worker_,
            "setMedia",
            Qt::QueuedConnection,
            Q_ARG(QString, source_path),
            Q_ARG(double, frame_rate),
            Q_ARG(quint64, playback_generation_));
    }
}

void MainWindow::addMediaItem(media::VideoMetadata metadata, media::VideoFrame first_frame) {
    media_items_.push_back({std::move(metadata), std::move(first_frame)});
    auto& item = media_items_.back();

    auto* list_item = new QListWidgetItem(mediaListText(item.metadata), media_list_);
    list_item->setToolTip(fromUtf8(pathToUtf8(item.metadata.source_path)));
    list_item->setData(
        Qt::UserRole,
        fromUtf8(pathToUtf8(item.metadata.source_path)));
    media_list_->setCurrentItem(list_item);
}

void MainWindow::handlePlaybackFrame(
    playback::VideoFramePtr frame,
    qint64 frame_index,
    quint64 generation) {
    if (generation != playback_generation_ || frame == nullptr) return;

    if (pending_clip_activation_.has_value() &&
        pending_clip_activation_->generation == generation) {
        const auto pending = *pending_clip_activation_;
        pending_clip_activation_.reset();
        commitTimelineClipActivation(
            pending.clip_index,
            pending.media_index,
            frame_index,
            false);
        preview_widget_->setFrame(*frame);
        if (timeline_widget_ != nullptr) {
            timeline_widget_->setPlayheadFrame(frame_index);
        }
        updatePlaybackStatus();
        return;
    }

    playback_frame_index_ = frame_index;
    preview_widget_->setFrame(*frame);
    if (timeline_widget_ != nullptr && selectedMediaMatchesTimeline()) {
        timeline_widget_->setPlayheadFrame(frame_index);
    }
    updatePlaybackStatus();
}

void MainWindow::handlePlaybackMediaReady(quint64 generation) {
    if (generation != playback_generation_ ||
        !pending_clip_activation_.has_value() ||
        pending_clip_activation_->generation != generation) {
        return;
    }

    const auto pending = *pending_clip_activation_;
    if (pending.target_frame > 0) {
        playback_worker_->requestSeek(
            static_cast<qint64>(pending.target_frame),
            generation);
        return;
    }

    pending_clip_activation_.reset();
    commitTimelineClipActivation(
        pending.clip_index,
        pending.media_index,
        0,
        true);

    if (pending.resume_playback && playback_worker_ != nullptr) {
        QMetaObject::invokeMethod(playback_worker_, "play", Qt::QueuedConnection);
    }
}

void MainWindow::handlePlaybackStateChanged(bool playing, quint64 generation) {
    if (generation != playback_generation_) return;

    playback_is_playing_ = playing;
    updatePlaybackControls();
    updatePlaybackStatus();
}

void MainWindow::handlePlaybackFinished(
    quint64 generation,
    bool during_playback) {
    if (generation != playback_generation_) return;

    playback_is_playing_ = false;
    if (during_playback && active_timeline_clip_index_.has_value()) {
        const auto next_clip_index = *active_timeline_clip_index_ + 1;
        if (next_clip_index < timeline_model_.clipCount()) {
            activateTimelineClip(next_clip_index, 0, true);
            return;
        }
    }

    updatePlaybackControls();
    if (playback_status_label_ != nullptr) {
        playback_status_label_->setText(
            during_playback ? "End of timeline." : "End of media.");
    }
}

void MainWindow::handlePlaybackError(
    const QString& message,
    qint64 error_code,
    quint64 generation) {
    if (generation != playback_generation_) return;

    if (pending_clip_activation_.has_value() &&
        pending_clip_activation_->generation == generation) {
        const auto pending = *pending_clip_activation_;
        if (pending.clip_index >= timeline_model_.clipCount()) {
            pending_clip_activation_.reset();
            playback_is_playing_ = false;
            updatePlaybackControls();
            updatePlaybackStatus();
            QMessageBox::warning(this, "Playback error", message);
            return;
        }
        const auto& clip = timeline_model_.clips()[pending.clip_index];
        logging::Context context{
            {"path", pathToUtf8(clip.source_path)},
            {"clip_index", std::to_string(pending.clip_index)},
            {"media_index", std::to_string(pending.media_index)},
            {"requested_frame", std::to_string(pending.target_frame)},
            {"generation", std::to_string(generation)}};
        if (error_code >= 0) {
            context.emplace_back("error_code", std::to_string(error_code));
        }
        logging::Logger::instance().log(
            logging::Level::Error,
            "playback",
            "transition",
            message.toUtf8().toStdString(),
            context);
        pending_clip_activation_.reset();
        statusBar()->showMessage("Could not activate the next timeline clip.");
    }

    playback_is_playing_ = false;
    updatePlaybackControls();
    if (playback_status_label_ != nullptr) {
        playback_status_label_->setText("Playback error.");
    }
    if (timeline_widget_ != nullptr && timeline_model_.hasClip()) {
        timeline_widget_->setPlayheadFrame(playback_frame_index_);
    }
    QMessageBox::warning(this, "Playback error", message);
}

void MainWindow::handleTimelineSeekStarted() {
    if (!canPreviewSelectedMedia() || pending_clip_activation_.has_value()) return;

    playback_is_playing_ = false;
    updatePlaybackControls();
    updatePlaybackStatus();
    sendPlaybackCommand("pause");
}

void MainWindow::handleTimelineSeek(qint64 frame_index) {
    if (playback_worker_ == nullptr ||
        !canPreviewSelectedMedia() ||
        pending_clip_activation_.has_value()) {
        return;
    }

    ++playback_generation_;
    playback_is_playing_ = false;
    if (timeline_widget_ != nullptr) timeline_widget_->setPlayheadFrame(frame_index);
    updatePlaybackControls();
    updatePlaybackStatus();

    playback_worker_->requestSeek(frame_index, playback_generation_);
}

void MainWindow::restoreDefaultLayout() {
    media_browser_dock_->setFloating(false);
    inspector_dock_->setFloating(false);
    timeline_dock_->setFloating(false);

    addDockWidget(Qt::LeftDockWidgetArea, media_browser_dock_);
    addDockWidget(Qt::RightDockWidgetArea, inspector_dock_);
    addDockWidget(Qt::BottomDockWidgetArea, timeline_dock_);

    media_browser_dock_->show();
    inspector_dock_->show();
    timeline_dock_->show();
}
