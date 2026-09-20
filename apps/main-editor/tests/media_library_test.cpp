#include "media/media_library.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

media::VideoMetadata metadata(const std::filesystem::path& path) {
    media::VideoMetadata value;
    value.source_path = path;
    value.display_name = "Video";
    value.frame_rate = 30.0;
    value.frame_count = 120;
    value.duration_seconds = 4.0;
    return value;
}

} // namespace

int main() {
    const auto source = std::filesystem::temp_directory_path() /
        ("creative-suite-media-library-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) + ".mkv");
    media::MediaLibrary library;
    media::VideoFrame frame;
    require(library.addOnline(metadata(source), frame, "Intro", "Footage/Scenes") ==
                media::MediaMutationResult::Changed,
            "The first media item was not added.");
    require(library.contains(source), "The canonical media path was not stored.");
    require(library.addOffline(source, "Duplicate", "Unsorted") ==
                media::MediaMutationResult::Duplicate,
            "A duplicate media path was accepted.");
    require(library.createBin("Footage/Scenes/Closeups") ==
                media::MediaMutationResult::Changed,
            "A hierarchical bin was not created.");
    require(library.moveToBin(0, "Footage/Scenes/Closeups") ==
                media::MediaMutationResult::Changed,
            "The media item was not moved.");
    require(library.isInBin(0, "Footage"), "Parent-bin filtering failed.");
    require(library.rename(0, "Renamed Intro") == media::MediaMutationResult::Changed,
            "The media label was not renamed.");
    require(library.markOffline(0) == media::MediaMutationResult::Changed,
            "The media item was not marked offline.");
    require(library.items()[0].offline, "Offline state was not stored.");
    require(library.restore(0, metadata(source), frame) == media::MediaMutationResult::Changed,
            "The media item was not restored.");
    require(!library.items()[0].offline, "Restore did not reactivate the media item.");
    return 0;
}
