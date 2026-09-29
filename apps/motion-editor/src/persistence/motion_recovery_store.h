#pragma once

#include "../model/motion_project_data.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace motion::persistence {

struct MotionRecoverySnapshot {
    std::filesystem::path path;
    std::filesystem::file_time_type modified_time{};
    std::filesystem::path target_document_path;
};

class MotionRecoveryStore final {
public:
    static constexpr int default_retention = 5;
    static constexpr int minimum_retention = 5;
    static constexpr int maximum_retention = 20;

    explicit MotionRecoveryStore(
        std::filesystem::path recovery_root = {},
        std::string session_id = {});

    [[nodiscard]] const std::filesystem::path& recoveryRoot() const noexcept;
    [[nodiscard]] const std::string& sessionId() const noexcept;

    void saveSnapshot(
        const model::MotionProjectData& document,
        const std::filesystem::path& target_document_path,
        int retention);
    void saveSnapshot(const model::MotionProjectData& document, int retention);

    [[nodiscard]] std::vector<MotionRecoverySnapshot> snapshotsForProject(
        const std::filesystem::path& project_path) const;
    [[nodiscard]] std::vector<MotionRecoverySnapshot> validSnapshotsForProject(
        const std::filesystem::path& project_path) const;
    [[nodiscard]] std::vector<MotionRecoverySnapshot> recoverableSnapshotsForProject(
        const std::filesystem::path& project_path) const;
    [[nodiscard]] std::vector<MotionRecoverySnapshot> unsavedSnapshots() const;
    [[nodiscard]] bool containsSnapshotData(
        const model::MotionProjectData& document,
        const std::filesystem::path& target_document_path = {}) const;

    void removeSnapshot(const std::filesystem::path& snapshot_path) const;
    void removeSnapshotsForProject(const std::filesystem::path& project_path) const;
    void removeUnsavedSnapshotsForSession(
        const std::filesystem::path& snapshot_path) const;
    void removeCurrentUnsavedSnapshots() const;

    [[nodiscard]] static std::filesystem::path savedProjectDirectory(
        const std::filesystem::path& project_path);

private:
    [[nodiscard]] std::filesystem::path currentUnsavedDirectory() const;
    [[nodiscard]] std::vector<MotionRecoverySnapshot> snapshotsInDirectory(
        const std::filesystem::path& directory,
        const std::filesystem::path& target_document_path) const;
    [[nodiscard]] std::vector<MotionRecoverySnapshot> validSnapshots(
        std::vector<MotionRecoverySnapshot> snapshots) const;
    void pruneDirectory(const std::filesystem::path& directory, int retention) const;
    void saveSnapshotInDirectory(
        const std::filesystem::path& directory,
        const std::filesystem::path& target_document_path,
        const std::string& session_id,
        const model::MotionProjectData& document,
        int retention);

    std::filesystem::path recovery_root_;
    std::string session_id_;
    mutable std::size_t sequence_ = 0;
};

} // namespace motion::persistence
