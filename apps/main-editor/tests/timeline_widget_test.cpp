#include "timeline/timeline_widget.h"

#include <QApplication>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QWheelEvent>

#include <cstdint>
#include <cmath>
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
    clip.duration_seconds = static_cast<double>(duration) / 30.0;
    clip.frame_rate = 30.0;
    clip.frame_count = duration;
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

void sendWheel(
    QWidget& widget,
    const QPointF& position,
    int angle_delta,
    Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QWheelEvent event(
        position,
        position,
        QPoint(0, 0),
        QPoint(0, angle_delta),
        Qt::NoButton,
        modifiers,
        Qt::NoScrollPhase,
        false,
        Qt::MouseEventNotSynthesized);
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
        widget.setTimelineViewportWidth(1000);

        constexpr std::int64_t test_clip_duration = 54000;

        timeline::TimelineTrack top_track{
            2,
            "Video 2",
            1.0,
            false,
            {makeClip("top.mkv", 0, test_clip_duration, "top.mkv")}};
        timeline::TimelineTrack lower_track{
            1,
            "Video 1",
            1.0,
            false,
            {makeClip("lower.mkv", 0, test_clip_duration, "lower.mkv")}};
        widget.setTracks({top_track, lower_track});
        application.processEvents();

        require(widget.minimumHeight() >= 48 + 2 * 72 + 10 + 12,
                "The Timeline minimum height does not fit all track rows.");
        require(widget.minimumWidth() == 1000,
                "A short timeline did not keep the standard viewport width.");
        require(widget.zoomFactor() == 1.0,
                "The timeline did not start at the expected zoom level.");
        require(widget.canZoomOut() && widget.canZoomIn(),
                "The timeline zoom limits were not initialized correctly.");
        require(widget.nextZoomFactor(-1) == 0.75 &&
                    widget.nextZoomFactor(1) == 1.25,
                "The timeline did not expose the expected adjacent zoom levels.");
        require(
            timeline::TimelineWidget::formatTimecode(0, 30.0) == "00:00:00.000" &&
                timeline::TimelineWidget::formatTimecode(30 * 60 + 15, 30.0) ==
                    "00:01:00.500" &&
                timeline::TimelineWidget::formatTimecode(30 * 60 * 60, 30.0) ==
                    "01:00:00.000",
            "The timeline ruler timecode format is incorrect.");
        require(!widget.moveRequiresAlt(),
                "The timeline did not default to moving clips without Alt.");
        widget.setMoveRequiresAlt(true);

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
        int transition_track = -1;
        int transition_from = -1;
        int transition_to = -1;
        int zoom_requests = 0;
        double requested_zoom = 0.0;
        double requested_anchor = 0.0;

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
        QObject::connect(
            &widget,
            &timeline::TimelineWidget::transitionSelectedAt,
            [&transition_track, &transition_from, &transition_to](
                qint64 track, qint64 from, qint64 to) {
                transition_track = static_cast<int>(track);
                transition_from = static_cast<int>(from);
                transition_to = static_cast<int>(to);
            });
        QObject::connect(
            &widget,
            &timeline::TimelineWidget::zoomRequested,
            [&zoom_requests, &requested_zoom, &requested_anchor](
                double factor, double anchor) {
                ++zoom_requests;
                requested_zoom = factor;
                requested_anchor = anchor;
            });

        sendWheel(widget, QPointF(600, 120), 120, Qt::ControlModifier);
        require(zoom_requests == 1 && requested_zoom == 1.25 &&
                    requested_anchor == 600.0,
                "Ctrl + wheel did not request the next timeline zoom level.");
        sendWheel(widget, QPointF(600, 120), -120);
        require(zoom_requests == 1,
                "Normal wheel scrolling was incorrectly treated as timeline zoom.");
        widget.setZoomFactor(0.25);
        require(widget.zoomFactor() == 0.25 && !widget.canZoomOut() &&
                    widget.minimumWidth() == 1000,
                "The minimum timeline zoom did not preserve the viewport width.");
        widget.setZoomFactor(8.0);
        require(widget.zoomFactor() == 8.0 && !widget.canZoomIn() &&
                    widget.minimumWidth() == 8000,
                "The maximum timeline zoom did not expand the timeline surface.");
        const auto anchor_frame = widget.frameAtContentX(600.0);
        require(anchor_frame.has_value(),
                "The timeline could not resolve a frame at a content coordinate.");
        const auto anchored_x = widget.contentXForFrame(*anchor_frame);
        require(std::abs(anchored_x - 600.0) < 2.0,
                "Timeline frame/content coordinate conversion lost its anchor.");
        for (const auto level : {0.25, 0.50, 0.75, 1.00, 1.25, 1.50,
                                 2.00, 3.00, 4.00, 6.00, 8.00}) {
            widget.setZoomFactor(level);
            require(widget.zoomFactor() == level,
                    "A documented timeline zoom level was not applied.");
        }
        widget.setZoomFactor(1.0);
        widget.resize(1000, 500);
        application.processEvents();

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
                    seek_frames.front() < test_clip_duration,
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

        // The user can disable the Alt requirement and move with a normal drag.
        require(widget.moveRequiresAlt(),
                "The timeline movement preference could not be enabled.");
        widget.setMoveRequiresAlt(false);
        require(!widget.moveRequiresAlt(),
                "The timeline movement preference could not be disabled.");
        move_from_track = -1;
        move_to_track = -1;
        widget.setActiveClip(std::nullopt);
        sendMouse(widget, QEvent::MouseButtonPress, QPointF(500, 120),
                  Qt::LeftButton);
        sendMouse(widget, QEvent::MouseButtonRelease, QPointF(500, 120),
                  Qt::NoButton);
        require(selected_track == 0 && selected_clip == 0 &&
                    move_from_track == -1 && move_to_track == -1,
                "A normal click in movement mode did not select without moving.");
        sendMouse(widget, QEvent::MouseButtonPress, QPointF(500, 120),
                  Qt::LeftButton);
        sendMouse(widget, QEvent::MouseMove, QPointF(500, 320),
                  Qt::LeftButton);
        sendMouse(widget, QEvent::MouseButtonRelease, QPointF(500, 320),
                  Qt::NoButton);
        require(move_from_track == 0 && move_to_track == 1,
                "A normal drag did not move the clip after disabling Alt requirement.");
        require(seek_frames.size() == 1,
                "A normal move drag was incorrectly treated as seeking.");

        // Alt becomes the seek override when movement no longer requires it.
        sendMouse(widget, QEvent::MouseButtonPress, QPointF(400, 120),
                  Qt::LeftButton, Qt::AltModifier);
        sendMouse(widget, QEvent::MouseMove, QPointF(700, 120),
                  Qt::LeftButton, Qt::AltModifier);
        sendMouse(widget, QEvent::MouseButtonRelease, QPointF(700, 120),
                  Qt::NoButton, Qt::AltModifier);
        require(seek_frames.size() == 2 && seek_frames.back() > 0 &&
                    seek_frames.back() < test_clip_duration,
                "Alt-drag did not seek after disabling the movement requirement.");

        // Blade Tool uses a click, not a drag, and reports the local frame.
        widget.setRazorMode(true);
        sendMouse(widget, QEvent::MouseButtonPress, QPointF(500, 120),
                  Qt::LeftButton);
        sendMouse(widget, QEvent::MouseButtonRelease, QPointF(500, 120),
                  Qt::NoButton);
        require(split_count == 1 && split_frame > 0 && split_frame < test_clip_duration,
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
        require(trim_start > 0 && trim_end == test_clip_duration,
                "The left edge did not request a bounded trim.");

        // A contiguous junction is selectable through the same hit area used
        // by the transition context menu.
        timeline::TimelineTrack junction_track{
            1,
            "Video 1",
            1.0,
            false,
            {makeClip("first.mkv", 0, test_clip_duration, "first.mkv"),
             makeClip("second.mkv", test_clip_duration, test_clip_duration, "second.mkv")}};
        widget.setTracks({junction_track});
        widget.setActiveClip(std::nullopt);
        application.processEvents();
        sendMouse(widget, QEvent::MouseButtonPress, QPointF(568, 120),
                  Qt::LeftButton);
        sendMouse(widget, QEvent::MouseButtonRelease, QPointF(568, 120),
                  Qt::NoButton);
        require(transition_track == 0 && transition_from == 0 && transition_to == 1,
                "The contiguous junction was not detected for transition selection.");

        // The viewport is the scale reference for the standard one-hour
        // range, while longer content expands the scrollable surface.
        widget.setTimelineViewportWidth(800);
        widget.setTracks({top_track, lower_track});
        require(widget.minimumWidth() == 800,
                "The standard timeline width did not follow the viewport.");
        const auto long_track = timeline::TimelineTrack{
            1,
            "Video 1",
            1.0,
            false,
            {makeClip("long.mkv", 0, 216000, "long.mkv")}};
        widget.setTracks({long_track});
        require(widget.minimumWidth() >= 1600,
                "A timeline longer than one hour did not expand horizontally.");
        widget.setTracks({top_track, lower_track});
        require(widget.minimumWidth() == 800,
                "The timeline did not return to the standard width after shrinking.");

        widget.close();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
