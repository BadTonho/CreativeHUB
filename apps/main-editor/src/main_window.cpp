#include "main_window.h"

#include "logging/logger.h"
#include "preview_widget.h"
#include "project/project_file.h"
#include "timeline/timeline_widget.h"
#include "ui/media_browser_list_widget.h"

#include <QAction>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QDesktopServices>
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
#include <QStatusBar>
#include <QUrl>
#include <QVBoxLayout>
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

QString mediaItemListText(const media::VideoMetadata& metadata,
                          std::string_view display_name,
                          std::string_view bin_path,
                          bool offline) {
    const QString state = offline ? " [Offline]" : "";
    return QString("%1%2 â€” %3 â€” %4")
        .arg(fromUtf8(display_name.empty() ? metadata.display_name : std::string(display_name)))
        .arg(state)
        .arg(fromUtf8(std::string(bin_path)))
        .arg(offline ? "Unavailable" : mediaListText(metadata));
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

    saved_project_document_ = currentProjectDocument();
    updateProjectDirtyState();

    statusBar()->showMessage("Ready");
}

MainWindow::~MainWindow() {
    shutdownPlayback();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (confirmProjectChange()) {
        event->accept();
        return;
    }
    event->ignore();
}

project::ProjectDocument MainWindow::currentProjectDocument() const {
    project::ProjectDocument document;
    document.media.reserve(media_items_.size());
    document.bins = bin_paths_;
    for (const auto& item : media_items_) {
        document.media.push_back(project::ProjectMedia{
            normalizedPath(item.metadata.source_path),
            item.display_name,
            item.bin_path,
            item.offline});
    }

    document.timeline_clips.reserve(timeline_model_.clipCount());
    for (const auto& clip : timeline_model_.clips()) {
        document.timeline_clips.push_back(project::ProjectClip{
            normalizedPath(clip.source_path),
            clip.source_start_frame,
            clip.timeline_duration_frames});
    }
    return document;
}

void MainWindow::updateProjectDirtyState() {
    if (!saved_project_document_.has_value()) {
        project_dirty_ = false;
    } else {
        project_dirty_ = currentProjectDocument() != *saved_project_document_;
    }

    setWindowTitle(project_dirty_ ? "Main Editor *" : "Main Editor");
    if (save_project_action_ != nullptr) {
        save_project_action_->setEnabled(!project_path_.has_value() || project_dirty_);
    }
    if (save_project_as_action_ != nullptr) save_project_as_action_->setEnabled(true);
}

bool MainWindow::saveProjectTo(
    const std::filesystem::path& project_path,
    const char* operation) {
    try {
        const auto document = currentProjectDocument();
        project::save(project_path, document);
        project_path_ = normalizedPath(project_path);
        saved_project_document_ = document;
        updateProjectDirtyState();
        statusBar()->showMessage(
            std::string_view(operation) == "save_as"
                ? "Project saved as."
                : "Project saved.");
        return true;
    } catch (const project::ProjectError& error) {
        logging::Context context{
            {"project_path", pathToUtf8(project_path)},
            {"cause", error.what()}};
        if (error.system_error().has_value()) {
            context.emplace_back("error_code", std::to_string(*error.system_error()));
        }
        logging::Logger::instance().log(
            logging::Level::Error,
            "project",
            operation,
            error.what(),
            context);
        QMessageBox::warning(this, "Could not save project", fromUtf8(error.what()));
        statusBar()->showMessage("Could not save project.");
        return false;
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "project",
            operation,
            error.what(),
            {{"project_path", pathToUtf8(project_path)}});
        QMessageBox::warning(this, "Could not save project", "The project could not be saved.");
        statusBar()->showMessage("Could not save project.");
        return false;
    }
}

void MainWindow::saveProject() {
    if (!project_path_.has_value()) {
        saveProjectAs();
        return;
    }
    static_cast<void>(saveProjectTo(*project_path_, "save"));
}

void MainWindow::saveProjectAs() {
    const QString initial_path = project_path_.has_value()
        ? fromUtf8(pathToUtf8(*project_path_))
        : QString();
    QString selected_path = QFileDialog::getSaveFileName(
        this,
        "Save Project As",
        initial_path,
        "Creative Suite Project (*.csp);;All Files (*)");
    if (selected_path.isEmpty()) return;

    if (!selected_path.endsWith(".csp", Qt::CaseInsensitive)) {
        selected_path += ".csp";
    }
    static_cast<void>(saveProjectTo(
        normalizedPath(QFileInfo(selected_path).filesystemFilePath()),
        "save_as"));
}

bool MainWindow::confirmProjectChange() {
    if (!project_dirty_) return true;

    QMessageBox prompt(this);
    prompt.setIcon(QMessageBox::Warning);
    prompt.setWindowTitle("Unsaved Changes");
    prompt.setText("The current project has unsaved changes.");
    prompt.setInformativeText("Do you want to save the project before continuing?");
    auto* save_button = prompt.addButton("Save", QMessageBox::AcceptRole);
    auto* discard_button = prompt.addButton("Discard", QMessageBox::DestructiveRole);
    auto* cancel_button = prompt.addButton("Cancel", QMessageBox::RejectRole);
    prompt.setDefaultButton(save_button);
    prompt.exec();

    if (prompt.clickedButton() == save_button) {
        saveProject();
        return !project_dirty_;
    }
    if (prompt.clickedButton() == discard_button) return true;
    static_cast<void>(cancel_button);
    return false;
}

void MainWindow::clearProjectState() {
    pending_clip_activation_.reset();
    ++playback_generation_;
    playback_is_playing_ = false;
    if (playback_worker_ != nullptr) {
        QMetaObject::invokeMethod(playback_worker_, "stop", Qt::QueuedConnection);
    }

    timeline_model_.clear();
    timeline_history_.clear();
    active_timeline_clip_index_.reset();
    playback_frame_index_ = 0;
    project_path_.reset();
    saved_project_document_ = project::ProjectDocument{};
    project_dirty_ = false;

    {
        const QSignalBlocker blocker(media_list_);
        media_list_->clear();
    }
    media_items_.clear();
    bin_paths_ = {"Unsorted"};
    populateMediaBrowser();
    media_details_->setText("No media imported.");
    preview_widget_->clearFrame("Preview area\n\nImport media to display its first frame.");
    updateTimelineState();
    updatePlaybackControls();
    updatePlaybackStatus();
    updateHistoryActions();
}

void MainWindow::newProject() {
    if (!confirmProjectChange()) return;

    try {
        clearProjectState();
        statusBar()->showMessage("New project created.");
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "project",
            "new",
            error.what(),
            {{"project_path", project_path_.has_value()
                    ? pathToUtf8(*project_path_)
                    : ""},
             {"generation", std::to_string(playback_generation_)}});
        QMessageBox::warning(this, "Could not create project", "The new project could not be created.");
        statusBar()->showMessage("Could not create project.");
    }
}

