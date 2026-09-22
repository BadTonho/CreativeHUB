#include "timeline/timeline_widget.h"
#include "ui/media_drag_mime.h"

#include <QApplication>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QImage>
#include <QMimeData>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QScrollArea>
#include <QScrollBar>
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
    Qt::KeyboardModifiers modifiers = Qt::NoModifier,
    const QPoint& pixel_delta = QPoint()) {
    QWheelEvent event(
        position,
        position,
        pixel_delta,
        QPoint(0, angle_delta),
        Qt::NoButton,
        modifiers,
        Qt::NoScrollPhase,
        false,
        Qt::MouseEventNotSynthesized);
    QApplication::sendEvent(&widget, &event);
}

int countRulerGuides(const QImage& image, int first_x, int last_x, int y) {
    const auto background = QColor("#171a20");
    int guide_count = 0;
    for (int x = first_x; x <= last_x; ++x) {
        if (image.pixelColor(x, y) != background) ++guide_count;
    }
    return guide_count;
}

bool isPlayheadPixel(const QColor& color) {
    return color.red() > 220 && color.green() > 170 && color.blue() < 150;
}

} // namespace

int main(int argc, char* argv[]) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication application(argc, argv);

    try {
        timeline::TimelineWidget widget;
        require(widget.trackRowHeight() == 70.0,
                "A new Timeline widget did not start with the 70-pixel default row height.");
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

        // Keep the drag/drop coordinate below independent from the default row
        // height so the behavior test remains focused on track/frame mapping.
        widget.setTrackRowHeight(timeline::kMaximumTrackRowHeight);
        application.processEvents();

        widget.grabMouse();
        if (QWidget::mouseGrabber() == &widget) {
            widget.setTracks({top_track, lower_track});
            require(
                QWidget::mouseGrabber() != &widget,
                "Replacing timeline tracks must release a stale mouse grab.");
        }

        bool effect_drop_received = false;
        qint64 effect_drop_track = -1;
        qint64 effect_drop_frame = -1;
        QObject::connect(
            &widget,
            &timeline::TimelineWidget::effectDropRequestedAt,
            [&effect_drop_received, &effect_drop_track, &effect_drop_frame](
                const QString& effect_id,
                qint64 track_index,
                qint64 timeline_frame) {
                effect_drop_received = effect_id == "text.text";
                effect_drop_track = track_index;
                effect_drop_frame = timeline_frame;
            });
        const QPointF effect_drop_position(500.0, 120.0);
        QMimeData text_effect_mime;
        text_effect_mime.setData(
            ui::kEffectIdMimeType,
            QByteArrayLiteral("text.text"));
        QDragEnterEvent effect_enter(
            effect_drop_position.toPoint(),
            Qt::CopyAction,
            &text_effect_mime,
            Qt::LeftButton,
            Qt::NoModifier);
        QApplication::sendEvent(&widget, &effect_enter);
        QDragMoveEvent effect_move(
            effect_drop_position.toPoint(),
            Qt::CopyAction,
            &text_effect_mime,
            Qt::LeftButton,
            Qt::NoModifier);
        QApplication::sendEvent(&widget, &effect_move);
        QDropEvent effect_drop(
            effect_drop_position,
            Qt::CopyAction,
            &text_effect_mime,
            Qt::LeftButton,
            Qt::NoModifier);
        QApplication::sendEvent(&widget, &effect_drop);
        require(effect_drop_received && effect_drop_track == 0 &&
                    effect_drop_frame > 0,
                "Text effect drop did not preserve the target track and frame.");

        bool media_drop_received = false;
        QString media_drop_path;
        qint64 media_drop_track = -1;
        qint64 media_drop_frame = -1;
        QObject::connect(
            &widget,
            &timeline::TimelineWidget::mediaDropRequestedAt,
            [&media_drop_received, &media_drop_path, &media_drop_track,
             &media_drop_frame](
                const QString& path,
                qint64 track,
                qint64 frame) {
                media_drop_received = true;
                media_drop_path = path;
                media_drop_track = track;
                media_drop_frame = frame;
            });
        QMimeData media_mime;
        media_mime.setData(
            ui::kMediaPathMimeType,
            QByteArrayLiteral("sample.mp4"));
        require(
            media_mime.hasFormat(ui::kMediaPathMimeType),
            "Media test MIME was not initialized.");
        QDragEnterEvent media_enter(
            effect_drop_position.toPoint(),
            Qt::CopyAction,
            &media_mime,
            Qt::LeftButton,
            Qt::NoModifier);
        QApplication::sendEvent(&widget, &media_enter);
        QDragMoveEvent media_move(
            effect_drop_position.toPoint(),
            Qt::CopyAction,
            &media_mime,
            Qt::LeftButton,
            Qt::NoModifier);
        QApplication::sendEvent(&widget, &media_move);
        QDropEvent media_drop(
            effect_drop_position,
            Qt::CopyAction,
            &media_mime,
            Qt::LeftButton,
            Qt::NoModifier);
        QApplication::sendEvent(&widget, &media_drop);
        require(
            media_drop.isAccepted(),
            "Direct media drop was not accepted by TimelineWidget.");
        require(
            media_drop_received,
            "Direct media drop did not emit the media signal.");
        require(
            media_drop_path == "sample.mp4" && media_drop_track == 0 &&
                media_drop_frame > 0,
            "Media drop did not preserve the source path and target position.");

        QDropEvent header_media_drop(
            QPointF(50.0, effect_drop_position.y()),
            Qt::CopyAction,
            &media_mime,
            Qt::LeftButton,
            Qt::NoModifier);
        QApplication::sendEvent(&widget, &header_media_drop);
        require(
            !header_media_drop.isAccepted(),
            "Timeline accepted a media drop on the track header.");

        QScrollArea scroll_area;
        scroll_area.resize(360, 220);
        scroll_area.setWidgetResizable(true);
        auto* viewport_timeline = new timeline::TimelineWidget;
        viewport_timeline->setTracks({lower_track});
        viewport_timeline->setZoomFactor(2.0);
        viewport_timeline->setAcceptDrops(false);
        scroll_area.setWidget(viewport_timeline);
        scroll_area.setAcceptDrops(true);
        scroll_area.viewport()->setAcceptDrops(true);
        scroll_area.viewport()->installEventFilter(viewport_timeline);
        scroll_area.show();
        application.processEvents();
        viewport_timeline->setTrackRowHeight(timeline::kMinimumTrackRowHeight);
        application.processEvents();
        require(scroll_area.verticalScrollBar()->maximum() == 0,
                "The minimum Timeline row height unexpectedly required vertical scrolling.");
        viewport_timeline->setTrackRowHeight(timeline::kMaximumTrackRowHeight);
        application.processEvents();
        require(scroll_area.verticalScrollBar()->maximum() > 0,
                "Increasing Timeline row height did not use the vertical scroll area.");
        viewport_timeline->setTimelineViewportWidth(
            scroll_area.viewport()->width());
        scroll_area.horizontalScrollBar()->setValue(100);
        application.processEvents();

        bool viewport_media_drop_received = false;
        qint64 viewport_media_drop_track = -1;
        qint64 viewport_media_drop_frame = -1;
        QObject::connect(
            viewport_timeline,
            &timeline::TimelineWidget::mediaDropRequestedAt,
            [&viewport_media_drop_received, &viewport_media_drop_track,
             &viewport_media_drop_frame](
                const QString&,
                qint64 track,
                qint64 frame) {
                viewport_media_drop_received = true;
                viewport_media_drop_track = track;
                viewport_media_drop_frame = frame;
            });

        const QPoint viewport_drop_position(220, 80);
        QDragEnterEvent viewport_enter(
            viewport_drop_position,
            Qt::CopyAction,
            &media_mime,
            Qt::LeftButton,
            Qt::NoModifier);
        QApplication::sendEvent(
            scroll_area.viewport(),
            &viewport_enter);
        QDragMoveEvent viewport_move(
            viewport_drop_position,
            Qt::CopyAction,
            &media_mime,
            Qt::LeftButton,
            Qt::NoModifier);
        QApplication::sendEvent(
            scroll_area.viewport(),
            &viewport_move);
        QDropEvent viewport_drop(
            QPointF(viewport_drop_position),
            Qt::CopyAction,
            &media_mime,
            Qt::LeftButton,
            Qt::NoModifier);
        QApplication::sendEvent(
            scroll_area.viewport(),
            &viewport_drop);
        require(
            viewport_media_drop_received && viewport_media_drop_track == 0 &&
                viewport_media_drop_frame > 0,
            "Media drop received by the QScrollArea viewport was not forwarded.");
        require(
            viewport_timeline->mapFrom(
                scroll_area.viewport(),
                viewport_drop_position).x() > viewport_drop_position.x(),
            "The viewport test did not exercise horizontal coordinate conversion.");
        scroll_area.close();

        QMimeData invalid_mime;
        invalid_mime.setData(
            "application/x-creative-suite-unknown",
            QByteArrayLiteral("invalid"));
        QDropEvent invalid_drop(
            effect_drop_position,
            Qt::CopyAction,
            &invalid_mime,
            Qt::LeftButton,
            Qt::NoModifier);
        QApplication::sendEvent(&widget, &invalid_drop);
        require(!invalid_drop.isAccepted(),
                "Timeline accepted an unsupported effect drop.");

        widget.setTrackRowHeight(timeline::kDefaultTrackRowHeight);

        require(widget.minimumHeight() >=
                    48 + 2 * static_cast<int>(timeline::kDefaultTrackRowHeight) +
                        10 + 12,
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
            [&zoom_requests, &requested_zoom](double factor) {
                ++zoom_requests;
                requested_zoom = factor;
            });

        sendWheel(widget, QPointF(600, 120), 120, Qt::ControlModifier);
        require(zoom_requests == 1 && requested_zoom == 1.25,
                "Ctrl + wheel did not request the next timeline zoom level.");
        require(widget.trackRowHeight() == timeline::kDefaultTrackRowHeight,
                "Ctrl + wheel unexpectedly changed the Timeline row height.");
        sendWheel(widget, QPointF(600, 120), -120);
        require(zoom_requests == 1,
                "Normal wheel scrolling was incorrectly treated as timeline zoom.");
        require(widget.trackRowHeight() == timeline::kDefaultTrackRowHeight,
                "Normal wheel scrolling unexpectedly changed the Timeline row height.");
        sendWheel(
            widget,
            QPointF(600, 120),
            120,
            Qt::ControlModifier | Qt::ShiftModifier);
        require(zoom_requests == 2 &&
                    requested_zoom == 1.25 &&
                    widget.trackRowHeight() == timeline::kDefaultTrackRowHeight,
                "Ctrl + Shift + wheel did not preserve the existing zoom behavior.");
        const auto frame_before_row_resize = widget.frameAtContentX(600.0);
        const auto width_before_row_resize = widget.minimumWidth();
        widget.setTrackRowHeight(120.0);
        sendWheel(widget, QPointF(600, 120), 0, Qt::ShiftModifier, QPoint(0, 12));
        require(std::abs(widget.trackRowHeight() - 132.0) < 0.000001,
                "Shift + pixel wheel did not adjust the Timeline row height smoothly.");
        sendWheel(widget, QPointF(600, 120), 120, Qt::ShiftModifier);
        require(std::abs(widget.trackRowHeight() - 147.0) < 0.000001,
                "Shift + angle wheel did not use the smooth angle fallback.");
        sendWheel(widget, QPointF(600, 120), -120, Qt::ShiftModifier);
        require(std::abs(widget.trackRowHeight() - 132.0) < 0.000001,
                "Shift + angle wheel did not reduce the Timeline row height.");
        widget.setTrackRowHeight(999.0);
        require(widget.trackRowHeight() == timeline::kMaximumTrackRowHeight,
                "Timeline row height did not clamp the upper bound.");
        widget.setTrackRowHeight(1.0);
        require(widget.trackRowHeight() == timeline::kMinimumTrackRowHeight,
                "Timeline row height did not clamp the lower bound.");
        require(widget.minimumHeight() >= 48 + 2 * 30 + 10 + 12,
                "Timeline minimum height did not include every track row.");
        require(widget.minimumWidth() == width_before_row_resize &&
                    widget.frameAtContentX(600.0) == frame_before_row_resize,
                "Changing row height altered horizontal Timeline geometry.");
        widget.setTrackRowHeight(timeline::kDefaultTrackRowHeight);
        widget.setZoomFactor(0.25);
        require(widget.zoomFactor() == 0.25 && !widget.canZoomOut() &&
                    widget.minimumWidth() == 1000,
                "The minimum timeline zoom did not preserve the viewport width.");
        widget.setZoomFactor(512.0);
        require(widget.zoomFactor() == 512.0 && !widget.canZoomIn() &&
                    widget.minimumWidth() == 512000,
                "The maximum timeline zoom did not expand the timeline surface.");
        widget.setZoomFactor(999.0);
        require(widget.zoomFactor() == 512.0,
                "Timeline zoom did not clamp values above the frame-level maximum.");
        const auto anchor_frame = widget.frameAtContentX(600.0);
        require(anchor_frame.has_value(),
                "The timeline could not resolve a frame at a content coordinate.");
        const auto anchored_x = widget.contentXForFrame(*anchor_frame);
        require(std::abs(anchored_x - 600.0) < 2.0,
                "Timeline frame/content coordinate conversion lost its anchor.");
        for (const auto level : timeline::kTimelineZoomLevels) {
            widget.setZoomFactor(level);
            require(widget.zoomFactor() == level,
                    "A documented timeline zoom level was not applied.");
        }
        const auto frame_zero_x = widget.contentXForFrame(0);
        const auto frame_one_x = widget.contentXForFrame(1);
        require(frame_one_x - frame_zero_x >= 1.0,
                "Frame-level zoom did not separate consecutive frames.");

        timeline::TimelineWidget frame_grid_widget;
        frame_grid_widget.resize(400, 300);
        frame_grid_widget.setTimelineViewportWidth(400);
        frame_grid_widget.setTracks({timeline::TimelineTrack{
            1, "Video 1", 1.0, false,
            {makeClip("grid.mkv", 0, 30, "grid")}}});
        frame_grid_widget.setZoomFactor(512.0);
        // Keep the offscreen render small while preserving the frame density
        // needed for this deterministic paint check.
        frame_grid_widget.setMinimumWidth(400);
        frame_grid_widget.resize(400, 300);
        frame_grid_widget.show();
        application.processEvents();

        const auto render_ruler = [&](double zoom) {
            frame_grid_widget.setZoomFactor(zoom);
            frame_grid_widget.setMinimumWidth(400);
            frame_grid_widget.resize(400, 300);
            application.processEvents();
            QImage image(400, 300, QImage::Format_ARGB32);
            image.fill(Qt::transparent);
            frame_grid_widget.render(&image);
            return countRulerGuides(image, 154, 394, 13);
        };
        const auto guides_at_25_percent = render_ruler(0.25);
        const auto guides_at_100_percent = render_ruler(1.0);
        const auto guides_at_400_percent = render_ruler(4.0);
        const auto guides_at_frame_level = render_ruler(512.0);
        require(guides_at_25_percent > 0 &&
                    guides_at_100_percent >= guides_at_25_percent - 2 &&
                    guides_at_400_percent >= guides_at_100_percent - 2 &&
                    guides_at_frame_level > guides_at_400_percent,
                "Timeline ruler guides did not become denser with zoom.");

        QImage frame_grid_image(400, 300, QImage::Format_ARGB32);
        frame_grid_image.fill(Qt::transparent);
        frame_grid_widget.render(&frame_grid_image);
        const auto ruler_background = frame_grid_image.pixelColor(210, 13);
        int frame_grid_pixels = 0;
        for (int x = 154; x < 195; ++x) {
            if (frame_grid_image.pixelColor(x, 13) != ruler_background) {
                ++frame_grid_pixels;
            }
        }
        require(frame_grid_pixels >= 4,
                "Frame-level zoom did not render individual frame guides in the ruler.");
        const auto track_background = frame_grid_image.pixelColor(180, 100);
        int track_grid_pixels = 0;
        for (int x = 165; x < 185; ++x) {
            if (frame_grid_image.pixelColor(x, 100) != track_background) {
                ++track_grid_pixels;
            }
        }
        require(track_grid_pixels == 0,
                "Frame-level guides must not be drawn across timeline clips.");
        frame_grid_widget.close();

        timeline::TimelineWidget clip_height_widget;
        clip_height_widget.resize(500, 220);
        clip_height_widget.setTimelineViewportWidth(500);
        clip_height_widget.setTracks({timeline::TimelineTrack{
            1, "Video 1", 1.0, false,
            {makeClip("clip-height.mkv", 0, 1800, "clip-height")}}});
        clip_height_widget.setZoomFactor(60.0);
        clip_height_widget.setMinimumWidth(500);
        clip_height_widget.resize(500, 220);
        clip_height_widget.show();
        application.processEvents();
        QImage clip_height_image(500, 220, QImage::Format_ARGB32);
        clip_height_image.fill(Qt::transparent);
        clip_height_widget.render(&clip_height_image);
        const auto clip_probe_x = 220;
        const auto clip_track_background = QColor("#202631");
        const auto top_clip_pixel = clip_height_image.pixelColor(clip_probe_x, 51);
        const auto middle_clip_pixel = clip_height_image.pixelColor(clip_probe_x, 100);
        const auto bottom_clip_pixel = clip_height_image.pixelColor(clip_probe_x, 205);
        require(top_clip_pixel != clip_track_background &&
                    bottom_clip_pixel != clip_track_background,
                "Timeline clips must fill the track row without vertical margins: top=" +
                    top_clip_pixel.name().toStdString() + " middle=" +
                    middle_clip_pixel.name().toStdString() + " bottom=" +
                    bottom_clip_pixel.name().toStdString() + " size=" +
                    std::to_string(clip_height_widget.width()) + "x" +
                    std::to_string(clip_height_widget.height()));
        clip_height_widget.close();

        timeline::TimelineWidget playhead_widget;
        playhead_widget.resize(400, 300);
        playhead_widget.setTimelineViewportWidth(400);
        playhead_widget.setTracks({timeline::TimelineTrack{
            1, "Video 1", 1.0, false,
            {makeClip("playhead.mkv", 0, 30, "playhead")}}});
        playhead_widget.setZoomFactor(512.0);
        playhead_widget.setMinimumWidth(400);
        playhead_widget.resize(400, 300);
        playhead_widget.setActiveClip(timeline::ClipLocation{0, 0});
        playhead_widget.show();
        application.processEvents();
        playhead_widget.setPlayheadFrame(5);
        const auto ruler_frame_x = playhead_widget.contentXForFrame(15);
        sendMouse(playhead_widget, QEvent::MouseButtonPress,
                  QPointF(ruler_frame_x, 25), Qt::LeftButton);
        sendMouse(playhead_widget, QEvent::MouseButtonRelease,
                  QPointF(ruler_frame_x, 25), Qt::NoButton);
        playhead_widget.setPlayheadFrame(18);
        application.processEvents();
        QImage playhead_image(400, 300, QImage::Format_ARGB32);
        playhead_image.fill(Qt::transparent);
        playhead_widget.render(&playhead_image);
        const auto live_playhead_x = static_cast<int>(std::lround(
            playhead_widget.contentXForFrame(18)));
        const auto stale_ruler_x = static_cast<int>(std::lround(
            playhead_widget.contentXForFrame(15)));
        require(isPlayheadPixel(playhead_image.pixelColor(live_playhead_x, 100)) &&
                    !isPlayheadPixel(playhead_image.pixelColor(stale_ruler_x, 100)),
                "A stale ruler seek position prevented the live playhead from advancing.");
        playhead_widget.close();
        widget.setZoomFactor(1.0);
        widget.setTrackRowHeight(timeline::kMaximumTrackRowHeight);
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

        // The upper time ruler seeks the playhead without selecting or moving
        // a clip, and it clamps the request to the real project duration.
        widget.setActiveClip(std::nullopt);
        const auto selected_track_before_ruler = selected_track;
        const auto selected_clip_before_ruler = selected_clip;
        const auto seek_started_before_ruler = seek_started;
        const auto seek_count_before_ruler = seek_frames.size();
        sendMouse(widget, QEvent::MouseButtonPress, QPointF(300, 25),
                  Qt::LeftButton);
        sendMouse(widget, QEvent::MouseMove, QPointF(450, 25),
                  Qt::LeftButton);
        require(seek_started == seek_started_before_ruler + 1 &&
                    seek_frames.size() == seek_count_before_ruler,
                "Dragging the time ruler committed a seek before release.");
        sendMouse(widget, QEvent::MouseButtonRelease, QPointF(450, 25),
                  Qt::NoButton);
        require(seek_frames.size() == seek_count_before_ruler + 1 &&
                    seek_frames.back() > 0 &&
                    seek_frames.back() < test_clip_duration &&
                    selected_track == selected_track_before_ruler &&
                    selected_clip == selected_clip_before_ruler,
                "Dragging the time ruler did not seek without selecting a clip.");

        // Ruler requests use absolute timeline frames even when the clip does
        // not start at frame zero.
        const auto offset_track = timeline::TimelineTrack{
            1,
            "Video 1",
            1.0,
            false,
            {makeClip("offset.mkv", 600, test_clip_duration, "offset.mkv")}};
        widget.setTracks({offset_track});
        widget.setActiveClip(timeline::ClipLocation{0, 0});
        const auto absolute_target = std::int64_t{12000};
        const auto target_x = widget.contentXForFrame(absolute_target);
        const auto seek_count_before_offset = seek_frames.size();
        sendMouse(widget, QEvent::MouseButtonPress, QPointF(target_x, 25),
                  Qt::LeftButton);
        sendMouse(widget, QEvent::MouseButtonRelease, QPointF(target_x, 25),
                  Qt::NoButton);
        require(seek_frames.size() == seek_count_before_offset + 1 &&
                    std::abs(seek_frames.back() - absolute_target) <= 1,
                "The time ruler did not report an absolute timeline frame.");

        widget.close();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
