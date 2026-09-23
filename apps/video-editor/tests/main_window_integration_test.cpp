#include "main_window.h"

#include "project/project_file.h"
#include "settings/user_preferences.h"

#include <QApplication>
#include <QSettings>
#include <QStandardPaths>

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
    for (const auto& track : document.timeline_tracks) {
        document.timeline_clips.insert(
            document.timeline_clips.end(),
            track.clips.begin(),
            track.clips.end());
    }
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
            require(window.openProjectPath(project_path),
                    "The MainWindow could not open the multi-track project.");
            QApplication::processEvents();

            require(window.timeline_model_.trackCount() == 2,
                    "Opening the project did not preserve both timeline tracks.");
            require(window.timeline_model_.clipCount(0) == 1 &&
                        window.timeline_model_.clipCount(1) == 1,
                    "Opening the project did not preserve clips on both tracks.");
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
            require(current.timeline_clips == loaded.timeline_clips,
                    "The current MainWindow document has a different compatibility clip view.");
            require(current.bins == loaded.bins,
                    "The current MainWindow document has different media bins.");
            require(current.timeline_clips.size() == 2,
                    "The compatibility clip view did not include every track.");

            require(window.saveProjectTo(round_trip_path, "save_as"),
                    "The MainWindow could not save the multi-track project.");
            const auto round_tripped = project::load(round_trip_path);
            require(round_tripped == current,
                    "The MainWindow multi-track project did not round-trip through save.");
            require(round_tripped.timeline_tracks.size() == 2 &&
                        round_tripped.timeline_tracks[0].clips.size() == 1 &&
                        round_tripped.timeline_tracks[1].clips.size() == 1,
                    "The round-trip project lost a track or clip.");
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