void MainWindow::openProject() {
    const QString selected_file = QFileDialog::getOpenFileName(
        this,
        "Open Project",
        QString(),
        "Creative Suite Project (*.csp);;All Files (*)");
    if (selected_file.isEmpty()) return;

    if (!confirmProjectChange()) return;

    const auto project_path = normalizedPath(
        QFileInfo(selected_file).filesystemFilePath());
    std::filesystem::path current_media_path;
    std::optional<std::size_t> current_clip_index;
    try {
        const auto document = project::load(project_path);
        auto normalized_document = document;
        std::vector<ImportedMedia> loaded_media;
        loaded_media.reserve(document.media.size());

        for (std::size_t media_index = 0; media_index < document.media.size(); ++media_index) {
            const auto& project_media = document.media[media_index];
            current_media_path = project_media.source_path;
            auto& normalized_media = normalized_document.media[media_index];
            if (normalized_media.bin_path.empty()) normalized_media.bin_path = "Unsorted";
            std::error_code file_error;
            const bool exists = std::filesystem::is_regular_file(
                project_media.source_path, file_error) && !file_error;
            if (project_media.offline || !exists) {
                if (!exists && !project_media.offline) {
                    normalized_document.media[media_index].offline = true;
                    logging::Logger::instance().log(
                        logging::Level::Warning,
                        "project",
                        "open",
                        "Project media is unavailable and was loaded offline.",
                        {{"project_path", pathToUtf8(project_path)},
                         {"media_path", pathToUtf8(project_media.source_path)},
                         {"name", project_media.display_name},
                         {"bin", project_media.bin_path},
                         {"offline", "true"}});
                }
                media::VideoMetadata metadata;
                metadata.source_path = normalizedPath(project_media.source_path);
                metadata.display_name = project_media.display_name.empty()
                    ? media::MediaLibrary::defaultDisplayName(metadata.source_path)
                    : project_media.display_name;
                normalized_media.display_name = metadata.display_name;
                loaded_media.push_back({std::move(metadata), {},
                                        loaded_media.empty() ? project_media.display_name : project_media.display_name,
                                        project_media.bin_path,
                                        true});
                if (loaded_media.back().display_name.empty()) {
                    loaded_media.back().display_name = loaded_media.back().metadata.display_name;
                }
                continue;
            }

            auto metadata = video_probe_.probe(project_media.source_path);
            auto first_frame = video_decoder_.decode_first_frame(project_media.source_path);
            const auto display_name = project_media.display_name.empty()
                ? metadata.display_name
                : project_media.display_name;
            metadata.display_name = display_name;
            normalized_media.display_name = display_name;
            loaded_media.push_back({std::move(metadata), std::move(first_frame),
                                    display_name, project_media.bin_path, false});
        }

        if (normalized_document.bins.empty()) normalized_document.bins.push_back("Unsorted");
        for (const auto& item : loaded_media) {
            std::size_t start = 0;
            while (start < item.bin_path.size()) {
                const auto separator = item.bin_path.find('/', start);
                const auto path = item.bin_path.substr(
                    0, separator == std::string::npos ? item.bin_path.size() : separator);
                if (std::find(normalized_document.bins.begin(), normalized_document.bins.end(), path) ==
                    normalized_document.bins.end()) {
                    normalized_document.bins.push_back(path);
                }
                start = separator == std::string::npos ? item.bin_path.size() : separator + 1;
            }
        }

        auto media_index_for = [&loaded_media](const std::filesystem::path& source_path)
            -> std::optional<std::size_t> {
            const auto normalized_source = normalizedPath(source_path);
            for (std::size_t index = 0; index < loaded_media.size(); ++index) {
                if (normalizedPath(loaded_media[index].metadata.source_path) == normalized_source) {
                    return index;
                }
            }
            return std::nullopt;
        };

        auto available_frame_count = [](const media::VideoMetadata& metadata)
            -> std::optional<std::int64_t> {
            if (metadata.frame_count.has_value() && *metadata.frame_count > 0) {
                return metadata.frame_count;
            }
            if (!metadata.duration_seconds.has_value() ||
                !metadata.frame_rate.has_value() ||
                !std::isfinite(*metadata.duration_seconds) ||
                !std::isfinite(*metadata.frame_rate) ||
                *metadata.duration_seconds <= 0.0 ||
                *metadata.frame_rate <= 0.0) {
                return std::nullopt;
            }
            const double estimated = *metadata.duration_seconds * *metadata.frame_rate;
            if (!std::isfinite(estimated) ||
                estimated > static_cast<double>(std::numeric_limits<std::int64_t>::max())) {
                return std::nullopt;
            }
            return std::max<std::int64_t>(1, static_cast<std::int64_t>(std::ceil(estimated)));
        };

        timeline::TimelineModel::Snapshot snapshot;
        std::int64_t timeline_start = 0;
        for (std::size_t index = 0; index < document.timeline_clips.size(); ++index) {
            current_clip_index = index;
            const auto& project_clip = document.timeline_clips[index];
            const auto media_index = media_index_for(project_clip.source_path);
            if (!media_index.has_value()) {
                throw project::ProjectError(
                    project::ProjectErrorCode::MediaUnavailable,
                    "A timeline clip refers to media that is not imported in the project.",
                    std::nullopt,
                    project_clip.source_path);
            }

            const auto& loaded_item = loaded_media[*media_index];
            const auto& metadata = loaded_item.metadata;
            const auto available_frames = loaded_item.offline
                ? std::optional<std::int64_t>{}
                : available_frame_count(metadata);
            if ((!loaded_item.offline && !available_frames.has_value()) ||
                project_clip.source_start_frame < 0 ||
                project_clip.duration_frames <= 0 ||
                (!loaded_item.offline &&
                 (project_clip.source_start_frame > *available_frames ||
                  project_clip.duration_frames > *available_frames -
                      project_clip.source_start_frame)) ||
                timeline_start > std::numeric_limits<std::int64_t>::max() -
                    project_clip.duration_frames) {
                throw project::ProjectError(
                    project::ProjectErrorCode::InvalidTimeline,
                    "A timeline clip is outside the current media bounds.",
                    std::nullopt,
                    project_clip.source_path);
            }

            snapshot.clips.push_back(timeline::TimelineClip{
                timeline_start,
                project_clip.source_start_frame,
                project_clip.duration_frames,
                normalizedPath(metadata.source_path),
                loaded_item.display_name,
                metadata.duration_seconds,
                metadata.frame_rate,
                metadata.frame_count});
            timeline_start += project_clip.duration_frames;
        }

        applyLoadedProject(
            std::move(loaded_media),
            std::move(snapshot),
            project_path,
            normalized_document);
        statusBar()->showMessage("Project opened.");
    } catch (const project::ProjectError& error) {
        logging::Context context{
            {"project_path", pathToUtf8(project_path)},
            {"cause", error.what()}};
        const bool related_path_is_project = !error.related_path().empty() &&
            normalizedPath(error.related_path()) == project_path;
        if (!error.related_path().empty() && !related_path_is_project) {
            context.emplace_back("media_path", pathToUtf8(error.related_path()));
        } else if (!current_media_path.empty() &&
                   error.code() == project::ProjectErrorCode::MediaUnavailable) {
            context.emplace_back("media_path", pathToUtf8(current_media_path));
        }
        if (current_clip_index.has_value()) {
            context.emplace_back("clip_index", std::to_string(*current_clip_index));
        }
        if (error.system_error().has_value()) {
            context.emplace_back("error_code", std::to_string(*error.system_error()));
        }
        logging::Logger::instance().log(
            logging::Level::Error,
            "project",
            "open",
            error.what(),
            context);
        QMessageBox::warning(this, "Could not open project", fromUtf8(error.what()));
        statusBar()->showMessage("Could not open project.");
    } catch (const media::MediaError& error) {
        logging::Context context{
            {"project_path", pathToUtf8(project_path)},
            {"media_path", pathToUtf8(current_media_path)},
            {"cause", error.what()}};
        if (current_clip_index.has_value()) {
            context.emplace_back("clip_index", std::to_string(*current_clip_index));
        }
        if (error.error_code().has_value()) {
            context.emplace_back("error_code", std::to_string(*error.error_code()));
        }
        logging::Logger::instance().log(
            logging::Level::Error,
            "project",
            "open",
            error.what(),
            context);
        QMessageBox::warning(this, "Could not open project", fromUtf8(error.what()));
        statusBar()->showMessage("Could not open project.");
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "project",
            "open",
            error.what(),
            { {"project_path", pathToUtf8(project_path)},
              {"media_path", pathToUtf8(current_media_path)},
              {"clip_index", current_clip_index.has_value()
                    ? std::to_string(*current_clip_index)
                    : "none"} });
        QMessageBox::warning(this, "Could not open project", "The project could not be opened.");
        statusBar()->showMessage("Could not open project.");
    }
}

