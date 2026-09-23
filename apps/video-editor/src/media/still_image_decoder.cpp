#include "still_image_decoder.h"

#include "../logging/logger.h"
#include "video_decoder.h"
#include "video_probe.h"

#include <QImage>
#include <QImageReader>
#include <QString>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <system_error>

namespace media {
namespace {

QString pathToQString(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return QString::fromUtf8(
        reinterpret_cast<const char*>(value.data()),
        static_cast<qsizetype>(value.size()));
}

std::string pathToUtf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return std::string(reinterpret_cast<const char*>(value.data()), value.size());
}

std::string safePathForLog(const std::filesystem::path& path) noexcept {
    try {
        return pathToUtf8(path);
    } catch (...) {
        return "<unavailable>";
    }
}

[[noreturn]] void throwImageError(
    const std::filesystem::path& path,
    const QString& cause) {
    static_cast<void>(path);
    const auto message = cause.isEmpty()
        ? QStringLiteral("The image could not be read.")
        : cause;
    throw MediaError(
        std::string(message.toUtf8().constData()));
}

VideoFrame frameFromImage(QImage image, const std::filesystem::path& path) {
    if (image.isNull()) throwImageError(path, {});
    image = image.convertToFormat(QImage::Format_RGBA8888);

    VideoFrame frame;
    frame.width = image.width();
    frame.height = image.height();
    frame.stride = image.bytesPerLine();
    if (frame.width <= 0 || frame.height <= 0 || frame.stride <= 0) {
        throwImageError(path, QStringLiteral("The image has invalid dimensions."));
    }

    const auto size = static_cast<std::size_t>(frame.stride) *
        static_cast<std::size_t>(frame.height);
    frame.rgba_pixels.resize(size);
    for (int row = 0; row < frame.height; ++row) {
        std::copy_n(
            image.constScanLine(row),
            frame.stride,
            frame.rgba_pixels.data() + static_cast<std::size_t>(row) * frame.stride);
    }
    return frame;
}

} // namespace

bool StillImageDecoder::supportsPath(
    const std::filesystem::path& source_path) noexcept {
    try {
        const auto extension = source_path.extension().u8string();
        std::string lower(reinterpret_cast<const char*>(extension.data()), extension.size());
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char value) {
            return static_cast<char>(std::tolower(value));
        });
        return lower == ".png" || lower == ".jpg" || lower == ".jpeg" ||
            lower == ".bmp" || lower == ".webp" || lower == ".tif" || lower == ".tiff";
    } catch (...) {
        return false;
    }
}

VideoMetadata StillImageDecoder::probe(
    const std::filesystem::path& source_path) const {
    try {
        if (source_path.empty()) throw MediaError("Image path is empty.");
        std::error_code file_error;
        if (!std::filesystem::is_regular_file(source_path, file_error) || file_error) {
            throw MediaError("Input is not a readable regular image file.",
                             file_error ? std::optional<int>(file_error.value()) : std::nullopt);
        }

        QImageReader reader(pathToQString(source_path));
        reader.setAutoTransform(true);
        const QSize size = reader.size();
        if (reader.canRead() && size.isValid() && size.width() > 0 && size.height() > 0) {
            VideoMetadata metadata;
            metadata.kind = MediaKind::Image;
            metadata.source_path = source_path;
            metadata.display_name = pathToUtf8(source_path.filename());
            metadata.container_format = reader.format().isEmpty()
                ? "image"
                : QString::fromLatin1(reader.format()).toStdString();
            metadata.video_codec = "Still image";
            metadata.width = size.width();
            metadata.height = size.height();
            metadata.frame_rate = kStillImageFrameRate;
            metadata.duration_seconds = kStillImageDurationSeconds;
            metadata.frame_count = kStillImageFrameCount;
            return metadata;
        }

        // Some Qt distributions do not ship optional WebP/TIFF image plugins.
        // Keep QImageReader as the primary path, but use the already-linked
        // FFmpeg boundary as a compatibility fallback for those still-image
        // codecs instead of exposing a format that cannot be imported.
        auto metadata = VideoProbe{}.probe(source_path);
        metadata.kind = MediaKind::Image;
        metadata.source_path = source_path;
        metadata.display_name = pathToUtf8(source_path.filename());
        metadata.frame_rate = kStillImageFrameRate;
        metadata.duration_seconds = kStillImageDurationSeconds;
        metadata.frame_count = kStillImageFrameCount;
        metadata.audio.reset();
        return metadata;
    } catch (const MediaError& error) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "media",
            "image_probe",
            error.what(),
            {{"path", safePathForLog(source_path)}});
        throw;
    }
}

VideoFrame StillImageDecoder::decode_first_frame(
    const std::filesystem::path& source_path) const {
    try {
        QImageReader reader(pathToQString(source_path));
        reader.setAutoTransform(true);
        const auto image = reader.read();
        if (!image.isNull()) return frameFromImage(image, source_path);

        // See the probe fallback above. This is primarily needed when the
        // runtime Qt image plugin set lacks WebP or TIFF support.
        return VideoDecoder{}.decode_first_frame(source_path);
    } catch (const MediaError& error) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "media",
            "image_decode_first_frame",
            error.what(),
            {{"path", safePathForLog(source_path)}});
        throw;
    }
}

} // namespace media
