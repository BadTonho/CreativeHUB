#include "media/video_decoder.h"
#include "media/video_metadata.h"
#include "media/video_probe.h"
#include "media/still_image_decoder.h"
#include <creative_suite/media/media_importer.h>
#include <creative_suite/diagnostics/logger.h>

#include <QByteArray>
#include <QCoreApplication>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

extern "C" {
#include <libavcodec/avcodec.h>
}

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

void writeGif(const std::filesystem::path& path, int frame_count) {
    const auto write_u16 = [](std::ostream& stream, std::uint16_t value) {
        stream.put(static_cast<char>(value & 0xff));
        stream.put(static_cast<char>((value >> 8) & 0xff));
    };
    std::ofstream file(path, std::ios::binary);
    file.write("GIF89a", 6);
    write_u16(file, 1);
    write_u16(file, 1);
    file.put(static_cast<char>(0x80));
    file.put(0);
    file.put(0);
    const std::array<unsigned char, 6> palette{0, 0, 0, 255, 255, 255};
    file.write(reinterpret_cast<const char*>(palette.data()), palette.size());
    for (int frame = 0; frame < frame_count; ++frame) {
        const std::array<unsigned char, 8> control{
            0x21, 0xf9, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00};
        file.write(reinterpret_cast<const char*>(control.data()), control.size());
        const std::array<unsigned char, 15> image{
            0x2c, 0, 0, 0, 0, 1, 0, 1, 0, 0,
            0x02, 0x02, 0x44, 0x01, 0x00};
        file.write(reinterpret_cast<const char*>(image.data()), image.size());
    }
    file.put(static_cast<char>(0x3b));
    if (!file) throw std::runtime_error("The test GIF could not be written.");
}