void MainWindow::applyLoadedProject(
    std::vector<ImportedMedia> media_items,
    timeline::TimelineModel::Snapshot timeline_snapshot,
    const std::filesystem::path& project_path,
    const project::ProjectDocument& saved_document) {
    pending_clip_activation_.reset();
    ++playback_generation_;
    playback_is_playing_ = false;
    if (playback_worker_ != nullptr) {
        QMetaObject::invokeMethod(playback_worker_, "stop", Qt::QueuedConnection);
    }

    timeline_model_.restore(std::move(timeline_snapshot));
    timeline_history_.clear();
    media_items_ = std::move(media_items);
    bin_paths_ = saved_document.bins.empty()
        ? std::vector<std::string>{"Unsorted"}
        : saved_document.bins;
    for (const auto& item : media_items_) {
        std::size_t start = 0;
        while (start < item.bin_path.size()) {
            const auto separator = item.bin_path.find('/', start);
            const auto path = item.bin_path.substr(
                0, separator == std::string::npos ? item.bin_path.size() : separator);
            if (std::find(bin_paths_.begin(), bin_paths_.end(), path) == bin_paths_.end()) {
                bin_paths_.push_back(path);
            }
            start = separator == std::string::npos ? item.bin_path.size() : separator + 1;
        }
    }
    active_timeline_clip_index_.reset();
    playback_frame_index_ = 0;
    project_path_ = normalizedPath(project_path);
    saved_project_document_ = saved_document;

    populateMediaBrowser();

    media_details_->setText("No media imported.");
    preview_widget_->clearFrame("Preview area\n\nImport media to display its first frame.");
    updateProjectDirtyState();

    if (timeline_model_.hasClip()) {
        active_timeline_clip_index_ = 0;
        const auto& clip = timeline_model_.clips().front();
        const auto media = std::find_if(
            media_items_.begin(),
            media_items_.end(),
            [&clip](const ImportedMedia& item) {
                return normalizedPath(item.metadata.source_path) == normalizedPath(clip.source_path);
            });
        if (media != media_items_.end()) {
            const auto media_index = static_cast<std::size_t>(std::distance(media_items_.begin(), media));
            populateMediaBrowser(clip.source_path);
            media_details_->setText(mediaDetailsText(media->metadata));
            if (!media->offline) preview_widget_->setFrame(media->first_frame);
            if (!media->offline) activateTimelineClip(0, 0, false);
            static_cast<void>(media_index);
        }
    } else if (!media_items_.empty()) {
        media_list_->setCurrentRow(0);
        updateMediaDetails(0);
    } else {
        updateTimelineState();
        updatePlaybackControls();
        updatePlaybackStatus();
    }

    updateTimelineState();
    updatePlaybackControls();
    updatePlaybackStatus();
    updateHistoryActions();
}

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
    new_project_action_ = file_menu->addAction("&New Project");
    new_project_action_->setShortcut(QKeySequence("Ctrl+N"));
    new_project_action_->setShortcutContext(Qt::WindowShortcut);
    connect(new_project_action_, &QAction::triggered, this, &MainWindow::newProject);
    auto* open_media_action = file_menu->addAction("Open &Media...");
    connect(open_media_action, &QAction::triggered, this, &MainWindow::openMedia);
    open_project_action_ = file_menu->addAction("&Open Project...");
    open_project_action_->setShortcut(QKeySequence("Ctrl+O"));
    open_project_action_->setShortcutContext(Qt::WindowShortcut);
    connect(open_project_action_, &QAction::triggered, this, &MainWindow::openProject);
    save_project_action_ = file_menu->addAction("&Save Project");
    save_project_action_->setShortcut(QKeySequence("Ctrl+S"));
    save_project_action_->setShortcutContext(Qt::WindowShortcut);
    connect(save_project_action_, &QAction::triggered, this, &MainWindow::saveProject);
    save_project_as_action_ = file_menu->addAction("Save Project &As...");
    save_project_as_action_->setShortcut(QKeySequence("Ctrl+Shift+S"));
    save_project_as_action_->setShortcutContext(Qt::WindowShortcut);
    connect(save_project_as_action_, &QAction::triggered, this, &MainWindow::saveProjectAs);
    file_menu->addSeparator();
    auto* exit_action = file_menu->addAction("E&xit");
    connect(exit_action, &QAction::triggered, this, &QWidget::close);

    auto* edit_menu = menuBar()->addMenu("&Edit");
    auto* undo_action = edit_menu->addAction("&Undo");
    undo_action->setShortcut(QKeySequence::Undo);
    undo_action->setShortcutContext(Qt::WindowShortcut);
    undo_action_ = undo_action;
    connect(undo_action_, &QAction::triggered, this, &MainWindow::undoTimelineEdit);
    auto* redo_action = edit_menu->addAction("&Redo");
    redo_action->setShortcut(QKeySequence::Redo);
    redo_action->setShortcutContext(Qt::WindowShortcut);
    redo_action_ = redo_action;
    connect(redo_action_, &QAction::triggered, this, &MainWindow::redoTimelineEdit);
    edit_menu->addSeparator();
    auto* delete_clip_action = edit_menu->addAction("Delete Selected Clip");
    delete_clip_action->setShortcut(QKeySequence(Qt::Key_Delete));
    delete_clip_action->setShortcutContext(Qt::WindowShortcut);
    delete_clip_action_ = delete_clip_action;
    delete_clip_action_->setEnabled(false);
    connect(
        delete_clip_action_,
        &QAction::triggered,
        this,
        &MainWindow::deleteActiveTimelineClip);
    auto* split_clip_action = edit_menu->addAction("Split Clip at Playhead");
    split_clip_action->setShortcut(QKeySequence("Ctrl+K"));
    split_clip_action->setShortcutContext(Qt::WindowShortcut);
    connect(
        split_clip_action,
        &QAction::triggered,
        this,
        &MainWindow::splitActiveClipAtPlayhead);
    auto* razor_tool_action = edit_menu->addAction("Blade Tool");
    razor_tool_action->setCheckable(true);
    razor_tool_action_ = razor_tool_action;
    connect(razor_tool_action_, &QAction::toggled, this, [this](bool enabled) {
        if (razor_button_ != nullptr && razor_button_->isChecked() != enabled) {
            razor_button_->setChecked(enabled);
        }
        if (timeline_widget_ != nullptr) timeline_widget_->setRazorMode(enabled);
    });

    auto* view_menu = menuBar()->addMenu("&View");
    view_menu->addAction(media_browser_dock_->toggleViewAction());
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

    auto* move_left_action = new QAction(this);
    move_left_action->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Left));
    move_left_action->setShortcutContext(Qt::WindowShortcut);
    connect(move_left_action, &QAction::triggered, this, [this]() {
        moveActiveTimelineClip(-1);
    });
    addAction(move_left_action);

    auto* move_right_action = new QAction(this);
    move_right_action->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Right));
    move_right_action->setShortcutContext(Qt::WindowShortcut);
    connect(move_right_action, &QAction::triggered, this, [this]() {
        moveActiveTimelineClip(1);
    });
    addAction(move_right_action);

    updateHistoryActions();
}

QWidget* MainWindow::createMediaBrowser() {
    auto* container = new QWidget;
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    auto* title = new QLabel("Media Browser", container);
    title->setStyleSheet("font-weight: 600; font-size: 14px;");
    layout->addWidget(title);

    auto* browser_controls = new QHBoxLayout;
    new_bin_button_ = new QPushButton("New Bin", container);
    connect(new_bin_button_, &QPushButton::clicked, this, &MainWindow::createBin);
    browser_controls->addWidget(new_bin_button_);
    browser_controls->addStretch();
    layout->addLayout(browser_controls);

    bin_tree_ = new QTreeWidget(container);
    bin_tree_->setHeaderHidden(true);
    bin_tree_->setMaximumHeight(150);
    bin_tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(bin_tree_, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem*, QTreeWidgetItem*) { updateMediaBrowserFilter(); });
    connect(bin_tree_, &QTreeWidget::customContextMenuRequested, this,
            &MainWindow::showMediaContextMenu);
    layout->addWidget(bin_tree_);

    media_list_ = new MediaBrowserListWidget(container);
    media_list_->setSelectionMode(QAbstractItemView::SingleSelection);
    media_list_->setWordWrap(true);
    connect(media_list_, &QListWidget::currentRowChanged, this, &MainWindow::updateMediaDetails);
    media_list_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(media_list_, &QListWidget::customContextMenuRequested, this,
            &MainWindow::showMediaContextMenu);
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

    populateMediaBrowser();
    updateTimelineState();

    return container;
}

std::optional<std::size_t> MainWindow::selectedMediaIndex() const noexcept {
    if (media_list_ == nullptr || media_list_->currentItem() == nullptr) return std::nullopt;
    bool ok = false;
    const auto value = media_list_->currentItem()->data(Qt::UserRole + 1).toLongLong(&ok);
    if (!ok || value < 0 || value >= static_cast<qint64>(media_items_.size())) return std::nullopt;
    return static_cast<std::size_t>(value);
}

std::string MainWindow::selectedBinPath() const {
    if (bin_tree_ == nullptr || bin_tree_->currentItem() == nullptr) return {};
    return bin_tree_->currentItem()->data(0, Qt::UserRole).toString().toStdString();
}

