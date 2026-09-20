#include "media/video_metadata.h"
#include "media/video_playback.h"

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

std::filesystem::path uniqueTestDirectory() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
        ("creative-suite-video-playback-test-" + std::to_string(stamp));
}

void expectMediaError(const std::filesystem::path& path, const std::string& expected_text) {
    try {
        static_cast<void>(media::VideoPlaybackSession::open(path));
    } catch (const media::MediaError& error) {
        require(std::string(error.what()).find(expected_text) != std::string::npos,
                "Unexpected media error: " + std::string(error.what()));
        return;
    }

    throw std::runtime_error("Expected VideoPlaybackSession to reject the input.");
}

void validateReference(const std::filesystem::path& path) {
    auto session = media::VideoPlaybackSession::open(path);
    require(session->current_frame_index() == -1, "Session did not start before the first frame.");

    const auto first = session->decode_next_frame();
    require(first.has_value(), "The first frame was not decoded.");
    require(first->width == 640 && first->height == 360, "Unexpected first frame dimensions.");
    require(session->current_frame_index() == 0, "First frame index is incorrect.");

    const auto second = session->decode_next_frame();
    require(second.has_value(), "The second frame was not decoded.");
    require(session->current_frame_index() == 1, "Second frame index is incorrect.");

    const auto first_seek = session->decode_frame_at(0);
    require(first_seek.has_value(), "Seeking to the first frame failed.");
    require(session->current_frame_index() == 0, "Previous frame index is incorrect.");
    require(first_seek->width == 640 && first_seek->height == 360,
            "The first seek frame dimensions are incorrect.");

    const auto intermediate = session->decode_frame_at(30);
    require(intermediate.has_value(), "The intermediate frame could not be decoded.");
    require(session->current_frame_index() == 30, "Intermediate frame index is incorrect.");
    require(intermediate->width == 640 && intermediate->height == 360,
            "The intermediate frame dimensions are incorrect.");

    const auto after_intermediate = session->decode_next_frame();
    require(after_intermediate.has_value(), "Decoding did not continue after seeking.");
    require(session->current_frame_index() == 31,
            "The frame after an intermediate seek has the wrong index.");

    bool negative_seek_rejected = false;
    try {
        static_cast<void>(session->decode_frame_at(-1));
    } catch (const media::MediaError& error) {
        require(std::string(error.what()).find("negative") != std::string::npos,
                "Unexpected negative seek error.");
        negative_seek_rejected = true;
    }
    require(negative_seek_rejected, "Negative seek was not rejected.");

    session->reset();
    std::int64_t final_frame_index = -1;
    std::size_t decoded_frames = 0;
    while (const auto frame = session->decode_next_frame()) {
        require(frame->width == 640 && frame->height == 360,
                "A sequential frame has unexpected dimensions.");
        final_frame_index = session->current_frame_index();
        ++decoded_frames;
    }
    require(decoded_frames >= 2, "The session did not decode a complete sequence.");
    require(final_frame_index >= 0, "The reference video has no final frame.");
    require(session->at_end(), "The session did not report end-of-file.");

    session->reset();
    const auto final_frame = session->decode_frame_at(final_frame_index);
    require(final_frame.has_value(), "Seeking to the final frame failed.");
    require(session->current_frame_index() == final_frame_index,
            "Final seek frame index is incorrect.");
    require(final_frame->width == 640 && final_frame->height == 360,
            "The final seek frame dimensions are incorrect.");

    const auto after_final = session->decode_next_frame();
    require(!after_final.has_value(), "A frame was decoded after the final frame.");
    require(session->at_end(), "The final-frame seek did not reach end-of-file.");

    const auto outside_range = session->decode_frame_at(10000);
    require(!outside_range.has_value(), "An out-of-range frame was decoded.");
    require(session->at_end(), "Out-of-range seeking did not reach end-of-file.");

    session->reset();
    require(session->current_frame_index() == -1, "Reset did not restore the initial index.");
}

} // namespace

int main(int argc, char* argv[]) {
    const auto directory = uniqueTestDirectory();

    try {
        std::filesystem::create_directories(directory);
        expectMediaError(directory / "missing.mkv", "Input is not a readable regular file");

        const auto empty_file = directory / "empty.mkv";
        std::ofstream(empty_file, std::ios::binary).close();
        expectMediaError(empty_file, "Opening media for playback");

        if (argc == 2) validateReference(argv[1]);
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
