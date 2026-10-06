#pragma once

#include <QString>

#include <filesystem>
#include <string>

namespace motion::ui::detail {

[[nodiscard]] std::filesystem::path pathFromQString(const QString& value);
[[nodiscard]] std::string pathForLog(const std::filesystem::path& path);
[[nodiscard]] QString pathForDisplay(const std::filesystem::path& path);

} // namespace motion::ui::detail