void MainWindow::populateMediaBrowser(const std::filesystem::path& selected_path) {
    if (media_list_ == nullptr || bin_tree_ == nullptr) return;

    std::filesystem::path path_to_select = selected_path;
    if (path_to_select.empty()) {
        const auto selected = selectedMediaIndex();
        if (selected.has_value()) path_to_select = media_items_[*selected].metadata.source_path;
    }

    std::string selected_bin = selectedBinPath();
    if (selected_bin.empty()) selected_bin = "";

    std::vector<std::string> bins;
    for (const auto& bin : bin_paths_) {
        if (std::find(bins.begin(), bins.end(), bin) == bins.end()) bins.push_back(bin);
    }
    for (const auto& item : media_items_) {
        std::string current;
        std::size_t start = 0;
        while (start < item.bin_path.size()) {
            const auto separator = item.bin_path.find('/', start);
            const auto part = item.bin_path.substr(
                start,
                separator == std::string::npos ? item.bin_path.size() - start : separator - start);
            if (!current.empty()) current += '/';
            current += part;
            if (std::find(bins.begin(), bins.end(), current) == bins.end()) bins.push_back(current);
            start = separator == std::string::npos ? item.bin_path.size() : separator + 1;
        }
    }
    if (std::find(bins.begin(), bins.end(), "Unsorted") == bins.end()) bins.emplace_back("Unsorted");
    std::sort(bins.begin(), bins.end());

    {
        const QSignalBlocker tree_blocker(bin_tree_);
        bin_tree_->clear();
        auto* all = new QTreeWidgetItem(bin_tree_, {"All Media"});
        all->setData(0, Qt::UserRole, QString());
        bin_tree_->setCurrentItem(all);
        for (const auto& bin : bins) {
            QTreeWidgetItem* parent = all;
            std::string current;
            std::size_t start = 0;
            while (start < bin.size()) {
                const auto separator = bin.find('/', start);
                const auto part = bin.substr(
                    start,
                    separator == std::string::npos ? bin.size() - start : separator - start);
                if (!current.empty()) current += '/';
                current += part;
                QTreeWidgetItem* child = nullptr;
                for (int index = 0; index < parent->childCount(); ++index) {
                    if (parent->child(index)->data(0, Qt::UserRole).toString().toStdString() == current) {
                        child = parent->child(index);
                        break;
                    }
                }
                if (child == nullptr) {
                    child = new QTreeWidgetItem(parent, {QString::fromStdString(part)});
                    child->setData(0, Qt::UserRole, QString::fromStdString(current));
                }
                parent = child;
                start = separator == std::string::npos ? bin.size() : separator + 1;
            }
        }
        std::function<QTreeWidgetItem*(QTreeWidgetItem*)> find_bin =
            [&find_bin, &selected_bin](QTreeWidgetItem* item) -> QTreeWidgetItem* {
                if (item->data(0, Qt::UserRole).toString().toStdString() == selected_bin) return item;
                for (int index = 0; index < item->childCount(); ++index) {
                    if (auto* found = find_bin(item->child(index)); found != nullptr) return found;
                }
                return nullptr;
            };
        if (!selected_bin.empty()) {
            if (auto* found = find_bin(all); found != nullptr) bin_tree_->setCurrentItem(found);
        }
    }

    {
        const QSignalBlocker list_blocker(media_list_);
        media_list_->clear();
        const auto active_bin = selectedBinPath();
        for (std::size_t index = 0; index < media_items_.size(); ++index) {
            const auto& item = media_items_[index];
            if (!active_bin.empty() &&
                item.bin_path != active_bin &&
                item.bin_path.rfind(active_bin + '/', 0) != 0) {
                continue;
            }
            auto* list_item = new QListWidgetItem(
                mediaItemListText(item.metadata, item.display_name, item.bin_path, item.offline),
                media_list_);
            list_item->setToolTip(fromUtf8(pathToUtf8(item.metadata.source_path)));
            list_item->setData(Qt::UserRole, fromUtf8(pathToUtf8(item.metadata.source_path)));
            list_item->setData(Qt::UserRole + 1, static_cast<qint64>(index));
            if (!path_to_select.empty() &&
                normalizedPath(item.metadata.source_path) == normalizedPath(path_to_select)) {
                media_list_->setCurrentItem(list_item);
            }
        }
    }

    if (media_list_->currentRow() >= 0) {
        updateMediaDetails(media_list_->currentRow());
    } else {
        media_details_->setText(media_items_.empty() ? "No media imported." : "No media in this bin.");
        updateTimelineState();
        updatePlaybackControls();
        updatePlaybackStatus();
    }
}

void MainWindow::updateMediaBrowserFilter() {
    populateMediaBrowser();
}

void MainWindow::createBin() {
    bool accepted = false;
    const QString value = QInputDialog::getText(
        this, "New Bin", "Bin path (use / for sub-bins):", QLineEdit::Normal,
        "New Bin", &accepted);
    if (!accepted) return;
    if (!media::MediaLibrary::validBinPath(value.toStdString())) {
        QMessageBox::warning(this, "Invalid bin", "Use a non-empty bin path with / separators.");
        return;
    }
    const std::string current = value.toStdString();
    if (std::find(bin_paths_.begin(), bin_paths_.end(), current) != bin_paths_.end()) return;
    std::size_t start = 0;
    while (start < current.size()) {
        const auto separator = current.find('/', start);
        const auto path = current.substr(
            0, separator == std::string::npos ? current.size() : separator);
        if (std::find(bin_paths_.begin(), bin_paths_.end(), path) == bin_paths_.end()) {
            bin_paths_.push_back(path);
        }
        start = separator == std::string::npos ? current.size() : separator + 1;
    }
    updateProjectDirtyState();
    populateMediaBrowser();
}

void MainWindow::renameSelectedBin() {
    const auto old_path = selectedBinPath();
    if (old_path.empty() || old_path == "Unsorted") return;
    bool accepted = false;
    const QString value = QInputDialog::getText(
        this, "Rename Bin", "New bin path:", QLineEdit::Normal,
        QString::fromStdString(old_path), &accepted);
    if (!accepted) return;
    media::MediaLibrary library;
    for (const auto& item : media_items_) {
        library.addOffline(item.metadata.source_path, item.display_name, item.bin_path);
    }
    const auto result = library.renameBin(old_path, value.toStdString());
    if (result != media::MediaMutationResult::Changed) {
        QMessageBox::warning(this, "Could not rename bin", "The bin name is invalid or already exists.");
        return;
    }
    for (std::size_t index = 0; index < media_items_.size(); ++index) {
        const auto library_index = library.indexForPath(media_items_[index].metadata.source_path);
        if (library_index != library.size()) media_items_[index].bin_path = library.items()[library_index].bin_path;
    }
    bin_paths_.clear();
    bin_paths_.push_back("Unsorted");
    for (const auto& item : media_items_) {
        if (std::find(bin_paths_.begin(), bin_paths_.end(), item.bin_path) == bin_paths_.end()) {
            bin_paths_.push_back(item.bin_path);
        }
    }
    updateProjectDirtyState();
    populateMediaBrowser();
}

void MainWindow::renameSelectedMedia() {
    const auto index = selectedMediaIndex();
    if (!index.has_value()) return;
    bool accepted = false;
    const QString value = QInputDialog::getText(
        this, "Rename Media", "Display name:", QLineEdit::Normal,
        QString::fromStdString(media_items_[*index].display_name), &accepted);
    if (!accepted) return;
    if (value.isEmpty()) {
        QMessageBox::warning(this, "Invalid name", "The display name cannot be empty.");
        return;
    }
    media_items_[*index].display_name = value.toStdString();
    media_items_[*index].metadata.display_name = media_items_[*index].display_name;
    timeline_model_.updateDisplayNameForSource(
        media_items_[*index].metadata.source_path,
        media_items_[*index].display_name);
    updateProjectDirtyState();
    populateMediaBrowser(media_items_[*index].metadata.source_path);
}

void MainWindow::moveSelectedMediaToBin() {
    const auto index = selectedMediaIndex();
    if (!index.has_value()) return;
    QStringList choices;
    for (const auto& bin : bin_paths_) {
        if (choices.indexOf(QString::fromStdString(bin)) < 0) {
            choices.push_back(QString::fromStdString(bin));
        }
    }
    if (choices.isEmpty()) choices << "Unsorted";
    bool accepted = false;
    const QString value = QInputDialog::getItem(
        this, "Move to Bin", "Bin:", choices,
        choices.indexOf(QString::fromStdString(media_items_[*index].bin_path)), false, &accepted);
    if (!accepted) return;
    if (value.toStdString() == media_items_[*index].bin_path) return;
    media_items_[*index].bin_path = value.toStdString();
    updateProjectDirtyState();
    populateMediaBrowser(media_items_[*index].metadata.source_path);
}

void MainWindow::removeSelectedMedia() {
    const auto index = selectedMediaIndex();
    if (!index.has_value() || media_items_[*index].offline) return;
    media_items_[*index].offline = true;
    media_items_[*index].first_frame = {};
    ++playback_generation_;
    playback_is_playing_ = false;
    if (playback_worker_ != nullptr) QMetaObject::invokeMethod(playback_worker_, "stop", Qt::QueuedConnection);
    preview_widget_->clearFrame("Preview area\n\nThe selected media is offline.");
    updateProjectDirtyState();
    populateMediaBrowser(media_items_[*index].metadata.source_path);
    statusBar()->showMessage("Media marked offline. Timeline clips were preserved.");
}

