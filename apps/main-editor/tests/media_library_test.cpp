#include "media/media_library.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string_view>

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
    require(library.createBin("Footage/Empty") ==
                media::MediaMutationResult::Changed,
            "An empty bin was not created.");
    require(library.renameBin("Footage/Scenes", "Footage/Renamed") ==
                media::MediaMutationResult::Changed,
            "A bin with descendants was not renamed.");
    require(library.items()[0].bin_path == "Footage/Renamed",
            "Media was not updated when its bin was renamed.");
    require(library.renameBin("Footage/Renamed", "Footage/Renamed/Child") ==
                media::MediaMutationResult::InvalidBin,
            "A bin was allowed to move into its own descendant.");
    require(library.renameBin("Footage/Renamed", "Footage/Empty") ==
                media::MediaMutationResult::InvalidBin,
            "A bin collision was not rejected during rename.");
    require(library.moveToBin(0, "Footage/Renamed/Closeups") ==
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

    require(library.createBin("Archive") == media::MediaMutationResult::Changed,
            "The archive bin was not created.");
    require(library.createBin("Existing/Scenes") == media::MediaMutationResult::Changed,
            "The collision bin was not created.");
    require(library.moveBin("Footage", "Archive/Footage") ==
                media::MediaMutationResult::Changed,
            "The bin subtree was not moved.");
    const auto has_bin = [&library](std::string_view path) {
        return std::find(library.bins().begin(), library.bins().end(), path) !=
            library.bins().end();
    };
    require(has_bin("Archive/Footage/Empty"),
            "The empty bin was not preserved while moving its parent.");
    require(has_bin("Archive/Footage/Renamed/Closeups"),
            "The child bin was not moved with its parent.");
    require(library.items()[0].bin_path == "Archive/Footage/Renamed/Closeups",
            "Media bin paths were not updated with the moved subtree.");
    require(library.moveBin("Archive", "Archive/Footage") ==
                media::MediaMutationResult::InvalidBin,
            "A bin was allowed to move into its own descendant.");
    require(library.moveBin("Archive/Footage", "Existing/Scenes") ==
                media::MediaMutationResult::InvalidBin,
            "A bin collision was not rejected.");
    require(library.moveBin("Unsorted", "Archive/Unsorted") ==
                media::MediaMutationResult::InvalidBin,
            "The default Unsorted bin was allowed to move.");
    return 0;
}
