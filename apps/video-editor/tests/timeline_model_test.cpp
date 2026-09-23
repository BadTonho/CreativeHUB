#include "media/video_metadata.h"
#include "timeline/timeline_history.h"
#include "timeline/timeline_model.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
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
        require(first_clip.source_start_frame == 0,
                "The first clip did not start at source frame zero.");
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

        media::VideoMetadata image_metadata;
        image_metadata.kind = media::MediaKind::Image;
        image_metadata.source_path = directory / "media" / "still.png";
        image_metadata.display_name = "still.png";
        image_metadata.duration_seconds = 5.0;
        image_metadata.frame_rate = 30.0;
        image_metadata.frame_count = 150;
        timeline::TimelineModel image_model;
        require(image_model.addClip(image_metadata) == timeline::AddClipResult::Added,
                "A still image was not accepted as a timeline clip.");
        require(image_model.clips().front().kind == timeline::ClipKind::Image &&
                    image_model.clips().front().timeline_duration_frames == 150,
                "The still image did not become a 150-frame image clip.");
        require(image_model.addClip(0, first_metadata, 30) ==
                    timeline::AddClipResult::Overlap,
                "A video was allowed to overlap a still image on the same track.");

        timeline::TimelineModel audio_model;
        require(audio_model.addClip(first_metadata) == timeline::AddClipResult::Added,
                "The audio parameter test source was not added.");
        require(audio_model.tracks()[0].audio_gain == 1.0 &&
                    !audio_model.tracks()[0].audio_muted &&
                    audio_model.clips()[0].audio_gain == 1.0 &&
                    !audio_model.clips()[0].audio_muted,
                "Audio parameters did not start with their defaults.");
        require(audio_model.setClipAudio(0, 0, 0.5, true) ==
                    timeline::AudioParameterResult::Changed,
                "Clip audio parameters could not be changed.");
        require(audio_model.setTrackAudio(0, 1.5, false) ==
                    timeline::AudioParameterResult::Changed,
                "Track audio parameters could not be changed.");
        require(audio_model.clips()[0].audio_gain == 0.5 &&
                    audio_model.clips()[0].audio_muted &&
                    audio_model.tracks()[0].audio_gain == 1.5,
                "Audio parameter changes were not preserved.");
        require(audio_model.setClipAudio(0, 0, 2.1, false) ==
                    timeline::AudioParameterResult::InvalidValue &&
                    audio_model.setTrackAudio(0, 99.0, false) ==
                    timeline::AudioParameterResult::InvalidValue,
                "Invalid audio gains were accepted.");

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

        timeline::TimelineModel split_model;
        require(split_model.addClip(first_metadata) == timeline::AddClipResult::Added,
                "The split test source was not added.");
        require(split_model.addClip(second_metadata) == timeline::AddClipResult::Added,
                "The split test second source was not added.");
        const auto split_total_before = split_model.totalDurationFrames();
        require(split_model.splitClip(0, 30) == timeline::SplitClipResult::Split,
                "The first clip was not split at an intermediate frame.");
        require(split_model.clipCount() == 3,
                "Splitting did not create a second segment.");
        require(split_model.clips()[0].source_start_frame == 0 &&
                    split_model.clips()[0].timeline_duration_frames == 30 &&
                    split_model.clips()[1].source_start_frame == 30 &&
                    split_model.clips()[1].timeline_duration_frames == 90,
                "The split source offsets or durations were incorrect.");
        require(split_model.clips()[0].timeline_start_frame == 0 &&
                    split_model.clips()[1].timeline_start_frame == 30 &&
                    split_model.clips()[2].timeline_start_frame == 120,
                "Splitting did not recalculate timeline starts.");
        require(split_model.totalDurationFrames() == split_total_before,
                "Splitting changed the total timeline duration.");
        require(split_model.clips()[1].source_path ==
                    std::filesystem::weakly_canonical(first_source) &&
                    split_model.clips()[1].display_name == first_metadata.display_name &&
                    split_model.clips()[1].frame_rate == first_metadata.frame_rate &&
                    split_model.clips()[1].frame_count == first_metadata.frame_count,
                "Splitting did not preserve source metadata.");
        require(split_model.trimClip(1, 50, 40) == timeline::TrimClipResult::Trimmed,
                "Trimming a previously split segment failed.");
        require(split_model.clips()[1].source_start_frame == 50 &&
                    split_model.clips()[1].timeline_duration_frames == 40 &&
                    split_model.clips()[1].timeline_start_frame == 30 &&
                    split_model.clips()[2].timeline_start_frame == 120,
                "Trimming a split segment produced incorrect offsets.");
        require(split_model.splitClip(2, 20) == timeline::SplitClipResult::Split,
                "The second source occurrence was not split.");
        require(split_model.clips()[2].source_start_frame == 0 &&
                    split_model.clips()[3].source_start_frame == 20,
                "Repeated source occurrences did not keep independent offsets.");
        require(split_model.splitClip(0, 0) == timeline::SplitClipResult::InvalidBoundary,
                "A split at the first frame was accepted.");
        require(split_model.splitClip(0, 30) == timeline::SplitClipResult::InvalidBoundary,
                "A split at the last frame was accepted.");
        require(split_model.splitClip(99, 1) == timeline::SplitClipResult::InvalidIndex,
                "An invalid split index was accepted.");

        timeline::TimelineModel trim_model;
        require(trim_model.addClip(first_metadata) == timeline::AddClipResult::Added,
                "The trim test first source was not added.");
        require(trim_model.addClip(second_metadata) == timeline::AddClipResult::Added,
                "The trim test second source was not added.");
        require(trim_model.addClip(first_metadata) == timeline::AddClipResult::Added,
                "The trim test repeated source was not added.");
        require(trim_model.trimClip(0, 20, 80) == timeline::TrimClipResult::Trimmed,
                "Trimming the start of a clip failed.");
        require(trim_model.clips()[0].source_start_frame == 20 &&
                    trim_model.clips()[0].timeline_duration_frames == 80 &&
                    trim_model.clips()[0].timeline_start_frame == 0 &&
                    trim_model.clips()[1].timeline_start_frame == 120 &&
                    trim_model.clips()[2].timeline_start_frame == 180,
                "Trimming the start did not preserve absolute placement.");
        require(trim_model.trimClip(1, 0, 30) == timeline::TrimClipResult::Trimmed,
                "Trimming the end of a clip failed.");
        require(trim_model.clips()[1].timeline_duration_frames == 30 &&
                    trim_model.clips()[2].timeline_start_frame == 180,
                "Trimming the end changed following clip placement.");
        require(trim_model.totalDurationFrames() == 300,
                "Trimming did not update the total timeline duration.");
        require(trim_model.clips()[0].display_name == first_metadata.display_name &&
                    trim_model.clips()[0].frame_rate == first_metadata.frame_rate &&
                    trim_model.clips()[0].frame_count == first_metadata.frame_count,
                "Trimming did not preserve source metadata.");
        require(trim_model.trimClip(0, 10, 50) == timeline::TrimClipResult::InvalidRange,
                "A trim before the current source start was accepted.");
        require(trim_model.trimClip(2, 1, 120) == timeline::TrimClipResult::InvalidRange,
                "A trim beyond the current segment was accepted.");
        require(trim_model.trimClip(2, 0, 0) == timeline::TrimClipResult::InvalidRange,
                "A zero-length trim was accepted.");
        require(trim_model.trimClip(2, 0, 121) == timeline::TrimClipResult::InvalidRange,
                "A trim beyond the current segment was accepted.");
        require(trim_model.trimClip(99, 0, 1) == timeline::TrimClipResult::InvalidIndex,
                "An invalid trim index was accepted.");

        timeline::TimelineModel edge_trim_model;
        auto edge_metadata = makeMetadata(first_source, "edge.mkv", 120);
        require(edge_trim_model.addClip(edge_metadata) == timeline::AddClipResult::Added &&
                    edge_trim_model.splitClip(0, 0, 40) ==
                        timeline::SplitClipResult::Split,
                "The edge trim split setup failed.");
        require(edge_trim_model.trimClipEdge(
                    0, 1, timeline::ClipEdge::Left, 30) ==
                    timeline::TrimClipResult::Trimmed,
                "A split clip could not extend backward by rolling its cut.");
        require(edge_trim_model.clips()[0].timeline_duration_frames == 30 &&
                    edge_trim_model.clips()[1].timeline_start_frame == 30 &&
                    edge_trim_model.clips()[1].source_start_frame == 30 &&
                    edge_trim_model.clips()[1].timeline_duration_frames == 90,
                "Rolling a cut backward did not resize both source ranges.");
        require(edge_trim_model.trimClipEdge(
                    0, 0, timeline::ClipEdge::Right, 50) ==
                    timeline::TrimClipResult::Trimmed,
                "A split clip could not extend forward by rolling its cut.");
        require(edge_trim_model.clips()[0].timeline_duration_frames == 50 &&
                    edge_trim_model.clips()[1].timeline_start_frame == 50 &&
                    edge_trim_model.clips()[1].source_start_frame == 50 &&
                    edge_trim_model.clips()[1].timeline_duration_frames == 70,
                "Rolling a cut forward did not resize both source ranges.");
        require(edge_trim_model.trimClipEdge(
                    0, 0, timeline::ClipEdge::Right, 500) ==
                    timeline::TrimClipResult::Trimmed &&
                    edge_trim_model.clips()[0].timeline_duration_frames == 119 &&
                    edge_trim_model.clips()[1].timeline_start_frame == 119 &&
                    edge_trim_model.clips()[1].timeline_duration_frames == 1,
                "A shared cut did not stop before reducing its neighbor to zero frames.");
        require(edge_trim_model.trimClipEdge(
                    0, 1, timeline::ClipEdge::Left, -20) ==
                    timeline::TrimClipResult::Trimmed &&
                    edge_trim_model.clips()[0].timeline_duration_frames == 1 &&
                    edge_trim_model.clips()[1].timeline_start_frame == 1,
                "A shared cut moved before the minimum one-frame segment boundary.");

        timeline::TimelineModel individual_right_model;
        require(individual_right_model.addClip(makeMetadata(
                    first_source, "individual-right.mkv", 120)) ==
                    timeline::AddClipResult::Added &&
                    individual_right_model.splitClip(0, 0, 40) ==
                        timeline::SplitClipResult::Split,
                "The individual right-edge setup failed.");
        const auto individual_right_preview = timeline::previewClipEdgeEdit(
            individual_right_model.tracks(),
            timeline::ClipLocation{0, 0},
            timeline::ClipEdge::Right,
            60,
            timeline::ClipEdgeEditMode::Individual);
        require(individual_right_preview.has_value() &&
                    !individual_right_preview->neighbor_location.has_value() &&
                    individual_right_preview->clip.timeline_duration_frames == 60 &&
                    individual_right_model.trimClipEdge(
                        0,
                        0,
                        timeline::ClipEdge::Right,
                        60,
                        timeline::ClipEdgeEditMode::Individual) ==
                        timeline::TrimClipResult::Trimmed,
                "An individual right-edge preview or commit changed the wrong clips.");
        require(individual_right_model.clips()[0].timeline_duration_frames == 60 &&
                    individual_right_model.clips()[1].timeline_start_frame == 40 &&
                    individual_right_model.clips()[1].source_start_frame == 40 &&
                    individual_right_model.clips()[1].timeline_duration_frames == 80 &&
                    individual_right_model.topClipAt(50) ==
                        timeline::ClipLocation{0, 1},
                "Extending one clip did not preserve the neighbor or show the later clip above it.");
        require(individual_right_model.trimClipEdge(
                    0,
                    0,
                    timeline::ClipEdge::Right,
                    30,
                    timeline::ClipEdgeEditMode::Individual) ==
                    timeline::TrimClipResult::Trimmed &&
                    individual_right_model.clips()[0].timeline_duration_frames == 30 &&
                    individual_right_model.clips()[1].timeline_start_frame == 40 &&
                    !individual_right_model.clipAt(0, 35).has_value(),
                "Shortening one clip did not leave a gap before the unchanged neighbor.");
        require(individual_right_model.trimClipEdge(
                    0,
                    0,
                    timeline::ClipEdge::Right,
                    1,
                    timeline::ClipEdgeEditMode::Individual) ==
                    timeline::TrimClipResult::Trimmed &&
                    individual_right_model.clips()[0].timeline_duration_frames == 1 &&
                    individual_right_model.clips()[1].timeline_start_frame == 40 &&
                    individual_right_model.clips()[1].timeline_duration_frames == 80,
                "An individual edge trim did not preserve the one-frame minimum.");

        timeline::TimelineModel individual_left_model;
        require(individual_left_model.addClip(makeMetadata(
                    first_source, "individual-left.mkv", 120)) ==
                    timeline::AddClipResult::Added &&
                    individual_left_model.splitClip(0, 0, 40) ==
                        timeline::SplitClipResult::Split &&
                    individual_left_model.trimClipEdge(
                        0,
                        1,
                        timeline::ClipEdge::Left,
                        30,
                        timeline::ClipEdgeEditMode::Individual) ==
                        timeline::TrimClipResult::Trimmed,
                "An individual left edge could not extend over its neighbor.");
        require(individual_left_model.clips()[0].timeline_start_frame == 0 &&
                    individual_left_model.clips()[0].timeline_duration_frames == 40 &&
                    individual_left_model.clips()[1].timeline_start_frame == 30 &&
                    individual_left_model.clips()[1].source_start_frame == 30 &&
                    individual_left_model.clips()[1].timeline_duration_frames == 90 &&
                    individual_left_model.topClipAt(35) ==
                        timeline::ClipLocation{0, 1},
                "Extending one clip's left edge changed its neighbor or visibility order.");

        timeline::TimelineModel individual_left_shrink_model;
        require(individual_left_shrink_model.addClip(makeMetadata(
                    first_source, "individual-left-shrink.mkv", 120)) ==
                    timeline::AddClipResult::Added &&
                    individual_left_shrink_model.splitClip(0, 0, 40) ==
                        timeline::SplitClipResult::Split &&
                    individual_left_shrink_model.trimClipEdge(
                        0,
                        1,
                        timeline::ClipEdge::Left,
                        50,
                        timeline::ClipEdgeEditMode::Individual) ==
                        timeline::TrimClipResult::Trimmed &&
                    individual_left_shrink_model.clips()[0].timeline_start_frame == 0 &&
                    individual_left_shrink_model.clips()[0].timeline_duration_frames == 40 &&
                    individual_left_shrink_model.clips()[1].timeline_start_frame == 50 &&
                    individual_left_shrink_model.clips()[1].timeline_duration_frames == 70 &&
                    !individual_left_shrink_model.clipAt(0, 45).has_value(),
                "Shortening one clip's left edge did not preserve the neighbor and leave a gap.");

        timeline::TimelineModel resumed_underlap_model;
        auto long_overlap_metadata = makeMetadata(
            first_source, "long-underlap.mkv", 200);
        auto short_overlap_metadata = makeMetadata(
            first_source, "short-overlap.mkv", 20);
        require(resumed_underlap_model.addClip(long_overlap_metadata) ==
                    timeline::AddClipResult::Added &&
                    resumed_underlap_model.trimClip(0, 0, 100) ==
                        timeline::TrimClipResult::Trimmed &&
                    resumed_underlap_model.addClip(
                        0, short_overlap_metadata, 100) ==
                        timeline::AddClipResult::Added &&
                    resumed_underlap_model.trimClipEdge(
                        0,
                        0,
                        timeline::ClipEdge::Right,
                        140,
                        timeline::ClipEdgeEditMode::Individual) ==
                        timeline::TrimClipResult::Trimmed &&
                    resumed_underlap_model.topClipAt(110) ==
                        timeline::ClipLocation{0, 1} &&
                    resumed_underlap_model.topClipAt(125) ==
                        timeline::ClipLocation{0, 0},
                "Visibility did not return to the underlying clip after the overlap ended.");

        timeline::TimelineModel individual_source_bound_model;
        require(individual_source_bound_model.addClip(makeMetadata(
                    first_source, "individual-source-bound.mkv", 60)) ==
                    timeline::AddClipResult::Added &&
                    individual_source_bound_model.splitClip(0, 0, 40) ==
                        timeline::SplitClipResult::Split &&
                    individual_source_bound_model.trimClipEdge(
                        0,
                        0,
                        timeline::ClipEdge::Right,
                        500,
                        timeline::ClipEdgeEditMode::Individual) ==
                        timeline::TrimClipResult::Trimmed &&
                    individual_source_bound_model.clips()[0].timeline_duration_frames == 60 &&
                    individual_source_bound_model.clips()[1].timeline_start_frame == 40 &&
                    individual_source_bound_model.clips()[1].timeline_duration_frames == 20,
                "An individual edge exceeded the video source limit or changed its neighbor.");

        timeline::TimelineModel mixed_edge_model;
        require(mixed_edge_model.addClip(makeMetadata(
                    first_source, "mixed-edge.mkv", 200)) ==
                    timeline::AddClipResult::Added &&
                    mixed_edge_model.trimClip(0, 0, 100) ==
                        timeline::TrimClipResult::Trimmed &&
                    mixed_edge_model.addTextClip(0, 100, 50) ==
                        timeline::AddClipResult::Added &&
                    mixed_edge_model.trimClipEdge(
                        0, 0, timeline::ClipEdge::Right, 110) ==
                        timeline::TrimClipResult::Trimmed &&
                    mixed_edge_model.clips()[0].timeline_duration_frames == 110 &&
                    mixed_edge_model.clips()[1].timeline_start_frame == 110 &&
                    mixed_edge_model.clips()[1].timeline_duration_frames == 40,
                "A shared boundary between different clip kinds did not roll.");

        timeline::TimelineModel gap_edge_model;
        auto long_metadata = makeMetadata(first_source, "long.mkv", 200);
        long_metadata.duration_seconds = 200.0 / 30.0;
        require(gap_edge_model.addClip(long_metadata) == timeline::AddClipResult::Added &&
                    gap_edge_model.trimClip(0, 50, 80) ==
                        timeline::TrimClipResult::Trimmed &&
                    gap_edge_model.moveClip(
                        timeline::ClipLocation{0, 0},
                        timeline::ClipLocation{0, 0},
                        100) == timeline::MoveClipResult::Moved,
                "The edge trim gap setup failed.");
        require(gap_edge_model.trimClipEdge(
                    0, 0, timeline::ClipEdge::Left, 90) ==
                    timeline::TrimClipResult::Trimmed &&
                    gap_edge_model.clips()[0].timeline_start_frame == 90 &&
                    gap_edge_model.clips()[0].source_start_frame == 40 &&
                    gap_edge_model.clips()[0].timeline_duration_frames == 90,
                "Extending a left edge into a gap did not move the clip start.");
        require(gap_edge_model.trimClipEdge(
                    0, 0, timeline::ClipEdge::Right, 190) ==
                    timeline::TrimClipResult::Trimmed &&
                    gap_edge_model.clips()[0].timeline_duration_frames == 100,
                "Extending a right edge into a gap did not increase its duration.");
        require(gap_edge_model.trimClipEdge(
                    0, 0, timeline::ClipEdge::Left, -100) ==
                    timeline::TrimClipResult::Trimmed &&
                    gap_edge_model.clips()[0].timeline_start_frame == 50 &&
                    gap_edge_model.clips()[0].source_start_frame == 0,
                "A video left edge extended before source frame zero.");
        require(gap_edge_model.trimClipEdge(
                    0, 0, timeline::ClipEdge::Right, 1000) ==
                    timeline::TrimClipResult::Trimmed &&
                    gap_edge_model.clips()[0].timeline_start_frame == 50 &&
                    gap_edge_model.clips()[0].timeline_duration_frames == 200,
                "A video right edge extended beyond the source frame count.");

        timeline::TimelineModel fallback_edge_model;
        auto fallback_edge_metadata = makeMetadata(first_source, "fallback.mkv", 20);
        fallback_edge_metadata.frame_count.reset();
        fallback_edge_metadata.duration_seconds = 2.0;
        fallback_edge_metadata.frame_rate = 10.0;
        require(fallback_edge_model.addClip(fallback_edge_metadata) ==
                    timeline::AddClipResult::Added &&
                    fallback_edge_model.trimClip(0, 0, 10) ==
                        timeline::TrimClipResult::Trimmed &&
                    fallback_edge_model.trimClipEdge(
                        0, 0, timeline::ClipEdge::Right, 100) ==
                        timeline::TrimClipResult::Trimmed &&
                    fallback_edge_model.clips()[0].timeline_duration_frames == 20,
                "Video edge extension ignored the duration/FPS source bound fallback.");

        timeline::TimelineModel image_edge_model;
        auto expandable_image = image_metadata;
        require(image_edge_model.addClip(expandable_image) ==
                    timeline::AddClipResult::Added &&
                    image_edge_model.trimClip(0, 0, 5) ==
                        timeline::TrimClipResult::Trimmed &&
                    image_edge_model.trimClipEdge(
                        0, 0, timeline::ClipEdge::Right, 300) ==
                        timeline::TrimClipResult::Trimmed &&
                    image_edge_model.clips()[0].timeline_duration_frames == 300,
                "A still image could not extend while holding its static frame.");

        timeline::TimelineModel text_edge_model;
        require(text_edge_model.addTextClip(0, 0, 10) == timeline::AddClipResult::Added &&
                    text_edge_model.trimClipEdge(
                        0, 0, timeline::ClipEdge::Right, 500) ==
                        timeline::TrimClipResult::Trimmed &&
                    text_edge_model.clips()[0].timeline_duration_frames == 500,
                "A text clip could not extend beyond its original duration.");

        timeline::TimelineModel zero_bound_edge_model;
        require(zero_bound_edge_model.addTextClip(0, 10, 10) ==
                    timeline::AddClipResult::Added &&
                    zero_bound_edge_model.trimClipEdge(
                        0, 0, timeline::ClipEdge::Left, -50) ==
                        timeline::TrimClipResult::Trimmed &&
                    zero_bound_edge_model.clips()[0].timeline_start_frame == 0 &&
                    zero_bound_edge_model.clips()[0].timeline_duration_frames == 20,
                "A clip edge moved its timeline start before frame zero.");

        timeline::TimelineModel keyframe_edge_model;
        require(keyframe_edge_model.addTextClip(0, 10, 20) ==
                    timeline::AddClipResult::Added &&
                    keyframe_edge_model.setClipKeyframe(
                        0, 0, timeline::TransformProperty::PositionX, 5, 0.2) ==
                        timeline::TransformParameterResult::Changed &&
                    keyframe_edge_model.setClipKeyframe(
                        0, 0, timeline::TransformProperty::PositionX, 15, 0.8) ==
                        timeline::TransformParameterResult::Changed &&
                    keyframe_edge_model.trimClipEdge(
                        0, 0, timeline::ClipEdge::Left, 5) ==
                        timeline::TrimClipResult::Trimmed,
                "A keyframed clip could not extend before its original start.");
        const auto& extended_keyframes = keyframe_edge_model.clips()[0];
        require(extended_keyframes.timeline_start_frame == 5 &&
                    extended_keyframes.timeline_duration_frames == 25 &&
                    timeline::evaluateTransform(
                        extended_keyframes.transform,
                        extended_keyframes.keyframes,
                        4).position_x == 0.2 &&
                    timeline::evaluateTransform(
                        extended_keyframes.transform,
                        extended_keyframes.keyframes,
                        10).position_x == 0.2 &&
                    timeline::evaluateTransform(
                        extended_keyframes.transform,
                        extended_keyframes.keyframes,
                        20).position_x == 0.8,
                "Extending a clip did not preserve keyframes and hold its boundary transform.");
        require(keyframe_edge_model.trimClipEdge(
                    0, 0, timeline::ClipEdge::Right, 40) ==
                    timeline::TrimClipResult::Trimmed &&
                    timeline::evaluateTransform(
                        keyframe_edge_model.clips()[0].transform,
                        keyframe_edge_model.clips()[0].keyframes,
                        30).position_x == 0.8,
                "Extending a right edge did not hold the last evaluated transform.");
        const auto unchanged_edge_snapshot = keyframe_edge_model.snapshot();
        require(keyframe_edge_model.trimClipEdge(
                    0, 0, timeline::ClipEdge::Right, 40) ==
                    timeline::TrimClipResult::NoChange &&
                    keyframe_edge_model.snapshot() == unchanged_edge_snapshot,
                "A no-movement edge gesture changed clip state or keyframes.");

        timeline::TimelineModel edge_history_model;
        require(edge_history_model.addClip(first_metadata) ==
                    timeline::AddClipResult::Added &&
                    edge_history_model.trimClip(0, 0, 60) ==
                        timeline::TrimClipResult::Trimmed,
                "The edge history setup failed.");
        timeline::EditState before_edge_edit;
        before_edge_edit.timeline = edge_history_model.snapshot();
        timeline::TimelineHistory edge_history;
        edge_history.recordBeforeEdit(before_edge_edit);
        require(edge_history_model.trimClipEdge(
                    0, 0, timeline::ClipEdge::Right, 90) ==
                    timeline::TrimClipResult::Trimmed,
                "The edge history edit failed.");
        timeline::EditState after_edge_edit;
        after_edge_edit.timeline = edge_history_model.snapshot();
        const auto undone_edge = edge_history.undo(after_edge_edit);
        require(undone_edge.has_value(), "Undo after edge extension was unavailable.");
        edge_history_model.restore(undone_edge->timeline);
        require(edge_history_model.clips()[0].timeline_duration_frames == 60,
                "Undo did not restore the original edge-trimmed duration.");
        const auto redone_edge = edge_history.redo(*undone_edge);
        require(redone_edge.has_value(), "Redo after edge extension was unavailable.");
        edge_history_model.restore(redone_edge->timeline);
        require(edge_history_model.clips()[0].timeline_duration_frames == 90,
                "Redo did not restore the extended clip duration.");

        timeline::TimelineModel individual_history_model;
        require(individual_history_model.addClip(makeMetadata(
                    first_source, "individual-history.mkv", 120)) ==
                    timeline::AddClipResult::Added &&
                    individual_history_model.splitClip(0, 0, 40) ==
                        timeline::SplitClipResult::Split,
                "The individual edge history setup failed.");
        timeline::EditState before_individual_edit;
        before_individual_edit.timeline = individual_history_model.snapshot();
        timeline::TimelineHistory individual_history;
        individual_history.recordBeforeEdit(before_individual_edit);
        require(individual_history_model.trimClipEdge(
                    0,
                    0,
                    timeline::ClipEdge::Right,
                    60,
                    timeline::ClipEdgeEditMode::Individual) ==
                    timeline::TrimClipResult::Trimmed,
                "The individual edge history edit failed.");
        timeline::EditState after_individual_edit;
        after_individual_edit.timeline = individual_history_model.snapshot();
        const auto undone_individual = individual_history.undo(after_individual_edit);
        require(undone_individual.has_value(),
                "Undo after an individual edge edit was unavailable.");
        individual_history_model.restore(undone_individual->timeline);
        require(individual_history_model.clips()[0].timeline_duration_frames == 40 &&
                    individual_history_model.clips()[1].timeline_start_frame == 40 &&
                    individual_history_model.clips()[1].timeline_duration_frames == 80,
                "Undo did not restore both clips after an individual edge edit.");
        const auto redone_individual = individual_history.redo(*undone_individual);
        require(redone_individual.has_value(),
                "Redo after an individual edge edit was unavailable.");
        individual_history_model.restore(redone_individual->timeline);
        require(individual_history_model.clips()[0].timeline_duration_frames == 60 &&
                    individual_history_model.clips()[1].timeline_start_frame == 40 &&
                    individual_history_model.clips()[1].timeline_duration_frames == 80,
                "Redo did not restore the individual overlap without changing the neighbor.");

        timeline::TimelineModel malformed_range_model;
        auto malformed_snapshot = malformed_range_model.snapshot();
        timeline::TimelineClip malformed_clip;
        malformed_clip.source_start_frame =
            std::numeric_limits<std::int64_t>::max() - 1;
        malformed_clip.timeline_duration_frames = 3;
        malformed_snapshot.tracks.front().clips.push_back(malformed_clip);
        malformed_range_model.restore(std::move(malformed_snapshot));
        require(malformed_range_model.trimClip(
                    0,
                    std::numeric_limits<std::int64_t>::max() - 1,
                    1) == timeline::TrimClipResult::InvalidRange,
                "Trimming a clip with an overflowing existing source range was not rejected.");

        timeline::TimelineModel overflowing_move_model;
        require(overflowing_move_model.addClip(first_metadata) ==
                    timeline::AddClipResult::Added,
                "The overflow move test source was not added.");
        require(overflowing_move_model.moveClip(
                    timeline::ClipLocation{0, 0},
                    timeline::ClipLocation{0, 0},
                    std::numeric_limits<std::int64_t>::max() - 30) ==
                    timeline::MoveClipResult::InvalidPosition,
                "Moving a clip to an overflowing timeline position was accepted.");

        timeline::TimelineModel remove_model;
        require(remove_model.addClip(first_metadata) == timeline::AddClipResult::Added,
                "The remove test first source was not added.");
        require(remove_model.addClip(second_metadata) == timeline::AddClipResult::Added,
                "The remove test second source was not added.");
        require(remove_model.addClip(first_metadata) == timeline::AddClipResult::Added,
                "The remove test repeated source was not added.");
        require(remove_model.removeClip(1) == timeline::RemoveClipResult::Removed,
                "Removing an intermediate clip failed.");
        require(remove_model.clipCount() == 2 &&
                    remove_model.clips()[0].timeline_start_frame == 0 &&
                    remove_model.clips()[1].timeline_start_frame == 180 &&
                    remove_model.clips()[1].source_path ==
                        std::filesystem::weakly_canonical(first_source),
                "Removing an intermediate clip did not preserve absolute placement.");
        require(remove_model.totalDurationFrames() == 300,
                "Removing an intermediate clip did not update total duration.");
        require(remove_model.removeClip(1) == timeline::RemoveClipResult::Removed,
                "Removing the last clip failed.");
        require(remove_model.clipCount() == 1 &&
                    remove_model.clips().front().timeline_start_frame == 0,
                "Removing the last clip left invalid placement.");
        require(remove_model.removeClip(0) == timeline::RemoveClipResult::Removed,
                "Removing the first clip failed.");
        require(remove_model.clips().empty(),
                "Removing the first clip did not empty the model.");
        require(remove_model.removeClip(0) == timeline::RemoveClipResult::InvalidIndex,
                "Removing an invalid clip index was accepted.");

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

        timeline::TimelineModel multi_track_model;
        require(multi_track_model.addTrack("Video 2") ==
                    timeline::AddTrackResult::Added,
                "The second video track was not created.");
        require(multi_track_model.trackCount() == 2,
                "The multi-track model has the wrong track count.");
        require(multi_track_model.tracks()[0].name == "Video 2" &&
                    multi_track_model.tracks()[1].name == "Video 1",
                "New tracks were not inserted above the existing tracks.");
        require(multi_track_model.addClip(0, first_metadata, 10) ==
                    timeline::AddClipResult::Added,
                "A clip was not added at an absolute frame.");
        require(multi_track_model.addClip(0, second_metadata, 150) ==
                    timeline::AddClipResult::Added,
                "A clip was not added after a gap.");
        require(multi_track_model.addClip(0, first_metadata, 50) ==
                    timeline::AddClipResult::Overlap,
                "An overlapping clip was accepted on one track.");
        require(multi_track_model.addClip(1, first_metadata, 50) ==
                    timeline::AddClipResult::Added,
                "A cross-track overlap was rejected.");
        require(multi_track_model.topClipAt(55).has_value() &&
                    multi_track_model.topClipAt(55)->track_index == 0,
                "The top track priority was incorrect.");
        require(!multi_track_model.clipAt(0, 140).has_value(),
                "A timeline gap was not detected.");
        const auto moved_id = multi_track_model.tracks()[0].clips[0].clip_id;
        require(multi_track_model.moveClip(
                    timeline::ClipLocation{0, 0},
                    timeline::ClipLocation{1, 0},
                    200) == timeline::MoveClipResult::Moved,
                "Moving a clip between tracks failed.");
        require(multi_track_model.locateClip(moved_id).has_value() &&
                    multi_track_model.locateClip(moved_id)->track_index == 1,
                "The moved clip could not be located by its stable id.");
        require(multi_track_model.removeTrack(0) ==
                    timeline::TrackMutationResult::NotEmpty,
                "A non-empty track was removed.");
        require(multi_track_model.removeClip(0, 0) ==
                    timeline::RemoveClipResult::Removed,
                "The remaining clip was not removed.");
        require(multi_track_model.removeTrack(0) ==
                    timeline::TrackMutationResult::Changed,
                "An empty track could not be removed.");

        timeline::TimelineModel transition_model;
        require(transition_model.addClip(0, first_metadata, 0) ==
                    timeline::AddClipResult::Added &&
                    transition_model.addClip(0, second_metadata, 120) ==
                    timeline::AddClipResult::Added &&
                    transition_model.addTextClip(0, 180, 30, 30.0) ==
                    timeline::AddClipResult::Added,
                "The transition test clips could not be created.");
        require(transition_model.addTransition(
                    0, 0, 1, timeline::TransitionKind::CrossDissolve) ==
                    timeline::TransitionMutationResult::Added,
                "A cross dissolve could not be created at a clip junction.");
        require(transition_model.transitionBetween(0, 0, 1) != nullptr &&
                    transition_model.tracks()[0].transitions.front().duration_frames == 15,
                "The default transition duration was not preserved.");
        require(transition_model.addTransition(
                    0, 1, 2, timeline::TransitionKind::FadeToBlack, 10) ==
                    timeline::TransitionMutationResult::Added,
                "A fade to black could not connect video and text clips.");
        require(transition_model.updateTransition(
                    0, 0, 1, timeline::TransitionKind::FadeToBlack, 20) ==
                    timeline::TransitionMutationResult::Updated,
                "A transition could not be updated.");
        require(transition_model.tracks()[0].transitions.front().kind ==
                    timeline::TransitionKind::FadeToBlack &&
                    transition_model.tracks()[0].transitions.front().duration_frames == 20,
                "The updated transition settings were not preserved.");
        require(transition_model.addTransition(
                    0, 0, 2, timeline::TransitionKind::CrossDissolve, 1) ==
                    timeline::TransitionMutationResult::InvalidBoundary,
                "A transition between non-consecutive clips was accepted.");
        require(transition_model.addTransition(
                    9, 0, 1, timeline::TransitionKind::CrossDissolve, 1) ==
                    timeline::TransitionMutationResult::InvalidIndex,
                "A transition on an invalid track was accepted.");
        require(transition_model.addTransition(
                    0, 0, 1, timeline::TransitionKind::CrossDissolve, 0) ==
                    timeline::TransitionMutationResult::InvalidRange &&
                    transition_model.addTransition(
                        0, 0, 1, timeline::TransitionKind::CrossDissolve, 121) ==
                    timeline::TransitionMutationResult::InvalidRange,
                "An invalid transition duration was accepted.");
        require(transition_model.removeTransition(0, 0, 1) ==
                    timeline::TransitionMutationResult::Removed &&
                    transition_model.transitionBetween(0, 0, 1) == nullptr,
                "A transition could not be removed.");

        timeline::TimelineModel gap_transition_model;
        require(gap_transition_model.addClip(0, first_metadata, 0) ==
                    timeline::AddClipResult::Added &&
                    gap_transition_model.addClip(0, second_metadata, 150) ==
                    timeline::AddClipResult::Added,
                "The gap transition test clips could not be created.");
        require(gap_transition_model.addTransition(
                    0, 0, 1, timeline::TransitionKind::CrossDissolve) ==
                    timeline::TransitionMutationResult::InvalidBoundary,
                "A transition across a gap was accepted.");

        timeline::TimelineModel cleanup_transition_model;
        require(cleanup_transition_model.addClip(0, first_metadata, 0) ==
                    timeline::AddClipResult::Added &&
                    cleanup_transition_model.addClip(0, second_metadata, 120) ==
                    timeline::AddClipResult::Added,
                "The transition cleanup clips could not be created.");
        require(cleanup_transition_model.addTransition(
                    0, 0, 1, timeline::TransitionKind::CrossDissolve) ==
                    timeline::TransitionMutationResult::Added,
                "The transition cleanup setup failed.");
        const auto transition_snapshot = cleanup_transition_model.snapshot();
        require(transition_snapshot.tracks[0].transitions.size() == 1,
                "Transitions were not included in a timeline snapshot.");
        require(cleanup_transition_model.splitClip(0, 0, 30) ==
                    timeline::SplitClipResult::Split &&
                    cleanup_transition_model.tracks()[0].transitions.empty(),
                "Splitting a transition endpoint did not remove the invalid transition.");
        cleanup_transition_model.restore(transition_snapshot);
        require(cleanup_transition_model.tracks()[0].transitions.size() == 1,
                "Restoring a snapshot did not restore its transition.");
        require(cleanup_transition_model.trimClip(0, 0, 0, 20) ==
                    timeline::TrimClipResult::Trimmed &&
                    cleanup_transition_model.tracks()[0].transitions.empty(),
                "Trimming a transition endpoint did not remove the gap transition.");

        timeline::TimelineModel history_model;
        require(history_model.addClip(first_metadata) == timeline::AddClipResult::Added,
                "The history test first clip was not added.");
        require(history_model.addClip(second_metadata) == timeline::AddClipResult::Added,
                "The history test second clip was not added.");

        const auto make_history_state =
            [&history_model](std::optional<std::size_t> active,
                             const std::filesystem::path& selected_source,
                             std::int64_t frame) {
                timeline::EditState state;
                state.timeline = history_model.snapshot();
                state.active_clip_index = active;
                state.selected_source_path = selected_source;
                state.playhead_frame = frame;
                return state;
            };

        timeline::TimelineHistory history;
        const auto before_move = make_history_state(0, first_source, 12);
        history.recordBeforeEdit(before_move);
        require(history.canUndo() && !history.canRedo() &&
                    history.undoCount() == 1,
                "The history did not record the initial edit state.");
        require(history_model.moveClip(0, 1) == timeline::MoveClipResult::Moved,
                "The history move operation failed.");
        auto moved_state = make_history_state(1, second_source, 5);
        const auto undone_move = history.undo(moved_state);
        require(undone_move.has_value() && history.canRedo(),
                "Undo did not return the previous timeline state.");
        history_model.restore(undone_move->timeline);
        require(history_model.clips()[0].source_path ==
                    std::filesystem::weakly_canonical(first_source) &&
                    history_model.clips()[1].source_path ==
                        std::filesystem::weakly_canonical(second_source) &&
                    undone_move->active_clip_index == 0 &&
                    undone_move->selected_source_path == first_source &&
                    undone_move->playhead_frame == 12,
                "Undo did not restore the Timeline and UI state.");

        const auto redone_move = history.redo(*undone_move);
        require(redone_move.has_value() && history.canUndo(),
                "Redo did not return the newer timeline state.");
        history_model.restore(redone_move->timeline);
        require(history_model.clips()[0].source_path ==
                    std::filesystem::weakly_canonical(second_source) &&
                    history_model.clips()[1].source_path ==
                        std::filesystem::weakly_canonical(first_source) &&
                    redone_move->active_clip_index == 1 &&
                    redone_move->playhead_frame == 5,
                "Redo did not restore the newer Timeline state.");

        const auto before_split = make_history_state(0, first_source, 30);
        history.clear();
        history.recordBeforeEdit(before_split);
        require(history_model.splitClip(1, 20) == timeline::SplitClipResult::Split,
                "The history split operation failed.");
        auto split_state = make_history_state(2, first_source, 0);
        const auto undone_split = history.undo(split_state);
        require(undone_split.has_value(), "Undo after split was unavailable.");
        history_model.restore(undone_split->timeline);
        require(history_model.clipCount() == 2 &&
                    history_model.totalDurationFrames() == 180,
                "Undo after split did not restore the original clips.");

        const auto before_trim = make_history_state(0, first_source, 8);
        history.clear();
        history.recordBeforeEdit(before_trim);
        require(history_model.trimClip(0, 10, 50) == timeline::TrimClipResult::Trimmed,
                "The history trim operation failed.");
        auto trim_state = make_history_state(0, first_source, 3);
        const auto undone_trim = history.undo(trim_state);
        require(undone_trim.has_value(), "Undo after trim was unavailable.");
        history_model.restore(undone_trim->timeline);
        require(history_model.clips()[0].source_start_frame == 0 &&
                    history_model.clips()[0].timeline_duration_frames == 60,
                "Undo after trim did not restore the source range.");

        const auto before_remove = make_history_state(0, first_source, 2);
        history.clear();
        history.recordBeforeEdit(before_remove);
        require(history_model.removeClip(1) == timeline::RemoveClipResult::Removed,
                "The history remove operation failed.");
        auto remove_state = make_history_state(0, first_source, 0);
        const auto undone_remove = history.undo(remove_state);
        require(undone_remove.has_value(), "Undo after remove was unavailable.");
        history_model.restore(undone_remove->timeline);
        require(history_model.clipCount() == 2 &&
                    history_model.totalDurationFrames() == 180,
                "Undo after remove did not restore the removed clip.");

        const auto before_clear = make_history_state(0, first_source, 1);
        history.clear();
        history.recordBeforeEdit(before_clear);
        history_model.clear();
        auto clear_state = make_history_state(std::nullopt, first_source, 0);
        const auto undone_clear = history.undo(clear_state);
        require(undone_clear.has_value(), "Undo after clear was unavailable.");
        history_model.restore(undone_clear->timeline);
        require(history_model.clipCount() == 2 &&
                    history_model.clips()[0].source_path ==
                        std::filesystem::weakly_canonical(second_source),
                "Undo after clear did not restore the Timeline.");

        const auto before_audio = make_history_state(0, first_source, 4);
        history.clear();
        history.recordBeforeEdit(before_audio);
        require(history_model.setClipAudio(0, 0, 0.25, true) ==
                    timeline::AudioParameterResult::Changed,
                "The history audio edit failed.");
        const auto after_audio = make_history_state(0, first_source, 4);
        const auto undone_audio = history.undo(after_audio);
        require(undone_audio.has_value(), "Undo after audio edit was unavailable.");
        history_model.restore(undone_audio->timeline);
        require(history_model.clips()[0].audio_gain == 1.0 &&
                    !history_model.clips()[0].audio_muted,
                "Undo after audio edit did not restore clip audio parameters.");
        const auto redone_audio = history.redo(*undone_audio);
        require(redone_audio.has_value(), "Redo after audio edit was unavailable.");
        history_model.restore(redone_audio->timeline);
        require(history_model.clips()[0].audio_gain == 0.25 &&
                    history_model.clips()[0].audio_muted,
                "Redo after audio edit did not restore clip audio parameters.");

        const auto before_transform = make_history_state(0, first_source, 4);
        history.clear();
        history.recordBeforeEdit(before_transform);
        auto edited_transform = history_model.clips()[0].transform;
        edited_transform.position_x = 0.25;
        require(history_model.setClipTransform(0, 0, edited_transform) ==
                    timeline::TransformParameterResult::Changed,
                "The history transform edit failed.");
        const auto after_transform = make_history_state(0, first_source, 4);
        const auto undone_transform = history.undo(after_transform);
        require(undone_transform.has_value(),
                "Undo after a transform edit was unavailable.");
        history_model.restore(undone_transform->timeline);
        require(history_model.clips()[0].transform.position_x == 0.5,
                "Undo after a transform edit did not restore the base value.");
        const auto redone_transform = history.redo(*undone_transform);
        require(redone_transform.has_value(),
                "Redo after a transform edit was unavailable.");
        history_model.restore(redone_transform->timeline);
        require(history_model.clips()[0].transform.position_x == 0.25,
                "Redo after a transform edit did not restore the edited value.");

        const auto before_keyframe = make_history_state(0, first_source, 4);
        history.clear();
        history.recordBeforeEdit(before_keyframe);
        require(history_model.setClipKeyframe(
                    0, 0, timeline::TransformProperty::Opacity, 4, 0.5) ==
                    timeline::TransformParameterResult::Changed,
                "The history keyframe edit failed.");
        const auto after_keyframe = make_history_state(0, first_source, 4);
        const auto undone_keyframe = history.undo(after_keyframe);
        require(undone_keyframe.has_value(),
                "Undo after a keyframe edit was unavailable.");
        history_model.restore(undone_keyframe->timeline);
        require(history_model.clips()[0].keyframes.opacity.empty(),
                "Undo after a keyframe edit did not restore the curve.");

        history.clear();
        for (std::size_t index = 0; index < 105; ++index) {
            history.recordBeforeEdit(make_history_state(
                0,
                first_source,
                static_cast<std::int64_t>(index)));
        }
        require(history.undoCount() == timeline::TimelineHistory::max_states,
                "The history exceeded its maximum Undo capacity.");
        require(history.undo(make_history_state(0, first_source, 105)).has_value(),
                "The bounded history could not undo its newest state.");
        history.recordBeforeEdit(make_history_state(0, first_source, 999));
        require(!history.canRedo(),
                "A new edit did not clear the Redo history.");

        timeline::TimelineModel text_model;
        require(text_model.addClip(first_metadata) == timeline::AddClipResult::Added,
                "The text overlap test video was not added.");
        require(text_model.addTextClip(0, 10, 150, 24.0) == timeline::AddClipResult::Added,
                "A text clip could not overlap a video clip.");
        require(text_model.clipCount() == 2 &&
                    text_model.tracks()[0].clips[1].kind == timeline::ClipKind::Text,
                "The text clip kind or count was incorrect.");
        const auto& text_clip = text_model.tracks()[0].clips[1];
        require(text_clip.text.content == "Text" &&
                    text_clip.text.font_family == "Sans Serif" &&
                    text_clip.text.font_size_pixels == 48.0 &&
                    text_clip.text.alignment == timeline::TextAlignment::Center &&
                    text_clip.text.color == std::array<std::uint8_t, 4>{255, 255, 255, 255} &&
                    text_clip.frame_rate == 24.0 && text_clip.timeline_duration_frames == 150,
                "Text clip defaults were incorrect.");
        require(text_model.addTextClip(0, 20, 10) == timeline::AddClipResult::Overlap,
                "Overlapping text clips were accepted.");
        require(text_model.addClip(0, second_metadata, 10) == timeline::AddClipResult::Overlap,
                "An overlapping video clip was accepted.");
        require(text_model.clipAt(0, 20).has_value() &&
                    text_model.clipAt(0, 20)->clip_index == 1,
                "Text was not preferred when selecting an overlapping frame.");
        auto text_style = text_clip.text;
        text_style.content = "Hello\nWorld";
        text_style.alignment = timeline::TextAlignment::Left;
        text_style.color = {10, 20, 30, 200};
        require(text_model.setClipText(0, 1, text_style) ==
                    timeline::TextParameterResult::Changed,
                "Text style could not be edited.");
        require(text_model.tracks()[0].clips[1].display_name == "Hello\nWorld" &&
                    text_model.tracks()[0].clips[1].text == text_style,
                "Text style changes were not preserved.");
        text_style.font_size_pixels = 0.0;
        require(text_model.setClipText(0, 1, text_style) ==
                    timeline::TextParameterResult::InvalidValue,
                "An invalid text style was accepted.");
        require(text_model.setClipText(0, 0, timeline::TextStyle{}) ==
                    timeline::TextParameterResult::InvalidValue,
                "Text style was accepted for a video clip.");
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