void MainWindow::restoreSelectedMedia() {
    const auto index = selectedMediaIndex();
    if (!index.has_value() || !media_items_[*index].offline) return;
    const auto path = media_items_[*index].metadata.source_path;
    try {
        auto metadata = video_probe_.probe(path);
        auto frame = video_decoder_.decode_first_frame(path);
        metadata.display_name = media_items_[*index].display_name;
        media_items_[*index].metadata = std::move(metadata);
        media_items_[*index].first_frame = std::move(frame);
        media_items_[*index].offline = false;
        updateProjectDirtyState();
        populateMediaBrowser(path);
        statusBar()->showMessage("Media restored.");
    } catch (const media::MediaError& error) {
        logging::Context context{{"path", pathToUtf8(path)}, {"cause", error.what()}};
        if (error.error_code().has_value()) context.emplace_back("error_code", std::to_string(*error.error_code()));
        logging::Logger::instance().log(logging::Level::Error, "media", "restore", error.what(), context);
        QMessageBox::warning(this, "Could not restore media", fromUtf8(error.what()));
    }
}

void MainWindow::showMediaContextMenu(const QPoint& position) {
    QMenu menu(this);
    auto* new_bin = menu.addAction("New Bin");
    connect(new_bin, &QAction::triggered, this, &MainWindow::createBin);
    const bool from_media_list = sender() == media_list_;
    if (from_media_list) {
        if (auto* item = media_list_->itemAt(position); item != nullptr) {
            media_list_->setCurrentItem(item);
        }
    } else if (bin_tree_ != nullptr) {
        if (auto* item = bin_tree_->itemAt(position); item != nullptr) {
            bin_tree_->setCurrentItem(item);
        }
    }
    const auto media_index = from_media_list ? selectedMediaIndex() : std::nullopt;
    if (media_index.has_value()) {
        menu.addSeparator();
        auto* rename = menu.addAction("Rename");
        connect(rename, &QAction::triggered, this, &MainWindow::renameSelectedMedia);
        auto* move = menu.addAction("Move to Bin");
        connect(move, &QAction::triggered, this, &MainWindow::moveSelectedMediaToBin);
        auto* remove = menu.addAction(media_items_[*media_index].offline
            ? "Restore Media" : "Remove from Browser");
        connect(remove, &QAction::triggered, this,
                media_items_[*media_index].offline
                    ? &MainWindow::restoreSelectedMedia
                    : &MainWindow::removeSelectedMedia);
    } else if (!selectedBinPath().empty()) {
        menu.addSeparator();
        auto* rename = menu.addAction("Rename");
        connect(rename, &QAction::triggered, this, &MainWindow::renameSelectedBin);
    }
    menu.exec((sender() == media_list_ ? media_list_->viewport()->mapToGlobal(position)
                                       : bin_tree_->viewport()->mapToGlobal(position)));
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
    razor_button_ = new QPushButton("Blade Tool", container);
    razor_button_->setCheckable(true);
    controls->addWidget(previous_frame_button_);
    controls->addWidget(play_pause_button_);
    controls->addWidget(next_frame_button_);
    controls->addWidget(clear_timeline_button_);
    controls->addWidget(razor_button_);
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
    connect(razor_button_, &QPushButton::toggled, this, [this](bool enabled) {
        if (razor_tool_action_ != nullptr &&
            razor_tool_action_->isChecked() != enabled) {
            razor_tool_action_->setChecked(enabled);
        }
        if (timeline_widget_ != nullptr) timeline_widget_->setRazorMode(enabled);
    });
    connect(
        timeline_widget_,
        &timeline::TimelineWidget::clipSelected,
        this,
        &MainWindow::handleTimelineClipSelected);
    connect(
        timeline_widget_,
        &timeline::TimelineWidget::clipMoveRequested,
        this,
        &MainWindow::handleTimelineClipMove);
    connect(
        timeline_widget_,
        &timeline::TimelineWidget::clipSplitRequested,
        this,
        &MainWindow::handleTimelineClipSplit);
    connect(
        timeline_widget_,
        &timeline::TimelineWidget::trimStarted,
        this,
        &MainWindow::handleTimelineTrimStarted);
    connect(
        timeline_widget_,
        &timeline::TimelineWidget::clipTrimRequested,
        this,
        &MainWindow::handleTimelineClipTrim);
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
    return selectedMediaIndex().has_value();
}

std::optional<std::size_t> MainWindow::selectedTimelineClipIndex() const noexcept {
    if (!hasSelectedMedia() || !timeline_model_.hasClip()) return std::nullopt;

    const auto selected_index = selectedMediaIndex();
    if (!selected_index.has_value()) return std::nullopt;
    const auto& selected = media_items_[*selected_index];
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
        !media_items_[*selectedMediaIndex()].offline &&
        (!timeline_model_.hasClip() || selectedMediaMatchesTimeline());
}

bool MainWindow::canPlaybackSelectedMedia() const noexcept {
    return timeline_model_.hasClip() &&
        active_timeline_clip_index_.has_value() &&
        *active_timeline_clip_index_ < timeline_model_.clipCount() &&
        canPreviewSelectedMedia();
}

timeline::EditState MainWindow::captureTimelineEditState() const {
    timeline::EditState state;
    state.timeline = timeline_model_.snapshot();
    state.active_clip_index = active_timeline_clip_index_;
    if (hasSelectedMedia()) {
        state.selected_source_path = normalizedPath(
        media_items_[*selectedMediaIndex()]
                .metadata.source_path);
    }
    state.playhead_frame = playback_frame_index_;
    return state;
}

void MainWindow::recordTimelineEdit(timeline::EditState state) {
    try {
        timeline_history_.recordBeforeEdit(std::move(state));
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "timeline",
            "record_history",
            error.what(),
            {{"generation", std::to_string(playback_generation_)}});
    }
    updateHistoryActions();
}

void MainWindow::updateHistoryActions() {
    if (undo_action_ != nullptr) undo_action_->setEnabled(timeline_history_.canUndo());
    if (redo_action_ != nullptr) redo_action_->setEnabled(timeline_history_.canRedo());
}

void MainWindow::updateTimelineState() {
    const bool selected = hasSelectedMedia() && !media_items_[*selectedMediaIndex()].offline;
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

    updateHistoryActions();
    updateProjectDirtyState();
}

