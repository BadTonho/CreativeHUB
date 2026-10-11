#pragma once

#include "media/video_metadata.h"

#include <QString>
#include <QStringList>
#include <QIcon>

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

class QDockWidget;
class QToolButton;
class QWidget;

namespace main_window_detail {

[[nodiscard]] QString fromUtf8(const std::string& value);
[[nodiscard]] std::string pathToUtf8(const std::filesystem::path& path);
[[nodiscard]] QString formatOptionalDouble(
    const std::optional<double>& value,
    const QString& suffix);
[[nodiscard]] QString compactMediaBrowserName(std::string_view display_name);
[[nodiscard]] QStringList stillImageFilePatterns();
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

struct DockTitleBarAction {
    QString object_name;
    QString accessible_name;
    QString tool_tip;
    QIcon icon;
    bool checkable = false;
    std::function<void(bool)> triggered;
};

[[nodiscard]] QDockWidget* createDock(
    const QString& title,
    const QString& object_name,
    QWidget* content);
[[nodiscard]] QToolButton* addDockTitleBarAction(
    QDockWidget* dock,
    DockTitleBarAction action);

} // namespace main_window_detail
