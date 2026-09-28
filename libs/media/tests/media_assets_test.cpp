#include <creative_suite/media/media_importer.h>
#include <creative_suite/media/media_library.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

creative_suite::media::VideoMetadata metadata(const std::filesystem::path& path)
{
    creative_suite::media::VideoMetadata value;
    value.source_path = path;
    value.display_name = path.filename().string();
    value.width = 1280;
    value.height = 720;
    return value;
}

creative_suite::media::MediaItem importedItem(const std::filesystem::path& path)
{
    auto item_metadata = metadata(path);
    creative_suite::media::VideoFrame frame;
    frame.width = 1;
    frame.height = 1;
    frame.stride = 4;
    frame.rgba_pixels = {1, 2, 3, 255};
    return {std::move(item_metadata), std::move(frame), path.filename().string(),
            std::string(creative_suite::media::default_bin), false};
}

void testCatalogOperations()
{
    namespace media = creative_suite::media;
    const auto root = std::filesystem::temp_directory_path() /
        ("creative-suite-shared-media-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto source = root / "clip.mkv";

    media::MediaLibrary library;
    require(library.addOnline(metadata(source), {}, "Clip", "Footage/Scenes") ==
                media::MediaMutationResult::Changed,
            "The shared catalog did not add an online item.");
    require(library.contains(source.lexically_normal()),
            "The shared catalog did not index the canonical source path.");
    require(library.addOffline(source, "Duplicate") == media::MediaMutationResult::Duplicate,
            "The shared catalog accepted a duplicate path.");
    require(library.createBin("Footage/Scenes/Closeups") ==
                media::MediaMutationResult::Changed,
            "The shared catalog did not create a nested bin.");
    require(library.createBin("Footage/Empty") == media::MediaMutationResult::Changed,
            "The shared catalog did not preserve an empty bin.");
    require(library.renameBin("Footage/Scenes", "Footage/Renamed") ==
                media::MediaMutationResult::Changed &&
                library.items().front().bin_path == "Footage/Renamed",
            "Renaming a bin did not update its media and descendants.");
    require(library.moveBin("Footage/Renamed", "Footage/Renamed/Child") ==
                media::MediaMutationResult::InvalidBin,
            "The shared catalog allowed a bin to move under itself.");
    require(library.moveToBin(0, "Footage/Renamed/Closeups") ==
                media::MediaMutationResult::Changed && library.isInBin(0, "Footage"),
            "The shared catalog failed to move or filter media by a parent bin.");
    require(library.rename(0, "Renamed clip") == media::MediaMutationResult::Changed,
            "The shared catalog did not rename the display label.");
    require(library.markOffline(0) == media::MediaMutationResult::Changed &&
                library.items().front().offline && library.items().front().first_frame.width == 0,
            "Marking media offline did not release its cached frame.");
    require(library.restore(0, metadata(source), {}) == media::MediaMutationResult::Changed &&
                !library.items().front().offline &&
                library.items().front().display_name == "Renamed clip",
            "Restoring media did not retain its user-facing name.");
}

void testBatchImporterPartialFailuresAndCancellation()
{
    namespace media = creative_suite::media;
    const auto root = std::filesystem::temp_directory_path() /
        ("creative-suite-media-import-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    const std::vector<std::filesystem::path> paths{
        root / "good-a.mkv", root / "broken.mkv", root / "good-b.mkv"};
    std::atomic_bool cancel{false};
    std::size_t progress_calls = 0;
    media::MediaImporter importer([](const std::filesystem::path& path) {
        if (path.filename() == "broken.mkv") {
            throw media::MediaError("synthetic decode failure", 17);
        }
        return importedItem(path);
    });
    const auto batch = importer.process(paths, cancel,
        [&progress_calls](std::size_t index, std::size_t total,
                          const std::filesystem::path&) {
            require(index == progress_calls && total == 3,
                    "Import progress was not reported in input order.");
            ++progress_calls;
        });
    require(!batch.cancelled && batch.files.size() == 3 && progress_calls == 3,
            "A partial import failure incorrectly aborted the batch.");
    require(batch.files[0].status == media::MediaImportFileStatus::Imported &&
                batch.files[1].status == media::MediaImportFileStatus::Failed &&
                batch.files[1].error_code == 17 &&
                batch.files[2].status == media::MediaImportFileStatus::Imported,
            "The batch importer did not preserve per-file success and failure results.");

    cancel.store(false, std::memory_order_relaxed);
    media::MediaImporter cancelling_importer([&cancel](const std::filesystem::path& path) {
        cancel.store(true, std::memory_order_relaxed);
        return importedItem(path);
    });
    const auto cancelled = cancelling_importer.process(paths, cancel);
    require(cancelled.cancelled && cancelled.files.size() == 1 &&
                cancelled.files.front().status == media::MediaImportFileStatus::Discarded &&
                !cancelled.files.front().item.has_value(),
            "Cancellation retained an unfinished imported frame or processed extra files.");
}

} // namespace

int main()
{
    try {
        testCatalogOperations();
        testBatchImporterPartialFailuresAndCancellation();
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
