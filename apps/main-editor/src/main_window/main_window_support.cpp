#include "main_window_support.h"

#include <QDockWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>

#include <system_error>

namespace main_window_detail {

QString fromUtf8(const std::string& value) {
    return QString::fromUtf8(value.data(), static_cast<int>(value.size()));
}

QString compactMediaBrowserName(std::string_view display_name) {
    constexpr int kMaximumVisibleCharacters = 7;
    const auto name = fromUtf8(std::string(display_name));
    if (name.size() <= kMaximumVisibleCharacters) return name;
    return name.left(kMaximumVisibleCharacters) + QStringLiteral("...");
}

std::string pathToUtf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return std::string(reinterpret_cast<const char*>(value.data()), value.size());
}

QString formatOptionalDouble(const std::optional<double>& value, const QString& suffix) {
    if (!value.has_value()) return "Unknown";
    return QString::number(*value, 'f', 3) + suffix;
}

std::filesystem::path normalizedPath(const std::filesystem::path& path) {
    std::error_code error;
    const auto canonical = std::filesystem::weakly_canonical(path, error);
    if (!error) return canonical;

    const auto absolute = std::filesystem::absolute(path, error);
    if (!error) return absolute.lexically_normal();
    return path.lexically_normal();
}

QString mediaListText(const media::VideoMetadata& metadata) {
    return QString("%1 — %2×%3 — %4 — %5 — %6")
        .arg(fromUtf8(metadata.display_name))
        .arg(metadata.width)
        .arg(metadata.height)
        .arg(formatOptionalDouble(metadata.frame_rate, " FPS"))
        .arg(formatOptionalDouble(metadata.duration_seconds, " s"))
        .arg(fromUtf8(metadata.container_format));
}

QString mediaDetailsText(const media::VideoMetadata& metadata) {
    const QString frame_count = metadata.frame_count.has_value()
        ? QString::number(*metadata.frame_count)
        : "Unknown";
    const QString audio = metadata.audio.has_value()
        ? QString("%1, %2 Hz, %3 channels")
            .arg(fromUtf8(metadata.audio->codec))
            .arg(metadata.audio->sample_rate)
            .arg(metadata.audio->channel_count)
        : "None";

    return QString("Name: %1\n"
                   "Format: %2\n"
                   "Codec: %3\n"
                   "Resolution: %4×%5\n"
                   "Frame rate: %6\n"
                   "Duration: %7\n"
                   "Frames: %8\n"
                   "Audio: %9\n"
                   "Path: %10")
        .arg(fromUtf8(metadata.display_name))
        .arg(fromUtf8(metadata.container_format))
        .arg(fromUtf8(metadata.video_codec))
        .arg(metadata.width)
        .arg(metadata.height)
        .arg(formatOptionalDouble(metadata.frame_rate, " FPS"))
        .arg(formatOptionalDouble(metadata.duration_seconds, " s"))
        .arg(frame_count)
        .arg(audio)
        .arg(fromUtf8(pathToUtf8(metadata.source_path)));
}

QString mediaItemListText(const media::VideoMetadata& metadata,
                          std::string_view display_name,
                          std::string_view bin_path,
                          bool offline) {
    const QString state = offline ? " [Offline]" : "";
    return QString("%1%2 — %3 — %4")
        .arg(fromUtf8(display_name.empty() ? metadata.display_name : std::string(display_name)))
        .arg(state)
        .arg(fromUtf8(std::string(bin_path)))
        .arg(offline ? "Unavailable" : mediaListText(metadata));
}

QString compactMediaItemListText(
    const media::VideoMetadata& metadata,
    std::string_view display_name,
    bool offline) {
    const auto full_name = display_name.empty()
        ? std::string_view(metadata.display_name)
        : display_name;
    const auto name = compactMediaBrowserName(full_name);
    if (offline) return QString("%1 [Offline]").arg(name);
    return name;
}

QWidget* createPlaceholder(const QString& title, const QString& description) {
    auto* container = new QWidget;
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(8);

    auto* title_label = new QLabel(title, container);
    title_label->setStyleSheet("font-weight: 600; font-size: 14px;");
    layout->addWidget(title_label);

    auto* description_label = new QLabel(description, container);
    description_label->setWordWrap(true);
    description_label->setStyleSheet("color: #9aa4b2;");
    layout->addWidget(description_label);
    layout->addStretch();

    return container;
}

QDockWidget* createDock(const QString& title, const QString& object_name, QWidget* content) {
    auto* dock = new QDockWidget(title);
    dock->setObjectName(object_name);
    dock->setAllowedAreas(Qt::AllDockWidgetAreas);
    dock->setFeatures(QDockWidget::DockWidgetClosable |
                      QDockWidget::DockWidgetMovable |
                      QDockWidget::DockWidgetFloatable);
    dock->setWidget(content);
    return dock;
}

} // namespace main_window_detail
