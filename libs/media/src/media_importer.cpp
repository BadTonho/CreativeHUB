#include <creative_suite/media/media_importer.h>

#include <creative_suite/media/still_image_decoder.h>
#include <creative_suite/media/video_decoder.h>
#include <creative_suite/media/video_probe.h>

#include <QMimeDatabase>
#include <QString>

#include <exception>
#include <utility>

namespace creative_suite::media {
namespace {

QString pathToQString(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return QString::fromUtf8(
        reinterpret_cast<const char*>(value.data()),
        static_cast<qsizetype>(value.size()));
}

bool hasImageContent(const std::filesystem::path& path) noexcept {
    try {
        const auto mime = QMimeDatabase{}.mimeTypeForFile(
            pathToQString(path), QMimeDatabase::MatchContent);
        return mime.isValid() && mime.name().startsWith(QStringLiteral("image/"));
    } catch (...) {
        return false;
    }
}

MediaItem importMedia(const std::filesystem::path& input_path)
{
    const auto path = MediaLibrary::canonicalPath(input_path);

    VideoMetadata metadata;
    VideoFrame first_frame;
    if (StillImageDecoder::supportsPath(path) || hasImageContent(path)) {
        const StillImageDecoder decoder;
        metadata = decoder.probe(path);
        first_frame = decoder.decode_first_frame(path);
    } else {
        const VideoProbe probe;
        metadata = probe.probe(path);
        if (metadata.kind != MediaKind::Audio) {
            const VideoDecoder decoder;
            first_frame = decoder.decode_first_frame(path);
        }
    }
    metadata.source_path = path;
    const auto name = metadata.display_name.empty()
        ? MediaLibrary::defaultDisplayName(path)
        : metadata.display_name;
    metadata.display_name = name;
    return {std::move(metadata), std::move(first_frame), name,
            std::string(default_bin), false};
}

} // namespace

MediaImporter::MediaImporter()
    : processor_(importMedia)
{
}

MediaImporter::MediaImporter(Processor processor)
    : processor_(std::move(processor))
{
}

MediaImportBatchResult MediaImporter::process(
    const std::vector<std::filesystem::path>& paths,
    const std::atomic_bool& cancel_requested,
    Progress progress) const
{
    MediaImportBatchResult batch;
    batch.files.reserve(paths.size());

    for (std::size_t index = 0; index < paths.size(); ++index) {
        if (cancel_requested.load(std::memory_order_relaxed)) {
            batch.cancelled = true;
            break;
        }

        const auto& input_path = paths[index];
        const auto path = MediaLibrary::canonicalPath(input_path);
        if (progress) progress(index, paths.size(), path);
        MediaImportFileResult result;
        result.path = path;
        try {
            auto item = processor_(path);
            item.metadata.source_path = MediaLibrary::canonicalPath(
                item.metadata.source_path.empty() ? path : item.metadata.source_path);
            result.status = MediaImportFileStatus::Imported;
            result.item = std::move(item);
        } catch (const MediaError& error) {
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

} // namespace creative_suite::media
