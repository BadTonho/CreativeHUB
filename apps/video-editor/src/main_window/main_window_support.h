#pragma once

#include "media/video_metadata.h"

#include <QString>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

class QDockWidget;
class QWidget;

namespace main_window_detail {

[[nodiscard]] QString fromUtf8(const std::string& value);
[[nodiscard]] std::string pathToUtf8(const std::filesystem::path& path);
[[nodiscard]] QString formatOptionalDouble(
    const std::optional<double>& value,
    const QString& suffix);
[[nodiscard]] QString compactMediaBrowserName(std::string_view display_name);
[[nodiscard]] std::filesystem::path normalizedPath(
    const std::filesystem::path& path);
[[nodiscard]] QString mediaListText(const media::VideoMetadata& metadata);
[[nodiscard]] QString mediaDetailsText(const media::VideoMetadata& metadata);
[[nodiscard]] QString mediaItemListText(
    const media::VideoMetadata& metadata,
    std::string_view display_name,
    std::string_view bin_path,
    bool offline);
[[nodiscard]] QString compactMediaItemListText(
    const media::VideoMetadata& metadata,
    std::string_view display_name,
    bool offline);
[[nodiscard]] QWidget* createPlaceholder(
    const QString& title,
    const QString& description);
[[nodiscard]] QDockWidget* createDock(
    const QString& title,
    const QString& object_name,
    QWidget* content);

} // namespace main_window_detail