void MainWindow::addSelectedMediaToTimeline() {
    if (!hasSelectedMedia()) return;

    const auto selected_index = selectedMediaIndex();
    if (!selected_index.has_value()) return;
    const auto& selected = media_items_[*selected_index];
    if (selected.offline) {
        statusBar()->showMessage("Offline media cannot be added to the timeline.");
        return;
    }
    try {
        const auto before_edit = captureTimelineEditState();
        switch (timeline_model_.addClip(selected.metadata)) {
        case timeline::AddClipResult::Added:
            recordTimelineEdit(before_edit);
            active_timeline_clip_index_ = timeline_model_.clipCount() - 1;
            playback_frame_index_ = 0;
            updateTimelineState();
            updatePlaybackControls();
            updatePlaybackStatus();
            activateTimelineClip(*active_timeline_clip_index_, 0, false);
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
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "timeline",
            "add_clip",
            error.what(),
            {{"path", pathToUtf8(selected.metadata.source_path)}});
        statusBar()->showMessage("Could not add media to the timeline.");
        QMessageBox::warning(
            this,
            "Timeline error",
            "The media could not be added to the timeline.");
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

        populateMediaBrowser(dropped_path);
        const auto index = selectedMediaIndex();
        if (!index.has_value()) return;
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

    const auto media_index = static_cast<std::size_t>(
        std::distance(media_items_.begin(), media_item));
    populateMediaBrowser(clip.source_path);
    const bool selected_after_filter = selectedMediaIndex().has_value() &&
        *selectedMediaIndex() == media_index;
    if (!selected_after_filter && had_pending_activation) updateMediaDetails(-1);

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
    if (media_item == media_items_.end() || media_item->offline ||
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
                : media_item->offline
                ? "The timeline clip refers to offline media."
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
        clip.source_start_frame,
        clip.timeline_duration_frames,
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
        Q_ARG(qint64, static_cast<qint64>(clip.source_start_frame)),
        Q_ARG(qint64, static_cast<qint64>(clip.timeline_duration_frames)),
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

    timeline::EditState before_edit;
    try {
        before_edit = captureTimelineEditState();
        pending_clip_activation_.reset();
        ++playback_generation_;
        playback_is_playing_ = false;
        if (playback_worker_ != nullptr) {
            QMetaObject::invokeMethod(
                playback_worker_,
                "stop",
                Qt::QueuedConnection);
        }

        timeline_model_.clear();
        recordTimelineEdit(before_edit);
        active_timeline_clip_index_.reset();
        playback_frame_index_ = 0;
        const int selected_row = media_list_ != nullptr ? media_list_->currentRow() : -1;
        if (selected_row >= 0) {
            updateMediaDetails(selected_row);
        } else {
            updateTimelineState();
            updatePlaybackControls();
            updatePlaybackStatus();
        }
        statusBar()->showMessage("Timeline cleared.");
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "timeline",
            "clear_timeline",
            error.what(),
            {{"generation", std::to_string(playback_generation_)}});
        playback_is_playing_ = false;
        updateTimelineState();
        updatePlaybackControls();
        updatePlaybackStatus();
        statusBar()->showMessage("Could not clear the timeline.");
        QMessageBox::warning(
            this,
            "Timeline error",
            "The timeline could not be cleared.");
    }
}

void MainWindow::restoreTimelineEditState(
    timeline::EditState state,
    const char* operation) {
    try {
        pending_clip_activation_.reset();
        ++playback_generation_;
        playback_is_playing_ = false;
        if (playback_worker_ != nullptr) {
            QMetaObject::invokeMethod(
                playback_worker_,
                "stop",
                Qt::QueuedConnection);
        }

        timeline_model_.restore(std::move(state.timeline));
        active_timeline_clip_index_ = state.active_clip_index;
        if (!active_timeline_clip_index_.has_value() ||
            *active_timeline_clip_index_ >= timeline_model_.clipCount()) {
            active_timeline_clip_index_.reset();
        }

        playback_frame_index_ = std::max<std::int64_t>(0, state.playhead_frame);
        if (active_timeline_clip_index_.has_value()) {
            const auto& active_clip = timeline_model_.clips()[*active_timeline_clip_index_];
            playback_frame_index_ = std::clamp<std::int64_t>(
                playback_frame_index_,
                0,
                std::max<std::int64_t>(0, active_clip.timeline_duration_frames - 1));
        } else {
            playback_frame_index_ = 0;
        }

        std::optional<std::size_t> selected_media_index;
        if (state.selected_source_path.has_value()) {
            const auto selected_path = normalizedPath(*state.selected_source_path);
            const auto selected_media = std::find_if(
                media_items_.begin(),
                media_items_.end(),
                [&selected_path](const ImportedMedia& item) {
                    return normalizedPath(item.metadata.source_path) == selected_path;
                });
            if (selected_media != media_items_.end()) {
                selected_media_index = static_cast<std::size_t>(
                    std::distance(media_items_.begin(), selected_media));
            } else {
                logging::Logger::instance().log(
                    logging::Level::Error,
                    "timeline",
                    operation,
                    "The selected media for the restored timeline state is unavailable.",
                    {{"path", pathToUtf8(selected_path)},
                     {"generation", std::to_string(playback_generation_)}});
            }
        }

        if (selected_media_index.has_value()) {
            const auto& item = media_items_[*selected_media_index];
            populateMediaBrowser(item.metadata.source_path);
            media_details_->setText(item.offline
                ? QString("Name: %1\nBin: %2\nStatus: Offline")
                    .arg(fromUtf8(item.display_name)).arg(fromUtf8(item.bin_path))
                : mediaDetailsText(item.metadata));
            if (!item.offline) preview_widget_->setFrame(item.first_frame);
        } else {
            media_details_->setText("No media imported.");
            preview_widget_->clearFrame(
                "Preview area\n\nImport media to display its first frame.");
        }

        updateTimelineState();
        updatePlaybackControls();
        updatePlaybackStatus();

        if (active_timeline_clip_index_.has_value() && selected_media_index.has_value() &&
            *active_timeline_clip_index_ < timeline_model_.clipCount() &&
            timeline_model_.clips()[*active_timeline_clip_index_].source_path ==
                normalizedPath(media_items_[*selected_media_index]
                                   .metadata.source_path)) {
            if (!media_items_[*selected_media_index].offline) activateTimelineClip(
                *active_timeline_clip_index_,
                playback_frame_index_,
                false);
        }

        statusBar()->showMessage(
            std::string_view(operation) == "undo"
                ? "Timeline edit undone."
                : "Timeline edit redone.");
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "timeline",
            operation,
            error.what(),
            {{"clip_index", active_timeline_clip_index_.has_value()
                    ? std::to_string(*active_timeline_clip_index_)
                    : "none"},
             {"generation", std::to_string(playback_generation_)}});
        playback_is_playing_ = false;
        updateTimelineState();
        updatePlaybackControls();
        updatePlaybackStatus();
        statusBar()->showMessage("Could not restore the timeline edit.");
        QMessageBox::warning(
            this,
            "Timeline history error",
            "The timeline edit could not be restored.");
    }
}

void MainWindow::undoTimelineEdit() {
    if (!timeline_history_.canUndo()) return;

    try {
        const auto state = timeline_history_.undo(captureTimelineEditState());
        if (!state.has_value()) return;
        restoreTimelineEditState(std::move(*state), "undo");
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "timeline",
            "undo",
            error.what(),
            {{"generation", std::to_string(playback_generation_)}});
        updateHistoryActions();
        statusBar()->showMessage("Could not undo the timeline edit.");
    }
}

void MainWindow::redoTimelineEdit() {
    if (!timeline_history_.canRedo()) return;

    try {
        const auto state = timeline_history_.redo(captureTimelineEditState());
        if (!state.has_value()) return;
        restoreTimelineEditState(std::move(*state), "redo");
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "timeline",
            "redo",
            error.what(),
            {{"generation", std::to_string(playback_generation_)}});
        updateHistoryActions();
        statusBar()->showMessage("Could not redo the timeline edit.");
    }
}

void MainWindow::handleTimelineClipMove(qint64 from_index, qint64 to_index) {
    if (from_index < 0 || to_index < 0 ||
        from_index >= static_cast<qint64>(timeline_model_.clipCount()) ||
        to_index >= static_cast<qint64>(timeline_model_.clipCount()) ||
        from_index == to_index) {
        return;
    }

    const auto from = static_cast<std::size_t>(from_index);
    const auto to = static_cast<std::size_t>(to_index);
    const auto source_path = timeline_model_.clips()[from].source_path;
    timeline::EditState before_edit;

    try {
        before_edit = captureTimelineEditState();
        pending_clip_activation_.reset();
        ++playback_generation_;
        playback_is_playing_ = false;
        if (playback_worker_ != nullptr) {
            QMetaObject::invokeMethod(
                playback_worker_,
                "stop",
                Qt::QueuedConnection);
        }

        const auto result = timeline_model_.moveClip(from, to);
        if (result == timeline::MoveClipResult::NoChange ||
            result == timeline::MoveClipResult::InvalidIndex) {
            return;
        }

        recordTimelineEdit(before_edit);

        if (active_timeline_clip_index_.has_value()) {
            const auto active = *active_timeline_clip_index_;
            if (active == from) {
                active_timeline_clip_index_ = to;
            } else if (from < active && to >= active) {
                active_timeline_clip_index_ = active - 1;
            } else if (from > active && to <= active) {
                active_timeline_clip_index_ = active + 1;
            }
        }

        updateTimelineState();
        updatePlaybackControls();
        updatePlaybackStatus();
        statusBar()->showMessage("Timeline clip moved.");
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "timeline",
            "move_clip",
            error.what(),
            {{"from_index", std::to_string(from_index)},
             {"to_index", std::to_string(to_index)},
             {"path", pathToUtf8(source_path)}});
        statusBar()->showMessage("Could not move the timeline clip.");
        QMessageBox::warning(
            this,
            "Timeline error",
            "The timeline clip could not be moved.");
    }
}

void MainWindow::moveActiveTimelineClip(int direction) {
    if (!active_timeline_clip_index_.has_value() ||
        timeline_model_.clipCount() < 2) {
        return;
    }

    const auto active = *active_timeline_clip_index_;
    if (direction < 0) {
        if (active == 0) return;
        handleTimelineClipMove(
            static_cast<qint64>(active),
            static_cast<qint64>(active - 1));
        return;
    }

    if (direction > 0 && active + 1 < timeline_model_.clipCount()) {
        handleTimelineClipMove(
            static_cast<qint64>(active),
            static_cast<qint64>(active + 1));
    }
}

