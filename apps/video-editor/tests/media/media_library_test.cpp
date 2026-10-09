#include "media/media_library.h"
#include "media/video_frame.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

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
    auto published_video_metadata = metadata(source);
    published_video_metadata.width = 640;
    published_video_metadata.height = 360;
    published_video_metadata.frame_rate = 60.0;
    published_video_metadata.frame_count = 240;
    media::VideoFrame published_video_frame{
        2, 1, 8, std::vector<std::uint8_t>{10, 20, 30, 255, 40, 50, 60, 255}};
    require(library.refreshVideoPresentation(
                source, published_video_metadata, published_video_frame) ==
                media::MediaMutationResult::Changed &&
                library.items()[0].metadata.source_path ==
                    media::MediaLibrary::canonicalPath(source) &&
                library.items()[0].display_name == "Renamed Intro" &&
                library.items()[0].metadata.frame_rate == 60.0 &&
                library.items()[0].first_frame.rgba_pixels ==
                    published_video_frame.rgba_pixels,
            "Refreshing a published video replaces its presentation and retains source identity and label.");
    require(library.refreshVideoPresentation(
                source, published_video_metadata, published_video_frame) ==
                media::MediaMutationResult::NoChange,
            "An unchanged published video presentation was refreshed twice.");
    require(library.markOffline(0) == media::MediaMutationResult::Changed &&
                library.refreshVideoPresentation(
                    source, published_video_metadata, published_video_frame) ==
                    media::MediaMutationResult::Changed && !library.items()[0].offline,
            "Refreshing an offline linked render did not restore its cached presentation.");

    const auto image_source = std::filesystem::temp_directory_path() /
        ("creative-suite-media-library-image-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) + ".png");
    auto image_metadata = metadata(image_source);
    image_metadata.kind = media::MediaKind::Image;
    image_metadata.width = 2;
    image_metadata.height = 2;
    media::VideoFrame image_frame{2, 2, 8, std::vector<std::uint8_t>(16, 0x20)};
    require(library.addOnline(image_metadata, image_frame, "Still", "Stills") ==
                media::MediaMutationResult::Changed,
            "The image item was not added.");
    require(library.markOffline(1) == media::MediaMutationResult::Changed,
            "The image item could not be marked offline.");
    auto refreshed_metadata = metadata(image_source.string() + ".published.png");
    refreshed_metadata.kind = media::MediaKind::Image;
    refreshed_metadata.width = 3;
    refreshed_metadata.height = 1;
    media::VideoFrame refreshed_frame{3, 1, 12, std::vector<std::uint8_t>(12, 0x80)};
    require(library.refreshImagePresentation(
                image_source, refreshed_metadata, refreshed_frame) ==
                media::MediaMutationResult::Changed &&
                !library.items()[1].offline &&
                library.items()[1].metadata.source_path ==
                    media::MediaLibrary::canonicalPath(image_source) &&
                library.items()[1].metadata.width == 3,
            "Refreshing an image presentation did not preserve its source identity.");

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
