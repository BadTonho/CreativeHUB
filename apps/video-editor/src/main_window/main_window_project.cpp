#include "main_window/main_window.h"
#include "main_window_support.h"

#include "logging/logger.h"
#include "ui/preview/preview_widget.h"
#include "project/project_file.h"
#include "settings/settings_dialog.h"
#include "settings/user_preferences.h"
#include "timeline/timeline_widget.h"
#include "ui/media_browser/media_browser_list_widget.h"

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
#include <QPointer>
#include <QProgressDialog>
#include <QRunnable>
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
#include <stdexcept>
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
    const application::TimelinePresentationState presentation{
        timeline_widget_ != nullptr ? timeline_widget_->zoomFactor() : 1.0,
        timeline_widget_ != nullptr
            ? timeline_widget_->trackRowHeight()
            : timeline::kDefaultTrackRowHeight};
    return project_controller_.document(presentation);
}

void MainWindow::autosaveProject() {
    const application::TimelinePresentationState presentation{
        timeline_widget_ != nullptr ? timeline_widget_->zoomFactor() : 1.0,
        timeline_widget_ != nullptr ? timeline_widget_->trackRowHeight()
                                   : timeline::kDefaultTrackRowHeight};
    const auto result = project_controller_.autosave(
        settings::projectAutosaveEnabled(),
        settings::projectAutosaveRetention(),
        presentation);
    if (!result.succeeded()) {
        logging::Logger::instance().log(
            logging::Level::Warning,
            "project",
            "autosave",
            result.cause,
            { {"project_path", project_path_.has_value()
                    ? pathToUtf8(*project_path_)
                    : ""},
              {"cause", result.cause},
              {"error_code", result.system_error.has_value()
                    ? std::to_string(*result.system_error)
                    : ""} });
    }
}

std::vector<settings::AutosaveSnapshotItem>
MainWindow::autosaveSnapshotsForSettings() const {
    std::vector<project::AutosaveSnapshot> snapshots;
    if (project_path_.has_value()) {
        snapshots = project_controller_.validSnapshotsForProject(*project_path_);
    }
    auto unsaved_snapshots = project_controller_.unsavedSnapshots();
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
            return left.path > right.path;
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
        return openProjectPath(snapshot, active_project, std::nullopt,
            [this, active_project](bool succeeded) {
                if (!succeeded) return;
                try {
                    project_controller_.removeSnapshotsForProject(active_project);
                } catch (const project::ProjectError& error) {
                    logging::Logger::instance().log(
                        logging::Level::Warning,
                        "project",
                        "autosave_cleanup",
                        error.what(),
                        {{"project_path", pathToUtf8(active_project)},
                         {"cause", error.what()}});
                }
            });
    }

    project::ProjectDocument blank_document;
    blank_document.bins = {"Unsorted"};
    return openProjectPath(snapshot, std::nullopt, std::move(blank_document),
        [this, snapshot](bool succeeded) {
            if (!succeeded) return;
            try {
                project_controller_.removeUnsavedSnapshotsForSession(snapshot);
            } catch (const project::ProjectError& error) {
                logging::Logger::instance().log(
                    logging::Level::Warning,
                    "project",
                    "autosave_cleanup",
                    error.what(),
                    {{"snapshot_path", pathToUtf8(snapshot)},
                     {"cause", error.what()}});
            }
        });
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
        project_controller_.removeSnapshot(path);
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
    const application::TimelinePresentationState presentation{
        timeline_widget_ != nullptr ? timeline_widget_->zoomFactor() : 1.0,
        timeline_widget_ != nullptr ? timeline_widget_->trackRowHeight()
                                   : timeline::kDefaultTrackRowHeight};
    static_cast<void>(project_controller_.updateDirtyState(presentation));

    setWindowTitle(project_dirty_ ? "Video Editor *" : "Video Editor");
    if (save_project_action_ != nullptr) {
        save_project_action_->setEnabled(!project_path_.has_value() || project_dirty_);
    }
    if (save_project_as_action_ != nullptr) save_project_as_action_->setEnabled(true);
}

