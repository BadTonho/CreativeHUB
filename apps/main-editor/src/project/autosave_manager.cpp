#include "autosave_manager.h"

#include "project_file.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <iterator>
#include <stdexcept>
#include <system_error>

namespace project {
namespace {

std::filesystem::path normalizedPath(const std::filesystem::path& path) {
    std::error_code error;
    const auto absolute = std::filesystem::absolute(path, error);
    if (!error) return absolute.lexically_normal();
    return path.lexically_normal();
}

std::filesystem::path defaultRecoveryRoot() {
    const auto location = QStandardPaths::writableLocation(
        QStandardPaths::AppDataLocation);
    if (location.isEmpty()) {
        throw std::runtime_error(
            "Could not determine the application data directory for autosave.");
    }
    return QFileInfo(location).filesystemFilePath() / "autosave";
}

bool isSnapshotName(const std::filesystem::path& path) {
    const auto name = path.filename().string();
    return name.rfind("snapshot-", 0) == 0 && path.extension() == ".csp";
}

bool isRegularFile(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error) && !error;
}

} // namespace

AutosaveManager::AutosaveManager(
    std::filesystem::path recovery_root,
    std::string session_id)
    : recovery_root_(recovery_root.empty()
                         ? defaultRecoveryRoot()
                         : std::move(recovery_root)),
      session_id_(session_id.empty()
                      ? QUuid::createUuid().toString(QUuid::WithoutBraces)
                            .toStdString()
                      : std::move(session_id)) {
    recovery_root_ = normalizedPath(recovery_root_);
}

const std::filesystem::path& AutosaveManager::recoveryRoot() const noexcept {
    return recovery_root_;
}

const std::string& AutosaveManager::sessionId() const noexcept {
    return session_id_;
}

std::filesystem::path AutosaveManager::savedProjectDirectory(
    const std::filesystem::path& project_path) {
    if (project_path.empty()) return {};
    return std::filesystem::path(project_path.string() + ".autosave");
}

std::filesystem::path AutosaveManager::currentUnsavedDirectory() const {
    return recovery_root_ / "unsaved" / session_id_;
}

void AutosaveManager::saveSnapshot(
    const ProjectDocument& document,
    const std::filesystem::path& project_path,
    int retention) {
    if (retention < kMinimumRetention || retention > kMaximumRetention) {
        throw ProjectError(
            ProjectErrorCode::InvalidValue,
            "Autosave retention must be between 5 and 20 snapshots.");
    }

    const auto directory = savedProjectDirectory(project_path);
    if (directory.empty()) {
        throw ProjectError(
            ProjectErrorCode::Io,
            "The project path is empty for a saved-project autosave.");
    }

    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        throw ProjectError(
            ProjectErrorCode::Io,
            "Could not create the project autosave directory.",
            error.value(),
            directory);
    }

    const auto timestamp = QDateTime::currentDateTimeUtc().toString(
        "yyyyMMdd-HHmmss-zzz").toStdString();
    auto snapshot_path = directory /
        ("snapshot-" + timestamp + "-" + std::to_string(sequence_++) + ".csp");
    while (std::filesystem::exists(snapshot_path)) {
        snapshot_path = directory /
            ("snapshot-" + timestamp + "-" + std::to_string(sequence_++) + ".csp");
    }
    project::save(snapshot_path, document);
    pruneDirectory(directory, retention);
}

void AutosaveManager::saveSnapshot(
    const ProjectDocument& document,
    int retention) {
    if (retention < kMinimumRetention || retention > kMaximumRetention) {
        throw ProjectError(
            ProjectErrorCode::InvalidValue,
            "Autosave retention must be between 5 and 20 snapshots.");
    }

    const auto directory = currentUnsavedDirectory();
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        throw ProjectError(
            ProjectErrorCode::Io,
            "Could not create the unsaved-project autosave directory.",
            error.value(),
            directory);
    }

    const auto timestamp = QDateTime::currentDateTimeUtc().toString(
        "yyyyMMdd-HHmmss-zzz").toStdString();
    auto snapshot_path = directory /
        ("snapshot-" + timestamp + "-" + std::to_string(sequence_++) + ".csp");
    while (std::filesystem::exists(snapshot_path)) {
        snapshot_path = directory /
            ("snapshot-" + timestamp + "-" + std::to_string(sequence_++) + ".csp");
    }
    project::save(snapshot_path, document);
    pruneDirectory(directory, retention);
}

std::vector<AutosaveSnapshot> AutosaveManager::snapshotsInDirectory(
    const std::filesystem::path& directory,
    const std::filesystem::path& project_path) const {
    std::vector<AutosaveSnapshot> snapshots;
    if (directory.empty()) return snapshots;

    std::error_code error;
    if (!std::filesystem::is_directory(directory, error) || error) {
        return snapshots;
    }

    for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
        if (error) break;
        if (!entry.is_regular_file(error) || error ||
            !isSnapshotName(entry.path())) {
            error.clear();
            continue;
        }
        const auto modified = std::filesystem::last_write_time(entry.path(), error);
        if (error) {
            error.clear();
            continue;
        }
        snapshots.push_back({entry.path(), modified, project_path});
    }

    std::sort(
        snapshots.begin(), snapshots.end(),
        [](const AutosaveSnapshot& left, const AutosaveSnapshot& right) {
            if (left.modified_time != right.modified_time) {
                return left.modified_time > right.modified_time;
            }
            return left.path.string() > right.path.string();
        });
    return snapshots;
}

