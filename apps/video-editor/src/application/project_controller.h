#pragma once

#include "editor_session.h"
#include "project_document_mapper.h"
#include "project/autosave_manager.h"

#include <filesystem>
#include <optional>
#include <string>

namespace application {

enum class ProjectOperationStatus { Applied, NoChange, Failed };
enum class ProjectOperationCode {
    None,
    Io,
    InvalidFormat,
    UnsupportedVersion,
    MissingField,
    InvalidValue,
    MediaUnavailable,
    InvalidTimeline,
    Unexpected,
};

struct ProjectOperationResult {
    ProjectOperationStatus status = ProjectOperationStatus::NoChange;
    ProjectOperationCode code = ProjectOperationCode::None;
    std::string cause;
    std::filesystem::path path;
    std::optional<int> system_error;

    [[nodiscard]] bool succeeded() const noexcept {
        return status != ProjectOperationStatus::Failed;
    }
};

class ProjectController final {
public:
    explicit ProjectController(
        EditorSession& session,
        std::filesystem::path recovery_root = {},
        std::string session_id = {});

    [[nodiscard]] project::ProjectDocument document(
        TimelinePresentationState presentation = {}) const;
    [[nodiscard]] bool updateDirtyState(
        TimelinePresentationState presentation = {});
    void establishBaseline(project::ProjectDocument document);
    [[nodiscard]] ProjectOperationResult saveTo(
        const std::filesystem::path& path,
        TimelinePresentationState presentation = {});
    [[nodiscard]] ProjectOperationResult autosave(
        bool enabled,
        int retention,
        TimelinePresentationState presentation = {});
    void reset();
    void commitPrepared(
        media::MediaLibrary library,
        timeline::TimelineModel::Snapshot timeline,
        std::optional<std::filesystem::path> active_project_path,
        const project::ProjectDocument& loaded_document,
        std::optional<project::ProjectDocument> saved_baseline = std::nullopt);

    [[nodiscard]] std::vector<project::AutosaveSnapshot> validSnapshotsForProject(
        const std::filesystem::path& path) const;
    [[nodiscard]] std::vector<project::AutosaveSnapshot> recoverableSnapshotsForProject(
        const std::filesystem::path& path) const;
    [[nodiscard]] std::vector<project::AutosaveSnapshot> unsavedSnapshots() const;
    [[nodiscard]] std::vector<project::AutosaveSnapshot> snapshotsForProject(
        const std::filesystem::path& path) const;
    void removeSnapshot(const std::filesystem::path& path) const;
    void removeSnapshotsForProject(const std::filesystem::path& path) const;
    void removeUnsavedSnapshotsForSession(const std::filesystem::path& path) const;
    void removeCurrentUnsavedSnapshots() const;

    [[nodiscard]] const std::optional<std::filesystem::path>& projectPath() const noexcept;
    [[nodiscard]] bool dirty() const noexcept;

private:
    void assertDirtyInvariant(TimelinePresentationState presentation) const;

    EditorSession& session_;
    project::AutosaveManager autosave_manager_;
    std::optional<project::ProjectDocument> last_autosaved_document_;
};

} // namespace application
