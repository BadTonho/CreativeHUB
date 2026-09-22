#include "main_window.h"
#include "main_window_support.h"

#include "logging/logger.h"
#include "preview_widget.h"
#include "project/project_file.h"
#include "settings/settings_dialog.h"
#include "settings/user_preferences.h"
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
#include <QFormLayout>
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
#include <QScrollBar>
#include <QSlider>
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


using namespace main_window_detail;

namespace {

QString autosaveSnapshotDateText(
    const project::AutosaveSnapshot& snapshot) {
    const auto modified = QFileInfo(
        main_window_detail::fromUtf8(
            main_window_detail::pathToUtf8(snapshot.path))).lastModified();
    return modified.isValid()
        ? modified.toLocalTime().toString(Qt::TextDate)
        : QStringLiteral("Unknown time");
}

} // namespace

project::ProjectDocument MainWindow::currentProjectDocument() const {
    project::ProjectDocument document;
    document.timeline_zoom = timeline_widget_ != nullptr
        ? timeline_widget_->zoomFactor()
        : 1.0;
    document.timeline_row_height = timeline_widget_ != nullptr
        ? timeline_widget_->trackRowHeight()
        : timeline::kDefaultTrackRowHeight;
    document.media.reserve(media_items_.size());
    document.bins = bin_paths_;
    for (const auto& item : media_items_) {
        document.media.push_back(project::ProjectMedia{
            normalizedPath(item.metadata.source_path),
            item.display_name,
            item.bin_path,
            item.offline,
            item.metadata.kind});
    }

    for (const auto& track : timeline_model_.tracks()) {
        project::ProjectTrack project_track;
        project_track.name = track.name;
        project_track.audio_gain = track.audio_gain;
        project_track.audio_muted = track.audio_muted;
        project_track.clips.reserve(track.clips.size());
        for (const auto& clip : track.clips) {
            project_track.clips.push_back(project::ProjectClip{
                timeline::isMediaClipKind(clip.kind)
                    ? normalizedPath(clip.source_path)
                    : std::filesystem::path{},
                clip.timeline_start_frame,
                clip.source_start_frame,
                clip.timeline_duration_frames,
                clip.audio_gain,
                clip.audio_muted,
                clip.transform,
                clip.keyframes,
                clip.kind,
                clip.text});
        }
        for (const auto& transition : track.transitions) {
            const auto from = std::find_if(
                track.clips.begin(), track.clips.end(),
                [&transition](const timeline::TimelineClip& clip) {
                    return clip.clip_id == transition.from_clip_id;
                });
            const auto to = std::find_if(
                track.clips.begin(), track.clips.end(),
                [&transition](const timeline::TimelineClip& clip) {
                    return clip.clip_id == transition.to_clip_id;
                });
            if (from != track.clips.end() && to != track.clips.end()) {
                project_track.transitions.push_back(project::ProjectTransition{
                    static_cast<std::size_t>(std::distance(track.clips.begin(), from)),
                    static_cast<std::size_t>(std::distance(track.clips.begin(), to)),
                    transition.kind,
                    transition.duration_frames});
            }
        }
        document.timeline_tracks.push_back(std::move(project_track));
    }
    if (!document.timeline_tracks.empty()) {
        document.timeline_clips = document.timeline_tracks.front().clips;
    }
    return document;
}

void MainWindow::autosaveProject() {
    if (!settings::projectAutosaveEnabled() || !project_dirty_) return;

    const auto document = currentProjectDocument();
    if (last_autosaved_document_.has_value() &&
        *last_autosaved_document_ == document) {
        return;
    }

    try {
        const auto retention = settings::projectAutosaveRetention();
        if (project_path_.has_value()) {
            autosave_manager_.saveSnapshot(
                document, *project_path_, retention);
        } else {
            autosave_manager_.saveSnapshot(document, retention);
        }
        last_autosaved_document_ = document;
    } catch (const project::ProjectError& error) {
        logging::Logger::instance().log(
            logging::Level::Warning,
            "project",
            "autosave",
            error.what(),
            { {"project_path", project_path_.has_value()
                    ? pathToUtf8(*project_path_)
                    : ""},
              {"cause", error.what()} });
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Warning,
            "project",
            "autosave",
            error.what(),
            { {"project_path", project_path_.has_value()
                    ? pathToUtf8(*project_path_)
                    : ""} });
    }
}

