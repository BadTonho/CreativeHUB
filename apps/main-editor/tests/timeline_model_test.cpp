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
    const std::string& display_name,
    std::int64_t frame_count = 120) {
    media::VideoMetadata metadata;
    metadata.source_path = source_path;
    metadata.display_name = display_name;
    metadata.duration_seconds = 4.0;
    metadata.frame_rate = 30.0;
    metadata.frame_count = frame_count;
    return metadata;
}

} // namespace

int main() {
    const auto directory = uniqueTestDirectory();

    try {
        std::filesystem::create_directories(directory / "media");
        const auto first_source = directory / "media" / "first.mkv";
        const auto second_source = directory / "media" / "second.mkv";
        std::ofstream(first_source, std::ios::binary).close();
        std::ofstream(second_source, std::ios::binary).close();

        const auto non_canonical_first =
            directory / "media" / ".." / "media" / "first.mkv";
        timeline::TimelineModel model;
        require(!model.hasClip(), "A new timeline should be empty.");
        require(model.clipCount() == 0, "A new timeline should have no clips.");
        require(model.totalDurationFrames() == 0,
                "An empty timeline should have zero duration.");

        const auto first_metadata = makeMetadata(
            non_canonical_first,
            "first.mkv");
        require(model.addClip(first_metadata) == timeline::AddClipResult::Added,
                "The first clip was not added.");
        require(model.clipCount() == 1, "The first clip count was incorrect.");

        const auto& first_clip = model.clips().front();
        require(first_clip.timeline_start_frame == 0,
                "The first clip did not start at frame zero.");
        require(first_clip.timeline_duration_frames == 120,
                "The first clip duration was incorrect.");
        require(first_clip.source_path == std::filesystem::weakly_canonical(first_source),
                "The first source path was not canonicalized.");
        require(first_clip.display_name == "first.mkv",
                "The first clip name was not preserved.");
        require(first_clip.duration_seconds == first_metadata.duration_seconds,
                "The first clip duration metadata was not preserved.");
        require(first_clip.frame_rate == first_metadata.frame_rate,
                "The first clip frame rate was not preserved.");
        require(first_clip.frame_count == first_metadata.frame_count,
                "The first clip frame count was not preserved.");

        const auto second_metadata = makeMetadata(second_source, "second.mkv", 60);
        require(model.addClip(second_metadata) == timeline::AddClipResult::Added,
                "The second clip was not added.");
        require(model.clips().size() == 2,
                "The timeline did not retain both clips.");
        require(model.clips()[1].timeline_start_frame == 120,
                "The second clip was not appended after the first.");
        require(model.clips()[1].timeline_duration_frames == 60,
                "The second clip duration was incorrect.");
        require(model.totalDurationFrames() == 180,
                "The total timeline duration was incorrect.");

        require(model.addClip(first_metadata) == timeline::AddClipResult::Added,
                "The same source could not be added a second time.");
        require(model.clips().size() == 3,
                "The repeated source did not create an independent clip.");
        require(model.clips()[2].timeline_start_frame == 180,
                "The repeated source was not appended at the end.");
        require(model.firstClipIndexForSource(first_source) == 0,
                "The first source lookup did not return the first occurrence.");

        media::VideoMetadata fallback_metadata;
        fallback_metadata.source_path = directory / "media" / "fallback.mkv";
        fallback_metadata.display_name = "fallback.mkv";
        fallback_metadata.duration_seconds = 2.5;
        fallback_metadata.frame_rate = 24.0;
        require(model.addClip(fallback_metadata) == timeline::AddClipResult::Added,
                "Duration and frame rate fallback metadata was rejected.");
        require(model.clips().back().timeline_duration_frames == 60,
                "Duration and frame rate fallback was calculated incorrectly.");

        const auto total_before_moves = model.totalDurationFrames();
        require(model.moveClip(0, 3) == timeline::MoveClipResult::Moved,
                "Moving the first clip to the end failed.");
        require(model.clips()[0].source_path ==
                    std::filesystem::weakly_canonical(second_source),
                "The first-to-last move produced the wrong first source.");
        require(model.clips()[0].timeline_start_frame == 0 &&
                    model.clips()[1].timeline_start_frame == 60 &&
                    model.clips()[2].timeline_start_frame == 180 &&
                    model.clips()[3].timeline_start_frame == 240,
                "The first-to-last move did not recalculate starts.");
        require(model.totalDurationFrames() == total_before_moves,
                "Moving a clip changed the total duration.");
        require(model.clips()[1].display_name == "first.mkv" &&
                    model.clips()[1].frame_count == first_metadata.frame_count,
                "Moving a clip did not preserve its metadata.");

        require(model.moveClip(3, 0) == timeline::MoveClipResult::Moved,
                "Moving the last clip to the beginning failed.");
        require(model.clips()[0].source_path ==
                    std::filesystem::weakly_canonical(first_source),
                "The last-to-first move produced the wrong first source.");
        require(model.clips()[0].timeline_start_frame == 0 &&
                    model.clips()[1].timeline_start_frame == 120 &&
                    model.clips()[2].timeline_start_frame == 180 &&
                    model.clips()[3].timeline_start_frame == 300,
                "The last-to-first move did not recalculate starts.");

        require(model.moveClip(1, 2) == timeline::MoveClipResult::Moved,
                "Moving an intermediate clip failed.");
        require(model.clips()[0].timeline_start_frame == 0 &&
                    model.clips()[1].timeline_start_frame == 120 &&
                    model.clips()[2].timeline_start_frame == 240 &&
                    model.clips()[3].timeline_start_frame == 300,
                "The intermediate move did not recalculate starts.");
        require(model.clips()[0].source_path == model.clips()[1].source_path,
                "Repeated sources were not preserved as independent occurrences.");
        require(model.firstClipIndexForSource(first_source) == 0,
                "Source lookup did not preserve the first occurrence after moves.");

        const auto starts_before_invalid_move = model.clips()[1].timeline_start_frame;
        require(model.moveClip(1, 1) == timeline::MoveClipResult::NoChange,
                "A no-op move was not reported as such.");
        require(model.moveClip(99, 0) == timeline::MoveClipResult::InvalidIndex,
                "An invalid source index was accepted.");
        require(model.moveClip(0, 99) == timeline::MoveClipResult::InvalidIndex,
                "An invalid destination index was accepted.");
        require(model.clips()[1].timeline_start_frame == starts_before_invalid_move,
                "An invalid move changed the timeline.");

        media::VideoMetadata invalid_metadata;
        invalid_metadata.source_path = directory / "media" / "invalid.mkv";
        invalid_metadata.display_name = "invalid.mkv";
        require(model.addClip(invalid_metadata) ==
                    timeline::AddClipResult::InvalidTimingMetadata,
                "Metadata without timing information was accepted.");
        require(model.clips().size() == 4,
                "Invalid metadata changed the timeline.");

        model.clear();
        require(!model.hasClip(), "The timeline was not cleared.");
        require(model.clipCount() == 0, "The cleared timeline still has clips.");
        require(model.clips().empty(), "The cleared clip collection was not empty.");
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
