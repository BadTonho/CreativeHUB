#include "media/video_decoder.h"
#include "media/video_metadata.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

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
        expectMediaError(decoder, empty_file, "Opening media for preview");

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