std::vector<settings::AutosaveSnapshotItem>
MainWindow::autosaveSnapshotsForSettings() const {
    std::vector<project::AutosaveSnapshot> snapshots;
    if (project_path_.has_value()) {
        snapshots = autosave_manager_.validSnapshotsForProject(*project_path_);
    }
    auto unsaved_snapshots = autosave_manager_.unsavedSnapshots();
    snapshots.insert(
        snapshots.end(),
        std::make_move_iterator(unsaved_snapshots.begin()),
        std::make_move_iterator(unsaved_snapshots.end()));
    std::sort(
        snapshots.begin(), snapshots.end(),
        [](const project::AutosaveSnapshot& left,
           const project::AutosaveSnapshot& right) {
            if (left.modified_time != right.modified_time) {
                return left.modified_time > right.modified_time;
            }
            return left.path.string() > right.path.string();
        });

    std::vector<settings::AutosaveSnapshotItem> rows;
    rows.reserve(snapshots.size());
    for (const auto& snapshot : snapshots) {
        const bool saved_project = !snapshot.project_path.empty();
        settings::AutosaveSnapshotItem row;
        row.project_name = saved_project
            ? fromUtf8(pathToUtf8(snapshot.project_path.filename()))
            : QStringLiteral("Unsaved project");
        row.type = saved_project
            ? QStringLiteral("Saved project")
            : QStringLiteral("Unsaved project");
        row.modified = autosaveSnapshotDateText(snapshot);
        row.snapshot_name = fromUtf8(pathToUtf8(snapshot.path.filename()));
        row.snapshot_path = fromUtf8(pathToUtf8(snapshot.path));
        row.project_path = saved_project
            ? fromUtf8(pathToUtf8(snapshot.project_path))
            : QString();
        row.folder_path = fromUtf8(pathToUtf8(snapshot.path.parent_path()));
        rows.push_back(std::move(row));
    }
    return rows;
}

bool MainWindow::restoreAutosaveSnapshot(
    const QString& snapshot_path,
    const QString& project_path) {
    if (snapshot_path.isEmpty() || !confirmProjectChange()) return false;

    const auto snapshot = QFileInfo(snapshot_path).filesystemFilePath();
    if (!project_path.isEmpty()) {
        const auto active_project = QFileInfo(project_path).filesystemFilePath();
        if (!openProjectPath(snapshot, active_project)) return false;
        try {
            autosave_manager_.removeSnapshotsForProject(active_project);
        } catch (const project::ProjectError& error) {
            logging::Logger::instance().log(
                logging::Level::Warning,
                "project",
                "autosave_cleanup",
                error.what(),
                {{"project_path", pathToUtf8(active_project)},
                 {"cause", error.what()}});
        }
        return true;
    }

    project::ProjectDocument blank_document;
    blank_document.bins = {"Unsorted"};
    if (!openProjectPath(snapshot, std::nullopt, std::move(blank_document))) {
        return false;
    }
    try {
        autosave_manager_.removeUnsavedSnapshotsForSession(snapshot);
    } catch (const project::ProjectError& error) {
        logging::Logger::instance().log(
            logging::Level::Warning,
            "project",
            "autosave_cleanup",
            error.what(),
            {{"snapshot_path", pathToUtf8(snapshot)},
             {"cause", error.what()}});
    }
    return true;
}

