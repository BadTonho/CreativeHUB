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
    const media::LinkedImageReference image_link{
        "shared-image-id", image_source.string() + ".image-editor/asset.cimg",
        image_source.string() + ".image-editor/asset.png"};
    require(library.setImageEditorLink(image_source, image_link) ==
                media::MediaMutationResult::Changed &&
                library.setImageEditorLink(image_source, image_link) ==
                media::MediaMutationResult::NoChange &&
                library.items()[1].image_editor_link == image_link,
            "A shared Image Editor link was not stored idempotently.");
    require(library.markOffline(1) == media::MediaMutationResult::Changed,
            "The linked image item could not be marked offline.");
    auto refreshed_metadata = metadata(image_link.published_output_path);
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
                library.items()[1].metadata.width == 3 &&
                library.items()[1].image_editor_link == image_link,
            "Publishing a linked image did not refresh its presentation while preserving the source identity and link.");

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
