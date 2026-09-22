#include "project/autosave_manager.h"
#include "project/project_file.h"

#include <QCoreApplication>

#include <cstdio>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

project::ProjectDocument document() {
    project::ProjectDocument result;
    result.bins = {"Unsorted"};
    result.timeline_tracks.push_back({"Video 1"});
    return result;
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    const auto root = std::filesystem::temp_directory_path() /
        "creative-suite-autosave-test";
    std::error_code cleanup_error;
    std::filesystem::remove_all(root, cleanup_error);

    try {
        const auto project_path = root / "sample.csp";
        std::filesystem::create_directories(root);
        const auto original = document();
        project::save(project_path, original);
        const auto original_contents = readFile(project_path);

        project::AutosaveManager manager(root / "recovery", "session-a");
        manager.saveSnapshot(original, project_path, 5);
        const auto snapshots = manager.snapshotsForProject(project_path);
        require(snapshots.size() == 1,
                "A saved-project autosave snapshot was not created.");
        require(project::load(snapshots.front().path) == original,
                "An autosave snapshot did not round-trip through the project format.");
        require(readFile(project_path) == original_contents,
                "Autosave changed the main project file.");

        auto recovery_document = original;
        recovery_document.timeline_zoom = 2.0;
        for (int index = 0; index < 7; ++index) {
            manager.saveSnapshot(recovery_document, project_path, 5);
        }
        require(manager.snapshotsForProject(project_path).size() == 5,
                "Saved-project autosave retention did not prune old snapshots.");
        const auto project_time = std::filesystem::last_write_time(project_path);
        for (const auto& snapshot : manager.snapshotsForProject(project_path)) {
            std::filesystem::last_write_time(
                snapshot.path,
                project_time + std::chrono::seconds(2));
        }

        const auto invalid_path =
            project::AutosaveManager::savedProjectDirectory(project_path) /
            "snapshot-invalid.csp";
        std::ofstream invalid_file(invalid_path, std::ios::binary);
        invalid_file << "not a project";
        invalid_file.close();
        require(manager.recoverableSnapshotsForProject(project_path).size() == 5,
                "An invalid snapshot was offered for recovery.");
        std::ofstream damaged_project(project_path, std::ios::binary | std::ios::trunc);
        damaged_project << "damaged project";
        damaged_project.close();
        require(manager.recoverableSnapshotsForProject(project_path).size() == 5,
                "A damaged main project hid valid recovery snapshots.");

        project::AutosaveManager unsaved_manager(root / "recovery", "session-b");
        for (int index = 0; index < 6; ++index) {
            unsaved_manager.saveSnapshot(original, 5);
        }
        const auto unsaved = unsaved_manager.unsavedSnapshots();
        require(unsaved.size() == 5,
                "Unsaved-project autosave retention did not prune old snapshots.");
        require(unsaved.front().project_path.empty(),
                "An unsaved snapshot unexpectedly contains a project path.");
        unsaved_manager.removeCurrentUnsavedSnapshots();
        require(unsaved_manager.unsavedSnapshots().empty(),
                "Unsaved-project autosave cleanup did not remove the session.");

        bool invalid_retention_rejected = false;
        try {
            manager.saveSnapshot(original, project_path, 4);
        } catch (const project::ProjectError& error) {
            invalid_retention_rejected =
                error.code() == project::ProjectErrorCode::InvalidValue;
        }
        require(invalid_retention_rejected,
                "Autosave accepted a retention value below the configured minimum.");

        std::filesystem::remove_all(root, cleanup_error);
        return 0;
    } catch (const std::exception& error) {
        std::filesystem::remove_all(root, cleanup_error);
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
