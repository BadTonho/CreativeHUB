#include "media/video_decoder.h"
#include "media/video_metadata.h"
#include "media/video_probe.h"
#include "media/still_image_decoder.h"
#include <creative_suite/media/media_importer.h>

#include <QCoreApplication>
#include <QImage>
#include <QImageWriter>
#include <array>
#include <atomic>
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

void writePcmWav(const std::filesystem::path& path) {
    constexpr std::uint32_t sample_rate = 48000;
    constexpr std::uint16_t channels = 2;
    constexpr std::uint16_t bits_per_sample = 16;
    constexpr std::uint32_t frame_count = 4800;
    constexpr std::uint32_t block_align = channels * bits_per_sample / 8;
    constexpr std::uint32_t data_size = frame_count * block_align;
    const auto write_u16 = [](std::ostream& stream, std::uint16_t value) {
        stream.put(static_cast<char>(value & 0xff));
        stream.put(static_cast<char>((value >> 8) & 0xff));
    };
    const auto write_u32 = [](std::ostream& stream, std::uint32_t value) {
        for (int byte = 0; byte < 4; ++byte) {
            stream.put(static_cast<char>((value >> (byte * 8)) & 0xff));
        }
    };
    std::ofstream file(path, std::ios::binary);
    file.write("RIFF", 4);
    write_u32(file, 36 + data_size);
    file.write("WAVEfmt ", 8);
    write_u32(file, 16);
    write_u16(file, 1);
    write_u16(file, channels);
    write_u32(file, sample_rate);
    write_u32(file, sample_rate * block_align);
    write_u16(file, static_cast<std::uint16_t>(block_align));
    write_u16(file, bits_per_sample);
    file.write("data", 4);
    write_u32(file, data_size);
    for (std::uint32_t frame = 0; frame < frame_count; ++frame) {
        const auto sample = static_cast<std::int16_t>(frame % 80 < 40 ? 5000 : -5000);
        write_u16(file, static_cast<std::uint16_t>(sample));
        write_u16(file, static_cast<std::uint16_t>(sample));
    }
    if (!file) throw std::runtime_error("The test WAV could not be written.");
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

        const auto wav_path = directory / "independent-audio.wav";
        writePcmWav(wav_path);
        const media::VideoProbe probe;
        const auto audio_metadata = probe.probe(wav_path);
        require(audio_metadata.kind == media::MediaKind::Audio &&
                    audio_metadata.width == 0 && audio_metadata.height == 0 &&
                    !audio_metadata.frame_rate.has_value() &&
                    !audio_metadata.frame_count.has_value() &&
                    audio_metadata.audio.has_value() &&
                    audio_metadata.audio->sample_rate == 48000 &&
                    audio_metadata.audio->channel_count == 2 &&
                    audio_metadata.duration_seconds.has_value(),
                "Audio-only media did not expose audio metadata without video timing.");
        std::atomic_bool cancel_import{false};
        const auto imported_audio = creative_suite::media::MediaImporter{}.process(
            {wav_path}, cancel_import);
        require(!imported_audio.cancelled && imported_audio.files.size() == 1 &&
                    imported_audio.files.front().status ==
                        creative_suite::media::MediaImportFileStatus::Imported &&
                    imported_audio.files.front().item.has_value() &&
                    imported_audio.files.front().item->metadata.kind ==
                        media::MediaKind::Audio &&
                    imported_audio.files.front().item->first_frame.width == 0 &&
                    imported_audio.files.front().item->first_frame.rgba_pixels.empty(),
                "Audio-only import attempted to create a visual frame.");
        const auto invalid_audio = creative_suite::media::MediaImporter{}.process(
            {empty_file}, cancel_import);
        require(invalid_audio.files.size() == 1 &&
                    invalid_audio.files.front().status ==
                        creative_suite::media::MediaImportFileStatus::Failed &&
                    !invalid_audio.files.front().cause.empty(),
                "An invalid audio source did not return an actionable import failure.");

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

            const media::VideoProbe reference_probe;
            const auto metadata = reference_probe.probe(argv[1]);
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