void MainWindow::deleteActiveTimelineClip() {
    if (!canPlaybackSelectedMedia() ||
        !active_timeline_clip_index_.has_value() ||
        *active_timeline_clip_index_ >= timeline_model_.clipCount() ||
        pending_clip_activation_.has_value()) {
        return;
    }

    const auto clip_index = *active_timeline_clip_index_;
    const auto clip = timeline_model_.clips()[clip_index];
    timeline::EditState before_edit;

    try {
        before_edit = captureTimelineEditState();
        pending_clip_activation_.reset();
        ++playback_generation_;
        playback_is_playing_ = false;
        if (playback_worker_ != nullptr) {
            QMetaObject::invokeMethod(
                playback_worker_,
                "stop",
                Qt::QueuedConnection);
        }

        if (timeline_model_.removeClip(clip_index) !=
            timeline::RemoveClipResult::Removed) {
            updateTimelineState();
            updatePlaybackControls();
            updatePlaybackStatus();
            return;
        }

        recordTimelineEdit(before_edit);

        playback_frame_index_ = 0;
        if (!timeline_model_.hasClip()) {
            active_timeline_clip_index_.reset();
            updateTimelineState();
            updatePlaybackControls();
            updatePlaybackStatus();
            statusBar()->showMessage("Timeline clip deleted.");
            return;
        }

        const auto next_index = std::min(
            clip_index,
            timeline_model_.clipCount() - 1);
        active_timeline_clip_index_ = next_index;
        updateTimelineState();
        updatePlaybackControls();
        updatePlaybackStatus();
        activateTimelineClip(next_index, 0, false);
        statusBar()->showMessage("Timeline clip deleted.");
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "timeline",
            "delete_clip",
            error.what(),
            {{"path", pathToUtf8(clip.source_path)},
             {"clip_index", std::to_string(clip_index)},
             {"source_start_frame", std::to_string(clip.source_start_frame)},
             {"frame_count", std::to_string(clip.timeline_duration_frames)},
             {"generation", std::to_string(playback_generation_)}});
        playback_is_playing_ = false;
        updateTimelineState();
        updatePlaybackControls();
        updatePlaybackStatus();
        statusBar()->showMessage("Could not delete the timeline clip.");
        QMessageBox::warning(
            this,
            "Timeline error",
            "The timeline clip could not be deleted.");
    }
}

void MainWindow::splitActiveClipAtPlayhead() {
    if (!active_timeline_clip_index_.has_value() ||
        *active_timeline_clip_index_ >= timeline_model_.clipCount() ||
        !canPlaybackSelectedMedia() ||
        pending_clip_activation_.has_value()) {
        return;
    }

    handleTimelineClipSplit(
        static_cast<qint64>(*active_timeline_clip_index_),
        static_cast<qint64>(playback_frame_index_));
}

void MainWindow::handleTimelineTrimStarted() {
    if (!timeline_model_.hasClip() || pending_clip_activation_.has_value()) {
        return;
    }

    pending_clip_activation_.reset();
    ++playback_generation_;
    playback_is_playing_ = false;
    if (playback_worker_ != nullptr) {
        QMetaObject::invokeMethod(
            playback_worker_,
            "stop",
            Qt::QueuedConnection);
    }
    updatePlaybackControls();
    updatePlaybackStatus();
}

void MainWindow::handleTimelineClipTrim(
    qint64 clip_index,
    qint64 local_start_frame,
    qint64 local_end_frame) {
    if (clip_index < 0 ||
        clip_index >= static_cast<qint64>(timeline_model_.clipCount())) {
        return;
    }

    const auto index = static_cast<std::size_t>(clip_index);
    const auto clip = timeline_model_.clips()[index];
    if (local_start_frame < 0 ||
        local_end_frame <= local_start_frame ||
        local_end_frame > clip.timeline_duration_frames) {
        statusBar()->showMessage("The clip cannot be trimmed to that range.");
        return;
    }

    if (local_start_frame > std::numeric_limits<std::int64_t>::max() -
            clip.source_start_frame) {
        statusBar()->showMessage("The clip cannot be trimmed to that range.");
        return;
    }

    const auto new_source_start_frame = clip.source_start_frame +
        local_start_frame;
    const auto new_duration_frames = local_end_frame - local_start_frame;
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
            "trim_clip",
            "The timeline clip has no matching imported media item.",
            {{"path", pathToUtf8(clip.source_path)},
             {"clip_index", std::to_string(clip_index)},
             {"old_source_start_frame", std::to_string(clip.source_start_frame)},
             {"old_frame_count", std::to_string(clip.timeline_duration_frames)},
             {"new_source_start_frame", std::to_string(new_source_start_frame)},
             {"new_frame_count", std::to_string(new_duration_frames)},
             {"generation", std::to_string(playback_generation_)}});
        statusBar()->showMessage("Could not trim the timeline clip.");
        QMessageBox::warning(
            this,
            "Timeline error",
            "The timeline clip could not be trimmed.");
        return;
    }

    const bool was_active = active_timeline_clip_index_.has_value() &&
        *active_timeline_clip_index_ == index;
    const auto old_playhead_frame = playback_frame_index_;
    timeline::EditState before_edit;

    try {
        before_edit = captureTimelineEditState();
        const auto result = timeline_model_.trimClip(
            index,
            new_source_start_frame,
            new_duration_frames);
        if (result != timeline::TrimClipResult::Trimmed) {
            updateTimelineState();
            updatePlaybackControls();
            updatePlaybackStatus();
            statusBar()->showMessage("The clip cannot be trimmed to that range.");
            return;
        }

        recordTimelineEdit(before_edit);

        active_timeline_clip_index_ = index;
        playback_frame_index_ = 0;
        if (was_active) {
            const auto adjusted_frame = old_playhead_frame - local_start_frame;
            playback_frame_index_ = std::clamp<std::int64_t>(
                adjusted_frame,
                0,
                new_duration_frames - 1);
        }

        updateTimelineState();
        updatePlaybackControls();
        updatePlaybackStatus();
        activateTimelineClip(index, playback_frame_index_, false);
        statusBar()->showMessage("Timeline clip trimmed.");
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "timeline",
            "trim_clip",
            error.what(),
            {{"path", pathToUtf8(clip.source_path)},
             {"clip_index", std::to_string(clip_index)},
             {"old_source_start_frame", std::to_string(clip.source_start_frame)},
             {"old_frame_count", std::to_string(clip.timeline_duration_frames)},
             {"new_source_start_frame", std::to_string(new_source_start_frame)},
             {"new_frame_count", std::to_string(new_duration_frames)},
             {"generation", std::to_string(playback_generation_)}});
        playback_is_playing_ = false;
        updateTimelineState();
        updatePlaybackControls();
        updatePlaybackStatus();
        statusBar()->showMessage("Could not trim the timeline clip.");
        QMessageBox::warning(
            this,
            "Timeline error",
            "The timeline clip could not be trimmed.");
    }
}

void MainWindow::handleTimelineClipSplit(qint64 clip_index, qint64 local_frame) {
    if (clip_index < 0 || local_frame < 0 ||
        clip_index >= static_cast<qint64>(timeline_model_.clipCount())) {
        return;
    }

    const auto source_index = static_cast<std::size_t>(clip_index);
    const auto& source_clip = timeline_model_.clips()[source_index];
    if (local_frame <= 0 ||
        local_frame >= source_clip.timeline_duration_frames ||
        source_clip.source_start_frame < 0 ||
        local_frame > std::numeric_limits<std::int64_t>::max() -
            source_clip.source_start_frame) {
        statusBar()->showMessage("A clip cannot be split at its boundary.");
        return;
    }

    const auto media_item = std::find_if(
        media_items_.begin(),
        media_items_.end(),
        [&source_clip](const ImportedMedia& item) {
            return item.metadata.source_path == source_clip.source_path;
        });
    if (media_item == media_items_.end()) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "timeline",
            "split_clip",
            "The timeline clip has no matching imported media item.",
            {{"path", pathToUtf8(source_clip.source_path)},
             {"clip_index", std::to_string(clip_index)},
             {"local_frame", std::to_string(local_frame)}});
        statusBar()->showMessage("Could not split the timeline clip.");
        QMessageBox::warning(
            this,
            "Timeline error",
            "The timeline clip could not be split.");
        return;
    }

    const auto source_frame = source_clip.source_start_frame + local_frame;
    timeline::EditState before_edit;
    try {
        before_edit = captureTimelineEditState();
        pending_clip_activation_.reset();
        ++playback_generation_;
        playback_is_playing_ = false;
        if (playback_worker_ != nullptr) {
            QMetaObject::invokeMethod(
                playback_worker_,
                "stop",
                Qt::QueuedConnection);
        }

        const auto result = timeline_model_.splitClip(
            source_index,
            local_frame);
        if (result != timeline::SplitClipResult::Split) {
            updatePlaybackControls();
            updatePlaybackStatus();
            statusBar()->showMessage("The clip could not be split at that frame.");
            return;
        }

        recordTimelineEdit(before_edit);

        const auto right_clip_index = source_index + 1;
        const auto& right_clip = timeline_model_.clips()[right_clip_index];
        const auto media_index = static_cast<std::size_t>(
            std::distance(media_items_.begin(), media_item));
        active_timeline_clip_index_ = right_clip_index;
        playback_frame_index_ = 0;
        {
            const QSignalBlocker blocker(media_list_);
            media_list_->setCurrentRow(static_cast<int>(media_index));
        }
        media_details_->setText(mediaDetailsText(media_item->metadata));

        pending_clip_activation_ = PendingClipActivation{
            right_clip_index,
            media_index,
            0,
            right_clip.source_start_frame,
            right_clip.timeline_duration_frames,
            false,
            playback_generation_};
        updateTimelineState();
        updatePlaybackControls();
        updatePlaybackStatus();
        statusBar()->showMessage("Clip split at the playhead.");

        if (playback_worker_ != nullptr) {
            QMetaObject::invokeMethod(
                playback_worker_,
                "setMedia",
                Qt::QueuedConnection,
                Q_ARG(QString, fromUtf8(pathToUtf8(right_clip.source_path))),
                Q_ARG(double, right_clip.frame_rate.value_or(30.0)),
                Q_ARG(qint64, static_cast<qint64>(right_clip.source_start_frame)),
                Q_ARG(qint64, static_cast<qint64>(right_clip.timeline_duration_frames)),
                Q_ARG(quint64, playback_generation_));
        }
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "timeline",
            "split_clip",
            error.what(),
            {{"path", pathToUtf8(source_clip.source_path)},
             {"clip_index", std::to_string(clip_index)},
             {"local_frame", std::to_string(local_frame)},
             {"source_frame", std::to_string(source_frame)},
             {"generation", std::to_string(playback_generation_)}});
        pending_clip_activation_.reset();
        playback_is_playing_ = false;
        updateTimelineState();
        updatePlaybackControls();
        updatePlaybackStatus();
        statusBar()->showMessage("Could not split the timeline clip.");
        QMessageBox::warning(
            this,
            "Timeline error",
            "The timeline clip could not be split.");
    }
}

