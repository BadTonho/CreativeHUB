#include "timeline/timeline_widget.h"

#include <QApplication>
#include <QMouseEvent>
#include <QPointingDevice>

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

timeline::TimelineClip makeClip(
    const std::filesystem::path& path,
    std::int64_t timeline_start,
    std::int64_t duration,
    const std::string& name) {
    timeline::TimelineClip clip;
    clip.timeline_start_frame = timeline_start;
    clip.source_start_frame = 0;
    clip.timeline_duration_frames = duration;
    clip.source_path = path;
    clip.display_name = name;
    clip.duration_seconds = 4.0;
    clip.frame_rate = 30.0;
    clip.frame_count = 120;
    return clip;
}

void sendMouse(
    QWidget& widget,
    QEvent::Type type,
    const QPointF& position,
    Qt::MouseButtons buttons,
    Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QMouseEvent event(
        type,
        position,
        position,
        position,
        Qt::LeftButton,
        buttons,
        modifiers,
        Qt::MouseEventNotSynthesized,
        QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(&widget, &event);
}

} // namespace

int main(int argc, char* argv[]) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication application(argc, argv);

    try {
        timeline::TimelineWidget widget;
        widget.resize(1000, 500);
        widget.show();
        application.processEvents();

        timeline::TimelineTrack top_track{
            2,
            "Video 2",
            1.0,
            false,
            {makeClip("top.mkv", 0, 100, "top.mkv")}};
        timeline::TimelineTrack lower_track{
            1,
            "Video 1",
            1.0,
            false,
            {makeClip("lower.mkv", 0, 100, "lower.mkv")}};
        widget.setTracks({top_track, lower_track});
        application.processEvents();

        require(widget.minimumHeight() >= 48 + 2 * 72 + 10 + 12,
                "The Timeline minimum height does not fit all track rows.");

        int selected_track = -1;
        int selected_clip = -1;
        int seek_started = 0;
        std::vector<qint64> seek_frames;
        qint64 move_from_track = -1;
        qint64 move_to_track = -1;
        int split_count = 0;
        qint64 split_frame = -1;
        qint64 trim_start = -1;
        qint64 trim_end = -1;

        QObject::connect(
            &widget,
            &timeline::TimelineWidget::clipSelectedAt,
            [&selected_track, &selected_clip](qint64 track, qint64 clip) {
                selected_track = static_cast<int>(track);
                selected_clip = static_cast<int>(clip);
            });
        QObject::connect(
            &widget,
            &timeline::TimelineWidget::seekStarted,
            [&seek_started]() { ++seek_started; });
        QObject::connect(
            &widget,
            &timeline::TimelineWidget::seekRequested,
            [&seek_frames](qint64 frame) { seek_frames.push_back(frame); });
        QObject::connect(
            &widget,
            &timeline::TimelineWidget::clipMoveRequestedAt,
            [&move_from_track, &move_to_track](
                qint64 from_track,
                qint64,
                qint64 to_track,
                qint64) {
                move_from_track = from_track;
                move_to_track = to_track;
            });
        QObject::connect(
            &widget,
            &timeline::TimelineWidget::clipSplitRequestedAt,
            [&split_count, &split_frame](qint64, qint64, qint64 frame) {
                ++split_count;
                split_frame = frame;
            });
        QObject::connect(
            &widget,
            &timeline::TimelineWidget::clipTrimRequestedAt,
            [&trim_start, &trim_end](qint64, qint64, qint64 start, qint64 end) {
                trim_start = start;
                trim_end = end;
            });

        // Clicking an inactive clip selects it without starting a seek.
        widget.setActiveClip(std::nullopt);
        sendMouse(widget, QEvent::MouseButtonPress, QPointF(500, 120),
                  Qt::LeftButton);
        sendMouse(widget, QEvent::MouseButtonRelease, QPointF(500, 120),
                  Qt::NoButton);
        require(selected_track == 0 && selected_clip == 0,
                "Clicking a clip did not select the expected occurrence.");
        require(seek_started == 0 && seek_frames.empty(),
                "Selecting an inactive clip unexpectedly started seeking.");

        // Normal interior dragging seeks only after the gesture is released.
        widget.setActiveClip(timeline::ClipLocation{0, 0});
        sendMouse(widget, QEvent::MouseButtonPress, QPointF(400, 120),
                  Qt::LeftButton);
        sendMouse(widget, QEvent::MouseMove, QPointF(700, 120),
                  Qt::LeftButton);
        require(seek_started == 1 && seek_frames.empty(),
                "Seeking decoded or committed a frame before release.");
        sendMouse(widget, QEvent::MouseButtonRelease, QPointF(700, 120),
                  Qt::NoButton);
        require(seek_frames.size() == 1 && seek_frames.front() > 0 &&
                    seek_frames.front() < 100,
                "Interior dragging did not request an in-range seek frame.");

        // Alt-drag moves a clip between tracks and does not request seeking.
        sendMouse(widget, QEvent::MouseButtonPress, QPointF(500, 120),
                  Qt::LeftButton, Qt::AltModifier);
        sendMouse(widget, QEvent::MouseMove, QPointF(500, 320),
                  Qt::LeftButton, Qt::AltModifier);
        sendMouse(widget, QEvent::MouseButtonRelease, QPointF(500, 320),
                  Qt::NoButton, Qt::AltModifier);
        require(move_from_track == 0 && move_to_track == 1,
                "Alt-drag did not request a move to the lower track.");
        require(seek_frames.size() == 1,
                "Alt-drag was incorrectly treated as seeking.");

        // Blade Tool uses a click, not a drag, and reports the local frame.
        widget.setRazorMode(true);
        sendMouse(widget, QEvent::MouseButtonPress, QPointF(600, 120),
                  Qt::LeftButton);
        sendMouse(widget, QEvent::MouseButtonRelease, QPointF(600, 120),
                  Qt::NoButton);
        require(split_count == 1 && split_frame > 0 && split_frame < 100,
                "Blade Tool did not request an interior split.");

        // Edge dragging requests a trim only when the pointer is released.
        widget.setRazorMode(false);
        sendMouse(widget, QEvent::MouseButtonPress, QPointF(156, 120),
                  Qt::LeftButton);
        sendMouse(widget, QEvent::MouseMove, QPointF(200, 120),
                  Qt::LeftButton);
        require(trim_start == -1 && trim_end == -1,
                "Trimming was committed before mouse release.");
        sendMouse(widget, QEvent::MouseButtonRelease, QPointF(200, 120),
                  Qt::NoButton);
        require(trim_start > 0 && trim_end == 100,
                "The left edge did not request a bounded trim.");

        widget.close();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
