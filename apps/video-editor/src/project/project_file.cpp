#include "project_file.h"

#include "project_file_detail.h"

namespace project {

ProjectDocument load(const std::filesystem::path& project_path) {
    return detail::load(project_path);
}

void save(const std::filesystem::path& project_path, const ProjectDocument& document) {
    detail::save(project_path, document);
}

} // namespace project