std::vector<AutosaveSnapshot> AutosaveManager::snapshotsForProject(
    const std::filesystem::path& project_path) const {
    return snapshotsInDirectory(
        savedProjectDirectory(project_path), normalizedPath(project_path));
}

std::vector<AutosaveSnapshot> AutosaveManager::validSnapshotsForProject(
    const std::filesystem::path& project_path) const {
    return validSnapshots(snapshotsForProject(project_path));
}

std::vector<AutosaveSnapshot> AutosaveManager::validSnapshots(
    std::vector<AutosaveSnapshot> snapshots) const {
    snapshots.erase(
        std::remove_if(
            snapshots.begin(), snapshots.end(),
            [](const AutosaveSnapshot& snapshot) {
                if (!isRegularFile(snapshot.path)) return true;
                try {
                    static_cast<void>(project::load(snapshot.path));
                    return false;
                } catch (...) {
                    return true;
                }
            }),
        snapshots.end());
    return snapshots;
}

std::vector<AutosaveSnapshot> AutosaveManager::recoverableSnapshotsForProject(
    const std::filesystem::path& project_path) const {
    auto snapshots = validSnapshots(snapshotsForProject(project_path));
    std::error_code error;
    const auto project_time = std::filesystem::last_write_time(project_path, error);
    if (error) return {};

    std::optional<ProjectDocument> current_document;
    try {
        current_document = project::load(project_path);
    } catch (...) {
        // A valid recovery snapshot must remain usable even when the main
        // project file is damaged.
    }

    snapshots.erase(
        std::remove_if(
            snapshots.begin(), snapshots.end(),
            [&project_time, &current_document](const AutosaveSnapshot& snapshot) {
                if (snapshot.modified_time <= project_time) return true;
                if (!current_document.has_value()) return false;
                try {
                    return project::load(snapshot.path) == *current_document;
                } catch (...) {
                    return false;
                }
            }),
        snapshots.end());
    return snapshots;
}

std::vector<AutosaveSnapshot> AutosaveManager::unsavedSnapshots() const {
    std::vector<AutosaveSnapshot> snapshots;
    const auto root = recovery_root_ / "unsaved";
    std::error_code error;
    if (!std::filesystem::is_directory(root, error) || error) return snapshots;

    for (const auto& entry : std::filesystem::directory_iterator(root, error)) {
        if (error) break;
        if (!entry.is_directory(error) || error) {
            error.clear();
            continue;
        }
        auto session_snapshots = snapshotsInDirectory(entry.path(), {});
        snapshots.insert(
            snapshots.end(),
            std::make_move_iterator(session_snapshots.begin()),
            std::make_move_iterator(session_snapshots.end()));
    }

    return validSnapshots(std::move(snapshots));
}

void AutosaveManager::pruneDirectory(
    const std::filesystem::path& directory,
    int retention) const {
    auto snapshots = snapshotsInDirectory(directory, {});
    for (std::size_t index = static_cast<std::size_t>(retention);
         index < snapshots.size(); ++index) {
        std::error_code error;
        std::filesystem::remove(snapshots[index].path, error);
    }
}

void AutosaveManager::removeSnapshot(
    const std::filesystem::path& snapshot_path) const {
    std::error_code error;
    std::filesystem::remove(snapshot_path, error);
    if (error) {
        throw ProjectError(
            ProjectErrorCode::Io,
            "Could not remove the autosave snapshot.",
            error.value(),
            snapshot_path);
    }
}

void AutosaveManager::removeSnapshotsForProject(
    const std::filesystem::path& project_path) const {
    const auto directory = savedProjectDirectory(project_path);
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    if (error) {
        throw ProjectError(
            ProjectErrorCode::Io,
            "Could not remove the project autosave directory.",
            error.value(),
            directory);
    }
}

void AutosaveManager::removeUnsavedSnapshotsForSession(
    const std::filesystem::path& snapshot_path) const {
    const auto directory = snapshot_path.parent_path();
    for (const auto& snapshot : snapshotsInDirectory(directory, {})) {
        std::error_code error;
        std::filesystem::remove(snapshot.path, error);
        if (error) {
            throw ProjectError(
                ProjectErrorCode::Io,
                "Could not remove the unsaved-project autosave snapshot.",
                error.value(),
                snapshot.path);
        }
    }
}

void AutosaveManager::removeCurrentUnsavedSnapshots() const {
    const auto directory = currentUnsavedDirectory();
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    if (error) {
        throw ProjectError(
            ProjectErrorCode::Io,
            "Could not remove the unsaved-project autosave directory.",
            error.value(),
            directory);
    }
}

} // namespace project