void MainWindow::deleteAutosaveSnapshot(const QString& snapshot_path) {
    if (snapshot_path.isEmpty()) return;
    const auto result = QMessageBox::question(
        this,
        "Delete Autosave Snapshot",
        "Delete the selected recovery snapshot?",
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (result != QMessageBox::Yes) return;

    const auto path = QFileInfo(snapshot_path).filesystemFilePath();
    try {
        autosave_manager_.removeSnapshot(path);
        statusBar()->showMessage("Autosave snapshot deleted.");
    } catch (const project::ProjectError& error) {
        logging::Logger::instance().log(
            logging::Level::Warning,
            "project",
            "autosave_remove",
            error.what(),
            {{"snapshot_path", pathToUtf8(path)},
             {"cause", error.what()}});
        statusBar()->showMessage("Could not delete autosave snapshot.");
    }
}

void MainWindow::openAutosaveFolder(const QString& folder_path) {
    if (folder_path.isEmpty()) return;
    const auto path = QFileInfo(folder_path).filesystemFilePath();
    if (QDesktopServices::openUrl(QUrl::fromLocalFile(
            main_window_detail::fromUtf8(pathToUtf8(path))))) {
        return;
    }
    logging::Logger::instance().log(
        logging::Level::Warning,
        "project",
        "autosave_open_folder",
        "The recovery folder could not be opened.",
        {{"folder_path", pathToUtf8(path)}});
    statusBar()->showMessage("Could not open autosave folder.");
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
        last_autosaved_document_ = document;
        try {
            autosave_manager_.removeCurrentUnsavedSnapshots();
        } catch (const project::ProjectError& error) {
            logging::Logger::instance().log(
                logging::Level::Warning,
                "project",
                "autosave_cleanup",
                error.what(),
                { {"cause", error.what()} });
        }
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
    try {
        autosave_manager_.removeCurrentUnsavedSnapshots();
    } catch (const project::ProjectError& error) {
        logging::Logger::instance().log(
            logging::Level::Warning,
            "project",
            "autosave_cleanup",
            error.what(),
            { {"cause", error.what()} });
    }
    pending_clip_activation_.reset();
    pending_audio_edit_.reset();
    pending_transform_edit_.reset();
    ++playback_generation_;
    playback_is_playing_ = false;
    if (playback_worker_ != nullptr) {
        QMetaObject::invokeMethod(playback_worker_, "stop", Qt::QueuedConnection);
    }

    timeline_model_.clear();
    timeline_history_.clear();
    active_timeline_track_index_.reset();
    active_timeline_clip_index_.reset();
    preserved_timeline_playhead_frame_.reset();
    playback_frame_index_ = 0;
    project_path_.reset();
    saved_project_document_ = project::ProjectDocument{};
    last_autosaved_document_.reset();
    project_dirty_ = false;
    if (timeline_widget_ != nullptr) {
        timeline_widget_->setZoomFactor(1.0);
        timeline_widget_->setTrackRowHeight(timeline::kDefaultTrackRowHeight);
    }
    if (timeline_scroll_ != nullptr) {
        timeline_scroll_->horizontalScrollBar()->setValue(0);
    }

    {
        const QSignalBlocker blocker(media_list_);
        media_list_->clear();
    }
    media_items_.clear();
    bin_paths_ = {"Unsorted"};
    populateMediaBrowser();
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

    const auto selected_project_path = normalizedPath(
        QFileInfo(selected_file).filesystemFilePath());
    std::filesystem::path source_path = selected_project_path;
    if (settings::projectAutosaveEnabled()) {
        const auto snapshots = autosave_manager_.recoverableSnapshotsForProject(
            selected_project_path);
        if (const auto selected_snapshot = chooseRecoverySnapshot(
                snapshots,
                main_window_detail::fromUtf8(
                    pathToUtf8(selected_project_path.filename())));
            selected_snapshot.has_value()) {
            source_path = *selected_snapshot;
        }
    }

    try {
        autosave_manager_.removeCurrentUnsavedSnapshots();
    } catch (const project::ProjectError& error) {
        logging::Logger::instance().log(
            logging::Level::Warning,
            "project",
            "autosave_cleanup",
            error.what(),
            { {"cause", error.what()} });
    }
    const auto recovered = source_path != selected_project_path;
    if (openProjectPath(source_path, selected_project_path) && recovered) {
        try {
            autosave_manager_.removeSnapshotsForProject(selected_project_path);
        } catch (const project::ProjectError& error) {
            logging::Logger::instance().log(
                logging::Level::Warning,
                "project",
                "autosave_cleanup",
                error.what(),
                { {"cause", error.what()} });
        }
    }
}

bool MainWindow::openProjectPath(
    const std::filesystem::path& source_path,
    std::optional<std::filesystem::path> active_project_path,
    std::optional<project::ProjectDocument> saved_baseline) {
    const auto project_path = normalizedPath(source_path);
    std::filesystem::path current_media_path;
    std::optional<std::size_t> current_clip_index;
    try {
        auto document = project::load(project_path);
        if (!saved_baseline.has_value() && active_project_path.has_value() &&
            normalizedPath(*active_project_path) != project_path) {
            try {
                saved_baseline = project::load(*active_project_path);
            } catch (const project::ProjectError& error) {
                logging::Logger::instance().log(
                    logging::Level::Warning,
                    "project",
                    "autosave_baseline",
                    "The original project could not be loaded; recovery will continue as a new dirty document.",
                    { {"project_path", pathToUtf8(*active_project_path)},
                      {"cause", error.what()} });
                project::ProjectDocument blank_document;
                blank_document.bins = {"Unsorted"};
                saved_baseline = std::move(blank_document);
            }
        }
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
                metadata.kind = project_media.kind;
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

            auto metadata = project_media.kind == media::MediaKind::Image
                ? still_image_decoder_.probe(project_media.source_path)
                : video_probe_.probe(project_media.source_path);
            auto first_frame = project_media.kind == media::MediaKind::Image
                ? still_image_decoder_.decode_first_frame(project_media.source_path)
                : video_decoder_.decode_first_frame(project_media.source_path);
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

        std::vector<project::ProjectTrack> project_tracks = document.timeline_tracks;
        if (project_tracks.empty() && !document.timeline_clips.empty()) {
            project_tracks.push_back(project::ProjectTrack{
                "Video 1", 1.0, false, document.timeline_clips});
        }

        timeline::TimelineModel::Snapshot snapshot;
        timeline::TrackId next_track_id = 1;
        timeline::ClipId next_clip_id = 1;
        for (std::size_t track_index = 0; track_index < project_tracks.size(); ++track_index) {
            const auto& project_track = project_tracks[track_index];
            const auto track_id = next_track_id++;
            snapshot.tracks.push_back(timeline::TimelineTrack{
                track_id,
                project_track.name.empty() ? "Video " + std::to_string(track_index + 1)
                                           : project_track.name,
                project_track.audio_gain,
                project_track.audio_muted,
                {}});
            for (std::size_t clip_index = 0; clip_index < project_track.clips.size(); ++clip_index) {
                current_clip_index = clip_index;
                const auto& project_clip = project_track.clips[clip_index];
                if (project_clip.kind == timeline::ClipKind::Text) {
                    if (project_clip.timeline_start_frame < 0 ||
                        project_clip.source_start_frame < 0 ||
                        project_clip.duration_frames <= 0 ||
                        !timeline::TimelineModel::validTextStyle(project_clip.text)) {
                        throw project::ProjectError(
                            project::ProjectErrorCode::InvalidTimeline,
                            "A text timeline clip has invalid timing or style.",
                            std::nullopt,
                            project_path);
                    }
                    timeline::TimelineClip text_clip;
                    text_clip.timeline_start_frame = project_clip.timeline_start_frame;
                    text_clip.source_start_frame = project_clip.source_start_frame;
                    text_clip.timeline_duration_frames = project_clip.duration_frames;
                    text_clip.display_name = project_clip.text.content.empty()
                        ? "Text"
                        : project_clip.text.content;
                    text_clip.duration_seconds =
                        static_cast<double>(project_clip.duration_frames) / 30.0;
                    text_clip.frame_rate = 30.0;
                    text_clip.frame_count = project_clip.duration_frames;
                    text_clip.audio_gain = project_clip.audio_gain;
                    text_clip.audio_muted = project_clip.audio_muted;
                    text_clip.clip_id = next_clip_id++;
                    text_clip.track_id = track_id;
                    text_clip.transform = project_clip.transform;
                    text_clip.keyframes = project_clip.keyframes;
                    text_clip.kind = timeline::ClipKind::Text;
                    text_clip.text = project_clip.text;
                    snapshot.clips.push_back(text_clip);
                    snapshot.tracks.back().clips.push_back(std::move(text_clip));
                    continue;
                }
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
                project_clip.timeline_start_frame < 0) {
                throw project::ProjectError(
                    project::ProjectErrorCode::InvalidTimeline,
                    "A timeline clip is outside the current media bounds.",
                    std::nullopt,
                    project_clip.source_path);
            }

            snapshot.clips.push_back(timeline::TimelineClip{
                project_clip.timeline_start_frame,
                project_clip.source_start_frame,
                project_clip.duration_frames,
                normalizedPath(metadata.source_path),
                loaded_item.display_name,
                metadata.duration_seconds,
                metadata.frame_rate,
                metadata.frame_count,
                project_clip.audio_gain,
                project_clip.audio_muted,
                next_clip_id++,
                track_id,
                project_clip.transform,
                project_clip.keyframes,
                metadata.kind == media::MediaKind::Image
                    ? timeline::ClipKind::Image
                    : timeline::ClipKind::Video,
                {}});
            snapshot.tracks.back().clips.push_back(snapshot.clips.back());
            }

            for (const auto& transition : project_track.transitions) {
                if (transition.from_clip_index >= snapshot.tracks.back().clips.size() ||
                    transition.to_clip_index >= snapshot.tracks.back().clips.size()) {
                    throw project::ProjectError(
                        project::ProjectErrorCode::InvalidTimeline,
                        "A timeline transition refers to an invalid clip.",
                        std::nullopt,
                        project_path);
                }
                const auto& from = snapshot.tracks.back().clips[transition.from_clip_index];
                const auto& to = snapshot.tracks.back().clips[transition.to_clip_index];
                snapshot.tracks.back().transitions.push_back(timeline::TimelineTransition{
                    from.clip_id,
                    to.clip_id,
                    transition.kind,
                    transition.duration_frames});
            }
        }
        snapshot.next_track_id = next_track_id;
        snapshot.next_clip_id = next_clip_id;

        applyLoadedProject(
            std::move(loaded_media),
            std::move(snapshot),
            active_project_path.has_value()
                ? std::optional<std::filesystem::path>(
                      normalizedPath(*active_project_path))
                : std::nullopt,
            normalized_document,
            std::move(saved_baseline));
        statusBar()->showMessage("Project opened.");
        return true;
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
        return false;
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
        return false;
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
        return false;
    }
}

