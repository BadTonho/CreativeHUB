#include "media/video_decoder.h"
#include "media/video_metadata.h"
#include "media/video_probe.h"
#include "media/still_image_decoder.h"

#include <QCoreApplication>
#include <QImage>
#include <QImageWriter>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void expectMediaError(const media::VideoDecoder& decoder,
                      const std::filesystem::path& path,
                      const std::string& expected_text) {
    try {
        static_cast<void>(decoder.decode_first_frame(path));
    } catch (const media::MediaError& error) {
        require(std::string(error.what()).find(expected_text) != std::string::npos,
                "Unexpected media error: " + std::string(error.what()));
        return;
    }

    throw std::runtime_error("Expected VideoDecoder to reject the input.");
}

std::filesystem::path uniqueTestDirectory() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
        ("creative-suite-video-decoder-test-" + std::to_string(stamp));
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    const auto directory = uniqueTestDirectory();

    try {
        std::filesystem::create_directories(directory);
        const media::VideoDecoder decoder;

        expectMediaError(
            decoder,
            directory / "missing.mkv",
            "Input is not a readable regular file");

        const auto empty_file = directory / "empty.mkv";
        std::ofstream(empty_file, std::ios::binary).close();
        expectMediaError(decoder, empty_file, "Opening media for");

        const auto image_path = directory / "transparent.png";
        QImage source_image(3, 2, QImage::Format_RGBA8888);
        source_image.fill(Qt::transparent);
        source_image.setPixelColor(1, 0, QColor(10, 20, 30, 128));
        require(source_image.save(QString::fromStdString(image_path.string()), "PNG"),
                "The test image could not be written.");

        const media::StillImageDecoder image_decoder;
        require(media::StillImageDecoder::supportsPath(image_path),
                "PNG was not recognized as a supported image path.");
        require(media::StillImageDecoder::supportsPath(directory / "image.jpg") &&
                    media::StillImageDecoder::supportsPath(directory / "image.jpeg") &&
                    media::StillImageDecoder::supportsPath(directory / "image.bmp") &&
                    media::StillImageDecoder::supportsPath(directory / "image.webp") &&
                    media::StillImageDecoder::supportsPath(directory / "image.tiff") &&
                    !media::StillImageDecoder::supportsPath(directory / "image.gif"),
                "The supported image extension set was incorrect.");
        const auto image_metadata = image_decoder.probe(image_path);
        require(image_metadata.kind == media::MediaKind::Image &&
                    image_metadata.width == 3 && image_metadata.height == 2 &&
                    image_metadata.frame_rate == 30.0 &&
                    image_metadata.duration_seconds == 5.0 &&
                    image_metadata.frame_count == 150 &&
                    !image_metadata.audio.has_value(),
                "Image metadata did not use the expected still-image defaults.");
        const auto image_frame = image_decoder.decode_first_frame(image_path);
        require(image_frame.width == 3 && image_frame.height == 2 &&
                    image_frame.stride == 12 && image_frame.rgba_pixels.size() == 24,
                "The image frame dimensions or buffer size were incorrect.");
        const auto pixel_offset = static_cast<std::size_t>(4);
        require(image_frame.rgba_pixels[pixel_offset] == 10 &&
                    image_frame.rgba_pixels[pixel_offset + 1] == 20 &&
                    image_frame.rgba_pixels[pixel_offset + 2] == 30 &&
                    image_frame.rgba_pixels[pixel_offset + 3] == 128,
                "Image RGBA pixels or transparency were not preserved.");

        const std::array<std::pair<const char*, const char*>, 5> image_formats{{
            {"jpeg", "JPG"},
            {"bmp", "BMP"},
            {"webp", "WEBP"},
            {"tif", "TIFF"},
            {"tiff", "TIFF"}}};
        for (const auto& [extension, format] : image_formats) {
            const auto format_name = QByteArray(format).toLower();
            if (!QImageWriter::supportedImageFormats().contains(format_name)) continue;
            const auto path = directory / (std::string("sample.") + extension);
            require(source_image.save(
                        QString::fromStdString(path.string()), format),
                    "The " + std::string(format) + " test image could not be written.");
            require(media::StillImageDecoder::supportsPath(path),
                    "The " + std::string(format) + " extension was not supported.");
            const auto metadata = image_decoder.probe(path);
            const auto frame = image_decoder.decode_first_frame(path);
            require(metadata.kind == media::MediaKind::Image &&
                        metadata.width == 3 && metadata.height == 2 &&
                        metadata.frame_count == 150 && frame.width == 3 &&
                        frame.height == 2 && frame.stride >= frame.width * 4,
                    "The " + std::string(format) + " image was not decoded correctly.");
        }

        if (argc == 4) {
            const auto frame = decoder.decode_first_frame(argv[1]);
            const auto expected_width = std::stoi(argv[2]);
            const auto expected_height = std::stoi(argv[3]);
            require(frame.width == expected_width, "Unexpected decoded frame width.");
            require(frame.height == expected_height, "Unexpected decoded frame height.");
            require(frame.stride == frame.width * 4, "Unexpected decoded frame stride.");
            require(frame.rgba_pixels.size() ==
                        static_cast<std::size_t>(frame.stride) *
                            static_cast<std::size_t>(frame.height),
                    "Decoded frame buffer size is incorrect.");

            const media::VideoProbe probe;
            const auto metadata = probe.probe(argv[1]);
            require(!metadata.audio.has_value(),
                    "The video-only reference unexpectedly reported audio.");
        }
    } catch (const std::exception& error) {
        std::error_code cleanup_error;
        std::filesystem::remove_all(directory, cleanup_error);
        std::cerr << error.what() << '\n';
        return 1;
    }

    std::error_code cleanup_error;
    std::filesystem::remove_all(directory, cleanup_error);
    return cleanup_error ? 1 : 0;
}
