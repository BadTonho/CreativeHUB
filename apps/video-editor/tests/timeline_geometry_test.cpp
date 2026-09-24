#include "timeline/timeline_drop_validation.h"
#include "timeline/timeline_interaction_painter.h"

#include <QImage>
#include <QPainter>

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

timeline::TimelineClip clip(
    timeline::ClipId id,
    std::int64_t start,
    std::int64_t duration,
    timeline::ClipKind kind = timeline::ClipKind::Video) {
    timeline::TimelineClip value;
    value.clip_id = id;
    value.timeline_start_frame = start;
    value.timeline_duration_frames = duration;
    value.frame_rate = 30.0;
    value.frame_count = 300;
    value.kind = kind;
    return value;
}

void run() {
    using namespace timeline;
    std::vector<TimelineTrack> tracks{
        {1, "V1", 1.0, false,
         {clip(1, 10, 20), clip(2, 40, 10),
          clip(3, 10, 20, ClipKind::Text)},
         {{1, 2, TransitionKind::CrossDissolve, 5}}},
        {2, "V2", 1.0, false, {clip(4, 60, 10)}, {}}};
    const TimelineGeometry geometry(tracks, QSizeF(900.0, 200.0), 70.0, 1.0, 100);

    const auto row = geometry.trackRect(0);
    const auto content = geometry.trackContentRect(0);
    require(row == QRectF(12.0, 48.0, 876.0, 70.0),
            "Track geometry did not preserve the established margins and row height.");
    require(geometry.trackRect(1).top() == 128.0,
            "Track row geometry did not preserve the vertical gap.");
    require(geometry.frameAtContentX(content.left()) == 0 &&
                geometry.frameAtContentX(content.right()) == 100 &&
                std::abs(geometry.contentXForFrame(50) - content.center().x()) < 0.001,
            "Frame and pixel conversion did not round-trip across the content extent.");
    require(geometry.playheadFrameAtRulerX(content.right()) == 69,
            "The ruler mapped its exclusive right edge outside the timeline.");

    const auto first_rect = geometry.clipRect(tracks[0].clips[1], 0);
    const auto media_hit = TimelineHitTester::clipAt(
        tracks, geometry, first_rect.left() + 4.0, first_rect.center().y());
    require(media_hit == ClipLocation{0, 1},
            "Hit testing did not resolve the clip under the pointer.");
    const auto text_rect = geometry.clipRect(tracks[0].clips[2], 0);
    const auto text_hit = TimelineHitTester::clipAt(
        tracks, geometry, text_rect.center().x(), text_rect.center().y());
    require(text_hit == ClipLocation{0, 2},
            "Hit testing did not prioritize a text clip at an overlapping location.");
    require(TimelineHitTester::trackAt(geometry, tracks.size(), 120.0) == std::nullopt &&
                TimelineHitTester::trackAt(geometry, tracks.size(), 135.0) == 1,
            "Track hit testing did not respect row gaps and row boundaries.");

    const std::vector<TimelineTrack> transition_tracks{{
        8, "Transition", 1.0, false, {clip(8, 0, 50), clip(9, 50, 30)},
        {{8, 9, TransitionKind::CrossDissolve, 5}}}};
    const TimelineGeometry transition_geometry(
        transition_tracks, QSizeF(900.0, 200.0), 70.0, 1.0, 100);
    const auto transition = TimelineHitTester::transitionPairAt(
        transition_tracks, transition_geometry,
        transition_geometry.contentXForFrame(50),
        transition_geometry.trackRect(0).center().y());
    require(transition == std::pair<std::size_t, std::size_t>{0, 1},
            "Transition hit testing did not resolve the adjacent clip pair.");

    require(TimelineDropValidator::overlaps(tracks, 0, 20, 2) &&
                !TimelineDropValidator::overlaps(tracks, 0, 30, 5) &&
                !TimelineDropValidator::overlaps(tracks, 1, 60, 10, ClipLocation{1, 0}),
            "Drop overlap validation mishandled overlap, touching, or excluded clips.");
    const auto snapped = TimelineDropValidator::snap(
        tracks, geometry, true, 0, 29, 5);
    require(snapped.start_frame == 30 && snapped.guide_frame == 30,
            "Snapping did not align a nearby clip edge to the nearest frame boundary.");
    const auto unsnapped = TimelineDropValidator::snap(
        tracks, geometry, false, 0, 29, 5);
    require(unsnapped.start_frame == 29 && !unsnapped.guide_frame.has_value(),
            "Disabled snapping still adjusted a drop position.");

    QImage interaction_image(900, 200, QImage::Format_ARGB32_Premultiplied);
    interaction_image.fill(Qt::transparent);
    QPainter painter(&interaction_image);
    TimelineInteractionPaintState paint_state;
    paint_state.tracks = &tracks;
    paint_state.geometry = &geometry;
    paint_state.moving_clip = ClipLocation{0, 0};
    paint_state.move_target_track = 1;
    paint_state.move_target_frame = 80;
    TimelineInteractionPainter::paint(painter, paint_state);
    painter.end();
    const auto ghost_pixel = interaction_image.pixelColor(
        static_cast<int>(std::lround(geometry.contentXForFrame(85))),
        static_cast<int>(geometry.trackRect(1).center().y()));
    require(ghost_pixel.alpha() > 0,
            "The extracted interaction painter did not render a clip drag preview.");
}

} // namespace

int main() {
    try {
        run();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