void MainWindow::applyLoadedProject(
    std::vector<ImportedMedia> media_items,
    timeline::TimelineModel::Snapshot timeline_snapshot,
    std::optional<std::filesystem::path> project_path,
    const project::ProjectDocument& loaded_document,
    std::optional<project::ProjectDocument> saved_baseline) {
    pending_clip_activation_.reset();
    pending_audio_edit_.reset();
    pending_transform_edit_.reset();
    ++playback_generation_;
    playback_is_playing_ = false;
    if (playback_worker_ != nullptr) {
        QMetaObject::invokeMethod(playback_worker_, "stop", Qt::QueuedConnection);
    }

    timeline_model_.restore(std::move(timeline_snapshot));
    timeline_history_.clear();
    media_items_ = std::move(media_items);
    bin_paths_ = loaded_document.bins.empty()
        ? std::vector<std::string>{"Unsorted"}
        : loaded_document.bins;
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
    active_timeline_track_index_.reset();
    active_timeline_clip_index_.reset();
    preserved_timeline_playhead_frame_.reset();
    playback_frame_index_ = 0;
    project_path_ = project_path.has_value()
        ? std::optional<std::filesystem::path>(normalizedPath(*project_path))
        : std::nullopt;
    saved_project_document_ = saved_baseline.has_value()
        ? std::move(saved_baseline)
        : std::optional<project::ProjectDocument>(loaded_document);
    last_autosaved_document_.reset();
    if (timeline_widget_ != nullptr) {
        timeline_widget_->setZoomFactor(loaded_document.timeline_zoom);
        timeline_widget_->setTrackRowHeight(loaded_document.timeline_row_height);
    }
    if (timeline_scroll_ != nullptr) {
        timeline_scroll_->horizontalScrollBar()->setValue(0);
    }

    populateMediaBrowser();

    preview_widget_->clearFrame("Preview area\n\nImport media to display its first frame.");
    updateProjectDirtyState();

    if (timeline_model_.hasClip()) {
        std::optional<timeline::ClipLocation> first_clip_location;
        for (std::size_t track_index = 0;
             track_index < timeline_model_.trackCount() && !first_clip_location.has_value();
             ++track_index) {
            if (timeline_model_.clipCount(track_index) > 0) {
                first_clip_location = timeline::ClipLocation{track_index, 0};
            }
        }

        if (!first_clip_location.has_value()) {
            updateTimelineState();
            updatePlaybackControls();
            updatePlaybackStatus();
            updateHistoryActions();
            return;
        }

        active_timeline_track_index_ = first_clip_location->track_index;
        active_timeline_clip_index_ = first_clip_location->clip_index;
        const auto& clip = timeline_model_.tracks()[first_clip_location->track_index]
            .clips[first_clip_location->clip_index];
        const auto media = std::find_if(
            media_items_.begin(),
            media_items_.end(),
            [&clip](const ImportedMedia& item) {
                return normalizedPath(item.metadata.source_path) == normalizedPath(clip.source_path);
            });
        if (clip.kind == timeline::ClipKind::Text) {
            activateTimelineClipAt(
                first_clip_location->track_index,
                first_clip_location->clip_index,
                0,
                false);
        } else if (media != media_items_.end()) {
            const auto media_index = static_cast<std::size_t>(std::distance(media_items_.begin(), media));
            populateMediaBrowser(clip.source_path);
            if (!media->offline) preview_widget_->setFrame(media->first_frame);
            if (!media->offline) {
                activateTimelineClipAt(
                    first_clip_location->track_index,
                    first_clip_location->clip_index,
                    0,
                    false);
            }
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