bool MainWindow::saveProjectTo(
    const std::filesystem::path& project_path,
    const char* operation) {
    const application::TimelinePresentationState presentation{
        timeline_widget_ != nullptr ? timeline_widget_->zoomFactor() : 1.0,
        timeline_widget_ != nullptr ? timeline_widget_->trackRowHeight()
                                   : timeline::kDefaultTrackRowHeight};
    const auto result = project_controller_.saveTo(project_path, presentation);
    if (!result.succeeded()) {
        logging::Context context{
            {"project_path", pathToUtf8(result.path.empty() ? project_path : result.path)},
            {"cause", result.cause}};
        if (result.system_error.has_value()) {
            context.emplace_back("error_code", std::to_string(*result.system_error));
        }
        logging::Logger::instance().log(
            logging::Level::Error,
            "project",
            operation,
            result.cause,
            context);
        QMessageBox::warning(this, "Could not save project", fromUtf8(result.cause));
        statusBar()->showMessage("Could not save project.");
        return false;
    }
    try {
        project_controller_.removeCurrentUnsavedSnapshots();
    } catch (const project::ProjectError& error) {
        logging::Logger::instance().log(
            logging::Level::Warning,
            "project",
            "autosave_cleanup",
            error.what(),
            {{"cause", error.what()}});
    }
    updateProjectDirtyState();
    statusBar()->showMessage(std::string_view(operation) == "save_as"
        ? "Project saved as."
        : "Project saved.");
    return true;
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
        project_controller_.removeCurrentUnsavedSnapshots();
    } catch (const project::ProjectError& error) {
        logging::Logger::instance().log(
            logging::Level::Warning,
            "project",
            "autosave_cleanup",
            error.what(),
            { {"cause", error.what()} });
    }
    timeline_command_service_.clearHistory();
    pending_audio_edit_batch_id_.reset();
    pending_transform_edit_batch_id_.reset();
    if (playback_controller_ != nullptr) {
        playback_controller_->invalidate(true);
    }
    playback_is_playing_ = false;

    project_controller_.reset();
    ++project_generation_;
    clearActiveTimelineSelection();
    playback_frame_index_ = 0;
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
    populateMediaBrowser();
    preview_widget_->clearFrame("Preview area\n\nImport media to display its first frame.");
    updateTimelineState();
    updatePlaybackControls();
    updatePlaybackStatus();
    updateHistoryActions();
    refreshLinkedImageTargets();
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
                    : ""}});
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
        const auto snapshots = project_controller_.recoverableSnapshotsForProject(
            selected_project_path);
        if (const auto selected_snapshot = chooseRecoverySnapshot(
                snapshots,
                main_window_detail::fromUtf8(
                    pathToUtf8(selected_project_path.filename())));
            selected_snapshot.has_value()) {
            source_path = *selected_snapshot;
        }
    }

    const auto recovered = source_path != selected_project_path;
    static_cast<void>(openProjectPath(
        source_path,
        selected_project_path,
        std::nullopt,
        [this, selected_project_path, recovered](bool succeeded) {
            if (!succeeded) return;
            try {
                project_controller_.removeCurrentUnsavedSnapshots();
                if (recovered) {
                    project_controller_.removeSnapshotsForProject(selected_project_path);
                }
            } catch (const project::ProjectError& error) {
            logging::Logger::instance().log(
                logging::Level::Warning,
                "project",
                "autosave_cleanup",
                error.what(),
                {{"project_path", pathToUtf8(selected_project_path)},
                 {"cause", error.what()}});
            }
        }));
}

