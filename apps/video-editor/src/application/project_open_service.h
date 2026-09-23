#pragma once

#include "media/media_library.h"
#include "project/project_document.h"
#include "timeline/timeline_model.h"

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace application {

enum class ProjectOpenStatus { Prepared, Cancelled, Failed };
enum class ProjectOpenIssueKind { Project, Media, Unexpected };

struct ProjectOpenIssue {
    ProjectOpenIssueKind kind = ProjectOpenIssueKind::Unexpected;
    std::filesystem::path path;
    std::string cause;
    std::optional<int> error_code;
    std::optional<project::ProjectErrorCode> project_error_code;
};

struct PreparedProject {
    media::MediaLibrary media_library;
    timeline::TimelineModel::Snapshot timeline;
    std::optional<std::filesystem::path> active_project_path;
    project::ProjectDocument document;
    std::optional<project::ProjectDocument> saved_baseline;
};

struct ProjectOpenResult {
    ProjectOpenStatus status = ProjectOpenStatus::Failed;
    std::optional<PreparedProject> prepared;
    std::optional<ProjectOpenIssue> failure;
    std::vector<ProjectOpenIssue> warnings;
};

class ProjectOpenService final {
public:
    using Progress = std::function<void(
        std::size_t,
        std::size_t,
        const std::filesystem::path&)>;

    [[nodiscard]] ProjectOpenResult prepare(
        const std::filesystem::path& source_path,
        std::optional<std::filesystem::path> active_project_path,
        std::optional<project::ProjectDocument> saved_baseline,
        const std::atomic_bool& cancel_requested,
        Progress progress = {}) const;
};

} // namespace application
