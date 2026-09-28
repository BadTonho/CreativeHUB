#include "model/composition_document.h"

#include <creative_suite/media/media_library.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

template<typename Function>
void requireThrows(Function&& function, const char* message)
{
    try {
        function();
    } catch (const std::invalid_argument&) {
        return;
    }
    require(false, message);
}

} // namespace

int main()
{
    using namespace creative_suite::animation;
    using namespace motion::model;

    const FrameRate fractional_rate{24000, 1001};
    requireThrows([&] {
        CompositionDocument document(0, 720, fractional_rate);
    }, "zero canvas width is rejected");
    requireThrows([&] {
        CompositionDocument document(1920, -1, fractional_rate);
    }, "negative canvas height is rejected");
    requireThrows([] {
        CompositionDocument document(1920, 1080, FrameRate{0, 1});
    }, "non-positive frame-rate numerator is rejected");
    requireThrows([] {
        CompositionDocument document(1920, 1080, FrameRate{30000, 0});
    }, "non-positive frame-rate denominator is rejected");
    requireThrows([] {
        CompositionDocument document(1920, 1080, FrameRate{2997, 100});
    }, "unsupported custom frame rate is rejected");

    CompositionDocument document(1920, 1080, fractional_rate);
    require(document.canvasSize() == CanvasSize{1920, 1080}, "explicit canvas size is retained");
    require(document.frameRate() == fractional_rate,
        "the exact fractional frame rate is retained");
    require(supportedFrameRates().size() == 13
            && isSupportedFrameRate(FrameRate{30000, 1001})
            && !isSupportedFrameRate(FrameRate{25, 1000}),
        "the supported common rates preserve exact fractional values");
    require(document.layers().empty(), "new document starts without layers");

    using creative_suite::media::MediaKind;
    using creative_suite::media::VideoMetadata;
    CompositionDocument media_document(1920, 1080, fractional_rate);
    VideoMetadata image_metadata;
    image_metadata.kind = MediaKind::Image;
    image_metadata.source_path = std::filesystem::temp_directory_path() / "motion-poster.png";
    image_metadata.display_name = "Poster";
    LayerId image_layer = 0;
    const auto image_result = media_document.addMediaLayer(image_metadata, 12, &image_layer);
    require(image_result == AddMediaLayerResult::Added,
            "a still image becomes a timed media layer");
    require(image_layer != 0, "the new image layer receives an ID");
    require(media_document.layers().size() == 1, "one still creates one layer occurrence");
    require(media_document.layers().back().timeline_start_frame == 12 &&
                media_document.layers().back().duration_frames == 120,
            "stills last five seconds at the fractional composition frame rate");
    require(media_document.layers().back().source_path ==
                creative_suite::media::MediaLibrary::canonicalPath(image_metadata.source_path),
            "media layers retain the canonical source path");

    CompositionDocument integer_rate_document(640, 360, FrameRate{24, 1});
    LayerId integer_image_layer = 0;
    require(integer_rate_document.addMediaLayer(image_metadata, 0, &integer_image_layer) ==
                AddMediaLayerResult::Added &&
                integer_rate_document.layers().front().duration_frames == 120,
            "five-second still duration is exact at an integer frame rate");
    CompositionDocument fractional_rate_document(640, 360, FrameRate{30000, 1001});
    LayerId fractional_image_layer = 0;
    require(fractional_rate_document.addMediaLayer(image_metadata, 0, &fractional_image_layer) ==
                AddMediaLayerResult::Added &&
                fractional_rate_document.layers().front().duration_frames == 150,
            "fractional still duration rounds up using exact rational composition timing");

    VideoMetadata video_metadata;
    video_metadata.kind = MediaKind::Video;
    video_metadata.source_path = std::filesystem::temp_directory_path() / "motion-footage.mkv";
    video_metadata.display_name = "Footage";
    video_metadata.frame_rate = 30.0;
    video_metadata.frame_count = 90;
    LayerId video_layer = 0;
    require(media_document.addMediaLayer(video_metadata, 240, &video_layer) ==
                AddMediaLayerResult::Added,
            "a video with source timing metadata becomes a timed layer");
    require(video_layer != image_layer && media_document.layers().back().source_frame_count == 90 &&
                media_document.layers().back().source_duration_frames == 90 &&
                media_document.layers().back().duration_frames == 72 &&
                media_document.layers().back().maximum_timeline_duration_frames == 72,
            "video duration converts source frames to the exact composition rate");
    LayerId repeated_video_layer = 0;
    require(media_document.addMediaLayer(video_metadata, 0, &repeated_video_layer) ==
                AddMediaLayerResult::Added && repeated_video_layer != video_layer &&
                media_document.layers().size() == 3,
            "each occurrence of one media path receives a distinct layer ID");

    auto invalid_rate_video = video_metadata;
    invalid_rate_video.frame_rate.reset();
    require(media_document.addMediaLayer(invalid_rate_video, 0) ==
                AddMediaLayerResult::InvalidTimingMetadata,
            "video insertion rejects frame counts that cannot be converted without source fps");
    auto invalid_duration_video = video_metadata;
    invalid_duration_video.frame_count.reset();
    invalid_duration_video.frame_rate = 30.0;
    invalid_duration_video.duration_seconds = 0.0;
    require(media_document.addMediaLayer(invalid_duration_video, 0) ==
                AddMediaLayerResult::InvalidTimingMetadata,
            "video insertion rejects non-positive timing metadata");
    auto duration_only_video = video_metadata;
    duration_only_video.frame_count.reset();
    duration_only_video.duration_seconds = 2.1;
    LayerId duration_only_id = 0;
    require(media_document.addMediaLayer(duration_only_video, 0, &duration_only_id) ==
                AddMediaLayerResult::Added &&
                media_document.layers().back().source_frame_count == 63 &&
                media_document.layers().back().duration_frames == 51,
            "video duration can be estimated from valid seconds and source fps metadata");
    require(media_document.addMediaLayer(image_metadata,
                std::numeric_limits<std::int64_t>::max() - 1) ==
                AddMediaLayerResult::InvalidPosition,
            "timeline placement rejects an unrepresentable end frame");

    require(media_document.moveLayerInTimeline(image_layer, 80) &&
                media_document.layers().front().timeline_start_frame == 80,
            "media layer start can be moved independently of its source");
    require(!media_document.moveLayerInTimeline(image_layer,
                std::numeric_limits<std::int64_t>::max() - 1),
            "moving a layer cannot overflow its end frame");
    require(media_document.resizeLayerDuration(image_layer, 600) &&
                media_document.layers().front().duration_frames == 600,
            "still image duration can be extended");
    require(!media_document.resizeLayerDuration(image_layer, 0),
            "layer duration must remain positive");
    require(media_document.resizeLayerDuration(video_layer, 60) &&
                !media_document.resizeLayerDuration(video_layer, 73),
            "video duration can be shortened and restored only up to its source length");
    require(media_document.moveLayer(repeated_video_layer, 0) &&
                media_document.layers().front().id == repeated_video_layer,
            "media layers can be reordered independently of timeline timing");

    const LayerId back = document.addLayer(LayerKind::Image, "Background image");
    const LayerId middle = document.addLayer(LayerKind::Shape, "Accent shape");
    const LayerId front = document.addLayer(LayerKind::Text, "Title");
    require(back != middle && middle != front && back != front, "layer IDs are unique within the document");
    require(document.layers().size() == 3, "layers are appended");
    require(document.layers()[0].id == back && document.layers()[2].id == front,
        "new layers append in back-to-front order");
    require(document.layers()[2].kind == LayerKind::Text && document.layers()[2].name == "Title",
        "layer kind and name are stored");
    require(document.layers()[2].visible, "layers start visible");

    require(document.moveLayer(front, 0), "moving an existing layer succeeds");
    require(document.layers()[0].id == front && document.layers()[1].id == back,
        "reordering places the layer at its final index");
    require(document.moveLayer(front, 0), "moving a layer to its current index succeeds");
    require(!document.moveLayer(front, 3), "out-of-range destination is rejected");
    require(!document.moveLayer(9999, 0), "unknown layer cannot be reordered");

    require(document.setLayerName(middle, "Shape 1"), "existing layer can be renamed");
    require(document.layers()[2].name == "Shape 1", "new layer name is stored");
    require(document.setLayerVisible(middle, false), "existing layer visibility can be changed");
    require(!document.layers()[2].visible, "new visibility is stored");

    Transform2D transform;
    transform.position_x = -0.25;
    transform.position_y = 1.25;
    transform.scale = 1.5;
    transform.rotation_degrees = 37.0;
    transform.opacity = 0.65;
    require(document.setLayerTransform(middle, transform), "valid shared transform is accepted");
    require(document.layers()[2].transform == transform, "shared transform values are retained");
    Transform2D invalid_transform = transform;
    invalid_transform.scale = 0.0;
    require(!document.setLayerTransform(middle, invalid_transform), "invalid transform is rejected");
    require(document.layers()[2].transform == transform, "invalid transform leaves prior values intact");

    require(document.setLayerKeyframe(middle, TransformProperty::PositionX, 12, -0.5),
        "valid shared keyframe is accepted");
    require(document.setLayerKeyframe(middle, TransformProperty::Opacity, 30, 0.25),
        "opacity keyframe is stored");
    require(keyframesFor(document.layers()[2].keyframes, TransformProperty::PositionX)
                == std::vector<Keyframe>{{12, -0.5}},
        "shared keyframe value and local frame are retained");
    require(!document.setLayerKeyframe(middle, TransformProperty::Scale, -1, 2.0),
        "negative local keyframe frame is rejected");
    require(!document.setLayerKeyframe(middle, TransformProperty::Opacity, 31, 1.5),
        "invalid property value is rejected");
    require(!document.setLayerKeyframe(9999, TransformProperty::Opacity, 0, 0.5),
        "keyframe cannot be set on an unknown layer");
    const auto stored_keyframes = document.layers()[2].keyframes;
    transform.position_x = 0.8;
    require(document.setLayerTransform(middle, transform),
        "base transform can change after keyframes are present");
    require(document.layers()[2].keyframes == stored_keyframes,
        "changing the base transform leaves keyframes untouched");

    require(document.removeLayer(back), "existing layer can be removed");
    require(!document.removeLayer(back), "removed layer cannot be removed again");
    const LayerId next = document.addLayer(LayerKind::Video, "Footage");
    require(next > front, "removed IDs are not reused");
    require(document.layers().size() == 3, "removal and insertion update layer storage");

    std::cout << "Motion Studio composition document tests passed.\n";
    return EXIT_SUCCESS;
}