bool MainWindow::openProjectPath(
    const std::filesystem::path& source_path,
    std::optional<std::filesystem::path> active_project_path,
    std::optional<project::ProjectDocument> saved_baseline,
    std::function<void(bool)> completion) {
    if (project_load_pending_) return false;

    if (active_media_import_cancel_) {
        active_media_import_cancel_->store(true, std::memory_order_relaxed);
    }
    const auto work_id = next_project_work_id_++;
    active_project_work_id_ = work_id;
    project_load_pending_ = true;
    active_project_source_path_ = media::MediaLibrary::canonicalPath(source_path);
    project_open_completion_ = std::move(completion);
    project_load_cancel_ = std::make_shared<std::atomic_bool>(false);
    setProjectLoadingState(true);

    auto* progress = new QProgressDialog(
        "Preparing project…", "Cancel", 0, 0, this);
    progress->setWindowTitle("Open Project");
    progress->setWindowModality(Qt::NonModal);
    progress->setAutoClose(false);
    progress->setAutoReset(false);
    progress->setMinimumDuration(150);
    const auto cancel = project_load_cancel_;
    connect(progress, &QProgressDialog::canceled, this, [cancel, progress]() {
        cancel->store(true, std::memory_order_relaxed);
        progress->setLabelText("Cancelling project preparation…");
        progress->setCancelButton(nullptr);
    });
    project_load_progress_ = progress;
    progress->show();

    const auto project_generation = project_generation_;
    const auto source = active_project_source_path_;
    QPointer<MainWindow> guard(this);
    media_task_pool_.start(QRunnable::create(
        [guard, work_id, project_generation, source,
         active_project_path = std::move(active_project_path),
         saved_baseline = std::move(saved_baseline), cancel]() mutable {
            application::ProjectOpenService service;
            auto result = service.prepare(
                source,
                std::move(active_project_path),
                std::move(saved_baseline),
                *cancel,
                [guard, work_id](std::size_t completed,
                                 std::size_t total,
                                 const std::filesystem::path& path) {
                    if (guard.isNull()) return;
                    const auto path_text = pathToUtf8(path.filename());
                    QMetaObject::invokeMethod(
                        guard.data(),
                        [guard, work_id, completed, total, path_text]() {
                            if (guard.isNull() || guard->active_project_work_id_ != work_id ||
                                guard->project_load_progress_ == nullptr) return;
                            guard->project_load_progress_->setRange(
                                0, static_cast<int>(total));
                            guard->project_load_progress_->setValue(
                                static_cast<int>(completed));
                            guard->project_load_progress_->setLabelText(
                                QString("Preparing %1 of %2: %3")
                                    .arg(completed + 1).arg(total)
                                    .arg(fromUtf8(path_text)));
                        },
                        Qt::QueuedConnection);
                });
            if (guard.isNull()) return;
            QMetaObject::invokeMethod(
                guard.data(),
                [guard, work_id, project_generation, result = std::move(result)]() mutable {
                    if (!guard.isNull()) {
                        guard->finishProjectOpen(
                            work_id, project_generation, std::move(result));
                    }
                },
                Qt::QueuedConnection);
        }));
    statusBar()->showMessage("Preparing project in the background...");
    return true;
}

void MainWindow::setProjectLoadingState(bool loading) {
    if (loading) {
        project_loading_widget_states_.clear();
        project_loading_action_states_.clear();
        const auto remember_widget = [this](QWidget* widget) {
            if (widget == nullptr) return;
            project_loading_widget_states_.emplace_back(widget, widget->isEnabled());
            widget->setEnabled(false);
        };
        remember_widget(centralWidget());
        const auto docks = findChildren<QDockWidget*>();
        for (auto* dock : docks) remember_widget(dock);
        const auto actions = findChildren<QAction*>();
        for (auto* action : actions) {
            if (!action->property("disabledDuringProjectLoad").toBool()) continue;
            project_loading_action_states_.emplace_back(action, action->isEnabled());
            action->setEnabled(false);
        }
        return;
    }

    for (const auto& [widget, was_enabled] : project_loading_widget_states_) {
        if (widget != nullptr) widget->setEnabled(was_enabled);
    }
    for (const auto& [action, was_enabled] : project_loading_action_states_) {
        if (action != nullptr) action->setEnabled(was_enabled);
    }
    project_loading_widget_states_.clear();
    project_loading_action_states_.clear();
}

