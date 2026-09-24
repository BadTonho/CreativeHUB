#include "timeline/timeline_interaction_controller.h"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void run() {
    timeline::TimelineInteractionController controller;

    controller.beginMove(timeline::ClipId{41}, timeline::TrackId{7}, 120,
                         QPointF(10.0, 20.0));
    require(controller.movePending() && !controller.moveActive() &&
                !controller.updateMoveActivation(QPointF(12.0, 22.0)),
            "A small pointer movement activated a clip move too early.");
    require(!controller.finishMove().has_value(),
            "A click on a clip produced a move request.");

    controller.beginMove(timeline::ClipId{41}, timeline::TrackId{7}, 120,
                         QPointF(10.0, 20.0));
    require(controller.updateMoveActivation(QPointF(14.0, 21.0)) &&
                controller.moveActive(),
            "A drag past the threshold did not activate clip movement.");
    controller.setMoveTarget(timeline::TimelineMoveTarget{9, 240, 238});
    const auto move = controller.finishMove();
    require(move.has_value() && move->clip_id == 41 &&
                move->target_track_id == 9 && move->timeline_start_frame == 240,
            "A completed move did not retain its stable IDs and snapped frame.");
    require(!controller.moveActive() && !controller.movePending(),
            "Finishing a move left stale gesture state behind.");

    controller.beginMove(41, 7, 120, QPointF(0.0, 0.0));
    require(controller.updateMoveActivation(QPointF(10.0, 0.0)),
            "The invalid-target move fixture did not activate.");
    controller.setMoveTarget(std::nullopt);
    require(!controller.finishMove().has_value(),
            "Dropping outside a valid track still produced a move request.");

    controller.beginSplit(41, 30, QPointF(50.0, 70.0));
    controller.updateSplit(QPointF(52.0, 71.0));
    const auto split = controller.finishSplit();
    require(split.has_value() && split->clip_id == 41 && split->local_frame == 30,
            "A blade click did not request a split by stable clip ID.");
    controller.beginSplit(41, 30, QPointF(50.0, 70.0));
    controller.updateSplit(QPointF(60.0, 70.0));
    require(!controller.finishSplit().has_value(),
            "Dragging while the blade tool was active produced a split.");

    controller.beginSeek(41, 900, 10, QPointF(20.0, 20.0));
    require(!controller.updateSeek(QPointF(22.0, 21.0), 12) &&
                controller.seekPending() && !controller.seekDragging(),
            "A click started a seek drag before crossing its threshold.");
    require(controller.updateSeek(QPointF(26.0, 20.0), 16) &&
                controller.seekDragging() &&
                controller.seekPreviewLocalFrame() == 16,
            "Dragging inside a clip did not start seek preview at the current frame.");
    require(controller.finishSeek() == 916 && !controller.seekDragging(),
            "The completed seek did not produce the clip-relative global frame.");
    controller.beginSeek(41, 900, 10, QPointF(20.0, 20.0));
    require(!controller.updateSeek(QPointF(20.0, 20.0), 10) &&
                !controller.finishSeek().has_value(),
            "A simple click on an active clip unexpectedly committed a seek.");

    controller.beginRulerSeek(30, 210.0);
    controller.updateRulerSeek(45, 315.0);
    require(controller.rulerSeekPending() && controller.rulerPreviewFrame() == 45 &&
                controller.rulerContentX() == 315.0,
            "Ruler dragging did not keep its latest frame and render position.");
    require(controller.finishRulerSeek() == 45 &&
                !controller.rulerSeekPending() &&
                !controller.rulerPreviewFrame().has_value(),
            "Releasing the ruler did not return and clear the final seek frame.");

    timeline::TimelineDropPreview drop;
    drop.hovering = true;
    drop.media = true;
    drop.target_track_index = 2;
    drop.target_frame = 300;
    drop.pointer_position = QPointF(40.0, 90.0);
    drop.duration_frames = 80;
    drop.label = QStringLiteral("Media source");
    drop.valid = true;
    drop.snap_guide_frame = 320;
    controller.setDropPreview(drop);
    require(controller.dropPreview().hovering && controller.dropPreview().media &&
                controller.dropPreview().target_track_index == 2 &&
                controller.dropPreview().target_frame == 300 &&
                controller.dropPreview().duration_frames == 80 &&
                controller.dropPreview().snap_guide_frame == 320,
            "The drag-and-drop controller did not retain its typed preview state.");
    controller.clearDropGhost();
    require(controller.dropPreview().hovering &&
                controller.dropPreview().target_frame == 300 &&
                !controller.dropPreview().media &&
                controller.dropPreview().duration_frames == 1 &&
                !controller.dropPreview().snap_guide_frame.has_value(),
            "Clearing a drop ghost erased the active target or kept stale ghost data.");
    controller.clearDropPreview();
    require(!controller.dropPreview().hovering &&
                !controller.dropPreview().target_track_index.has_value(),
            "Clearing a drag-and-drop interaction left stale target state behind.");
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
