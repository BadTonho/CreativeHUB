#include "media_import_service.h"

#include "media/still_image_decoder.h"
#include "media/video_decoder.h"
#include "media/video_probe.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <utility>

namespace application {
namespace {

media::MediaItem importMedia(const std::filesystem::path& input_path) {
    const auto path = media::MediaLibrary::canonicalPath(input_path);
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    if (extension == ".gif") {
        throw media::MediaError("Animated GIF files are not supported.");
    }

    media::VideoMetadata metadata;
    media::VideoFrame first_frame;
    if (media::StillImageDecoder::supportsPath(path)) {
        const media::StillImageDecoder decoder;
        metadata = decoder.probe(path);
        first_frame = decoder.decode_first_frame(path);
    } else {
        const media::VideoProbe probe;
        const media::VideoDecoder decoder;
        metadata = probe.probe(path);
        first_frame = decoder.decode_first_frame(path);
    }
    metadata.source_path = path;
    const auto name = metadata.display_name.empty()
        ? media::MediaLibrary::defaultDisplayName(path)
        : metadata.display_name;
    metadata.display_name = name;
    return {std::move(metadata), std::move(first_frame), name,
            std::string(media::default_bin), false};
}

} // namespace

MediaImportService::MediaImportService() : processor_(importMedia) {}

MediaImportService::MediaImportService(Processor processor)
    : processor_(std::move(processor)) {}

MediaImportBatchResult MediaImportService::process(
    std::uint64_t work_id,
    std::uint64_t project_generation,
    std::uint64_t selection_generation,
    const std::vector<std::filesystem::path>& paths,
    const std::atomic_bool& cancel_requested,
    Progress progress) const {
    MediaImportBatchResult batch;
    batch.work_id = work_id;
    batch.project_generation = project_generation;
    batch.selection_generation = selection_generation;
    batch.files.reserve(paths.size());

    for (std::size_t index = 0; index < paths.size(); ++index) {
        if (cancel_requested.load(std::memory_order_relaxed)) {
            batch.cancelled = true;
            break;
        }

        const auto& input_path = paths[index];
        const auto path = media::MediaLibrary::canonicalPath(input_path);
        if (progress) progress(index, paths.size(), path);
        MediaImportFileResult result;
        result.path = path;
        try {
            auto item = processor_(path);
            item.metadata.source_path = media::MediaLibrary::canonicalPath(
                item.metadata.source_path.empty() ? path : item.metadata.source_path);
            result.status = MediaImportFileStatus::Imported;
            result.item = std::move(item);
        } catch (const media::MediaError& error) {
            result.status = MediaImportFileStatus::Failed;
            result.cause = error.what();
            result.error_code = error.error_code();
        } catch (const std::exception& error) {
            result.status = MediaImportFileStatus::Failed;
            result.cause = error.what();
        } catch (...) {
            result.status = MediaImportFileStatus::Failed;
            result.cause = "Unknown media import failure.";
        }
        if (cancel_requested.load(std::memory_order_relaxed)) {
            result.status = MediaImportFileStatus::Discarded;
            result.item.reset();
            result.error_code.reset();
            result.cause.clear();
            batch.cancelled = true;
            batch.files.push_back(std::move(result));
            break;
        }
        batch.files.push_back(std::move(result));
    }

    if (cancel_requested.load(std::memory_order_relaxed)) batch.cancelled = true;
    return batch;
}

} // namespace application
