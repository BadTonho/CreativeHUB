#pragma once

#include "project_document.h"

#include <filesystem>

namespace project {

ProjectDocument load(const std::filesystem::path& project_path);
void save(const std::filesystem::path& project_path, const ProjectDocument& document);

} // namespace project