void writeMinimalTiff(const std::filesystem::path& path) {
    const auto write_u16 = [](std::ostream& stream, std::uint16_t value) {
        stream.put(static_cast<char>(value & 0xff));
        stream.put(static_cast<char>((value >> 8) & 0xff));
    };
    const auto write_u32 = [](std::ostream& stream, std::uint32_t value) {
        for (int byte = 0; byte < 4; ++byte) {
            stream.put(static_cast<char>((value >> (byte * 8)) & 0xff));
        }
    };
    const auto short_entry = [&write_u16, &write_u32](
                                 std::ostream& stream,
                                 std::uint16_t tag,
                                 std::uint16_t value) {
        write_u16(stream, tag);
        write_u16(stream, 3);
        write_u32(stream, 1);
        write_u16(stream, value);
        write_u16(stream, 0);
    };
    const auto long_entry = [&write_u16, &write_u32](
                                std::ostream& stream,
                                std::uint16_t tag,
                                std::uint32_t value) {
        write_u16(stream, tag);
        write_u16(stream, 4);
        write_u32(stream, 1);
        write_u32(stream, value);
    };

    std::ofstream file(path, std::ios::binary);
    file.write("II", 2);
    write_u16(file, 42);
    write_u32(file, 8);
    write_u16(file, 10);
    short_entry(file, 256, 1);
    short_entry(file, 257, 1);
    write_u16(file, 258);
    write_u16(file, 3);
    write_u32(file, 3);
    write_u32(file, 134);
    short_entry(file, 259, 1);
    short_entry(file, 262, 2);
    long_entry(file, 273, 140);
    short_entry(file, 277, 3);
    long_entry(file, 278, 1);
    long_entry(file, 279, 3);
    short_entry(file, 284, 1);
    write_u32(file, 0);
    write_u16(file, 8);
    write_u16(file, 8);
    write_u16(file, 8);
    file.put(10);
    file.put(20);
    file.put(30);
    if (!file) throw std::runtime_error("The test TIFF could not be written.");
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    const auto directory = uniqueTestDirectory();

    try {
        std::filesystem::create_directories(directory);
        creative_suite::diagnostics::Options logger_options;
        logger_options.minimum_level = creative_suite::diagnostics::Level::Debug;
        require(creative_suite::diagnostics::Logger::instance().initialize(
                    directory / "logs", logger_options),
                "The test diagnostics logger could not be initialized.");
        const media::VideoDecoder decoder;

        expectMediaError(
            decoder,
            directory / "missing.mkv",
            "Input is not a readable regular file");

        const auto empty_file = directory / "empty.mkv";
        std::ofstream(empty_file, std::ios::binary).close();
        expectMediaError(decoder, empty_file, "Opening media for");
        const auto invalid_webp = directory / "invalid.webp";
        std::ofstream(invalid_webp, std::ios::binary).close();
        require(!media::StillImageDecoder::supportsPath(invalid_webp) &&
                    !media::StillImageDecoder::supportsPath(directory / "missing.jpg"),
                "A missing or empty file was accepted from its image extension alone.");

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
                "PNG content was not recognized as a supported image.");
        const auto extensionless_image = directory / "image-without-extension";
        const auto image_with_wrong_extension = directory / "image-content.mp4";
        std::filesystem::copy_file(image_path, extensionless_image);
        std::filesystem::copy_file(image_path, image_with_wrong_extension);
        require(media::StillImageDecoder::supportsPath(extensionless_image) &&
                    media::StillImageDecoder::supportsPath(image_with_wrong_extension),
                "Image recognition depended on the file extension.");
        const auto imported_renamed_images =
            creative_suite::media::MediaImporter{}.process(
                {extensionless_image, image_with_wrong_extension}, cancel_import);
        require(imported_renamed_images.files.size() == 2 &&
                    std::all_of(imported_renamed_images.files.begin(),
                                imported_renamed_images.files.end(),
                                [](const auto& result) {
                                    return result.status ==
                                               creative_suite::media::MediaImportFileStatus::Imported &&
                                           result.item.has_value() &&
                                           result.item->metadata.kind == media::MediaKind::Image;
                                }),
                "Image content without its usual extension was not imported as a still image.");
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
            const auto reader_formats = QImageReader::supportedImageFormats();
            const bool qt_can_read = reader_formats.contains(QByteArray(extension).toLower()) ||
                (std::string(extension) == "tif" && reader_formats.contains("tiff")) ||
                (std::string(extension) == "tiff" && reader_formats.contains("tif"));
            const auto fallback_codec = std::string(extension) == "webp"
                ? AV_CODEC_ID_WEBP
                : AV_CODEC_ID_TIFF;
            if (!qt_can_read && avcodec_find_decoder(fallback_codec) == nullptr) continue;
            const auto path = directory / (std::string("sample.") + extension);
            require(source_image.save(
                        QString::fromStdString(path.string()), format),
                    "The " + std::string(format) + " test image could not be written.");
            require(media::StillImageDecoder::supportsPath(path),
                    "The " + std::string(format) + " image content was not supported.");
            const auto metadata = image_decoder.probe(path);
            const auto frame = image_decoder.decode_first_frame(path);
            require(metadata.kind == media::MediaKind::Image &&
                        metadata.width == 3 && metadata.height == 2 &&
                        metadata.frame_count == 150 && frame.width == 3 &&
                        frame.height == 2 && frame.stride >= frame.width * 4,
                    "The " + std::string(format) + " image was not decoded correctly.");
        }

        const auto reader_formats = QImageReader::supportedImageFormats();
        if (!reader_formats.contains("webp") &&
            avcodec_find_decoder(AV_CODEC_ID_WEBP) != nullptr) {
            const auto webp_path = directory / "ffmpeg-fallback.webp";
            const auto webp_fixture = QByteArray::fromBase64(
                "UklGRiIAAABXRUJQVlA4IBYAAAAwAQCdASoBAAEAAUAmJaQAA3AA/v89WAAAAA==");
            std::ofstream webp_file(webp_path, std::ios::binary);
            webp_file.write(webp_fixture.constData(), webp_fixture.size());
            webp_file.close();
            if (!webp_fixture.isEmpty()) {
                require(media::StillImageDecoder::supportsPath(webp_path),
                        "The available FFmpeg WebP decoder was not used as a fallback.");
                const auto metadata = image_decoder.probe(webp_path);
                const auto frame = image_decoder.decode_first_frame(webp_path);
                require(metadata.kind == media::MediaKind::Image &&
                            metadata.frame_count == 150 && frame.width > 0 && frame.height > 0,
                        "The FFmpeg WebP fallback did not produce a static image.");
            }
        }

        const auto tiff_reader_formats = QImageReader::supportedImageFormats();
        if (!tiff_reader_formats.contains("tif") &&
            !tiff_reader_formats.contains("tiff") &&
            avcodec_find_decoder(AV_CODEC_ID_TIFF) != nullptr) {
            const auto tiff_path = directory / "ffmpeg-fallback.tiff";
            writeMinimalTiff(tiff_path);
            require(media::StillImageDecoder::supportsPath(tiff_path),
                    "The available FFmpeg TIFF decoder was not used as a fallback.");
            const auto metadata = image_decoder.probe(tiff_path);
            const auto frame = image_decoder.decode_first_frame(tiff_path);
            require(metadata.kind == media::MediaKind::Image &&
                        metadata.frame_count == 150 && frame.width == 1 && frame.height == 1,
                    "The FFmpeg TIFF fallback did not produce a static image.");
        }

        if (reader_formats.contains("gif")) {
            const auto single_frame_gif = directory / "single-frame-content.bin";
            writeGif(single_frame_gif, 1);
            require(media::StillImageDecoder::supportsPath(single_frame_gif),
                    "A single-frame GIF was not recognized by its content.");
            const auto single_gif_metadata = image_decoder.probe(single_frame_gif);
            const auto single_gif_frame = image_decoder.decode_first_frame(single_frame_gif);
            require(single_gif_metadata.kind == media::MediaKind::Image &&
                        single_gif_metadata.frame_count == 150 &&
                        single_gif_frame.width == 1 && single_gif_frame.height == 1,
                    "A single-frame GIF did not import as a static image.");

            const auto animated_gif = directory / "animated-content.png";
            writeGif(animated_gif, 2);
            require(media::StillImageDecoder::supportsPath(animated_gif),
                    "Animated image content was not recognized independently of its extension.");
            const auto log_path =
                creative_suite::diagnostics::Logger::instance().log_path();
            const auto log_size_before = std::filesystem::file_size(log_path);
            const auto animated_result = creative_suite::media::MediaImporter{}.process(
                {animated_gif}, cancel_import);
            const auto log_size_after = std::filesystem::file_size(log_path);
            require(animated_result.files.size() == 1 &&
                        animated_result.files.front().status ==
                            creative_suite::media::MediaImportFileStatus::Failed &&
                        animated_result.files.front().cause.find(
                            "Animated images are not supported") != std::string::npos,
                    "Multi-frame image import did not return the expected rejection.");
            require(log_size_after == log_size_before,
                    "Expected animated-image rejection was written as a diagnostic error.");
        }

        const auto wav_with_image_extension = directory / "audio-content.png";
        std::filesystem::copy_file(wav_path, wav_with_image_extension);
        require(!media::StillImageDecoder::supportsPath(wav_with_image_extension),
                "Audio content was misclassified as a still image by its extension.");
        const auto imported_audio_with_wrong_extension =
            creative_suite::media::MediaImporter{}.process(
                {wav_with_image_extension}, cancel_import);
        require(imported_audio_with_wrong_extension.files.size() == 1 &&
                    imported_audio_with_wrong_extension.files.front().status ==
                        creative_suite::media::MediaImportFileStatus::Imported &&
                    imported_audio_with_wrong_extension.files.front().item.has_value() &&
                    imported_audio_with_wrong_extension.files.front().item->metadata.kind ==
                        media::MediaKind::Audio,
                "Audio content was not classified by its media content.");

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

            const auto misleading_video_path = directory / "video-content.png";
            std::filesystem::copy_file(argv[1], misleading_video_path);
            require(!media::StillImageDecoder::supportsPath(misleading_video_path),
                    "Video content was misclassified as a still image by its extension.");
            const auto imported_misleading_video =
                creative_suite::media::MediaImporter{}.process(
                    {misleading_video_path}, cancel_import);
            require(imported_misleading_video.files.size() == 1 &&
                        imported_misleading_video.files.front().status ==
                            creative_suite::media::MediaImportFileStatus::Imported &&
                        imported_misleading_video.files.front().item.has_value() &&
                        imported_misleading_video.files.front().item->metadata.kind ==
                            media::MediaKind::Video,
                    "Video content was not classified by its media content.");
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
