#include "media_controller.h"

namespace application {

MediaController::MediaController(EditorSession& session) noexcept : session_(session) {}

const media::MediaLibrary& MediaController::library() const noexcept {
    return session_.media_library_;
}

MediaCommandResult MediaController::apply(
    media::MediaMutationResult result,
    std::optional<std::filesystem::path> path) const {
    MediaCommandResult output;
    output.path = std::move(path);
    switch (result) {
    case media::MediaMutationResult::Changed:
        output.status = MediaCommandStatus::Applied;
        return output;
    case media::MediaMutationResult::NoChange:
        output.status = MediaCommandStatus::NoChange;
        return output;
    case media::MediaMutationResult::Duplicate:
        output.code = MediaCommandCode::Duplicate;
        break;
    case media::MediaMutationResult::InvalidIndex:
        output.code = MediaCommandCode::InvalidItem;
        break;
    case media::MediaMutationResult::InvalidName:
        output.code = MediaCommandCode::InvalidName;
        break;
    case media::MediaMutationResult::InvalidBin:
        output.code = MediaCommandCode::InvalidBin;
        break;
    case media::MediaMutationResult::NotOffline:
        output.code = MediaCommandCode::NotOffline;
        break;
    }
    output.status = MediaCommandStatus::Rejected;
    return output;
}

MediaCommandResult MediaController::commitImported(media::MediaItem item) {
    const auto canonical = media::MediaLibrary::canonicalPath(item.metadata.source_path);
    const auto index = session_.media_library_.indexForPath(canonical);
    if (index != session_.media_library_.size()) {
        auto& library = session_.media_library_;
        if (!library.items_[index].offline) {
            return apply(media::MediaMutationResult::Duplicate, canonical);
        }
        item.metadata.source_path = canonical;
        item.metadata.display_name = library.items_[index].display_name;
        return apply(library.restore(index, std::move(item.metadata), std::move(item.first_frame)), canonical);
    }

    if (item.offline) {
        return apply(session_.media_library_.addOffline(
            std::move(item.metadata.source_path),
            std::move(item.display_name),
            std::move(item.bin_path)), canonical);
    }
    return apply(session_.media_library_.addOnline(
        std::move(item.metadata), std::move(item.first_frame),
        std::move(item.display_name), std::move(item.bin_path)), canonical);
}

MediaCommandResult MediaController::restore(
    const std::filesystem::path& path,
    media::VideoMetadata metadata,
    media::VideoFrame first_frame) {
    const auto canonical = media::MediaLibrary::canonicalPath(path);
    const auto index = session_.media_library_.indexForPath(canonical);
    if (index == session_.media_library_.size()) {
        return apply(media::MediaMutationResult::InvalidIndex, canonical);
    }
    metadata.source_path = canonical;
    return apply(session_.media_library_.restore(index, std::move(metadata), std::move(first_frame)), canonical);
}

MediaCommandResult MediaController::markOffline(const std::filesystem::path& path) {
    const auto canonical = media::MediaLibrary::canonicalPath(path);
    const auto index = session_.media_library_.indexForPath(canonical);
    if (index == session_.media_library_.size()) {
        return apply(media::MediaMutationResult::InvalidIndex, canonical);
    }
    return apply(session_.media_library_.markOffline(index), canonical);
}

MediaCommandResult MediaController::rename(
    const std::filesystem::path& path,
    std::string display_name) {
    const auto canonical = media::MediaLibrary::canonicalPath(path);
    const auto index = session_.media_library_.indexForPath(canonical);
    if (index == session_.media_library_.size()) {
        return apply(media::MediaMutationResult::InvalidIndex, canonical);
    }
    const auto mutation = session_.media_library_.rename(index, std::move(display_name));
    if (mutation == media::MediaMutationResult::Changed) {
        session_.timeline_.updateDisplayNameForSource(
            canonical, session_.media_library_.items()[index].display_name);
    }
    return apply(mutation, canonical);
}

MediaCommandResult MediaController::moveToBin(
    const std::filesystem::path& path,
    std::string bin_path) {
    const auto canonical = media::MediaLibrary::canonicalPath(path);
    const auto index = session_.media_library_.indexForPath(canonical);
    if (index == session_.media_library_.size()) {
        return apply(media::MediaMutationResult::InvalidIndex, canonical);
    }
    return apply(session_.media_library_.moveToBin(index, std::move(bin_path)), canonical);
}

MediaCommandResult MediaController::createBin(std::string bin_path) {
    return apply(session_.media_library_.createBin(std::move(bin_path)));
}

MediaCommandResult MediaController::renameBin(
    std::string old_path,
    std::string new_path) {
    return apply(session_.media_library_.renameBin(std::move(old_path), std::move(new_path)));
}

MediaCommandResult MediaController::moveBin(
    std::string old_path,
    std::string new_path) {
    return apply(session_.media_library_.moveBin(std::move(old_path), std::move(new_path)));
}

void MediaController::replaceLibrary(media::MediaLibrary library) {
    session_.media_library_ = std::move(library);
}

void MediaController::clear() noexcept {
    session_.media_library_.clear();
}

} // namespace application
