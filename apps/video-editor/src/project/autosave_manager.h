#pragma once

#include "project_document.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace project {

struct AutosaveSnapshot {
    std::filesystem::path path;
    std::filesystem::file_time_type modified_time{};
    std::filesystem::path project_path;

    friend bool operator==(const AutosaveSnapshot&, const AutosaveSnapshot&) = default;
};

class AutosaveManager final {
public:
    static constexpr int kDefaultRetention = 5;
    static constexpr int kMinimumRetention = 5;
    static constexpr int kMaximumRetention = 20;

    explicit AutosaveManager(
        std::filesystem::path recovery_root = {},
        std::string session_id = {});

    [[nodiscard]] const std::filesystem::path& recoveryRoot() const noexcept;
    [[nodiscard]] const std::string& sessionId() const noexcept;

    void saveSnapshot(
        const ProjectDocument& document,
        const std::filesystem::path& project_path,
        int retention);
    void saveSnapshot(
        const ProjectDocument& document,
        int retention);

    [[nodiscard]] std::vector<AutosaveSnapshot> snapshotsForProject(
        const std::filesystem::path& project_path) const;
    [[nodiscard]] std::vector<AutosaveSnapshot> validSnapshotsForProject(
        const std::filesystem::path& project_path) const;
    [[nodiscard]] std::vector<AutosaveSnapshot> recoverableSnapshotsForProject(
        const std::filesystem::path& project_path) const;
    [[nodiscard]] std::vector<AutosaveSnapshot> unsavedSnapshots() const;

    void removeSnapshot(const std::filesystem::path& snapshot_path) const;
    void removeSnapshotsForProject(const std::filesystem::path& project_path) const;
    void removeUnsavedSnapshotsForSession(
        const std::filesystem::path& snapshot_path) const;
    void removeCurrentUnsavedSnapshots() const;

    [[nodiscard]] static std::filesystem::path savedProjectDirectory(
        const std::filesystem::path& project_path);

private:
    [[nodiscard]] std::filesystem::path currentUnsavedDirectory() const;
    [[nodiscard]] std::vector<AutosaveSnapshot> snapshotsInDirectory(
        const std::filesystem::path& directory,
        const std::filesystem::path& project_path) const;
    [[nodiscard]] std::vector<AutosaveSnapshot> validSnapshots(
        std::vector<AutosaveSnapshot> snapshots) const;
    void pruneDirectory(
        const std::filesystem::path& directory,
        int retention) const;

    std::filesystem::path recovery_root_;
    std::string session_id_;
    mutable std::size_t sequence_ = 0;
};

} // namespace project