void MainWindow::sendPlaybackCommand(const char* command) {
    if (playback_worker_ == nullptr || media_list_ == nullptr ||
        !canPlaybackSelectedMedia() || pending_clip_activation_.has_value()) {
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
    const bool has_media = canPlaybackSelectedMedia() &&
        !pending_clip_activation_.has_value();
    if (previous_frame_button_ != nullptr) previous_frame_button_->setEnabled(has_media);
    if (play_pause_button_ != nullptr) play_pause_button_->setEnabled(has_media);
    if (next_frame_button_ != nullptr) next_frame_button_->setEnabled(has_media);
    if (delete_clip_action_ != nullptr) {
        delete_clip_action_->setEnabled(
            canPlaybackSelectedMedia() && !pending_clip_activation_.has_value());
    }
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

    const auto selected_index = selectedMediaIndex();
    if (!selected_index.has_value()) {
        playback_status_label_->setText("No media selected.");
        return;
    }

    if (!canPreviewSelectedMedia()) {
        playback_status_label_->setText("Select the timeline media to play.");
        return;
    }

    const auto& metadata = media_items_[*selected_index].metadata;
    QString total = metadata.frame_count.has_value()
        ? QString::number(*metadata.frame_count)
        : "?";
    if (active_timeline_clip_index_.has_value() &&
        *active_timeline_clip_index_ < timeline_model_.clipCount()) {
        total = QString::number(
            timeline_model_.clips()[*active_timeline_clip_index_]
                .timeline_duration_frames);
    }
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
        const auto index = static_cast<std::size_t>(std::distance(media_items_.begin(), existing));
        if (existing->offline) {
            try {
                auto metadata = video_probe_.probe(source_path);
                auto first_frame = video_decoder_.decode_first_frame(source_path);
                existing->metadata = std::move(metadata);
                existing->metadata.display_name = existing->display_name;
                existing->first_frame = std::move(first_frame);
                existing->offline = false;
                updateProjectDirtyState();
                populateMediaBrowser(source_path);
                statusBar()->showMessage("Offline media restored.");
            } catch (const media::MediaError& error) {
                logging::Context context{{"path", pathToUtf8(source_path)}, {"cause", error.what()}};
                if (error.error_code().has_value()) context.emplace_back("error_code", std::to_string(*error.error_code()));
                logging::Logger::instance().log(logging::Level::Error, "media", "restore", error.what(), context);
                QMessageBox::warning(this, "Could not restore media", fromUtf8(error.what()));
            }
        } else {
            populateMediaBrowser(source_path);
            static_cast<void>(index);
            statusBar()->showMessage("Media is already imported.");
        }
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
        logging::Context context{{"path", pathToUtf8(source_path)}, {"cause", error.what()}};
        if (error.error_code().has_value()) context.emplace_back("error_code", std::to_string(*error.error_code()));
        logging::Logger::instance().log(logging::Level::Error, "media", "import", error.what(), context);
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

    if (row < 0 || media_list_ == nullptr || media_list_->currentItem() == nullptr) {
        active_timeline_clip_index_.reset();
        media_details_->setText("No media imported.");
        preview_widget_->clearFrame("Preview area\n\nImport media to display its first frame.");
        updateTimelineState();
        updatePlaybackControls();
        updatePlaybackStatus();
        return;
    }

    const auto item_index = selectedMediaIndex();
    if (!item_index.has_value()) {
        media_details_->setText("No media selected.");
        updateTimelineState();
        updatePlaybackControls();
        updatePlaybackStatus();
        return;
    }
    const auto& item = media_items_[*item_index];
    active_timeline_clip_index_ = selectedTimelineClipIndex();
    media_details_->setText(
        item.offline
            ? QString("Name: %1\nBin: %2\nStatus: Offline\nPath: %3")
                .arg(fromUtf8(item.display_name))
                .arg(fromUtf8(item.bin_path))
                .arg(fromUtf8(pathToUtf8(item.metadata.source_path)))
            : mediaDetailsText(item.metadata));
    if (item.offline) {
        preview_widget_->clearFrame("Preview area\n\nThe selected media is offline.");
    } else {
        preview_widget_->setFrame(item.first_frame);
    }
    updateTimelineState();
    updatePlaybackControls();
    updatePlaybackStatus();

    if (playback_worker_ != nullptr && canPlaybackSelectedMedia()) {
        const QString source_path = fromUtf8(pathToUtf8(item.metadata.source_path));
        const double frame_rate = item.metadata.frame_rate.value_or(30.0);
        std::int64_t source_start_frame = 0;
        std::int64_t segment_frame_count = item.metadata.frame_count.value_or(0);
        if (active_timeline_clip_index_.has_value() &&
            *active_timeline_clip_index_ < timeline_model_.clipCount()) {
            const auto& clip = timeline_model_.clips()[*active_timeline_clip_index_];
            source_start_frame = clip.source_start_frame;
            segment_frame_count = clip.timeline_duration_frames;
        }
        QMetaObject::invokeMethod(
            playback_worker_,
            "setMedia",
            Qt::QueuedConnection,
            Q_ARG(QString, source_path),
            Q_ARG(double, frame_rate),
            Q_ARG(qint64, static_cast<qint64>(source_start_frame)),
            Q_ARG(qint64, static_cast<qint64>(segment_frame_count)),
            Q_ARG(quint64, playback_generation_));
    }
}

void MainWindow::addMediaItem(
    media::VideoMetadata metadata,
    media::VideoFrame first_frame,
    std::string display_name,
    std::string bin_path,
    bool offline,
    bool mark_dirty) {
    if (display_name.empty()) display_name = metadata.display_name;
    if (display_name.empty()) display_name = media::MediaLibrary::defaultDisplayName(metadata.source_path);
    metadata.display_name = display_name;
    if (std::find(bin_paths_.begin(), bin_paths_.end(), bin_path) == bin_paths_.end()) {
        bin_paths_.push_back(bin_path);
    }
    media_items_.push_back({std::move(metadata), std::move(first_frame),
                            std::move(display_name), std::move(bin_path), offline});
    auto& item = media_items_.back();
    populateMediaBrowser(item.metadata.source_path);
    if (mark_dirty) updateProjectDirtyState();
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
        if (pending.resume_playback && playback_worker_ != nullptr) {
            QMetaObject::invokeMethod(playback_worker_, "play", Qt::QueuedConnection);
        }
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
    if (pending.target_frame > 0 || pending.source_start_frame > 0) {
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
            {"source_start_frame", std::to_string(pending.source_start_frame)},
            {"segment_frame_count", std::to_string(pending.segment_frame_count)},
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
    if (!canPlaybackSelectedMedia() || pending_clip_activation_.has_value()) return;

    playback_is_playing_ = false;
    updatePlaybackControls();
    updatePlaybackStatus();
    sendPlaybackCommand("pause");
}

void MainWindow::handleTimelineSeek(qint64 frame_index) {
    if (playback_worker_ == nullptr ||
        !canPlaybackSelectedMedia() ||
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