void MainWindow::finishProjectOpen(
    std::uint64_t work_id,
    std::uint64_t project_generation,
    application::ProjectOpenResult result) {
    if (work_id != active_project_work_id_) return;
    project_load_pending_ = false;
    if (project_load_progress_ != nullptr) {
        project_load_progress_->hide();
        project_load_progress_->deleteLater();
        project_load_progress_ = nullptr;
    }
    project_load_cancel_.reset();
    setProjectLoadingState(false);

    const auto finish_callback = [this](bool succeeded) {
        auto callback = std::move(project_open_completion_);
        project_open_completion_ = {};
        if (callback) callback(succeeded);
    };
    if (project_generation != project_generation_) {
        statusBar()->showMessage("Project opening result was superseded.");
        finish_callback(false);
        return;
    }
    for (const auto& warning : result.warnings) {
        logging::Context context{
            {"path", pathToUtf8(warning.path)},
            {"cause", warning.cause}};
        if (warning.error_code.has_value()) {
            context.emplace_back("error_code", std::to_string(*warning.error_code));
        }
        logging::Logger::instance().log(
            logging::Level::Warning,
            warning.kind == application::ProjectOpenIssueKind::Media ? "media" : "project",
            "open_recovery_baseline",
            warning.cause,
            context);
    }

    if (result.status == application::ProjectOpenStatus::Cancelled) {
        statusBar()->showMessage("Project opening cancelled. The current project is unchanged.");
        finish_callback(false);
        return;
    }
    if (result.status == application::ProjectOpenStatus::Failed || !result.prepared) {
        const auto issue = result.failure.value_or(application::ProjectOpenIssue{
            application::ProjectOpenIssueKind::Unexpected,
            active_project_source_path_,
            "The project could not be prepared.",
            std::nullopt,
            std::nullopt});
        logging::Context context{
            {"project_path", pathToUtf8(active_project_source_path_)},
            {"path", pathToUtf8(issue.path)},
            {"cause", issue.cause}};
        if (issue.error_code.has_value()) {
            context.emplace_back("error_code", std::to_string(*issue.error_code));
        }
        if (issue.project_error_code.has_value()) {
            context.emplace_back(
                "project_error_code",
                std::to_string(static_cast<int>(*issue.project_error_code)));
        }
        const auto subsystem = issue.kind == application::ProjectOpenIssueKind::Media
            ? "media"
            : "project";
        logging::Logger::instance().log(
            logging::Level::Error,
            subsystem,
            "open",
            issue.cause,
            context);
        QMessageBox::warning(this, "Could not open project", fromUtf8(issue.cause));
        statusBar()->showMessage("Could not open project. The current project is unchanged.");
        finish_callback(false);
        return;
    }

    try {
        applyLoadedProject(std::move(*result.prepared));
        statusBar()->showMessage("Project opened.");
        finish_callback(true);
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "project",
            "open_commit",
            error.what(),
            {{"project_path", pathToUtf8(active_project_source_path_)},
             {"cause", error.what()}});
        QMessageBox::warning(this, "Could not open project", "The prepared project could not be applied.");
        statusBar()->showMessage("Could not open project.");
        finish_callback(false);
    }
}
void MainWindow::applyLoadedProject(application::PreparedProject prepared) {
    const auto& loaded_document = prepared.document;
    timeline_command_service_.clearHistory();
    pending_audio_edit_batch_id_.reset();
    pending_transform_edit_batch_id_.reset();
    if (playback_controller_ != nullptr) {
        playback_controller_->invalidate(true);
    }
    playback_is_playing_ = false;

    project_controller_.commitPrepared(
        std::move(prepared.media_library),
        std::move(prepared.timeline),
        std::move(prepared.active_project_path),
        loaded_document,
        std::move(prepared.saved_baseline));
    ++project_generation_;
    clearActiveTimelineSelection();
    playback_frame_index_ = 0;
    if (timeline_widget_ != nullptr) {
        timeline_widget_->setZoomFactor(loaded_document.timeline_zoom);
        timeline_widget_->setTrackRowHeight(loaded_document.timeline_row_height);
    }
    if (timeline_scroll_ != nullptr) {
        timeline_scroll_->horizontalScrollBar()->setValue(0);
    }

    populateMediaBrowser();
    refreshLinkedImageTargets();

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

        setActiveTimelineSelection(*first_clip_location);
        const auto& clip = timeline_model_.tracks()[first_clip_location->track_index]
            .clips[first_clip_location->clip_index];
        if (clip.kind == timeline::ClipKind::Text) {
            activateTimelineClipAt(
                first_clip_location->track_index,
                first_clip_location->clip_index,
                0,
                false);
        } else {
            const auto media_index = media_controller_.library().indexForPath(clip.source_path);
            if (media_index == media_controller_.library().size()) {
                throw std::runtime_error("The prepared project lost a timeline media item.");
            }
            const auto& media = media_items_[media_index];
            populateMediaBrowser(clip.source_path);
            if (!media.offline) preview_widget_->setFrame(media.first_frame);
            if (!media.offline) {
                activateTimelineClipAt(
                    first_clip_location->track_index,
                    first_clip_location->clip_index,
                    0,
                    false);
            }
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
