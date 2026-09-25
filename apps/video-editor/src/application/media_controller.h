#pragma once

#include "editor_session.h"

#include <filesystem>
#include <optional>
#include <string>

namespace application {

enum class MediaCommandStatus { Applied, NoChange, Rejected };
enum class MediaCommandCode {
    None,
    Duplicate,
    InvalidItem,
    InvalidName,
    InvalidBin,
    NotOffline,
};

struct MediaCommandResult {
    MediaCommandStatus status = MediaCommandStatus::Rejected;
    MediaCommandCode code = MediaCommandCode::None;
    std::optional<std::filesystem::path> path;

    [[nodiscard]] bool changed() const noexcept {
        return status == MediaCommandStatus::Applied;
    }
};

class MediaController final {
public:
    explicit MediaController(EditorSession& session) noexcept;

    [[nodiscard]] const media::MediaLibrary& library() const noexcept;
    [[nodiscard]] MediaCommandResult commitImported(media::MediaItem item);
    [[nodiscard]] MediaCommandResult restore(
        const std::filesystem::path& path,
        media::VideoMetadata metadata,
        media::VideoFrame first_frame);
    [[nodiscard]] MediaCommandResult markOffline(const std::filesystem::path& path);
    [[nodiscard]] MediaCommandResult setImageEditorLink(
        const std::filesystem::path& path,
        std::optional<media::LinkedImageReference> link);
    [[nodiscard]] MediaCommandResult refreshImagePresentation(
        const std::filesystem::path& path,
        media::VideoMetadata metadata,
        media::VideoFrame first_frame);
    [[nodiscard]] MediaCommandResult rename(
        const std::filesystem::path& path,
        std::string display_name);
    [[nodiscard]] MediaCommandResult moveToBin(
        const std::filesystem::path& path,
        std::string bin_path);
    [[nodiscard]] MediaCommandResult createBin(std::string bin_path);
    [[nodiscard]] MediaCommandResult renameBin(
        std::string old_path,
        std::string new_path);
    [[nodiscard]] MediaCommandResult moveBin(
        std::string old_path,
        std::string new_path);
    void replaceLibrary(media::MediaLibrary library);
    void clear() noexcept;

private:
    [[nodiscard]] MediaCommandResult apply(media::MediaMutationResult result,
                                           std::optional<std::filesystem::path> path = {}) const;

    EditorSession& session_;
};

} // namespace application
