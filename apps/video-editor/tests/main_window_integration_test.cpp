#include "main_window.h"

#include "project/project_file.h"
#include "settings/user_preferences.h"

#include <QApplication>
#include <QEventLoop>
#include <QDockWidget>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::filesystem::path uniqueTestDirectory() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
        ("creative-suite-main-window-test-" + std::to_string(stamp));
}

project::ProjectDocument makeMultiTrackProject(
    const std::filesystem::path& first_source,
    const std::filesystem::path& second_source) {
    project::ProjectDocument document;
    document.bins = {"Unsorted"};
    document.media = {
        {first_source, "Reference", "Unsorted", false},
        {second_source, "Reference Second", "Unsorted", false},
    };
    document.timeline_tracks = {
        {"Video 1", 1.0, false, {
            {first_source, 0, 0, 1},
        }},
        {"Video 2", 1.0, false, {
            {second_source, 0, 0, 1},
        }},
    };
    document.timeline_tracks[0].track_id = 1;
    document.timeline_tracks[0].clips[0].clip_id = 1;
    document.timeline_tracks[1].track_id = 2;
    document.timeline_tracks[1].clips[0].clip_id = 2;
    return document;
}

} // namespace

class MainWindowIntegrationTest final {
public:
    static void run(
        const std::filesystem::path& first_source,
        const std::filesystem::path& second_source) {
        const auto directory = uniqueTestDirectory();
        std::filesystem::create_directories(directory);

        QStandardPaths::setTestModeEnabled(true);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(
            QSettings::IniFormat,
            QSettings::UserScope,
            QString::fromStdString((directory / "settings").string()));
        QCoreApplication::setOrganizationName("CreativeSuiteTests");
        QCoreApplication::setApplicationName("MainWindowIntegration");
        QSettings().clear();
        settings::setProjectAutosaveEnabled(false);
        settings::setPreviewPerformanceMetricsEnabled(false);

        const auto project_path = directory / "multi-track.csp";
        const auto round_trip_path = directory / "multi-track-round-trip.csp";
        const auto original = makeMultiTrackProject(first_source, second_source);
        project::save(project_path, original);
        const auto loaded = project::load(project_path);

        {
            MainWindow window;
            QEventLoop open_loop;
            QTimer timeout;
            timeout.setSingleShot(true);
            bool open_succeeded = false;
            QObject::connect(&timeout, &QTimer::timeout, &open_loop, &QEventLoop::quit);
            require(window.openProjectPath(
                        project_path,
                        std::nullopt,
                        std::nullopt,
                        [&open_loop, &open_succeeded](bool succeeded) {
                            open_succeeded = succeeded;
                            open_loop.quit();
                        }),
                    "The MainWindow could not open the multi-track project.");
            require(window.project_load_pending_ && window.timeline_dock_ != nullptr &&
                        !window.timeline_dock_->isEnabled() &&
                        window.timeline_model_.trackCount() == 1 &&
                        !window.timeline_model_.hasClip(),
                    "Opening a project did not preserve the visible session while disabling editing.");
            timeout.start(30000);
            open_loop.exec();
            require(open_succeeded && !window.project_load_pending_,
                    "The background project open did not finish successfully.");

            require(window.timeline_model_.trackCount() == 2,
                    "Opening the project did not preserve both timeline tracks.");
            require(window.timeline_model_.clipCount(0) == 1 &&
                        window.timeline_model_.clipCount(1) == 1,
                    "Opening the project did not preserve clips on both tracks.");
            require(window.timeline_model_.tracks()[0].track_id == 1 &&
                        window.timeline_model_.tracks()[1].track_id == 2 &&
                        window.timeline_model_.tracks()[0].clips[0].clip_id == 1 &&
                        window.timeline_model_.tracks()[1].clips[0].clip_id == 2,
                    "Opening the project did not preserve stable track and clip identifiers.");
            require(window.active_timeline_track_id_.has_value() &&
                        window.active_timeline_clip_id_.has_value() &&
                        *window.active_timeline_track_id_ == 1 &&
                        *window.active_timeline_clip_id_ == 1,
                    "Opening the project did not select the first clip by stable identity.");
            require(!window.project_dirty_,
                    "Opening a saved multi-track project incorrectly marked it dirty.");
            require(window.windowTitle() == QStringLiteral("Main Editor"),
                    "Opening a saved multi-track project incorrectly added the dirty marker.");

            const auto current = window.currentProjectDocument();
            require(current.canvas_width == loaded.canvas_width &&
                        current.canvas_height == loaded.canvas_height &&
                        current.timeline_zoom == loaded.timeline_zoom &&
                        current.timeline_row_height == loaded.timeline_row_height,
                    "The current MainWindow document has different canvas or view settings.");
            require(current.media == loaded.media,
                    "The current MainWindow document has different media entries.");
            require(current.timeline_tracks == loaded.timeline_tracks,
                    "The current MainWindow document has different timeline tracks.");
            require(current.bins == loaded.bins,
                    "The current MainWindow document has different media bins.");

            require(window.saveProjectTo(round_trip_path, "save_as"),
                    "The MainWindow could not save the multi-track project.");
            const auto round_tripped = project::load(round_trip_path);
            require(round_tripped == current,
                    "The MainWindow multi-track project did not round-trip through save.");
            require(round_tripped.timeline_tracks.size() == 2 &&
                        round_tripped.timeline_tracks[0].clips.size() == 1 &&
                        round_tripped.timeline_tracks[1].clips.size() == 1,
                    "The round-trip project lost a track or clip.");

            window.pending_clip_activation_ = MainWindow::PendingClipActivation{
                0,
                0,
                1,
                false,
                window.playback_generation_,
                false,
                1,
                first_source};
            require(window.timeline_model_.removeClip(0, 0) ==
                        timeline::RemoveClipResult::Removed,
                    "The stale activation test could not remove its source clip.");
            window.handlePlaybackMediaReady(window.playback_generation_);
            require(!window.pending_clip_activation_.has_value() &&
                        !window.active_timeline_clip_id_.has_value() &&
                        !window.playback_is_playing_,
                    "A stale activation for a removed clip was not discarded safely.");

            window.setActiveTimelineSelection(timeline::ClipLocation{1, 0});
            window.handleTimelineClipMoveAt(1, 0, 0, 0);
            require(window.active_timeline_track_id_ == 1 &&
                        window.active_timeline_clip_id_ == 2 &&
                        window.active_timeline_track_index_cache_ == 0 &&
                        window.active_timeline_clip_index_cache_ == 0,
                    "A service-backed move did not update the MainWindow selection projection.");
            require(window.project_dirty_,
                    "A service-backed move did not update the project dirty state.");

            window.undoTimelineEdit();
            require(window.active_timeline_track_id_ == 2 &&
                        window.active_timeline_clip_id_ == 2 &&
                        window.active_timeline_track_index_cache_ == 1 &&
                        window.timeline_model_.locateClip(2) == timeline::ClipLocation{1, 0},
                    "Undo did not restore the service-backed move and selection.");

            window.media_controller_.clear();
            window.populateMediaBrowser();
            require(window.startMediaImport({first_source}),
                    "The MainWindow did not start the background media import.");
            media::VideoMetadata selected_metadata;
            selected_metadata.source_path = second_source;
            selected_metadata.display_name = "Keep this selection";
            require(window.media_controller_.commitImported({
                        selected_metadata, {}, selected_metadata.display_name,
                        "Unsorted", true}).changed(),
                    "The integration test could not add its selection fixture.");
            const auto selection_after_import_start = window.selection_generation_;
            window.populateMediaBrowser(second_source);
            QEventLoop import_loop;
            QTimer import_timeout;
            import_timeout.setSingleShot(true);
            QObject::connect(&import_timeout, &QTimer::timeout,
                             &import_loop, &QEventLoop::quit);
            QTimer import_poll;
            QObject::connect(&import_poll, &QTimer::timeout, &import_loop, [&]() {
                if (!window.active_media_import_cancel_) import_loop.quit();
            });
            require(window.media_import_progress_ != nullptr,
                    "The MainWindow did not present media import progress.");
            import_timeout.start(30000);
            import_poll.start(10);
            import_loop.exec();
            require(!window.active_media_import_cancel_ &&
                        window.media_controller_.library().contains(first_source) &&
                        window.selection_generation_ > selection_after_import_start,
                    "The MainWindow did not apply the completed import to the session library.");
            require(window.selectedMediaIndex().has_value(),
                    "The media-browser selection was lost after the import completed.");
            require(window.media_items_[*window.selectedMediaIndex()].metadata.source_path ==
                        media::MediaLibrary::canonicalPath(second_source),
                    "A late import changed the selection made while it was running.");

            window.media_controller_.clear();
            window.populateMediaBrowser();
            QEventLoop stale_import_loop;
            QTimer stale_import_timeout;
            stale_import_timeout.setSingleShot(true);
            QObject::connect(&stale_import_timeout, &QTimer::timeout,
                             &stale_import_loop, &QEventLoop::quit);
            QTimer stale_import_poll;
            QObject::connect(&stale_import_poll, &QTimer::timeout,
                             &stale_import_loop, [&]() {
                if (!window.active_media_import_cancel_) stale_import_loop.quit();
            });
            require(window.startMediaImport({second_source}),
                    "The MainWindow did not start the stale-generation import.");
            ++window.project_generation_;
            stale_import_timeout.start(30000);
            stale_import_poll.start(10);
            stale_import_loop.exec();
            require(!window.active_media_import_cancel_ &&
                        !window.media_controller_.library().contains(second_source) &&
                        !window.selectedMediaIndex().has_value(),
                    "An import result from an earlier project generation changed the session.");
        }

        std::error_code cleanup_error;
        std::filesystem::remove_all(directory, cleanup_error);
        if (cleanup_error) {
            throw std::runtime_error(
                "The MainWindow integration test could not clean up its temporary directory.");
        }
    }
};

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    if (argc != 3) {
        std::cerr << "Expected two media fixture paths.\n";
        return 1;
    }

    try {
        MainWindowIntegrationTest::run(argv[1], argv[2]);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
