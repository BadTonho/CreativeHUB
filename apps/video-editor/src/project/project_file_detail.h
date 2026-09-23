#pragma once

#include "project_document.h"

namespace project::detail {

ProjectDocument load(const std::filesystem::path& project_path);
void save(const std::filesystem::path& project_path, const ProjectDocument& document);
void validateDocument(const ProjectDocument& document,
                      const std::filesystem::path& project_path);

} // namespace project::detail
