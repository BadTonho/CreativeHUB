#include "media/video_metadata.h"
#include "timeline/timeline_model.h"

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
        ("creative-suite-timeline-test-" + std::to_string(stamp));
}

media::VideoMetadata makeMetadata(
    const std::filesystem::path& source_path,
    const std::string& display_name) {
    media::VideoMetadata metadata;
    metadata.source_path = source_path;
    metadata.display_name = display_name;
    metadata.duration_seconds = 4.0;
    metadata.frame_rate = 30.0;
    metadata.frame_count = 120;
    return metadata;
}

} // namespace

int main() {
    const auto directory = uniqueTestDirectory();

    try {
        std::filesystem::create_directories(directory / "media");
        const auto source_path = directory / "media" / "reference.mkv";
        std::ofstream(source_path, std::ios::binary).close();
        const auto non_canonical_path = directory / "media" / ".." / "media" / "reference.mkv";

        timeline::TimelineModel model;
        require(!model.hasClip(), "A new timeline should be empty.");
        require(model.clip() == nullptr, "An empty timeline should have no clip.");

        const auto metadata = makeMetadata(non_canonical_path, "reference.mkv");
        require(model.addClip(metadata) == timeline::AddClipResult::Added,
                "The first clip was not added.");
        require(model.hasClip(), "The timeline did not report its added clip.");

        const auto* clip = model.clip();
        require(clip != nullptr, "The added clip was not accessible.");
        require(clip->source_path == std::filesystem::weakly_canonical(source_path),
                "The clip source path was not canonicalized.");
        require(clip->display_name == "reference.mkv", "The clip name was not preserved.");
        require(clip->duration_seconds == metadata.duration_seconds,
                "The clip duration was not preserved.");
        require(clip->frame_rate == metadata.frame_rate,
                "The clip frame rate was not preserved.");
        require(clip->frame_count == metadata.frame_count,
                "The clip frame count was not preserved.");

        require(model.addClip(makeMetadata(source_path, "duplicate.mkv")) ==
                    timeline::AddClipResult::AlreadyPresent,
                "A duplicate clip was accepted.");
        require(model.addClip(makeMetadata(directory / "other.mkv", "other.mkv")) ==
                    timeline::AddClipResult::Occupied,
                "A second clip was accepted while the timeline was occupied.");

        model.clear();
        require(!model.hasClip(), "The timeline was not cleared.");
        require(model.clip() == nullptr, "The cleared timeline still exposed a clip.");
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
