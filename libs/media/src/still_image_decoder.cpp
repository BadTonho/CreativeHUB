#include <creative_suite/media/still_image_decoder.h>

#include <creative_suite/diagnostics/logger.h>
#include <creative_suite/media/video_playback.h>
#include <creative_suite/media/video_probe.h>

#include <QImage>
#include <QImageReader>
#include <QString>

extern "C" {
#include <libavcodec/avcodec.h>
}

#include <algorithm>
#include <array>
#include <cstddef>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>

namespace creative_suite::media {
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

class UnsupportedAnimatedImage final : public std::runtime_error {
public:
    UnsupportedAnimatedImage()
        : std::runtime_error(
              "Animated images are not supported yet. Import a single-frame image instead.") {}
};

std::optional<AVCodecID> ffmpegFallbackCodec(
    const std::filesystem::path& path) noexcept {
    try {
        std::ifstream input(path, std::ios::binary);
        std::array<unsigned char, 12> signature{};
        input.read(
            reinterpret_cast<char*>(signature.data()),
            static_cast<std::streamsize>(signature.size()));
        const auto bytes_read = input.gcount();
        if (bytes_read >= 12 &&
            signature[0] == 'R' && signature[1] == 'I' &&
            signature[2] == 'F' && signature[3] == 'F' &&
            signature[8] == 'W' && signature[9] == 'E' &&
            signature[10] == 'B' && signature[11] == 'P') {
            return AV_CODEC_ID_WEBP;
        }
        if (bytes_read >= 4 &&
            ((signature[0] == 'I' && signature[1] == 'I' &&
              (signature[2] == 42 || signature[2] == 43) && signature[3] == 0) ||
             (signature[0] == 'M' && signature[1] == 'M' &&
              signature[2] == 0 && (signature[3] == 42 || signature[3] == 43)))) {
            return AV_CODEC_ID_TIFF;
        }
    } catch (...) {
    }
    return std::nullopt;
}

bool hasFfmpegFallback(const std::filesystem::path& path) noexcept {
    const auto codec_id = ffmpegFallbackCodec(path);
    return codec_id.has_value() && avcodec_find_decoder(*codec_id) != nullptr;
}

void rejectAdditionalQtImageFrame(QImageReader& reader) {
    if (reader.imageCount() > 1 || reader.jumpToNextImage()) {
        throw UnsupportedAnimatedImage{};
    }
}

[[noreturn]] void throwImageError(
    const std::filesystem::path& path,
    const QString& cause);

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
        std::error_code file_error;
        if (!std::filesystem::is_regular_file(source_path, file_error) || file_error) {
            return false;
        }
        QImageReader reader(pathToQString(source_path));
        reader.setDecideFormatFromContent(true);
        return reader.canRead() || hasFfmpegFallback(source_path);
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
        reader.setDecideFormatFromContent(true);
        reader.setAutoTransform(true);
        const QSize size = reader.size();
        if (reader.canRead() && size.isValid() && size.width() > 0 && size.height() > 0) {
            rejectAdditionalQtImageFrame(reader);
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
        // Use the already-linked FFmpeg boundary only for those image formats
        // when the runtime has the matching decoder.
        if (!hasFfmpegFallback(source_path)) {
            throw MediaError("No installed image reader can decode this image format.");
        }
        auto metadata = VideoProbe{}.probe(source_path);
        if (metadata.kind != MediaKind::Video || metadata.width <= 0 || metadata.height <= 0) {
            throw MediaError("The image could not be read by the available decoder.");
        }
        if (metadata.frame_count.has_value() && *metadata.frame_count > 1) {
            throw UnsupportedAnimatedImage{};
        }
        metadata.kind = MediaKind::Image;
        metadata.source_path = source_path;
        metadata.display_name = pathToUtf8(source_path.filename());
        metadata.frame_rate = kStillImageFrameRate;
        metadata.duration_seconds = kStillImageDurationSeconds;
        metadata.frame_count = kStillImageFrameCount;
        metadata.audio.reset();
        return metadata;
    } catch (const MediaError& error) {
        diagnostics::Logger::instance().log(
            diagnostics::Level::Error,
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
        reader.setDecideFormatFromContent(true);
        reader.setAutoTransform(true);
        if (reader.canRead()) rejectAdditionalQtImageFrame(reader);
        const auto image = reader.read();
        if (!image.isNull()) return frameFromImage(image, source_path);

        // See the probe fallback above. Decoding through a session also lets
        // this boundary reject a second frame instead of silently flattening
        // an animated WebP or multi-page TIFF.
        if (!hasFfmpegFallback(source_path)) {
            throwImageError(source_path, QStringLiteral(
                "No installed image reader can decode this image format."));
        }
        auto session = VideoPlaybackSession::open(source_path);
        const auto first_frame = session->decode_next_frame();
        if (!first_frame.has_value()) {
            throwImageError(source_path, QStringLiteral("The image contains no decodable frame."));
        }
        if (session->decode_next_frame().has_value()) {
            throw UnsupportedAnimatedImage{};
        }
        return **first_frame;
    } catch (const MediaError& error) {
        diagnostics::Logger::instance().log(
            diagnostics::Level::Error,
            "media",
            "image_decode_first_frame",
            error.what(),
            {{"path", safePathForLog(source_path)}});
        throw;
    }
}

} // namespace creative_suite::media
