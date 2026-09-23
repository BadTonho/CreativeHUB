#include "project_controller.h"

#include "media/media_library.h"
#include "project/project_file.h"

#include <utility>

namespace application {
namespace {

ProjectOperationCode mapError(project::ProjectErrorCode code) noexcept {
    switch (code) {
    case project::ProjectErrorCode::Io: return ProjectOperationCode::Io;
    case project::ProjectErrorCode::InvalidFormat: return ProjectOperationCode::InvalidFormat;
    case project::ProjectErrorCode::UnsupportedVersion: return ProjectOperationCode::UnsupportedVersion;
    case project::ProjectErrorCode::MissingField: return ProjectOperationCode::MissingField;
    case project::ProjectErrorCode::InvalidValue: return ProjectOperationCode::InvalidValue;
    case project::ProjectErrorCode::MediaUnavailable: return ProjectOperationCode::MediaUnavailable;
    case project::ProjectErrorCode::InvalidTimeline: return ProjectOperationCode::InvalidTimeline;
    }
    return ProjectOperationCode::Unexpected;
}

std::filesystem::path canonicalProjectPath(const std::filesystem::path& path) {
    return media::MediaLibrary::canonicalPath(path);
}

} // namespace

ProjectController::ProjectController(
    EditorSession& session,
    std::filesystem::path recovery_root,
    std::string session_id)
    : session_(session),
      autosave_manager_(std::move(recovery_root), std::move(session_id)) {}

project::ProjectDocument ProjectController::document(
    TimelinePresentationState presentation) const {
    return ProjectDocumentMapper::toDocument(session_, presentation);
}

bool ProjectController::updateDirtyState(TimelinePresentationState presentation) {
    if (!session_.saved_project_document_) {
        session_.project_dirty_ = false;
    } else {
        session_.project_dirty_ = document(presentation) != *session_.saved_project_document_;
    }
    return session_.project_dirty_;
}

void ProjectController::establishBaseline(project::ProjectDocument document) {
    session_.saved_project_document_ = std::move(document);
    session_.project_dirty_ = false;
    last_autosaved_document_.reset();
}

ProjectOperationResult ProjectController::saveTo(
    const std::filesystem::path& path,
    TimelinePresentationState presentation) {
    ProjectOperationResult result;
    result.path = path;
    try {
        const auto current = document(presentation);
        project::save(path, current);
        const auto canonical = canonicalProjectPath(path);
        session_.project_path_ = canonical;
        session_.saved_project_document_ = current;
        session_.project_dirty_ = false;
        last_autosaved_document_.reset();
        result.status = ProjectOperationStatus::Applied;
        result.path = canonical;
    } catch (const project::ProjectError& error) {
        result.status = ProjectOperationStatus::Failed;
        result.code = mapError(error.code());
        result.cause = error.what();
        result.path = error.related_path().empty() ? path : error.related_path();
        result.system_error = error.system_error();
    } catch (const std::exception& error) {
        result.status = ProjectOperationStatus::Failed;
        result.code = ProjectOperationCode::Unexpected;
        result.cause = error.what();
    }
    return result;
}

ProjectOperationResult ProjectController::autosave(
    bool enabled,
    int retention,
    TimelinePresentationState presentation) {
    ProjectOperationResult result;
    if (!enabled || !session_.project_dirty_) return result;
    const auto current = document(presentation);
    if (last_autosaved_document_ && *last_autosaved_document_ == current) return result;
    try {
        if (session_.project_path_) {
            autosave_manager_.saveSnapshot(current, *session_.project_path_, retention);
        } else {
            autosave_manager_.saveSnapshot(current, retention);
        }
        last_autosaved_document_ = current;
        result.status = ProjectOperationStatus::Applied;
        if (session_.project_path_) result.path = *session_.project_path_;
    } catch (const project::ProjectError& error) {
        result.status = ProjectOperationStatus::Failed;
        result.code = mapError(error.code());
        result.cause = error.what();
        result.path = error.related_path().empty()
            ? (session_.project_path_.value_or(std::filesystem::path{}))
            : error.related_path();
        result.system_error = error.system_error();
    } catch (const std::exception& error) {
        result.status = ProjectOperationStatus::Failed;
        result.code = ProjectOperationCode::Unexpected;
        result.cause = error.what();
        if (session_.project_path_) result.path = *session_.project_path_;
    }
    return result;
}

void ProjectController::reset() {
    timeline::TimelineModel::Snapshot empty_timeline;
    session_.timeline_.restore(std::move(empty_timeline));
    session_.history_.clear();
    session_.media_library_.clear();
    session_.selection_ = {};
    session_.project_path_.reset();
    project::ProjectDocument blank;
    blank.bins = {std::string(media::default_bin)};
    session_.saved_project_document_ = std::move(blank);
    session_.project_dirty_ = false;
    session_.playhead_frame_ = 0;
    session_.preserved_playhead_frame_.reset();
    last_autosaved_document_.reset();
}

void ProjectController::commitPrepared(
    media::MediaLibrary library,
    timeline::TimelineModel::Snapshot timeline,
    std::optional<std::filesystem::path> active_project_path,
    const project::ProjectDocument& loaded_document,
    std::optional<project::ProjectDocument> saved_baseline) {
    session_.timeline_.restore(std::move(timeline));
    session_.history_.clear();
    session_.media_library_ = std::move(library);
    session_.selection_ = {};
    session_.project_path_ = active_project_path.has_value()
        ? std::optional<std::filesystem::path>(canonicalProjectPath(*active_project_path))
        : std::nullopt;
    session_.saved_project_document_ = saved_baseline.has_value()
        ? std::move(saved_baseline)
        : std::optional<project::ProjectDocument>(loaded_document);
    session_.project_dirty_ = false;
    session_.playhead_frame_ = 0;
    session_.preserved_playhead_frame_.reset();
    last_autosaved_document_.reset();
}

std::vector<project::AutosaveSnapshot> ProjectController::validSnapshotsForProject(
    const std::filesystem::path& path) const {
    return autosave_manager_.validSnapshotsForProject(path);
}

std::vector<project::AutosaveSnapshot> ProjectController::recoverableSnapshotsForProject(
    const std::filesystem::path& path) const {
    return autosave_manager_.recoverableSnapshotsForProject(path);
}

std::vector<project::AutosaveSnapshot> ProjectController::unsavedSnapshots() const {
    return autosave_manager_.unsavedSnapshots();
}

std::vector<project::AutosaveSnapshot> ProjectController::snapshotsForProject(
    const std::filesystem::path& path) const {
    return autosave_manager_.snapshotsForProject(path);
}

void ProjectController::removeSnapshot(const std::filesystem::path& path) const {
    autosave_manager_.removeSnapshot(path);
}

void ProjectController::removeSnapshotsForProject(const std::filesystem::path& path) const {
    autosave_manager_.removeSnapshotsForProject(path);
}

void ProjectController::removeUnsavedSnapshotsForSession(
    const std::filesystem::path& path) const {
    autosave_manager_.removeUnsavedSnapshotsForSession(path);
}

void ProjectController::removeCurrentUnsavedSnapshots() const {
    autosave_manager_.removeCurrentUnsavedSnapshots();
}

const std::optional<std::filesystem::path>& ProjectController::projectPath() const noexcept {
    return session_.project_path_;
}

bool ProjectController::dirty() const noexcept {
    return session_.project_dirty_;
}

} // namespace application
