#include "timeline/timeline_widget.h"
#include "timeline/timeline_geometry.h"
#include "timeline/timeline_track_header_overlay.h"
#include "timeline/timeline_time.h"
#include "ui/media_browser/media_drag_mime.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QMimeData>
#include <QMenu>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QTemporaryDir>
#include <QUrl>
#include <QWheelEvent>

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
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
    static timeline::ClipId next_clip_id = 1;
    clip.clip_id = next_clip_id++;
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

bool hasChangedPixel(
    const QImage& image,
    const QImage& baseline,
    const QPoint& center,
    int horizontal_radius,
    int vertical_radius) {
    for (int y = std::max(0, center.y() - vertical_radius);
         y <= std::min(image.height() - 1, center.y() + vertical_radius); ++y) {
        for (int x = std::max(0, center.x() - horizontal_radius);
             x <= std::min(image.width() - 1, center.x() + horizontal_radius); ++x) {
            if (image.pixelColor(x, y) != baseline.pixelColor(x, y)) return true;
        }
    }
    return false;
}

void testAudioWaveformRendering(QApplication& application) {
    QScrollArea scroll;
    scroll.resize(1200, 320);
    auto* widget = new timeline::TimelineWidget;
    widget->resize(1200, 300);
    widget->setFrameRate({30, 1});
    widget->setTimelineViewportWidth(1200);
    widget->setZoomFactor(30.0);
    require(!widget->stereoWaveformDisplayEnabled(),
            "The Timeline waveform mode must default to Mono.");

    auto clip = makeClip("waveform.wav", 0, 1800, "waveform.wav");
    clip.kind = timeline::ClipKind::Audio;
    clip.source_start_time_us = 500'000;
    clip.source_duration_time_us = 60'000'000;
    timeline::TimelineTrack audio_track{1, "Audio 1", 1.0, false, {clip}};
    audio_track.kind = timeline::TrackKind::Audio;
    widget->setTracks({audio_track});

    auto waveform = std::make_shared<media::AudioWaveform>();
    waveform->peaks.resize(6100, media::AudioWaveformPeak{});
    std::fill(waveform->peaks.begin() + 50, waveform->peaks.begin() + 70,
              media::AudioWaveformPeak{220, 220});
    widget->setAudioWaveform(clip.source_path, waveform);
    scroll.setWidgetResizable(false);
    scroll.setWidget(widget);
    scroll.show();
    application.processEvents();

    const std::vector<timeline::TimelineTrack> audio_tracks{audio_track};
    timeline::TimelineGeometry geometry(
        audio_tracks,
        QSizeF(widget->size()),
        widget->trackRowHeight(),
        widget->zoomFactor(),
        std::nullopt,
        30.0);
    const auto clip_bounds = geometry.clipRect(clip, 0);
    const auto y = static_cast<int>(std::round(
        widget->trackBounds(0).center().y() - 15.0));
    const auto source_offset_point = widget->mapTo(
        scroll.viewport(), QPoint(static_cast<int>(clip_bounds.left()) + 2, y));
    const auto later_silence_point = widget->mapTo(
        scroll.viewport(), QPoint(static_cast<int>(clip_bounds.left()) + 100, y));
    const auto audio_image = scroll.viewport()->grab().toImage();
    widget->clearAudioWaveforms();
    application.processEvents();
    const auto silent_audio_image = scroll.viewport()->grab().toImage();
    require(hasChangedPixel(audio_image, silent_audio_image, source_offset_point, 2, 2),
            "The audio waveform did not render at the clip's source in-point.");
    require(!hasChangedPixel(audio_image, silent_audio_image, later_silence_point, 2, 2),
            "The audio waveform did not follow the source-time range.");

    clip.timeline_duration_frames = 150;
    clip.source_start_time_us = 1'000'000;
    clip.source_duration_time_us = 5'000'000;
    audio_track.clips = {clip};
    widget->setZoomFactor(15.0);
    widget->setTrackRowHeight(100.0);
    widget->setTracks({audio_track});
    waveform->peaks.assign(700, media::AudioWaveformPeak{});
    std::fill(waveform->peaks.begin() + 100, waveform->peaks.begin() + 120,
              media::AudioWaveformPeak{220, 220});
    widget->setAudioWaveform(clip.source_path, waveform);
    application.processEvents();
    const std::vector<timeline::TimelineTrack> trimmed_audio_tracks{audio_track};
    const timeline::TimelineGeometry trimmed_geometry(
        trimmed_audio_tracks,
        QSizeF(widget->size()),
        widget->trackRowHeight(),
        widget->zoomFactor(),
        std::nullopt,
        30.0);
    const auto trimmed_bounds = trimmed_geometry.clipRect(clip, 0);
    const auto trimmed_y = static_cast<int>(
        std::round(widget->trackBounds(0).center().y() - 15.0));
    const auto trimmed_source_point = widget->mapTo(
        scroll.viewport(), QPoint(static_cast<int>(trimmed_bounds.left()) + 2, trimmed_y));
    const auto trimmed_silence_point = widget->mapTo(
        scroll.viewport(), QPoint(static_cast<int>(trimmed_bounds.left()) + 100, trimmed_y));
    const auto trimmed_image = scroll.viewport()->grab().toImage();
    widget->clearAudioWaveforms();
    application.processEvents();
    const auto silent_trimmed_image = scroll.viewport()->grab().toImage();
    require(hasChangedPixel(trimmed_image, silent_trimmed_image,
                            trimmed_source_point, 2, 2),
            "A trimmed clip did not render the waveform at its updated source in-point.");
    require(!hasChangedPixel(trimmed_image, silent_trimmed_image,
                             trimmed_silence_point, 2, 2),
            "A trimmed clip rendered waveform peaks outside its source-time range.");

    widget->setZoomFactor(5.0);
    application.processEvents();
    const timeline::TimelineGeometry zoomed_out_geometry(
        trimmed_audio_tracks,
        QSizeF(widget->size()),
        widget->trackRowHeight(),
        widget->zoomFactor(),
        std::nullopt,
        30.0);
    auto zoomed_out_bounds = zoomed_out_geometry.clipRect(clip, 0);
    zoomed_out_bounds.moveTop(widget->trackBounds(0).top());
    const auto zoomed_out_y = static_cast<int>(
        std::round(zoomed_out_bounds.center().y() - 15.0));
    const auto peak_pixel_x = static_cast<int>(zoomed_out_bounds.left()) + 2;
    const auto peak_pixel_begin_fraction =
        (static_cast<double>(peak_pixel_x) - zoomed_out_bounds.left()) /
        zoomed_out_bounds.width();
    const auto peak_pixel_end_fraction =
        (static_cast<double>(peak_pixel_x + 1) - zoomed_out_bounds.left()) /
        zoomed_out_bounds.width();
    const auto peak_pixel_center_fraction =
        (static_cast<double>(peak_pixel_x) + 0.5 - zoomed_out_bounds.left()) /
        zoomed_out_bounds.width();
    const auto peak_pixel_first_bucket = static_cast<std::size_t>(
        (clip.source_start_time_us + peak_pixel_begin_fraction *
            clip.source_duration_time_us) / 10'000.0);
    const auto peak_pixel_end_bucket = static_cast<std::size_t>(std::ceil(
        (clip.source_start_time_us + peak_pixel_end_fraction *
            clip.source_duration_time_us) / 10'000.0));
    const auto peak_pixel_center_bucket = static_cast<std::size_t>(
        (clip.source_start_time_us + peak_pixel_center_fraction *
            clip.source_duration_time_us) / 10'000.0);
    const auto aggregated_bucket = peak_pixel_first_bucket != peak_pixel_center_bucket
        ? peak_pixel_first_bucket : peak_pixel_end_bucket - 1;
    require(aggregated_bucket != peak_pixel_center_bucket && aggregated_bucket < 700,
            "The zoomed-out test did not select a peak missed by center-only sampling.");
    waveform->peaks.assign(700, media::AudioWaveformPeak{});
    waveform->peaks[aggregated_bucket] = media::AudioWaveformPeak{255, 255};
    widget->setAudioWaveform(clip.source_path, waveform);
    application.processEvents();
    const auto aggregated_peak_point = widget->mapTo(
        scroll.viewport(),
        QPoint(peak_pixel_x, zoomed_out_y));
    const auto zoomed_out_image = scroll.viewport()->grab().toImage();
    widget->clearAudioWaveforms();
    application.processEvents();
    const auto silent_zoomed_out_image = scroll.viewport()->grab().toImage();
    require(hasChangedPixel(zoomed_out_image, silent_zoomed_out_image,
                            aggregated_peak_point, 1, 2),
            "Zooming out hid a waveform peak that fell inside the visible pixel interval.");

    waveform->source_channel_count = 2;
    waveform->peaks.assign(700, media::AudioWaveformPeak{});
    std::fill(waveform->peaks.begin() + 100, waveform->peaks.begin() + 120,
              media::AudioWaveformPeak{220, 0});
    widget->setStereoWaveformDisplayEnabled(true);
    require(widget->stereoWaveformDisplayEnabled(),
            "The Timeline did not enable stereo waveform display.");
    widget->setAudioWaveform(clip.source_path, waveform);
    application.processEvents();
    const auto stereo_image = scroll.viewport()->grab().toImage();
    widget->clearAudioWaveforms();
    application.processEvents();
    const auto empty_stereo_image = scroll.viewport()->grab().toImage();
    const auto upper_channel_point = widget->mapTo(
        scroll.viewport(),
        QPoint(peak_pixel_x, static_cast<int>(std::round(
            zoomed_out_bounds.top() + zoomed_out_bounds.height() * 0.25))));
    const auto lower_channel_point = widget->mapTo(
        scroll.viewport(),
        QPoint(peak_pixel_x, static_cast<int>(std::round(
            zoomed_out_bounds.top() + zoomed_out_bounds.height() * 0.75))));
    require(hasChangedPixel(stereo_image, empty_stereo_image,
                            upper_channel_point, 1, 2),
            "The left channel did not render in the upper stereo waveform half.");
    require(!hasChangedPixel(stereo_image, empty_stereo_image,
                             lower_channel_point, 1, 2),
            "The left-channel signal leaked into the lower/right stereo half.");

    waveform->peaks.assign(700, media::AudioWaveformPeak{});
    std::fill(waveform->peaks.begin() + 100, waveform->peaks.begin() + 120,
              media::AudioWaveformPeak{0, 220});
    widget->setAudioWaveform(clip.source_path, waveform);
    application.processEvents();
    const auto right_channel_image = scroll.viewport()->grab().toImage();
    widget->clearAudioWaveforms();
    application.processEvents();
    const auto empty_right_channel_image = scroll.viewport()->grab().toImage();
    require(hasChangedPixel(right_channel_image, empty_right_channel_image,
                            lower_channel_point, 1, 2),
            "The right channel did not render in the lower stereo waveform half.");
    require(!hasChangedPixel(right_channel_image, empty_right_channel_image,
                             upper_channel_point, 1, 2),
            "The right-channel signal leaked into the upper/left stereo half.");

    waveform->source_channel_count = 1;
    waveform->peaks.assign(700, media::AudioWaveformPeak{});
    std::fill(waveform->peaks.begin() + 100, waveform->peaks.begin() + 120,
              media::AudioWaveformPeak{220, 220});
    widget->setAudioWaveform(clip.source_path, waveform);
    application.processEvents();
    const auto mono_source_image = scroll.viewport()->grab().toImage();
    widget->clearAudioWaveforms();
    application.processEvents();
    const auto empty_mono_source_image = scroll.viewport()->grab().toImage();
    const auto centered_waveform_point = widget->mapTo(
        scroll.viewport(), QPoint(peak_pixel_x, zoomed_out_y));
    require(hasChangedPixel(mono_source_image, empty_mono_source_image,
                            centered_waveform_point, 1, 2),
            "A mono source did not keep one centered waveform in stereo display mode.");
    widget->setStereoWaveformDisplayEnabled(false);
    require(!widget->stereoWaveformDisplayEnabled(),
            "The Timeline did not return to Mono waveform display.");

    auto video_clip = clip;
    video_clip.kind = timeline::ClipKind::Video;
    timeline::TimelineTrack video_track{2, "Video 1", 1.0, false, {video_clip}};
    widget->setTracks({video_track});
    widget->setAudioWaveform(clip.source_path, waveform);
    application.processEvents();
    const auto video_image = scroll.viewport()->grab().toImage();
    widget->clearAudioWaveforms();
    application.processEvents();
    const auto silent_video_image = scroll.viewport()->grab().toImage();
    const auto video_source_offset_point = widget->mapTo(
        scroll.viewport(),
        QPoint(static_cast<int>(zoomed_out_bounds.left()) + 2, zoomed_out_y));
    require(!hasChangedPixel(video_image, silent_video_image,
                             video_source_offset_point, 2, 2),
            "A visual clip rendered an audio waveform.");
}

void testAudioGainEnvelopeTool(QApplication& application) {
    timeline::TimelineWidget widget;
    widget.resize(1200, 180);
    widget.setFrameRate({30, 1});
    widget.setTimelineViewportWidth(1200);
    widget.setZoomFactor(15.0);
    auto clip = makeClip("volume-curve.wav", 0, 150, "volume-curve.wav");
    clip.kind = timeline::ClipKind::Audio;
    clip.source_duration_time_us = 5'000'000;
    timeline::TimelineTrack track{1, "Audio 1", 1.0, false, {clip}};
    track.kind = timeline::TrackKind::Audio;
    const std::vector<timeline::TimelineTrack> tracks{track};
    widget.setTracks(tracks);
    require(!widget.volumeMode(),
            "The Timeline Volume tool should start inactive.");
    int edit_starts = 0;
    int edit_finishes = 0;
    int envelope_updates = 0;
    std::vector<timeline::AudioGainKeyframe> latest;
    QObject::connect(&widget,
        &timeline::TimelineWidget::audioGainEnvelopeEditStarted,
        [&edit_starts]() { ++edit_starts; });
    QObject::connect(&widget,
        &timeline::TimelineWidget::audioGainEnvelopeEditFinished,
        [&edit_finishes]() { ++edit_finishes; });
    QObject::connect(&widget,
        &timeline::TimelineWidget::audioGainEnvelopeChanged,
        [&envelope_updates, &latest](timeline::ClipId,
                                    const std::vector<timeline::AudioGainKeyframe>& points) {
            ++envelope_updates;
            latest = points;
        });
    widget.show();
    application.processEvents();
    widget.setVolumeMode(true);
    require(widget.volumeMode() && !widget.razorMode(),
            "The Volume tool did not activate exclusively from the Blade tool.");
    const timeline::TimelineGeometry geometry(
        tracks, QSizeF(widget.size()), widget.trackRowHeight(),
        widget.zoomFactor(), std::nullopt, 30.0);
    auto clip_bounds = geometry.clipRect(clip, 0);
    clip_bounds.moveTop(widget.trackBounds(0).top());
    const auto bounds = clip_bounds.adjusted(1.0, 6.0, -1.0, -6.0);
    const auto start = QPointF(bounds.left() + bounds.width() * 0.5,
                               bounds.top() + 1.0);
    const auto finish = QPointF(bounds.left() + bounds.width() * 0.72,
                                bounds.center().y());
    sendMouse(widget, QEvent::MouseButtonPress, start, Qt::LeftButton);
    sendMouse(widget, QEvent::MouseMove, finish, Qt::LeftButton);
    sendMouse(widget, QEvent::MouseButtonRelease, finish, Qt::NoButton);
    require(edit_starts == 1 && edit_finishes == 1 && envelope_updates >= 2 &&
                latest.size() == 3 && latest[0] == timeline::AudioGainKeyframe{0, 1.0} &&
                latest[1].frame > 90 && latest[1].frame < 120 &&
                std::abs(latest[1].gain - 1.0) < 0.05 &&
                latest[2] == timeline::AudioGainKeyframe{150, 1.0},
            "Volume-tool insertion and dragging did not preserve edge points and edit values.");
    clip.audio_gain_keyframes = latest;
    track.clips.front() = clip;
    widget.setTracks({track});
    const auto edge_start = QPointF(
        bounds.left(), bounds.bottom() - bounds.height() * 0.5);
    const auto edge_finish = QPointF(
        bounds.left() + bounds.width() * 0.4, bounds.top() + 1.0);
    sendMouse(widget, QEvent::MouseButtonPress, edge_start, Qt::LeftButton);
    sendMouse(widget, QEvent::MouseMove, edge_finish, Qt::LeftButton);
    sendMouse(widget, QEvent::MouseButtonRelease, edge_finish, Qt::NoButton);
    require(edit_starts == 2 && edit_finishes == 2 && latest.size() == 3 &&
                latest.front().frame == 0 && latest.front().gain > 1.8,
            "Dragging an edge volume point moved it away from the clip boundary.");
    widget.setRazorMode(true);
    require(!widget.volumeMode() && widget.razorMode(),
            "Selecting the Blade tool did not leave Volume mode.");
    widget.close();
}

void testTimelineMultiSelection() {
    timeline::TimelineWidget widget;
    widget.resize(1200, 180);
    widget.setTimelineViewportWidth(1200);
    widget.setZoomFactor(20.0);
    auto first = makeClip("selection-first.mkv", 0, 60, "First");
    auto second = makeClip("selection-second.mkv", 100, 60, "Second");
    auto third = makeClip("selection-third.mkv", 200, 60, "Third");
    first.clip_id = 8101;
    second.clip_id = 8102;
    third.clip_id = 8103;
    const timeline::TimelineTrack track{
        81, "Video 1", 1.0, false, {first, second, third}};
    widget.setTracks({track});
    widget.show();
    const timeline::TimelineGeometry geometry(
        {track}, QSizeF(widget.size()), widget.trackRowHeight(),
        widget.zoomFactor(), std::nullopt, 30.0);
    const auto first_point = geometry.clipRect(first, 0).center();
    const auto second_point = geometry.clipRect(second, 0).center();
    const auto third_point = geometry.clipRect(third, 0).center();
    const auto gap_point = QPointF(
        widget.contentXForFrame(80), geometry.trackRect(0).center().y());
    timeline::ClipId primary_id = 0;
    QObject::connect(
        &widget, &timeline::TimelineWidget::clipSelected,
        [&primary_id](timeline::TrackId, timeline::ClipId clip_id) {
            primary_id = clip_id;
        });
    const auto click = [&widget](
        const QPointF& point, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        sendMouse(widget, QEvent::MouseButtonPress, point, Qt::LeftButton, modifiers);
        sendMouse(widget, QEvent::MouseButtonRelease, point, Qt::NoButton, modifiers);
    };

    click(first_point);
    require(widget.selectedClipIds() == std::vector<timeline::ClipId>{first.clip_id},
            "A normal click did not select only one Timeline clip.");
    click(second_point, Qt::ControlModifier);
    require(widget.selectedClipIds() ==
                (std::vector<timeline::ClipId>{first.clip_id, second.clip_id}) &&
                widget.selectedClipIds().back() == second.clip_id &&
                primary_id == second.clip_id,
            "Ctrl+click did not add a clip and make it the primary selection.");
    click(third_point);
    require(widget.selectedClipIds() == std::vector<timeline::ClipId>{third.clip_id} &&
                primary_id == third.clip_id,
            "A normal click did not collapse the selection to the newly selected clip.");
    click(first_point, Qt::ControlModifier);
    require(widget.selectedClipIds() ==
                (std::vector<timeline::ClipId>{third.clip_id, first.clip_id}) &&
                primary_id == first.clip_id,
            "Adding another clip did not make that clip the primary selection.");
    click(third_point, Qt::ControlModifier);
    require(widget.selectedClipIds() == std::vector<timeline::ClipId>{first.clip_id},
            "Ctrl+click did not remove an already selected non-primary clip.");
    click(first_point, Qt::ControlModifier);
    require(widget.selectedClipIds().empty(),
            "Ctrl+click did not clear the selection when toggling off its last clip.");
    click(first_point);
    click(gap_point);
    require(widget.selectedClipIds().empty(),
            "Clicking an empty Timeline gap did not clear the multi-selection.");
    widget.close();
}

} // namespace

int main(int argc, char* argv[]) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication application(argc, argv);

    try {
        testAudioWaveformRendering(application);
        testAudioGainEnvelopeTool(application);
        testTimelineMultiSelection();

        timeline::TimelineWidget widget;
        require(widget.trackRowHeight() == 70.0,
                "A new Timeline widget did not start with the 70-pixel default row height.");
        require(widget.snapEnabled(),
                "A new Timeline widget did not enable magnetic snapping by default.");
        widget.resize(1000, 700);
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
        timeline::TrackId effect_drop_expected_track = top_track.track_id;
        QObject::connect(
            &widget,
            &timeline::TimelineWidget::effectDropRequested,
            [&effect_drop_received, &effect_drop_track, &effect_drop_frame](
                const QString& effect_id,
                timeline::TrackId track_id,
                qint64 timeline_frame) {
                effect_drop_received = effect_id == "text.text";
                effect_drop_track = static_cast<qint64>(track_id);
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
        require(effect_drop_received && effect_drop_track ==
                    static_cast<qint64>(effect_drop_expected_track) &&
                    effect_drop_frame > 0,
                "Text effect drop did not preserve the target track and frame.");

        bool visual_filter_drop_received = false;
        QObject::connect(
            &widget,
            &timeline::TimelineWidget::effectDropRequested,
            [&visual_filter_drop_received](const QString& effect_id,
                                           timeline::TrackId,
                                           qint64) {
                if (effect_id == QStringLiteral("video.grayscale")) {
                    visual_filter_drop_received = true;
                }
            });
        QMimeData visual_filter_mime;
        visual_filter_mime.setData(
            ui::kEffectIdMimeType,
            QByteArrayLiteral("video.grayscale"));
        QDragEnterEvent visual_filter_enter(
            effect_drop_position.toPoint(),
            Qt::CopyAction,
            &visual_filter_mime,
            Qt::LeftButton,
            Qt::NoModifier);
        QApplication::sendEvent(&widget, &visual_filter_enter);
        QDragMoveEvent visual_filter_move(
            effect_drop_position.toPoint(),
            Qt::CopyAction,
            &visual_filter_mime,
            Qt::LeftButton,
            Qt::NoModifier);
        QApplication::sendEvent(&widget, &visual_filter_move);
        QDropEvent visual_filter_drop(
            effect_drop_position,
            Qt::CopyAction,
            &visual_filter_mime,
            Qt::LeftButton,
            Qt::NoModifier);
        QApplication::sendEvent(&widget, &visual_filter_drop);
        require(visual_filter_drop_received,
                "A visual filter could not be dropped onto a video clip.");

        qint64 transition_drop_count = 0;
        qint64 transition_drop_kind = -1;
        const auto sendTransitionDrop = [
            &application,
            &transition_drop_count,
            &transition_drop_kind](
            const QByteArray& effect_id,
            bool target_cut) {
            timeline::TimelineWidget transition_widget;
            transition_widget.resize(1000, 200);
            timeline::TimelineTrack adjacent_clips_track{
                3,
                "Video 3",
                1.0,
                false,
                {makeClip("first.mkv", 0, 12000, "first.mkv"),
                 makeClip("second.mkv", 12000, 12000, "second.mkv")}};
            adjacent_clips_track.clips[0].clip_id = 1;
            adjacent_clips_track.clips[1].clip_id = 2;
            transition_widget.setTracks({adjacent_clips_track});
            transition_widget.setTimelineViewportWidth(1000);
            transition_widget.show();
            application.processEvents();

            QObject::connect(
                &transition_widget,
            &timeline::TimelineWidget::transitionAddRequested,
                [&transition_drop_count, &transition_drop_kind](
                    qint64 track,
                    qint64 from,
                    qint64 to,
                    qint64 kind) {
                    if (track == 3 && from == 1 && to == 2) {
                        ++transition_drop_count;
                        transition_drop_kind = kind;
                    }
                });

            const auto x = target_cut
                ? transition_widget.contentXForFrame(12000) + 3.0
                : transition_widget.contentXForFrame(400);
            const bool should_be_accepted = target_cut;
            QMimeData transition_mime;
            transition_mime.setData(ui::kEffectIdMimeType, effect_id);
            const QPointF position(x, 60.0);
            QDragEnterEvent enter(
                position.toPoint(), Qt::CopyAction, &transition_mime,
                Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(&transition_widget, &enter);
            QDragMoveEvent move(
                position.toPoint(), Qt::CopyAction, &transition_mime,
                Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(&transition_widget, &move);
            require(
                move.isAccepted() == should_be_accepted,
                "Transition drag hover validity was wrong for " +
                    effect_id.toStdString() + " at x=" + std::to_string(x));
            QDropEvent drop(
                position, Qt::CopyAction, &transition_mime,
                Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(&transition_widget, &drop);
            require(drop.isAccepted() == should_be_accepted,
                    "Transition drop did not match cut validity.");
            QDragLeaveEvent leave;
            QApplication::sendEvent(&transition_widget, &leave);
        };
        sendTransitionDrop(
            QByteArrayLiteral("transitions.cross_dissolve"),
            true);
        require(transition_drop_count == 1 && transition_drop_kind == 0,
            "Cross Dissolve drop did not target the adjacent clip cut by stable IDs.");
        sendTransitionDrop(
            QByteArrayLiteral("transitions.fade_to_black"),
            true);
        require(transition_drop_count == 2 && transition_drop_kind == 1,
                "Fade to Black drop did not target the adjacent clip cut.");
        sendTransitionDrop(
            QByteArrayLiteral("transitions.fade_to_black"),
            false);

        auto first_audio_clip = makeClip("first-audio.wav", 0, 12000, "First audio");
        first_audio_clip.kind = timeline::ClipKind::Audio;
        first_audio_clip.frame_rate.reset();
        first_audio_clip.frame_count.reset();
        auto second_audio_clip = makeClip(
            "second-audio.wav", 12000, 12000, "Second audio");
        second_audio_clip.kind = timeline::ClipKind::Audio;
        second_audio_clip.frame_rate.reset();
        second_audio_clip.frame_count.reset();
        timeline::TimelineTrack adjacent_audio_track{
            9, "Audio 1", 1.0, false,
            {first_audio_clip, second_audio_clip}};
        adjacent_audio_track.kind = timeline::TrackKind::Audio;
        timeline::TimelineWidget audio_transition_widget;
        audio_transition_widget.resize(1000, 200);
        audio_transition_widget.setTracks({adjacent_audio_track});
        audio_transition_widget.setTimelineViewportWidth(1000);
        audio_transition_widget.show();
        application.processEvents();
        qint64 audio_transition_count = 0;
        qint64 audio_transition_kind = -1;
        QObject::connect(
            &audio_transition_widget,
            &timeline::TimelineWidget::transitionAddRequested,
            [&audio_transition_count, &audio_transition_kind,
             &first_audio_clip, &second_audio_clip](
                qint64 track, qint64 from, qint64 to, qint64 kind) {
                if (track == 9 &&
                    static_cast<timeline::ClipId>(from) == first_audio_clip.clip_id &&
                    static_cast<timeline::ClipId>(to) == second_audio_clip.clip_id) {
                    ++audio_transition_count;
                    audio_transition_kind = kind;
                }
            });
        QTimer::singleShot(0, [&application]() {
            for (auto* window : application.topLevelWidgets()) {
                if (auto* menu = qobject_cast<QMenu*>(window)) {
                    for (auto* action : menu->actions()) {
                        if (action->text() == "Add Audio Crossfade") {
                            const auto position = menu->actionGeometry(action).center();
                            sendMouse(*menu, QEvent::MouseButtonPress,
                                      position, Qt::LeftButton);
                            sendMouse(*menu, QEvent::MouseButtonRelease,
                                      position, Qt::NoButton);
                            break;
                        }
                    }
                    menu->close();
                }
            }
        });
        const QPoint audio_cut_position(
            static_cast<int>(audio_transition_widget.contentXForFrame(12000) + 2),
            static_cast<int>(std::lround(
                audio_transition_widget.trackBounds(0).center().y())));
        QContextMenuEvent audio_cut_context(
            QContextMenuEvent::Mouse,
            audio_cut_position,
            audio_transition_widget.mapToGlobal(audio_cut_position),
            Qt::NoModifier);
        QApplication::sendEvent(&audio_transition_widget, &audio_cut_context);
        require(audio_transition_count == 1 && audio_transition_kind == 2,
                "The Audio track context menu did not add an Audio Crossfade at its cut.");

        bool media_drop_received = false;
        QString media_drop_path;
        qint64 media_drop_track = -1;
        qint64 media_drop_frame = -1;
        QObject::connect(
            &widget,
            &timeline::TimelineWidget::mediaDropRequested,
            [&media_drop_received, &media_drop_path, &media_drop_track,
             &media_drop_frame](
                const QString& path,
                timeline::TrackId track,
                qint64 frame) {
                media_drop_received = true;
                media_drop_path = path;
                media_drop_track = static_cast<qint64>(track);
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
            media_drop_path == "sample.mp4" && media_drop_track ==
                static_cast<qint64>(top_track.track_id) &&
                media_drop_frame > 0,
            "Media drop did not preserve the source path and target position.");

        QScrollArea external_drop_area;
        external_drop_area.resize(1000, 360);
        external_drop_area.setWidgetResizable(true);
        external_drop_area.setAcceptDrops(true);
        external_drop_area.viewport()->setAcceptDrops(true);
        auto* external_drop_widget = new timeline::TimelineWidget;
        external_drop_widget->setTrackRowHeight(timeline::kMaximumTrackRowHeight);
        external_drop_widget->setTracks({top_track});
        external_drop_area.setWidget(external_drop_widget);
        external_drop_widget->setAcceptDrops(false);
        external_drop_area.viewport()->installEventFilter(external_drop_widget);
        external_drop_widget->setTimelineViewportWidth(
            external_drop_area.viewport()->width());
        external_drop_area.show();
        application.processEvents();
        require(external_drop_area.viewport()->acceptDrops(),
                "The Timeline scroll viewport must accept operating-system drops.");
        QTemporaryDir external_drop_directory(
            QDir::currentPath() + QStringLiteral("/timeline-drop-XXXXXX"));
        require(external_drop_directory.isValid(),
                "Could not create a Timeline external-drop fixture directory.");
        const QString external_file_path = external_drop_directory.path() +
            QStringLiteral("/source clip.mp4");
        QFile external_file(external_file_path);
        require(external_file.open(QIODevice::WriteOnly),
                "Could not create a Timeline external-drop fixture: " +
                    external_file.errorString().toStdString());
        external_file.write("fixture");
        external_file.close();
        QMimeData external_file_mime;
        external_file_mime.setUrls({QUrl::fromLocalFile(external_file_path)});
        QStringList external_drop_paths;
        timeline::TrackId external_drop_track = 0;
        qint64 external_drop_frame = -1;
        QObject::connect(
            external_drop_widget,
            &timeline::TimelineWidget::externalFilesDropRequested,
            [&external_drop_paths, &external_drop_track, &external_drop_frame](
                const QStringList& paths, timeline::TrackId track, qint64 frame) {
                external_drop_paths = paths;
                external_drop_track = track;
                external_drop_frame = frame;
            });
        const std::vector<timeline::TimelineTrack> external_drop_tracks{top_track};
        const timeline::TimelineGeometry external_drop_geometry(
            external_drop_tracks,
            QSizeF(external_drop_widget->size()),
            external_drop_widget->trackRowHeight(),
            external_drop_widget->zoomFactor());
        const QPointF external_position(
            360.0, external_drop_geometry.trackRect(0).center().y());
        QDragEnterEvent external_enter(
            external_position.toPoint(), Qt::CopyAction, &external_file_mime,
            Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(
            external_drop_area.viewport(), &external_enter);
        QDragMoveEvent external_move(
            external_position.toPoint(), Qt::CopyAction, &external_file_mime,
            Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(
            external_drop_area.viewport(), &external_move);
        QDropEvent external_drop(
            external_position, Qt::CopyAction, &external_file_mime,
            Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(
            external_drop_area.viewport(), &external_drop);
        require(external_drop.isAccepted() && !external_drop_paths.isEmpty() &&
                    external_drop_paths.front() == external_file_path &&
                    external_drop_track == top_track.track_id &&
                    external_drop_frame >= 0,
                "Timeline viewport did not capture local file URLs, track, and frame.");

        external_drop_paths.clear();
        const QPointF outside_track_position(360.0, 20.0);
        QDragEnterEvent outside_track_enter(
            outside_track_position.toPoint(), Qt::CopyAction,
            &external_file_mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(
            external_drop_area.viewport(), &outside_track_enter);
        QDragMoveEvent outside_track_move(
            outside_track_position.toPoint(), Qt::CopyAction,
            &external_file_mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(
            external_drop_area.viewport(), &outside_track_move);
        QDropEvent outside_track_drop(
            outside_track_position, Qt::CopyAction, &external_file_mime,
            Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(
            external_drop_area.viewport(), &outside_track_drop);
        require(!outside_track_drop.isAccepted() && external_drop_paths.isEmpty(),
                "Timeline viewport accepted an external file above its tracks.");

        QMimeData remote_external_mime;
        remote_external_mime.setUrls({
            QUrl(QStringLiteral("https://example.invalid/source.mp4"))});
        QDragEnterEvent remote_external_enter(
            external_position.toPoint(), Qt::CopyAction,
            &remote_external_mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(
            external_drop_area.viewport(), &remote_external_enter);
        require(!remote_external_enter.isAccepted(),
                "Timeline viewport accepted a remote URL as an external file.");

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

        timeline::TimelineWidget typed_drop_widget;
        typed_drop_widget.resize(1000, 700);
        typed_drop_widget.setTrackRowHeight(timeline::kMaximumTrackRowHeight);
        timeline::TimelineTrack audio_target{
            3, "Audio 1", 1.0, false, {}};
        audio_target.kind = timeline::TrackKind::Audio;
        typed_drop_widget.setTracks({top_track, lower_track, audio_target});
        typed_drop_widget.setTimelineViewportWidth(1000);
        typed_drop_widget.show();
        application.processEvents();
        timeline::TrackId typed_drop_track = 0;
        QObject::connect(
            &typed_drop_widget,
            &timeline::TimelineWidget::mediaDropRequested,
            [&typed_drop_track](const QString&, timeline::TrackId track_id, qint64) {
                typed_drop_track = track_id;
            });
        QMimeData audio_mime;
        audio_mime.setData(ui::kMediaPathMimeType, QByteArrayLiteral("song.wav"));
        audio_mime.setData(ui::kMediaKindMimeType, QByteArrayLiteral("audio"));
        audio_mime.setData(
            ui::kMediaDurationSecondsMimeType, QByteArrayLiteral("1.25"));
        const auto sendTypedDrop = [&application, &typed_drop_widget](
            QMimeData& mime, const QPointF& position) {
            QDragEnterEvent enter(
                position.toPoint(), Qt::CopyAction, &mime,
                Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(&typed_drop_widget, &enter);
            QDragMoveEvent move(
                position.toPoint(), Qt::CopyAction, &mime,
                Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(&typed_drop_widget, &move);
            QDropEvent drop(
                position, Qt::CopyAction, &mime,
                Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(&typed_drop_widget, &drop);
            return std::pair{move.isAccepted(), drop.isAccepted()};
        };
        const auto audio_on_video = sendTypedDrop(audio_mime, QPointF(400, 120));
        require(audio_on_video.first && audio_on_video.second &&
                    typed_drop_track == top_track.track_id,
                "Audio-only media could not target a video row for automatic Audio-track creation.");
        typed_drop_track = 0;
        const auto audio_on_audio = sendTypedDrop(audio_mime, QPointF(400, 480));
        require(audio_on_audio.first && audio_on_audio.second &&
                    typed_drop_track == audio_target.track_id,
                "Audio-only media could not target an existing Audio track.");
        audio_target.clips.push_back(makeClip(
            "existing-audio.wav", 0, test_clip_duration, "existing-audio.wav"));
        audio_target.clips.front().kind = timeline::ClipKind::Audio;
        typed_drop_widget.setTracks({top_track, lower_track, audio_target});
        typed_drop_track = 0;
        const auto overlapping_audio = sendTypedDrop(
            audio_mime, QPointF(400, 480));
        require(!overlapping_audio.first && !overlapping_audio.second &&
                    typed_drop_track == 0,
                "Audio-only media was accepted over a clip on the same Audio track.");
        QMimeData visual_mime;
        visual_mime.setData(ui::kMediaPathMimeType, QByteArrayLiteral("picture.png"));
        visual_mime.setData(ui::kMediaKindMimeType, QByteArrayLiteral("image"));
        const auto visual_on_audio = sendTypedDrop(visual_mime, QPointF(400, 480));
        require(!visual_on_audio.first && !visual_on_audio.second,
                "Visual media was accepted on an Audio track.");
        typed_drop_widget.close();

        timeline::TimelineWidget empty_group_widget;
        empty_group_widget.resize(900, 500);
        timeline::TimelineTrack empty_group_audio{9, "Audio 1", 1.0, false, {}};
        empty_group_audio.kind = timeline::TrackKind::Audio;
        empty_group_widget.setTracks({empty_group_audio});
        empty_group_widget.show();
        application.processEvents();

        timeline::TimelineWidget compact_groups_widget;
        compact_groups_widget.resize(900, 500);
        compact_groups_widget.setTracks({top_track, empty_group_audio});
        compact_groups_widget.show();
        application.processEvents();
        const auto compact_video_row = compact_groups_widget.trackBounds(0);
        const auto compact_splitter = compact_groups_widget.trackSplitterRect();
        const auto compact_audio_row = compact_groups_widget.trackBounds(1);
        const auto compact_video_viewport = compact_groups_widget
            .trackGroupViewportRect(timeline::TrackKind::Video);
        const auto compact_audio_viewport = compact_groups_widget
            .trackGroupViewportRect(timeline::TrackKind::Audio);
        require(std::abs(compact_video_viewport.height() -
                         compact_audio_viewport.height()) < 0.001 &&
                    std::abs(compact_video_row.height() -
                             timeline::kDefaultTrackRowHeight) < 0.001 &&
                    std::abs(compact_audio_row.height() -
                             timeline::kDefaultTrackRowHeight) < 0.001 &&
                    compact_video_row.bottom() < compact_splitter.top() - 100.0 &&
                    compact_audio_row.top() >= compact_splitter.bottom(),
                "The default 50/50 panes did not preserve row height and viewport space.");
        compact_groups_widget.close();

        timeline::TrackKind requested_group = timeline::TrackKind::Audio;
        int group_drop_count = 0;
        QObject::connect(
            &empty_group_widget,
            &timeline::TimelineWidget::mediaGroupDropRequested,
            [&requested_group, &group_drop_count](
                const QString&, timeline::TrackKind kind, qint64 frame) {
                requested_group = kind;
                if (frame >= 0) ++group_drop_count;
            });
        QObject::connect(
            &empty_group_widget,
            &timeline::TimelineWidget::externalFilesGroupDropRequested,
            [&requested_group, &group_drop_count](
                const QStringList& paths, timeline::TrackKind kind, qint64 frame) {
                requested_group = kind;
                if (!paths.isEmpty() && frame >= 0) ++group_drop_count;
            });
        const auto sendEmptyGroupDrop = [&application, &empty_group_widget](
            QMimeData& mime, QPointF position) {
            QDragEnterEvent enter(position.toPoint(), Qt::CopyAction, &mime,
                Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(&empty_group_widget, &enter);
            QDragMoveEvent move(position.toPoint(), Qt::CopyAction, &mime,
                Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(&empty_group_widget, &move);
            QDropEvent drop(position, Qt::CopyAction, &mime,
                Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(&empty_group_widget, &drop);
            return std::pair{move.isAccepted(), drop.isAccepted()};
        };
        QMimeData empty_video_mime;
        empty_video_mime.setData(
            ui::kMediaPathMimeType, QByteArrayLiteral("first-image.png"));
        empty_video_mime.setData(
            ui::kMediaKindMimeType, QByteArrayLiteral("image"));
        const auto empty_video_position = QPointF(
            360.0, empty_group_widget.trackGroupViewportRect(
                timeline::TrackKind::Video).center().y());
        const auto accepted_empty_video = sendEmptyGroupDrop(
            empty_video_mime, empty_video_position);
        require(accepted_empty_video.first && accepted_empty_video.second &&
                    group_drop_count == 1 &&
                    requested_group == timeline::TrackKind::Video,
                "Dropping an image into an empty Video pane did not request a Video track.");
        QMimeData incompatible_empty_audio;
        incompatible_empty_audio.setData(
            ui::kMediaPathMimeType, QByteArrayLiteral("wrong.wav"));
        incompatible_empty_audio.setData(
            ui::kMediaKindMimeType, QByteArrayLiteral("audio"));
        const auto rejected_empty_audio = sendEmptyGroupDrop(
            incompatible_empty_audio, empty_video_position);
        require(!rejected_empty_audio.first && !rejected_empty_audio.second &&
                    group_drop_count == 1,
                "Audio media was accepted in an empty Video pane.");

        empty_group_widget.setTracks({top_track});
        QMimeData empty_audio_mime;
        empty_audio_mime.setData(
            ui::kMediaPathMimeType, QByteArrayLiteral("first-audio.wav"));
        empty_audio_mime.setData(
            ui::kMediaKindMimeType, QByteArrayLiteral("audio"));
        const auto empty_audio_position = QPointF(
            360.0, empty_group_widget.trackGroupViewportRect(
                timeline::TrackKind::Audio).center().y());
        const auto accepted_empty_audio = sendEmptyGroupDrop(
            empty_audio_mime, empty_audio_position);
        require(accepted_empty_audio.first && accepted_empty_audio.second &&
                    group_drop_count == 2 &&
                    requested_group == timeline::TrackKind::Audio,
                "Dropping audio into an empty Audio pane did not request an Audio track.");

        QStringList external_group_paths;
        QObject::connect(
            &empty_group_widget,
            &timeline::TimelineWidget::externalFilesGroupDropRequested,
            [&external_group_paths](const QStringList& paths,
                                    timeline::TrackKind kind, qint64) {
                if (kind == timeline::TrackKind::Audio) external_group_paths = paths;
            });
        const auto external_empty_audio = sendEmptyGroupDrop(
            external_file_mime, empty_audio_position);
        require(external_empty_audio.first && external_empty_audio.second &&
                    external_group_paths == QStringList{external_file_path},
                "External file drops into an empty group did not retain the destination group.");
        empty_group_widget.close();

        QScrollArea scroll_area;
        scroll_area.resize(360, 220);
        scroll_area.setWidgetResizable(true);
        scroll_area.setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto* viewport_timeline = new timeline::TimelineWidget;
        auto video_overflow_track = top_track;
        video_overflow_track.track_id = 3;
        video_overflow_track.name = "Video 3";
        video_overflow_track.clips.front() = makeClip(
            "video-overflow-target.mp4", 0, test_clip_duration,
            "video-overflow-target.mp4");
        viewport_timeline->setTracks({top_track, lower_track, video_overflow_track});
        viewport_timeline->setFrameRate({30000, 1001});
        viewport_timeline->setPlayheadFrame(30);
        viewport_timeline->setZoomFactor(2.0);
        viewport_timeline->setActiveClip(timeline::ClipLocation{0, 0});
        viewport_timeline->setAcceptDrops(false);
        scroll_area.setWidget(viewport_timeline);
        scroll_area.setAcceptDrops(true);
        scroll_area.viewport()->setAcceptDrops(true);
        scroll_area.viewport()->installEventFilter(viewport_timeline);
        auto* header_overlay = new timeline::TimelineTrackHeaderOverlay(
            viewport_timeline,
            scroll_area.viewport());
        auto* playhead_timecode = header_overlay->findChild<QLabel*>(
            QStringLiteral("timelinePlayheadTimecode"));
        require(playhead_timecode != nullptr &&
                    playhead_timecode->text() == QStringLiteral("00:00:01.001") &&
                    playhead_timecode->accessibleName() ==
                        QStringLiteral("Timeline playhead timecode"),
                "The fixed Timeline corner did not expose the global playhead timecode.");
        scroll_area.show();
        application.processEvents();
        auto* video_scroll = header_overlay->videoScrollBar();
        auto* audio_scroll = header_overlay->audioScrollBar();
        require(video_scroll != nullptr && audio_scroll != nullptr &&
                    scroll_area.verticalScrollBarPolicy() == Qt::ScrollBarAlwaysOff &&
                    !scroll_area.verticalScrollBar()->isVisible(),
                "The Timeline did not expose independent group scrollbars.");
        viewport_timeline->setTrackRowHeight(timeline::kMinimumTrackRowHeight);
        application.processEvents();
        const auto video_minimum_scroll = video_scroll->maximum();
        require(video_minimum_scroll > 0 && audio_scroll->maximum() == 0,
                "The Video scrollbar range did not remain independent of Audio.");
        viewport_timeline->setTrackRowHeight(timeline::kMaximumTrackRowHeight);
        application.processEvents();
        require(video_scroll->maximum() > video_minimum_scroll &&
                    audio_scroll->maximum() == 0,
                "Increasing row height did not update the Video-only scroll range.");
        viewport_timeline->setTimelineViewportWidth(
            scroll_area.viewport()->width());
        require(
            header_overlay->geometry().x() == 0 &&
                header_overlay->geometry().width() == 154,
            "The Timeline header overlay did not stay anchored to the viewport.");
        scroll_area.horizontalScrollBar()->setValue(0);
        application.processEvents();
        scroll_area.horizontalScrollBar()->setValue(100);
        application.processEvents();
        require(playhead_timecode->text() == QStringLiteral("00:00:01.001") &&
                    playhead_timecode->geometry() == QRect(12, 12, 130, 25),
                "The Timeline timecode moved or changed during horizontal scrolling.");
        QImage header_after_scroll(360, 220, QImage::Format_ARGB32);
        header_after_scroll.fill(Qt::transparent);
        scroll_area.viewport()->render(&header_after_scroll);
        require(header_overlay->geometry().x() == 0 &&
                    header_overlay->geometry().width() == 154,
                "Horizontal scrolling moved the fixed Timeline track header.");
        viewport_timeline->setPlayheadFrame(60);
        require(playhead_timecode->text() == QStringLiteral("00:00:02.002"),
                "The fixed Timeline timecode did not refresh when the playhead advanced.");
        require(
            header_after_scroll.pixelColor(120, 60) == QColor("#252d3a"),
            "The fixed header did not cover horizontally scrolled clip content.");
        const auto horizontal_frame_before = viewport_timeline->frameAtContentX(220.0);
        scroll_area.horizontalScrollBar()->setValue(160);
        application.processEvents();
        const auto horizontal_frame_after = viewport_timeline->frameAtContentX(220.0);
        require(
            horizontal_frame_before.has_value() && horizontal_frame_after.has_value() &&
                *horizontal_frame_before == *horizontal_frame_after,
            "Horizontal scrolling changed Timeline coordinate behavior.");
        video_scroll->setValue(video_scroll->maximum());
        application.processEvents();
        QImage header_after_vertical_scroll(360, 220, QImage::Format_ARGB32);
        header_after_vertical_scroll.fill(Qt::transparent);
        scroll_area.viewport()->render(&header_after_vertical_scroll);
        require(
            header_overlay->geometry().x() == 0 &&
                header_overlay->geometry().width() == 154,
            "Vertical scrolling changed the fixed header column geometry.");
        require(
            header_after_vertical_scroll.pixelColor(120, 60) == QColor("#202631"),
            "The fixed Video header did not follow its group's vertical scroll.");
        video_scroll->setValue(0);
        application.processEvents();

        bool viewport_media_drop_received = false;
        qint64 viewport_media_drop_track = -1;
        qint64 viewport_media_drop_frame = -1;
        QObject::connect(
            viewport_timeline,
            &timeline::TimelineWidget::mediaDropRequested,
            [&viewport_media_drop_received, &viewport_media_drop_track,
             &viewport_media_drop_frame](
                const QString&,
                timeline::TrackId track,
                qint64 frame) {
                viewport_media_drop_received = true;
                viewport_media_drop_track = static_cast<qint64>(track);
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
            viewport_media_drop_received && viewport_media_drop_track ==
                static_cast<qint64>(top_track.track_id) &&
                viewport_media_drop_frame > 0,
            "Media drop received by the QScrollArea viewport was not forwarded.");
        require(
            viewport_timeline->mapFrom(
                scroll_area.viewport(),
                viewport_drop_position).x() > viewport_drop_position.x(),
            "The viewport test did not exercise horizontal coordinate conversion.");

        auto video_second = top_track;
        video_second.track_id = 4;
        video_second.name = "Video 4";
        video_second.clips.front() = makeClip(
            "video-scroll-target.mp4", 0, test_clip_duration, "video-scroll-target.mp4");
        auto video_third = top_track;
        video_third.track_id = 5;
        video_third.name = "Video 5";
        video_third.clips.front() = makeClip(
            "video-scroll-last.mp4", 0, test_clip_duration, "video-scroll-last.mp4");
        timeline::TimelineTrack audio_first{6, "Audio 1", 1.0, false, {}};
        timeline::TimelineTrack audio_second{7, "Audio 2", 1.0, false, {}};
        timeline::TimelineTrack audio_third{8, "Audio 3", 1.0, false, {}};
        audio_first.kind = audio_second.kind = audio_third.kind =
            timeline::TrackKind::Audio;
        viewport_timeline->setTracks({
            top_track, video_second, video_third,
            audio_first, audio_second, audio_third});
        viewport_timeline->setTrackRowHeight(70.0);
        application.processEvents();
        require(video_scroll->maximum() > 0 && audio_scroll->maximum() > 0,
                "Both track groups did not receive their own overflow ranges.");
        video_scroll->setValue(video_scroll->maximum());
        const auto video_value_at_bottom = video_scroll->value();
        require(viewport_timeline->trackBounds(2).center().y() >=
                    viewport_timeline->trackGroupViewportRect(
                        timeline::TrackKind::Video).top() &&
                    viewport_timeline->trackBounds(2).center().y() <=
                    viewport_timeline->trackGroupViewportRect(
                        timeline::TrackKind::Video).bottom() &&
                    viewport_timeline->trackScrollOffset(timeline::TrackKind::Audio) == 0,
                "Scrolling Video did not keep its last row aligned inside its own pane.");
        audio_scroll->setValue(audio_scroll->maximum());
        require(video_scroll->value() == video_value_at_bottom &&
                    viewport_timeline->trackBounds(5).center().y() >=
                        viewport_timeline->trackGroupViewportRect(
                            timeline::TrackKind::Audio).top() &&
                    viewport_timeline->trackBounds(5).center().y() <=
                        viewport_timeline->trackGroupViewportRect(
                            timeline::TrackKind::Audio).bottom(),
                "Scrolling Audio changed Video's offset or lost Audio row alignment.");
        int scrolled_video_selection = 0;
        QObject::connect(
            viewport_timeline,
            &timeline::TimelineWidget::clipSelected,
            [&scrolled_video_selection](timeline::TrackId, timeline::ClipId clip_id) {
                if (clip_id != 0) ++scrolled_video_selection;
            });
        const auto video_target = viewport_timeline->trackBounds(2);
        sendMouse(*viewport_timeline, QEvent::MouseButtonPress,
            QPointF(220.0, video_target.center().y()), Qt::LeftButton);
        sendMouse(*viewport_timeline, QEvent::MouseButtonRelease,
            QPointF(220.0, video_target.center().y()), Qt::NoButton);
        require(scrolled_video_selection == 1,
                "Hit testing and selection failed after independently scrolling Video.");

        const auto audio_value_before_video_wheel = audio_scroll->value();
        sendWheel(*viewport_timeline, QPointF(220.0,
            viewport_timeline->trackGroupViewportRect(
                timeline::TrackKind::Video).center().y()), 120);
        require(video_scroll->value() < video_scroll->maximum() &&
                    audio_scroll->value() == audio_value_before_video_wheel,
                "The mouse wheel did not scroll the group beneath the pointer only.");

        const auto initial_split = viewport_timeline->trackGroupSplitRatio();
        const auto splitter = viewport_timeline->trackSplitterRect();
        require(std::abs(splitter.height() - 18.0) < 0.001,
                "The Video/Audio divider does not expose the expanded drag area.");

        const auto video_row_height_before_split =
            viewport_timeline->trackBounds(0).height();
        const auto audio_row_height_before_split =
            viewport_timeline->trackBounds(3).height();
        const auto check_divider_cursor = [&application, viewport_timeline](
            QPointF position) {
            sendMouse(*viewport_timeline, QEvent::MouseMove,
                position, Qt::NoButton);
            application.processEvents();
            require(viewport_timeline->cursor().shape() == Qt::SplitVCursor,
                    "The Video/Audio divider did not use the resize cursor across its width.");
        };
        check_divider_cursor(QPointF(splitter.left() + 3.0, splitter.center().y()));
        check_divider_cursor(splitter.center());
        check_divider_cursor(QPointF(splitter.right() - 3.0, splitter.center().y()));
        sendMouse(*viewport_timeline, QEvent::MouseMove,
            QPointF(0.0, splitter.center().y()), Qt::NoButton);

        QImage divider_idle(
            viewport_timeline->size(), QImage::Format_ARGB32_Premultiplied);
        divider_idle.fill(Qt::transparent);
        viewport_timeline->render(&divider_idle);
        const auto grip_sample = QPoint(
            static_cast<int>(std::lround(splitter.center().x() + 12.0)),
            static_cast<int>(std::lround(splitter.center().y())));
        const auto idle_grip_color = divider_idle.pixelColor(grip_sample);

        sendMouse(*viewport_timeline, QEvent::MouseMove,
            splitter.center(), Qt::NoButton);
        application.processEvents();
        QImage divider_hover(
            viewport_timeline->size(), QImage::Format_ARGB32_Premultiplied);
        divider_hover.fill(Qt::transparent);
        viewport_timeline->render(&divider_hover);
        require(divider_hover.pixelColor(grip_sample) != idle_grip_color,
                "Hovering the Video/Audio divider did not highlight its grip.");

        video_scroll->setValue(0);
        audio_scroll->setValue(0);
        application.processEvents();
        const auto drag_divider_at = [
            &application, viewport_timeline,
            video_row_height_before_split, audio_row_height_before_split](
                double x, double vertical_delta) {
            const auto current_splitter = viewport_timeline->trackSplitterRect();
            const auto press_position = QPointF(x, current_splitter.center().y());
            const auto drag_position =
                press_position + QPointF(0.0, vertical_delta);
            const auto ratio_before = viewport_timeline->trackGroupSplitRatio();
            const auto video_view_height_before = viewport_timeline
                ->trackGroupViewportRect(timeline::TrackKind::Video).height();
            sendMouse(*viewport_timeline, QEvent::MouseButtonPress,
                press_position, Qt::LeftButton);
            require(viewport_timeline->cursor().shape() == Qt::SplitVCursor,
                    "Dragging the divider did not retain the resize cursor.");
            QImage divider_active(
                viewport_timeline->size(), QImage::Format_ARGB32_Premultiplied);
            divider_active.fill(Qt::transparent);
            viewport_timeline->render(&divider_active);
            sendMouse(*viewport_timeline, QEvent::MouseMove,
                drag_position, Qt::LeftButton);
            sendMouse(*viewport_timeline, QEvent::MouseButtonRelease,
                drag_position, Qt::NoButton);
            application.processEvents();
            require(std::abs(viewport_timeline->trackGroupSplitRatio() -
                             ratio_before) > 0.01 &&
                        std::abs(viewport_timeline->trackGroupViewportRect(
                            timeline::TrackKind::Video).height() -
                            video_view_height_before) > 1.0,
                    "Dragging the divider did not resize the pane viewports.");
            require(std::abs(viewport_timeline->trackBounds(0).height() -
                             video_row_height_before_split) < 0.001 &&
                        std::abs(viewport_timeline->trackBounds(3).height() -
                                 audio_row_height_before_split) < 0.001,
                    "Resizing the divider changed the height of a track row.");
        };
        drag_divider_at(splitter.left() + 3.0, 20.0);
        viewport_timeline->setTrackGroupSplitRatio(initial_split);
        drag_divider_at(viewport_timeline->trackSplitterRect().center().x(), 20.0);
        viewport_timeline->setTrackGroupSplitRatio(initial_split);
        drag_divider_at(viewport_timeline->trackSplitterRect().right() - 3.0, 20.0);
        viewport_timeline->setTrackGroupSplitRatio(initial_split);

        sendMouse(*viewport_timeline, QEvent::MouseMove,
            viewport_timeline->trackSplitterRect().center(), Qt::NoButton);
        application.processEvents();
        QImage divider_active(
            viewport_timeline->size(), QImage::Format_ARGB32_Premultiplied);
        divider_active.fill(Qt::transparent);
        sendMouse(*viewport_timeline, QEvent::MouseButtonPress,
            viewport_timeline->trackSplitterRect().center(), Qt::LeftButton);
        viewport_timeline->render(&divider_active);
        require(divider_active.pixelColor(grip_sample) !=
                    divider_hover.pixelColor(grip_sample),
                "Dragging the divider did not highlight its grip.");
        sendMouse(*viewport_timeline, QEvent::MouseButtonRelease,
            viewport_timeline->trackSplitterRect().center(), Qt::NoButton);

        video_scroll->setValue(0);
        audio_scroll->setValue(0);
        application.processEvents();
        const auto upper_splitter = viewport_timeline->trackSplitterRect();
        const auto left_splitter_edge = QPointF(
            upper_splitter.left() + 2.0, upper_splitter.center().y());
        sendMouse(*viewport_timeline, QEvent::MouseMove,
            left_splitter_edge, Qt::NoButton);
        require(viewport_timeline->cursor().shape() == Qt::SplitVCursor,
                "The left divider did not retain the resize cursor.");
        sendMouse(*viewport_timeline, QEvent::MouseButtonPress,
            left_splitter_edge, Qt::LeftButton);
        sendMouse(*viewport_timeline, QEvent::MouseMove,
            QPointF(left_splitter_edge.x(), -100.0), Qt::LeftButton);
        sendMouse(*viewport_timeline, QEvent::MouseButtonRelease,
            QPointF(left_splitter_edge.x(), -100.0), Qt::NoButton);
        require(std::abs(viewport_timeline->trackGroupSplitRatio() - 0.2) < 0.001,
                "Dragging the divider above its range did not preserve the lower bound.");

        const auto lower_splitter = viewport_timeline->trackSplitterRect();
        const auto right_splitter_edge = QPointF(
            lower_splitter.right() - 2.0, lower_splitter.center().y());
        sendMouse(*viewport_timeline, QEvent::MouseButtonPress,
            right_splitter_edge, Qt::LeftButton);
        sendMouse(*viewport_timeline, QEvent::MouseMove,
            QPointF(right_splitter_edge.x(), viewport_timeline->height() + 100.0),
            Qt::LeftButton);
        sendMouse(*viewport_timeline, QEvent::MouseButtonRelease,
            QPointF(right_splitter_edge.x(), viewport_timeline->height() + 100.0),
            Qt::NoButton);
        require(std::abs(viewport_timeline->trackGroupSplitRatio() - 0.8) < 0.001,
                "Dragging the divider below its range did not preserve the upper bound.");
        viewport_timeline->setTrackGroupSplitRatio(0.5);

        std::vector<timeline::TimelineTrack> video_overflow_tracks;
        for (int index = 0; index < 6; ++index) {
            auto track = top_track;
            track.track_id = static_cast<timeline::TrackId>(100 + index);
            track.name = "Video overflow " + std::to_string(index + 1);
            track.clips.clear();
            video_overflow_tracks.push_back(std::move(track));
        }
        video_overflow_tracks.push_back(audio_first);
        viewport_timeline->setTracks(std::move(video_overflow_tracks));
        application.processEvents();
        require(video_scroll->maximum() > 0 && audio_scroll->maximum() == 0,
                "The pane resize test did not isolate Video overflow.");
        video_scroll->setValue(0);
        audio_scroll->setValue(0);
        application.processEvents();
        const auto split_before_empty_group_drag =
            viewport_timeline->trackGroupSplitRatio();
        const auto expanded_hit_target = QPointF(
            viewport_timeline->trackSplitterRect().center().x(),
            viewport_timeline->trackSplitterRect().center().y());
        sendMouse(*viewport_timeline, QEvent::MouseButtonPress,
            expanded_hit_target, Qt::LeftButton);
        sendMouse(*viewport_timeline, QEvent::MouseMove,
            expanded_hit_target + QPointF(0.0, 20.0),
            Qt::LeftButton);
        sendMouse(*viewport_timeline, QEvent::MouseButtonRelease,
            expanded_hit_target + QPointF(0.0, 20.0),
            Qt::NoButton);
        require(viewport_timeline->trackGroupSplitRatio() >
                    split_before_empty_group_drag &&
                    video_scroll->value() == 0 && audio_scroll->value() == 0,
                "Dragging the divider with a non-overflowing group did not resize panes independently of scrolling.");

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

        require(widget.minimumHeight() >= 130,
                "The Timeline minimum height does not keep both group viewports usable.");
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
        require(
            timeline::formatTimelineTimecode(30, {30000, 1001}) ==
                    "00:00:01.001" &&
                timeline::formatTimelineTimecode(90000, {24000, 1001}) ==
                    "01:02:33.750" &&
                std::abs(static_cast<double>(
                    timeline::timelineTimeSeconds(30, {30000, 1001}) - 1.001L)) <
                    0.000001,
            "Timeline timecode and seconds did not use the rational project rate.");
        require(!widget.moveRequiresAlt(),
                "The timeline did not default to moving clips without Alt.");
        widget.setMoveRequiresAlt(true);

        qint64 selected_track = -1;
        qint64 selected_clip = -1;
        int seek_started = 0;
        std::vector<qint64> seek_frames;
        qint64 move_from_track = -1;
        qint64 move_to_track = -1;
        int split_count = 0;
        qint64 split_frame = -1;
        qint64 trim_edge = -1;
        qint64 trim_boundary = -1;
        qint64 trim_mode = -1;
        qint64 transition_track = -1;
        qint64 transition_from = -1;
        qint64 transition_to = -1;
        int zoom_requests = 0;
        double requested_zoom = 0.0;

        QObject::connect(
            &widget,
            &timeline::TimelineWidget::clipSelected,
            [&selected_track, &selected_clip](timeline::TrackId track, timeline::ClipId clip) {
                selected_track = static_cast<qint64>(track);
                selected_clip = static_cast<qint64>(clip);
            });
        QObject::connect(
            &widget,
            &timeline::TimelineWidget::clipSelectionCleared,
            [&selected_track, &selected_clip]() {
                selected_track = -1;
                selected_clip = -1;
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
            &timeline::TimelineWidget::clipMoveRequested,
            [&move_from_track, &move_to_track](
                timeline::ClipId clip_id,
                timeline::TrackId target_track_id,
                qint64) {
                move_from_track = static_cast<qint64>(clip_id);
                move_to_track = static_cast<qint64>(target_track_id);
            });
        QObject::connect(
            &widget,
            &timeline::TimelineWidget::clipSplitRequested,
            [&split_count, &split_frame](timeline::ClipId, qint64 frame) {
                ++split_count;
                split_frame = frame;
            });
        QObject::connect(
            &widget,
            &timeline::TimelineWidget::clipEdgeTrimRequested,
            [&trim_edge, &trim_boundary, &trim_mode](
                timeline::ClipId, qint64 edge, qint64 boundary, qint64 mode) {
                trim_edge = edge;
                trim_boundary = boundary;
                trim_mode = mode;
            });
        QObject::connect(
            &widget,
            &timeline::TimelineWidget::transitionSelected,
            [&transition_track, &transition_from, &transition_to](
                timeline::TrackId track, timeline::ClipId from, timeline::ClipId to) {
                transition_track = static_cast<qint64>(track);
                transition_from = static_cast<qint64>(from);
                transition_to = static_cast<qint64>(to);
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
                    guides_at_400_percent > 0 &&
                    guides_at_frame_level > guides_at_400_percent,
                "Timeline ruler guides were missing at an expected zoom level.");

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
        const auto frame_grid_track_y = static_cast<int>(std::lround(
            frame_grid_widget.trackBounds(0).top() + 8.0));
        const auto track_background = frame_grid_image.pixelColor(
            220, frame_grid_track_y);
        int track_grid_pixels = 0;
        for (int x = 205; x < 225; ++x) {
            if (frame_grid_image.pixelColor(x, frame_grid_track_y) != track_background) {
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
        require(playhead_widget.displayedPlayheadFrame() == 15 &&
                    playhead_widget.playheadTimecode() == "00:00:00.500",
                "The playhead timecode did not follow the transient ruler scrub.");
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

        // Clearing selection must not hide the playhead while the timeline
        // still contains clips.
        playhead_widget.setActiveClip(std::nullopt);
        playhead_widget.setPlayheadFrame(18);
        application.processEvents();
        QImage deselected_playhead_image(400, 300, QImage::Format_ARGB32);
        deselected_playhead_image.fill(Qt::transparent);
        playhead_widget.render(&deselected_playhead_image);
        require(isPlayheadPixel(
                    deselected_playhead_image.pixelColor(live_playhead_x, 100)),
                "Clearing the active clip hid the timeline playhead.");
        playhead_widget.close();

        timeline::TimelineWidget global_time_widget;
        global_time_widget.setTracks({timeline::TimelineTrack{
            8, "Video 1", 1.0, false,
            {makeClip("before-cut.mkv", 0, 30, "before"),
             makeClip("after-cut.mkv", 30, 30, "after")}}});
        global_time_widget.setFrameRate({24, 1});
        global_time_widget.setActiveClip(timeline::ClipLocation{0, 1});
        global_time_widget.setPlayheadFrame(35);
        require(global_time_widget.displayedPlayheadFrame() == 35 &&
                    global_time_widget.playheadTimecode() == "00:00:01.458",
                "The playhead timecode restarted at a clip cut instead of using global Timeline frames.");
        widget.setZoomFactor(1.0);
        widget.setTrackRowHeight(timeline::kMaximumTrackRowHeight);
        widget.resize(1000, 700);
        application.processEvents();

        // Clicking an inactive clip selects it without starting a seek.
        widget.setActiveClip(std::nullopt);
        sendMouse(widget, QEvent::MouseButtonPress, QPointF(500, 120),
                  Qt::LeftButton);
        sendMouse(widget, QEvent::MouseButtonRelease, QPointF(500, 120),
                  Qt::NoButton);
        require(selected_track == static_cast<qint64>(top_track.track_id) &&
                    selected_clip == static_cast<qint64>(top_track.clips.front().clip_id),
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
        require(move_from_track == static_cast<qint64>(top_track.clips.front().clip_id) &&
                    move_to_track == static_cast<qint64>(lower_track.track_id),
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
        require(selected_track == static_cast<qint64>(top_track.track_id) &&
                    selected_clip == static_cast<qint64>(top_track.clips.front().clip_id) &&
                    move_from_track == -1 && move_to_track == -1,
                "A normal click in movement mode did not select without moving.");
        sendMouse(widget, QEvent::MouseButtonPress, QPointF(500, 120),
                  Qt::LeftButton);
        sendMouse(widget, QEvent::MouseMove, QPointF(500, 320),
                  Qt::LeftButton);
        sendMouse(widget, QEvent::MouseButtonRelease, QPointF(500, 320),
                  Qt::NoButton);
        require(move_from_track == static_cast<qint64>(top_track.clips.front().clip_id) &&
                    move_to_track == static_cast<qint64>(lower_track.track_id),
                "A normal drag did not move the clip after disabling Alt requirement.");
        require(seek_frames.size() == 1,
                "A normal move drag was incorrectly treated as seeking.");

        // Clip hit testing is local to the row under the pointer. A clip on a
        // different row must not be selected or moved through an empty row.
        const auto empty_lower_track = timeline::TimelineTrack{
            1, "Video 1", 1.0, false, {}};
        widget.setTracks({top_track, empty_lower_track});
        widget.setActiveClip(timeline::ClipLocation{0, 0});
        selected_track = 0;
        selected_clip = 0;
        move_from_track = -1;
        move_to_track = -1;
        sendMouse(widget, QEvent::MouseButtonPress, QPointF(500, 320),
                  Qt::LeftButton);
        sendMouse(widget, QEvent::MouseMove, QPointF(700, 320),
                  Qt::LeftButton);
        sendMouse(widget, QEvent::MouseButtonRelease, QPointF(700, 320),
                  Qt::NoButton);
        require(selected_track == -1 && selected_clip == -1 &&
                    move_from_track == -1 && move_to_track == -1,
                "Dragging an empty row selected or moved a clip from another row.");

        // An actual clip in the clicked row remains selectable even when the
        // same frame is occupied by a clip on another row.
        widget.setTracks({top_track, lower_track});
        widget.setActiveClip(std::nullopt);
        selected_track = -1;
        selected_clip = -1;
        sendMouse(widget, QEvent::MouseButtonPress, QPointF(500, 320),
                  Qt::LeftButton);
        sendMouse(widget, QEvent::MouseButtonRelease, QPointF(500, 320),
                  Qt::NoButton);
        require(selected_track == static_cast<qint64>(lower_track.track_id) &&
                    selected_clip == static_cast<qint64>(lower_track.clips.front().clip_id),
                "The Timeline did not select the clip in the clicked row.");

        // Alt becomes the seek override when movement no longer requires it.
        widget.setActiveClip(timeline::ClipLocation{0, 0});
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
        const auto left_edge_x = widget.contentXForFrame(0) + 2.0;
        const auto right_edge_x = widget.contentXForFrame(test_clip_duration);
        sendMouse(widget, QEvent::MouseMove, QPointF(left_edge_x, 120), Qt::NoButton);
        require(widget.cursor().shape() == Qt::SizeHorCursor,
                "Hovering the left clip edge did not show the resize cursor.");
        require(trim_edge == -1 && trim_boundary == -1,
                "Hovering a clip edge changed the timeline.");
        sendMouse(widget, QEvent::MouseMove, QPointF(300, 120), Qt::NoButton);
        require(widget.cursor().shape() == Qt::ArrowCursor,
                "The resize cursor remained inside the clip away from its edges.");
        sendMouse(widget, QEvent::MouseMove, QPointF(right_edge_x, 120), Qt::NoButton);
        require(widget.cursor().shape() == Qt::SizeHorCursor,
                "Hovering the exact right clip edge did not show the resize cursor.");
        sendMouse(widget, QEvent::MouseMove,
                  QPointF(right_edge_x + 20.0, 120), Qt::NoButton);
        require(widget.cursor().shape() == Qt::ArrowCursor,
                "The resize cursor remained outside the clip edge.");
        sendMouse(widget, QEvent::MouseMove, QPointF(left_edge_x, 120), Qt::NoButton);
        QEvent leave_timeline(QEvent::Leave);
        QApplication::sendEvent(&widget, &leave_timeline);
        require(widget.cursor().shape() == Qt::ArrowCursor,
                "Leaving the Timeline did not restore the default cursor.");

        sendMouse(widget, QEvent::MouseButtonPress, QPointF(156, 120),
                  Qt::LeftButton);
        sendMouse(widget, QEvent::MouseButtonRelease, QPointF(156, 120),
                  Qt::NoButton);
        require(trim_edge == -1 && trim_boundary == -1,
                "Clicking a clip edge changed its duration without a drag.");
        sendMouse(widget, QEvent::MouseButtonPress, QPointF(156, 120),
                  Qt::LeftButton);
        sendMouse(widget, QEvent::MouseMove, QPointF(200, 120),
                  Qt::LeftButton);
        require(trim_edge == -1 && trim_boundary == -1,
                "Trimming was committed before mouse release.");
        sendMouse(widget, QEvent::MouseButtonRelease, QPointF(200, 120),
                  Qt::NoButton);
        require(trim_edge == static_cast<qint64>(timeline::ClipEdge::Left) &&
                    trim_boundary > 0 && trim_boundary < test_clip_duration &&
                    trim_mode == static_cast<qint64>(
                        timeline::ClipEdgeEditMode::Individual),
                "The left edge did not request an absolute trim boundary.");

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
        require(transition_track == static_cast<qint64>(junction_track.track_id) &&
                    transition_from == static_cast<qint64>(junction_track.clips[0].clip_id) &&
                    transition_to == static_cast<qint64>(junction_track.clips[1].clip_id),
                "The contiguous junction was not detected for transition selection.");

        timeline::TimelineWidget trim_preview_widget;
        trim_preview_widget.resize(1000, 300);
        trim_preview_widget.setTimelineViewportWidth(1000);
        auto preview_video = makeClip("preview.mkv", 50, 50, "preview.mkv");
        preview_video.source_start_frame = 50;
        preview_video.frame_count = 200;
        preview_video.duration_seconds = 200.0 / 30.0;
        timeline::TimelineClip preview_text;
        preview_text.timeline_start_frame = 100;
        preview_text.timeline_duration_frames = 50;
        preview_text.display_name = "Title";
        preview_text.kind = timeline::ClipKind::Text;
        preview_text.frame_rate = 30.0;
        preview_text.frame_count = 50;
        trim_preview_widget.setTracks({timeline::TimelineTrack{
            1, "Video 1", 1.0, false, {preview_video, preview_text}}});
        trim_preview_widget.setZoomFactor(512.0);
        trim_preview_widget.setMinimumWidth(1000);
        trim_preview_widget.resize(1000, 300);
        trim_preview_widget.show();
        application.processEvents();
        QImage trim_before(1000, 180, QImage::Format_ARGB32);
        trim_before.fill(Qt::transparent);
        trim_preview_widget.render(&trim_before);
        const auto preview_sample_x = static_cast<int>(std::lround(
            trim_preview_widget.contentXForFrame(105)));
        const auto trim_track_y = static_cast<int>(std::lround(
            trim_preview_widget.trackBounds(0).center().y()));
        const auto preview_sample = QPoint(preview_sample_x, trim_track_y);
        const auto color_before_trim = trim_before.pixelColor(preview_sample);
        int edge_edit_count = 0;
        qint64 edge_edit_boundary = -1;
        qint64 edge_edit_mode = -1;
        QObject::connect(
            &trim_preview_widget,
            &timeline::TimelineWidget::clipEdgeTrimRequested,
            [&edge_edit_count, &edge_edit_boundary, &edge_edit_mode](
                timeline::ClipId, qint64, qint64 boundary, qint64 mode) {
                ++edge_edit_count;
                edge_edit_boundary = boundary;
                edge_edit_mode = mode;
            });
        const auto seam_x = trim_preview_widget.contentXForFrame(100);
        const auto moved_seam_x = trim_preview_widget.contentXForFrame(110);
        sendMouse(trim_preview_widget, QEvent::MouseButtonPress,
                  QPointF(seam_x, trim_track_y), Qt::LeftButton);
        sendMouse(trim_preview_widget, QEvent::MouseMove,
                  QPointF(moved_seam_x, trim_track_y), Qt::LeftButton);
        require(edge_edit_count == 0,
                "Dragging a shared edge committed the edit before release.");
        QImage trim_during(1000, 180, QImage::Format_ARGB32);
        trim_during.fill(Qt::transparent);
        trim_preview_widget.render(&trim_during);
        require(trim_during.pixelColor(preview_sample) != color_before_trim,
                "The shared clip boundary did not update in the live preview.");
        sendMouse(trim_preview_widget, QEvent::MouseButtonRelease,
                  QPointF(moved_seam_x, trim_track_y), Qt::NoButton);
        require(edge_edit_count == 1 && edge_edit_boundary == 110 &&
                    edge_edit_mode == static_cast<qint64>(
                        timeline::ClipEdgeEditMode::Rolling),
                "Dragging a shared edge did not commit the requested boundary once.");
        trim_preview_widget.close();

        timeline::TimelineWidget single_clip_edge_widget;
        single_clip_edge_widget.resize(1000, 300);
        single_clip_edge_widget.setTimelineViewportWidth(1000);
        auto first_overlapping_video = makeClip(
            "first-overlap.mkv", 0, 100, "first-overlap.mkv");
        auto second_overlapping_video = makeClip(
            "second-overlap.mkv", 100, 100, "second-overlap.mkv");
        first_overlapping_video.frame_count = 300;
        first_overlapping_video.duration_seconds = 10.0;
        second_overlapping_video.source_start_frame = 100;
        second_overlapping_video.frame_count = 300;
        second_overlapping_video.duration_seconds = 10.0;
        single_clip_edge_widget.setTracks({timeline::TimelineTrack{
            1, "Video 1", 1.0, false,
            {first_overlapping_video, second_overlapping_video}}});
        single_clip_edge_widget.setZoomFactor(512.0);
        single_clip_edge_widget.setMinimumWidth(1000);
        single_clip_edge_widget.resize(1000, 300);
        single_clip_edge_widget.show();
        application.processEvents();
        int single_clip_edit_count = 0;
        qint64 single_clip_edit_edge = -1;
        qint64 single_clip_edit_mode = -1;
        qint64 single_clip_edit_boundary = -1;
        QObject::connect(
            &single_clip_edge_widget,
            &timeline::TimelineWidget::clipEdgeTrimRequested,
            [&single_clip_edit_count, &single_clip_edit_edge, &single_clip_edit_mode,
             &single_clip_edit_boundary](
                timeline::ClipId, qint64 edge, qint64 boundary, qint64 mode) {
                ++single_clip_edit_count;
                single_clip_edit_edge = edge;
                single_clip_edit_boundary = boundary;
                single_clip_edit_mode = mode;
            });
        const auto single_seam_x =
            single_clip_edge_widget.contentXForFrame(100);
        const auto single_clip_sample = QPoint(
            static_cast<int>(std::lround(
                single_clip_edge_widget.contentXForFrame(95))),
            110);
        const auto samplePixel = [&single_clip_edge_widget, single_clip_sample]() {
            QImage image(1000, 180, QImage::Format_ARGB32);
            image.fill(Qt::transparent);
            single_clip_edge_widget.render(&image);
            return image.pixelColor(single_clip_sample);
        };
        const auto preview_edge_sample = QPoint(
            static_cast<int>(std::lround(
                single_clip_edge_widget.contentXForFrame(110))),
            110);
        const auto previewEdgePixel = [&single_clip_edge_widget,
                                       preview_edge_sample]() {
            QImage image(1000, 180, QImage::Format_ARGB32);
            image.fill(Qt::transparent);
            single_clip_edge_widget.render(&image);
            return image.pixelColor(preview_edge_sample);
        };
        const auto individual_before = samplePixel();
        const auto edge_guide_before = previewEdgePixel();
        const QPointF individual_grab(single_seam_x - 8.0, 110.0);
        sendMouse(single_clip_edge_widget, QEvent::MouseMove,
                  individual_grab, Qt::NoButton);
        require(single_clip_edge_widget.cursor().shape() == Qt::SizeHorCursor,
                "The individual side of a shared cut did not show the resize cursor.");
        sendMouse(single_clip_edge_widget, QEvent::MouseButtonPress,
                  individual_grab, Qt::LeftButton);
        sendMouse(single_clip_edge_widget, QEvent::MouseMove,
                  QPointF(single_clip_edge_widget.contentXForFrame(110), 110),
                  Qt::LeftButton);
        require(single_clip_edit_count == 0 && samplePixel() != individual_before &&
                    previewEdgePixel() != edge_guide_before,
                "An individual edge drag did not preview without committing.");
        sendMouse(single_clip_edge_widget, QEvent::MouseButtonRelease,
                  QPointF(single_clip_edge_widget.contentXForFrame(110), 110),
                  Qt::NoButton);
        require(single_clip_edit_count == 1 &&
                    single_clip_edit_edge == static_cast<qint64>(
                        timeline::ClipEdge::Right) &&
                    single_clip_edit_mode == static_cast<qint64>(
                        timeline::ClipEdgeEditMode::Individual) &&
                    single_clip_edit_boundary == 110,
                "A side handle did not commit one individual edge edit on release.");
        const QPointF right_individual_grab(single_seam_x + 8.0, 110.0);
        sendMouse(single_clip_edge_widget, QEvent::MouseMove,
                  right_individual_grab, Qt::NoButton);
        require(single_clip_edge_widget.cursor().shape() == Qt::SizeHorCursor,
                "The opposite side of a shared cut did not show the resize cursor.");
        sendMouse(single_clip_edge_widget, QEvent::MouseButtonPress,
                  right_individual_grab, Qt::LeftButton);
        sendMouse(single_clip_edge_widget, QEvent::MouseMove,
                  QPointF(single_clip_edge_widget.contentXForFrame(90), 110),
                  Qt::LeftButton);
        sendMouse(single_clip_edge_widget, QEvent::MouseButtonRelease,
                  QPointF(single_clip_edge_widget.contentXForFrame(90), 110),
                  Qt::NoButton);
        require(single_clip_edit_count == 2 &&
                    single_clip_edit_edge == static_cast<qint64>(
                        timeline::ClipEdge::Left) &&
                    single_clip_edit_mode == static_cast<qint64>(
                        timeline::ClipEdgeEditMode::Individual) &&
                    single_clip_edit_boundary == 90,
                "The opposite side handle did not commit one individual edge edit.");
        single_clip_edge_widget.close();

        timeline::TimelineWidget edge_drag_widget;
        edge_drag_widget.resize(1000, 300);
        edge_drag_widget.setTimelineViewportWidth(1000);
        auto edge_drag_video = makeClip("edge-drag.mkv", 50, 50, "edge-drag.mkv");
        edge_drag_video.source_start_frame = 50;
        edge_drag_video.frame_count = 200;
        edge_drag_video.duration_seconds = 200.0 / 30.0;
        edge_drag_widget.setTracks({timeline::TimelineTrack{
            1, "Video 1", 1.0, false, {edge_drag_video}}});
        edge_drag_widget.setZoomFactor(512.0);
        edge_drag_widget.setMinimumWidth(1000);
        edge_drag_widget.resize(1000, 300);
        edge_drag_widget.show();
        application.processEvents();
        int edge_drag_commit_count = 0;
        qint64 last_drag_edge = -1;
        qint64 last_drag_boundary = -1;
        QObject::connect(
            &edge_drag_widget,
            &timeline::TimelineWidget::clipEdgeTrimRequested,
            [&edge_drag_commit_count, &last_drag_edge, &last_drag_boundary](
                timeline::ClipId, qint64 edge, qint64 boundary, qint64) {
                ++edge_drag_commit_count;
                last_drag_edge = edge;
                last_drag_boundary = boundary;
            });
        const auto pixelAtFrame = [&edge_drag_widget](std::int64_t frame) {
            QImage image(1000, 180, QImage::Format_ARGB32);
            image.fill(Qt::transparent);
            edge_drag_widget.render(&image);
            return image.pixelColor(
                QPoint(static_cast<int>(std::lround(
                            edge_drag_widget.contentXForFrame(frame))),
                       110));
        };
        const auto left_extension_before = pixelAtFrame(45);
        const auto left_edge_drag_x = edge_drag_widget.contentXForFrame(50);
        sendMouse(edge_drag_widget, QEvent::MouseMove,
                  QPointF(left_edge_drag_x, 110), Qt::NoButton);
        require(edge_drag_widget.cursor().shape() == Qt::SizeHorCursor,
                "The split clip's left edge did not expose the resize cursor.");
        sendMouse(edge_drag_widget, QEvent::MouseButtonPress,
                  QPointF(left_edge_drag_x, 110), Qt::LeftButton);
        sendMouse(edge_drag_widget, QEvent::MouseMove,
                  QPointF(edge_drag_widget.contentXForFrame(40), 110),
                  Qt::LeftButton);
        require(edge_drag_commit_count == 0,
                "The left edge extension committed before release.");
        const auto left_extension_during = pixelAtFrame(45);
        require(left_extension_during != left_extension_before,
                "Extending the left edge did not update the live preview (before " +
                    left_extension_before.name().toStdString() + ", during " +
                    left_extension_during.name().toStdString() + ").");
        sendMouse(edge_drag_widget, QEvent::MouseButtonRelease,
                  QPointF(edge_drag_widget.contentXForFrame(40), 110),
                  Qt::NoButton);
        require(edge_drag_commit_count == 1 &&
                    last_drag_edge == static_cast<qint64>(timeline::ClipEdge::Left) &&
                    last_drag_boundary == 40,
                "The left edge extension was not committed once on release.");

        const auto right_extension_before = pixelAtFrame(105);
        const auto right_edge_drag_x = edge_drag_widget.contentXForFrame(100);
        sendMouse(edge_drag_widget, QEvent::MouseMove,
                  QPointF(right_edge_drag_x, 110), Qt::NoButton);
        require(edge_drag_widget.cursor().shape() == Qt::SizeHorCursor,
                "The split clip's exact right edge did not expose the resize cursor.");
        sendMouse(edge_drag_widget, QEvent::MouseButtonPress,
                  QPointF(right_edge_drag_x, 110), Qt::LeftButton);
        sendMouse(edge_drag_widget, QEvent::MouseMove,
                  QPointF(edge_drag_widget.contentXForFrame(110), 110),
                  Qt::LeftButton);
        require(edge_drag_commit_count == 1 &&
                    pixelAtFrame(105) != right_extension_before,
                "Extending the right edge did not preview until release.");
        sendMouse(edge_drag_widget, QEvent::MouseButtonRelease,
                  QPointF(edge_drag_widget.contentXForFrame(110), 110),
                  Qt::NoButton);
        require(edge_drag_commit_count == 2 &&
                    last_drag_edge == static_cast<qint64>(timeline::ClipEdge::Right) &&
                    last_drag_boundary == 110,
                "The right edge extension was not committed once on release.");
        edge_drag_widget.close();

        // The shared-cut gesture defers selection until a valid move and
        // commits at the release position, preserving the signal order.
        timeline::TimelineWidget gesture_widget;
        gesture_widget.resize(1000, 300);
        gesture_widget.setTimelineViewportWidth(1000);
        auto gesture_first = makeClip("gesture-first.mkv", 0, 100, "First");
        auto gesture_second = makeClip("gesture-second.mkv", 100, 100, "Second");
        gesture_first.clip_id = 501;
        gesture_second.clip_id = 502;
        gesture_first.frame_count = 300;
        gesture_second.source_start_frame = 100;
        gesture_second.frame_count = 300;
        gesture_widget.setTracks({timeline::TimelineTrack{
            1, "Video 1", 1.0, false, {gesture_first, gesture_second}}});
        gesture_widget.setZoomFactor(512.0);
        gesture_widget.setMinimumWidth(1000);
        gesture_widget.resize(1000, 300);
        gesture_widget.show();
        application.processEvents();
        std::vector<std::string> gesture_signals;
        qint64 gesture_boundary = -1;
        QObject::connect(
            &gesture_widget, &timeline::TimelineWidget::transitionSelected,
            [&gesture_signals](timeline::TrackId, timeline::ClipId from,
                               timeline::ClipId to) {
                if (from >= 0 && to >= 0) gesture_signals.emplace_back("transition");
            });
        QObject::connect(
            &gesture_widget, &timeline::TimelineWidget::clipSelected,
            [&gesture_signals](timeline::TrackId, timeline::ClipId) {
                gesture_signals.emplace_back("select");
            });
        QObject::connect(
            &gesture_widget, &timeline::TimelineWidget::trimStarted,
            [&gesture_signals]() { gesture_signals.emplace_back("start"); });
        QObject::connect(
            &gesture_widget, &timeline::TimelineWidget::clipEdgeTrimRequested,
            [&gesture_signals, &gesture_boundary](
                timeline::ClipId, qint64, qint64 boundary, qint64) {
                gesture_signals.emplace_back("modern");
                gesture_boundary = boundary;
            });
        const auto gesture_seam_x = gesture_widget.contentXForFrame(100);
        sendMouse(gesture_widget, QEvent::MouseButtonPress,
                  QPointF(gesture_seam_x, 110), Qt::LeftButton);
        require(gesture_signals.empty(),
                "Pressing a shared cut selected a clip or started a trim.");
        sendMouse(gesture_widget, QEvent::MouseMove,
                  QPointF(gesture_seam_x, 110), Qt::LeftButton);
        require(gesture_signals.empty(),
                "An unchanged shared boundary started a trim.");
        sendMouse(gesture_widget, QEvent::MouseButtonRelease,
                  QPointF(gesture_seam_x, 110), Qt::NoButton);
        require(gesture_signals == std::vector<std::string>{"transition"},
                "A shared-cut click did not select only the transition.");
        gesture_signals.clear();
        sendMouse(gesture_widget, QEvent::MouseButtonPress,
                  QPointF(gesture_seam_x, 110), Qt::LeftButton);
        sendMouse(gesture_widget, QEvent::MouseMove,
                  QPointF(gesture_widget.contentXForFrame(110), 110), Qt::LeftButton);
        require(gesture_signals ==
                    (std::vector<std::string>{"select", "start"}),
                "A valid shared-cut move changed the selection/start signal order.");
        sendMouse(gesture_widget, QEvent::MouseButtonRelease,
                  QPointF(gesture_widget.contentXForFrame(112), 110), Qt::NoButton);
        std::string gesture_result;
        for (const auto& signal : gesture_signals) gesture_result += signal + ",";
        require(gesture_signals ==
                    (std::vector<std::string>{"select", "start", "modern"}) &&
                    gesture_boundary == 112,
                "A shared-cut release did not request one trim at its final position: " +
                    gesture_result + " boundary=" + std::to_string(gesture_boundary));

        // MainWindow refreshes the widget's tracks synchronously from the
        // selection signal. Promotion must survive that refresh.
        gesture_signals.clear();
        const auto selection_refresh = QObject::connect(
            &gesture_widget, &timeline::TimelineWidget::clipSelected,
            [&gesture_widget, &gesture_first, &gesture_second](
                timeline::TrackId, timeline::ClipId) {
                gesture_widget.setTracks({timeline::TimelineTrack{
                    1, "Video 1", 1.0, false,
                    {gesture_first, gesture_second}}});
            });
        const auto refreshed_start_x = gesture_widget.contentXForFrame(100);
        sendMouse(gesture_widget, QEvent::MouseButtonPress,
                  QPointF(refreshed_start_x, 110), Qt::LeftButton);
        sendMouse(gesture_widget, QEvent::MouseMove,
                  QPointF(gesture_widget.contentXForFrame(110), 110), Qt::LeftButton);
        sendMouse(gesture_widget, QEvent::MouseButtonRelease,
                  QPointF(gesture_widget.contentXForFrame(112), 110), Qt::NoButton);
        QObject::disconnect(selection_refresh);
        require(gesture_signals ==
                    (std::vector<std::string>{"select", "start", "modern"}),
                "A synchronous selection refresh cancelled the shared-cut trim.");

        gesture_signals.clear();
        const auto cancelled_seam_x = gesture_widget.contentXForFrame(100);
        sendMouse(gesture_widget, QEvent::MouseButtonPress,
                  QPointF(cancelled_seam_x, 110), Qt::LeftButton);
        sendMouse(gesture_widget, QEvent::MouseMove,
                  QPointF(gesture_widget.contentXForFrame(110), 110), Qt::LeftButton);
        gesture_widget.setTracks({timeline::TimelineTrack{
            1, "Video 1", 1.0, false, {gesture_first, gesture_second}}});
        require(gesture_widget.cursor().shape() == Qt::ArrowCursor,
                "Replacing tracks during a trim left the resize cursor active.");
        sendMouse(gesture_widget, QEvent::MouseButtonRelease,
                  QPointF(gesture_widget.contentXForFrame(112), 110), Qt::NoButton);
        require(gesture_signals ==
                    (std::vector<std::string>{"select", "start"}),
                "Replacing tracks during a trim left a pending edit request.");
        gesture_signals.clear();
        const auto refreshed_seam_x = gesture_widget.contentXForFrame(100);
        sendMouse(gesture_widget, QEvent::MouseButtonPress,
                  QPointF(refreshed_seam_x, 110), Qt::LeftButton);
        require(gesture_signals.empty(),
                "Pressing the refreshed shared cut was not pending: old=" +
                    std::to_string(gesture_seam_x) + " new=" +
                    std::to_string(refreshed_seam_x));
        gesture_widget.clearClips();
        require(gesture_widget.cursor().shape() == Qt::ArrowCursor,
                "Clearing clips during a trim left the resize cursor active.");
        sendMouse(gesture_widget, QEvent::MouseButtonRelease,
                  QPointF(gesture_seam_x, 110), Qt::NoButton);
        std::string cleared_signals;
        for (const auto& signal : gesture_signals) cleared_signals += signal + ",";
        require(gesture_signals.empty(),
                "Clearing clips during a pending shared cut emitted a trim or selection: " +
                    cleared_signals);
        gesture_widget.close();

        timeline::TimelineWidget legacy_trim_widget;
        legacy_trim_widget.resize(1000, 300);
        legacy_trim_widget.setTimelineViewportWidth(1000);
        auto legacy_clip = makeClip("legacy-trim.mkv", 50, 50, "Legacy");
        legacy_clip.source_start_frame = 50;
        legacy_clip.frame_count = 300;
        legacy_trim_widget.setTracks({timeline::TimelineTrack{
            1, "Video 1", 1.0, false, {legacy_clip}}});
        legacy_trim_widget.setZoomFactor(512.0);
        legacy_trim_widget.setMinimumWidth(1000);
        legacy_trim_widget.resize(1000, 300);
        legacy_trim_widget.show();
        application.processEvents();
        std::vector<std::string> legacy_signals;
        QObject::connect(&legacy_trim_widget,
                         &timeline::TimelineWidget::clipSelected,
                         [&legacy_signals](timeline::TrackId, timeline::ClipId) {
                             legacy_signals.emplace_back("select");
                         });
        QObject::connect(&legacy_trim_widget,
                         &timeline::TimelineWidget::trimStarted,
                         [&legacy_signals]() {
                             legacy_signals.emplace_back("start");
                         });
        QObject::connect(&legacy_trim_widget,
                         &timeline::TimelineWidget::clipEdgeTrimRequested,
                         [&legacy_signals](timeline::ClipId, qint64, qint64, qint64) {
                             legacy_signals.emplace_back("modern");
                         });
        const auto legacy_left_x = legacy_trim_widget.contentXForFrame(50);
        sendMouse(legacy_trim_widget, QEvent::MouseButtonPress,
                  QPointF(legacy_left_x, 110), Qt::LeftButton);
        sendMouse(legacy_trim_widget, QEvent::MouseButtonRelease,
                  QPointF(legacy_left_x, 110), Qt::NoButton);
        require(legacy_signals ==
                    (std::vector<std::string>{"select", "start"}),
                "A no-op edge click requested a trim.");
        legacy_signals.clear();
        sendMouse(legacy_trim_widget, QEvent::MouseButtonPress,
                  QPointF(legacy_left_x, 110), Qt::LeftButton);
        sendMouse(legacy_trim_widget, QEvent::MouseMove,
                  QPointF(legacy_trim_widget.contentXForFrame(60), 110),
                  Qt::LeftButton);
        require(legacy_signals ==
                    (std::vector<std::string>{"select", "start"}),
                "An individual trim committed before release.");
        sendMouse(legacy_trim_widget, QEvent::MouseButtonRelease,
                  QPointF(legacy_trim_widget.contentXForFrame(70), 110),
                  Qt::NoButton);
        require(legacy_signals ==
                    (std::vector<std::string>{"select", "start", "modern"}),
                "An individual trim did not emit one stable-ID trim request.");
        legacy_trim_widget.close();

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

        // Internal clip movement keeps the source visible but dimmed and
        // paints a destination ghost without changing the model until release.
        timeline::TimelineWidget drag_preview_widget;
        drag_preview_widget.resize(900, 380);
        drag_preview_widget.setMoveRequiresAlt(false);
        const auto preview_source_track = timeline::TimelineTrack{
            2,
            "Video 2",
            1.0,
            false,
            {makeClip("preview-source.mkv", 0, 12000, "Preview source")}};
        const auto preview_empty_track = timeline::TimelineTrack{
            1,
            "Video 1",
            1.0,
            false,
            {}};
        drag_preview_widget.setTracks(
            {preview_source_track, preview_empty_track});
        drag_preview_widget.show();
        application.processEvents();
        drag_preview_widget.setTimelineViewportWidth(900);
        application.processEvents();

        QImage preview_before(900, 260, QImage::Format_ARGB32);
        preview_before.fill(Qt::transparent);
        drag_preview_widget.render(&preview_before);
        const auto source_sample_x = static_cast<int>(
            std::lround(drag_preview_widget.contentXForFrame(6000)));
        const auto destination_sample_x = static_cast<int>(
            std::lround(drag_preview_widget.contentXForFrame(36000)));
        const auto source_sample_y = 48 + 35;
        const auto destination_sample_y = 48 + 70 + 10 + 35;
        const auto source_before = preview_before.pixelColor(
            source_sample_x, source_sample_y);
        const auto destination_before = preview_before.pixelColor(
            destination_sample_x, destination_sample_y);

        int preview_move_count = 0;
        QObject::connect(
            &drag_preview_widget,
            &timeline::TimelineWidget::clipMoveRequested,
            [&preview_move_count](timeline::ClipId, timeline::TrackId, qint64) {
                ++preview_move_count;
            });
        const auto source_press = QPointF(
            drag_preview_widget.contentXForFrame(6000), source_sample_y);
        const auto destination_move = QPointF(
            drag_preview_widget.contentXForFrame(30000), destination_sample_y);
        sendMouse(
            drag_preview_widget,
            QEvent::MouseButtonPress,
            source_press,
            Qt::LeftButton);
        sendMouse(
            drag_preview_widget,
            QEvent::MouseMove,
            destination_move,
            Qt::LeftButton);
        require(
            preview_move_count == 0,
            "Moving a clip emitted its model change before release.");

        QImage preview_moving(900, 260, QImage::Format_ARGB32);
        preview_moving.fill(Qt::transparent);
        drag_preview_widget.render(&preview_moving);
        require(
            preview_moving.pixelColor(source_sample_x, source_sample_y) !=
                source_before,
            "The source clip was not visually dimmed during movement.");
        require(
            preview_moving.pixelColor(destination_sample_x, destination_sample_y) !=
                destination_before,
            "The internal clip movement ghost was not painted at its destination.");

        sendMouse(
            drag_preview_widget,
            QEvent::MouseButtonRelease,
            destination_move,
            Qt::NoButton);
        require(
            preview_move_count == 1,
            "The internal clip movement did not emit exactly once on release.");
        QImage preview_after_release(900, 260, QImage::Format_ARGB32);
        preview_after_release.fill(Qt::transparent);
        drag_preview_widget.render(&preview_after_release);
        require(
            preview_after_release.pixelColor(
                destination_sample_x, destination_sample_y) ==
                destination_before,
            "The internal movement ghost was not cleared after release.");

        // An occupied destination keeps the existing drop/move policy but is
        // shown as a red translucent ghost so the conflict is visible.
        const auto preview_occupied_track = timeline::TimelineTrack{
            1,
            "Video 1",
            1.0,
            false,
            {makeClip("occupied.mkv", 30000, 12000, "Occupied")}};
        drag_preview_widget.setTracks(
            {preview_source_track, preview_occupied_track});
        application.processEvents();
        sendMouse(
            drag_preview_widget,
            QEvent::MouseButtonPress,
            source_press,
            Qt::LeftButton);
        sendMouse(
            drag_preview_widget,
            QEvent::MouseMove,
            destination_move,
            Qt::LeftButton);
        QImage preview_occupied(900, 260, QImage::Format_ARGB32);
        preview_occupied.fill(Qt::transparent);
        drag_preview_widget.render(&preview_occupied);
        const auto occupied_color = preview_occupied.pixelColor(
            destination_sample_x, destination_sample_y);
        require(
            occupied_color.red() > occupied_color.green() &&
                occupied_color.red() > occupied_color.blue(),
            "An occupied movement destination was not painted as an invalid red ghost.");
        sendMouse(
            drag_preview_widget,
            QEvent::MouseButtonRelease,
            destination_move,
            Qt::NoButton);

        // Media drops use optional metadata for an exact ghost duration while
        // retaining the existing path MIME and drop behavior.
        timeline::TimelineWidget media_preview_widget;
        media_preview_widget.resize(900, 180);
        media_preview_widget.setTracks({preview_empty_track});
        media_preview_widget.show();
        application.processEvents();
        media_preview_widget.setTimelineViewportWidth(900);
        QMimeData preview_media_mime;
        preview_media_mime.setData(
            ui::kMediaPathMimeType,
            QByteArrayLiteral("preview-media.mp4"));
        preview_media_mime.setData(
            ui::kMediaFrameCountMimeType,
            QByteArrayLiteral("12000"));
        preview_media_mime.setData(
            ui::kMediaFrameRateMimeType,
            QByteArrayLiteral("30"));
        preview_media_mime.setData(
            ui::kMediaDisplayNameMimeType,
            QByteArrayLiteral("Preview media"));
        const auto media_preview_position = QPointF(
            media_preview_widget.contentXForFrame(30000),
            48 + 35);
        QImage media_preview_before(900, 180, QImage::Format_ARGB32);
        media_preview_before.fill(Qt::transparent);
        media_preview_widget.render(&media_preview_before);
        const auto media_preview_sample_x = static_cast<int>(std::lround(
            media_preview_widget.contentXForFrame(36000)));
        const auto media_preview_sample_y = 48 + 35;
        const auto media_before = media_preview_before.pixelColor(
            media_preview_sample_x, media_preview_sample_y);
        QDragEnterEvent media_preview_enter(
            media_preview_position.toPoint(),
            Qt::CopyAction,
            &preview_media_mime,
            Qt::LeftButton,
            Qt::NoModifier);
        QApplication::sendEvent(&media_preview_widget, &media_preview_enter);
        QDragMoveEvent media_preview_move(
            media_preview_position.toPoint(),
            Qt::CopyAction,
            &preview_media_mime,
            Qt::LeftButton,
            Qt::NoModifier);
        QApplication::sendEvent(&media_preview_widget, &media_preview_move);
        QImage media_preview_active(900, 180, QImage::Format_ARGB32);
        media_preview_active.fill(Qt::transparent);
        media_preview_widget.render(&media_preview_active);
        require(
            media_preview_active.pixelColor(
                media_preview_sample_x, media_preview_sample_y) != media_before,
            "The media drop ghost was not painted from the MIME metadata.");
        QDragLeaveEvent media_preview_leave;
        QApplication::sendEvent(&media_preview_widget, &media_preview_leave);
        QImage media_preview_after_leave(900, 180, QImage::Format_ARGB32);
        media_preview_after_leave.fill(Qt::transparent);
        media_preview_widget.render(&media_preview_after_leave);
        require(
            media_preview_after_leave.pixelColor(
                media_preview_sample_x, media_preview_sample_y) == media_before,
            "The media drop ghost was not cleared after cancellation.");

        media_preview_widget.close();
        drag_preview_widget.close();

        // Magnetic snapping aligns either edge of a dragged clip to another
        // clip edge while the model remains untouched until release.
        timeline::TimelineWidget snap_widget;
        snap_widget.resize(900, 380);
        snap_widget.setTimelineViewportWidth(900);
        const auto snap_source_clip = makeClip(
            "snap-source.mkv", 0, 12000, "Snap source");
        const auto snap_target_clip = makeClip(
            "snap-target.mkv", 30000, 12000, "Snap target");
        const auto snap_same_track = timeline::TimelineTrack{
            1,
            "Video 1",
            1.0,
            false,
            {snap_source_clip, snap_target_clip}};
        snap_widget.setTracks({snap_same_track});
        snap_widget.show();
        application.processEvents();
        require(snap_widget.snapEnabled(),
                "Magnetic snapping was not enabled on the Timeline widget.");
        int snap_state_changes = 0;
        bool last_snap_state = true;
        QObject::connect(
            &snap_widget,
            &timeline::TimelineWidget::snapEnabledChanged,
            [&snap_state_changes, &last_snap_state](bool enabled) {
                ++snap_state_changes;
                last_snap_state = enabled;
            });
        snap_widget.setSnapEnabled(false);
        require(!snap_widget.snapEnabled() && snap_state_changes == 1 &&
                    !last_snap_state,
                "Magnetic snapping could not be disabled through its API.");
        snap_widget.setSnapEnabled(true);
        require(snap_widget.snapEnabled() && snap_state_changes == 2 &&
                    last_snap_state,
                "Magnetic snapping could not be re-enabled through its API.");

        int snap_move_count = 0;
        qint64 snap_target_track = -1;
        qint64 snap_target_frame = -1;
        QObject::connect(
            &snap_widget,
            &timeline::TimelineWidget::clipMoveRequested,
            [&snap_move_count, &snap_target_track, &snap_target_frame](
                timeline::ClipId, timeline::TrackId track, qint64 frame) {
                ++snap_move_count;
                snap_target_track = track;
                snap_target_frame = frame;
            });
        const auto snap_source_y = 48.0 + 35.0;
        const auto snap_press = QPointF(
            snap_widget.contentXForFrame(6000), snap_source_y);
        const auto snap_near_target = QPointF(
            snap_widget.contentXForFrame(19000), snap_source_y);
        QImage snap_before(900, 260, QImage::Format_ARGB32);
        snap_before.fill(Qt::transparent);
        snap_widget.render(&snap_before);
        sendMouse(
            snap_widget,
            QEvent::MouseButtonPress,
            snap_press,
            Qt::LeftButton);
        sendMouse(
            snap_widget,
            QEvent::MouseMove,
            snap_near_target,
            Qt::LeftButton);
        require(snap_move_count == 0,
                "Magnetic snapping emitted a move before release.");
        QImage snap_active(900, 260, QImage::Format_ARGB32);
        snap_active.fill(Qt::transparent);
        snap_widget.render(&snap_active);
        const auto snap_guide_x = static_cast<int>(std::lround(
            snap_widget.contentXForFrame(30000)));
        require(
            snap_active.pixelColor(snap_guide_x, 52) !=
                snap_before.pixelColor(snap_guide_x, 52),
            "Magnetic snapping did not paint its aligned guide line.");
        sendMouse(
            snap_widget,
            QEvent::MouseButtonRelease,
            snap_near_target,
            Qt::NoButton);
        require(snap_move_count == 1 && snap_target_track ==
                    static_cast<qint64>(snap_same_track.track_id) &&
                    snap_target_frame == 18000,
                "A clip edge did not snap to the adjacent clip boundary.");

        // A pointer farther than the visual tolerance keeps its raw frame.
        snap_widget.setTracks({snap_same_track});
        const auto snap_outside_tolerance = QPointF(
            snap_widget.contentXForFrame(21000), snap_source_y);
        sendMouse(
            snap_widget,
            QEvent::MouseButtonPress,
            snap_press,
            Qt::LeftButton);
        sendMouse(
            snap_widget,
            QEvent::MouseMove,
            snap_outside_tolerance,
            Qt::LeftButton);
        sendMouse(
            snap_widget,
            QEvent::MouseButtonRelease,
            snap_outside_tolerance,
            Qt::NoButton);
        require(snap_move_count == 2 && snap_target_frame == 21000,
                "A clip outside the snap tolerance was moved to an adjusted frame.");

        snap_widget.setSnapEnabled(false);
        snap_widget.setTracks({snap_same_track});
        sendMouse(
            snap_widget,
            QEvent::MouseButtonPress,
            snap_press,
            Qt::LeftButton);
        sendMouse(
            snap_widget,
            QEvent::MouseMove,
            snap_near_target,
            Qt::LeftButton);
        sendMouse(
            snap_widget,
            QEvent::MouseButtonRelease,
            snap_near_target,
            Qt::NoButton);
        require(snap_move_count == 3 && snap_target_frame == 19000,
                "Disabling magnetic snapping did not preserve the raw frame.");
        snap_widget.setSnapEnabled(true);

        // The same edge calculation works when moving to another track and
        // when the source clip is brought to either Timeline boundary.
        const auto snap_destination_track = timeline::TimelineTrack{
            2,
            "Video 2",
            1.0,
            false,
            {snap_target_clip}};
        snap_widget.setTracks({
            timeline::TimelineTrack{1, "Video 1", 1.0, false, {snap_source_clip}},
            snap_destination_track});
        const auto snap_second_track_y = 48.0 + 70.0 + 10.0 + 35.0;
        const auto snap_cross_track = QPointF(
            snap_widget.contentXForFrame(19000), snap_second_track_y);
        sendMouse(
            snap_widget,
            QEvent::MouseButtonPress,
            snap_press,
            Qt::LeftButton);
        sendMouse(
            snap_widget,
            QEvent::MouseMove,
            snap_cross_track,
            Qt::LeftButton);
        sendMouse(
            snap_widget,
            QEvent::MouseButtonRelease,
            snap_cross_track,
            Qt::NoButton);
        require(snap_move_count == 4 && snap_target_track ==
                    static_cast<qint64>(snap_destination_track.track_id) &&
                    snap_target_frame == 18000,
                "Moving a clip between tracks did not preserve magnetic snapping.");

        snap_widget.setTracks({snap_same_track});
        const auto snap_start_boundary = QPointF(
            snap_widget.contentXForFrame(600), snap_source_y);
        sendMouse(
            snap_widget,
            QEvent::MouseButtonPress,
            snap_press,
            Qt::LeftButton);
        sendMouse(
            snap_widget,
            QEvent::MouseMove,
            snap_start_boundary,
            Qt::LeftButton);
        sendMouse(
            snap_widget,
            QEvent::MouseButtonRelease,
            snap_start_boundary,
            Qt::NoButton);
        require(snap_move_count == 5 && snap_target_frame == 0,
                "A clip did not snap to the start of the Timeline.");

        const auto snap_end_boundary = QPointF(
            snap_widget.contentXForFrame(96800), snap_source_y);
        snap_widget.setTracks({snap_same_track});
        sendMouse(
            snap_widget,
            QEvent::MouseButtonPress,
            snap_press,
            Qt::LeftButton);
        sendMouse(
            snap_widget,
            QEvent::MouseMove,
            snap_end_boundary,
            Qt::LeftButton);
        sendMouse(
            snap_widget,
            QEvent::MouseButtonRelease,
            snap_end_boundary,
            Qt::NoButton);
        require(snap_move_count == 6 && snap_target_frame == 96000,
                "A clip did not snap to the end of the Timeline.");
        snap_widget.close();

        // Media drops use the same target calculation and preserve their
        // existing path MIME while using the known duration for the ghost.
        timeline::TimelineWidget media_snap_widget;
        media_snap_widget.resize(900, 180);
        media_snap_widget.setTimelineViewportWidth(900);
        media_snap_widget.setTracks({snap_same_track});
        media_snap_widget.show();
        application.processEvents();
        int media_snap_drop_count = 0;
        qint64 media_snap_frame = -1;
        QObject::connect(
            &media_snap_widget,
            &timeline::TimelineWidget::mediaDropRequested,
            [&media_snap_drop_count, &media_snap_frame](
                const QString&, qint64, qint64 frame) {
                ++media_snap_drop_count;
                media_snap_frame = frame;
            });
        QMimeData snap_media_mime;
        snap_media_mime.setData(
            ui::kMediaPathMimeType,
            QByteArrayLiteral("snap-media.mp4"));
        snap_media_mime.setData(
            ui::kMediaFrameCountMimeType,
            QByteArrayLiteral("12000"));
        const auto media_snap_position = QPointF(
            media_snap_widget.contentXForFrame(19000), snap_source_y);
        QDragEnterEvent snap_media_enter(
            media_snap_position.toPoint(),
            Qt::CopyAction,
            &snap_media_mime,
            Qt::LeftButton,
            Qt::NoModifier);
        QApplication::sendEvent(&media_snap_widget, &snap_media_enter);
        QDragMoveEvent snap_media_move(
            media_snap_position.toPoint(),
            Qt::CopyAction,
            &snap_media_mime,
            Qt::LeftButton,
            Qt::NoModifier);
        QApplication::sendEvent(&media_snap_widget, &snap_media_move);
        QDropEvent snap_media_drop(
            media_snap_position,
            Qt::CopyAction,
            &snap_media_mime,
            Qt::LeftButton,
            Qt::NoModifier);
        QApplication::sendEvent(&media_snap_widget, &snap_media_drop);
        require(media_snap_drop_count == 1 && media_snap_frame == 18000,
                "A media drop did not snap to the adjacent clip boundary.");

        // With no metadata, a one-frame fallback makes the dragged end align
        // to a target one frame ahead instead of moving the start to it.
        timeline::TimelineWidget fallback_snap_widget;
        fallback_snap_widget.resize(900, 180);
        fallback_snap_widget.setTimelineViewportWidth(900);
        fallback_snap_widget.setTracks({timeline::TimelineTrack{
            1,
            "Video 1",
            1.0,
            false,
            {makeClip("fallback-target.mkv", 101, 120, "Fallback target")}}});
        fallback_snap_widget.show();
        application.processEvents();
        int fallback_drop_count = 0;
        qint64 fallback_drop_frame = -1;
        QObject::connect(
            &fallback_snap_widget,
            &timeline::TimelineWidget::mediaDropRequested,
            [&fallback_drop_count, &fallback_drop_frame](
                const QString&, qint64, qint64 frame) {
                ++fallback_drop_count;
                fallback_drop_frame = frame;
            });
        QMimeData fallback_snap_mime;
        fallback_snap_mime.setData(
            ui::kMediaPathMimeType,
            QByteArrayLiteral("snap-fallback.mp4"));
        const auto fallback_snap_position = QPointF(
            fallback_snap_widget.contentXForFrame(100), snap_source_y);
        QDragEnterEvent fallback_snap_enter(
            fallback_snap_position.toPoint(),
            Qt::CopyAction,
            &fallback_snap_mime,
            Qt::LeftButton,
            Qt::NoModifier);
        QApplication::sendEvent(&fallback_snap_widget, &fallback_snap_enter);
        QDragMoveEvent fallback_snap_move(
            fallback_snap_position.toPoint(),
            Qt::CopyAction,
            &fallback_snap_mime,
            Qt::LeftButton,
            Qt::NoModifier);
        QApplication::sendEvent(&fallback_snap_widget, &fallback_snap_move);
        QDropEvent fallback_snap_drop(
            fallback_snap_position,
            Qt::CopyAction,
            &fallback_snap_mime,
            Qt::LeftButton,
            Qt::NoModifier);
        QApplication::sendEvent(&fallback_snap_widget, &fallback_snap_drop);
        require(fallback_drop_count == 1 && fallback_drop_frame == 100,
                "A metadata-free media drop did not use the one-frame fallback duration.");
        fallback_snap_widget.close();

        media_snap_widget.close();

        timeline::TimelineWidget image_context_widget;
        image_context_widget.resize(900, 180);
        image_context_widget.setTimelineViewportWidth(900);
        auto image_context_clip = makeClip(
            "linked-still.png", 0, 100, "Linked still");
        image_context_clip.clip_id = 919;
        image_context_clip.kind = timeline::ClipKind::Image;
        image_context_widget.setTracks({timeline::TimelineTrack{
            717, "Images", 1.0, false, {image_context_clip}}});
        image_context_widget.show();
        application.processEvents();
        bool image_clip_selected = false;
        bool image_fusion_requested = false;
        bool image_edit_requested = false;
        QObject::connect(
            &image_context_widget,
            &timeline::TimelineWidget::clipSelected,
            [&image_clip_selected](timeline::TrackId track_id, timeline::ClipId clip_id) {
                image_clip_selected = track_id == 717 && clip_id == 919;
            });
        QObject::connect(
            &image_context_widget,
            &timeline::TimelineWidget::openFusionClipRequested,
            [&image_fusion_requested](timeline::ClipId clip_id) {
                image_fusion_requested = clip_id == 919;
            });
        QObject::connect(
            &image_context_widget,
            &timeline::TimelineWidget::editImageClipRequested,
            [&image_edit_requested](timeline::ClipId clip_id) {
                image_edit_requested = clip_id == 919;
            });
        const QPoint image_context_position(
            static_cast<int>(image_context_widget.contentXForFrame(15)), 100);
        bool image_fusion_action_found = false;
        QTimer::singleShot(0, [&image_context_widget, &image_fusion_action_found]() {
            auto* menu = image_context_widget.findChild<QMenu*>();
            if (menu == nullptr) return;
            for (auto* action : menu->actions()) {
                if (action->text() == QStringLiteral("Open in Fusion")) {
                    image_fusion_action_found = true;
                    action->trigger();
                    break;
                }
            }
            menu->close();
        });
        QContextMenuEvent image_context_event(
            QContextMenuEvent::Mouse,
            image_context_position,
            image_context_widget.mapToGlobal(image_context_position),
            Qt::NoModifier);
        QApplication::sendEvent(&image_context_widget, &image_context_event);
        require(image_clip_selected && image_fusion_requested &&
                    image_fusion_action_found,
                "The image clip context menu did not expose Fusion and route its stable clip ID "
                "(selected=" + std::to_string(image_clip_selected) +
                ", Fusion action=" + std::to_string(image_fusion_action_found) +
                ", Fusion requested=" + std::to_string(image_fusion_requested) + ").");
        bool image_editor_action_found = false;
        QTimer::singleShot(0, [&image_context_widget, &image_editor_action_found]() {
            auto* menu = image_context_widget.findChild<QMenu*>();
            if (menu == nullptr) return;
            for (auto* action : menu->actions()) {
                if (action->text() ==
                    QStringLiteral("Edit Clip Image in Image Editor")) {
                    image_editor_action_found = true;
                    action->trigger();
                    break;
                }
            }
            menu->close();
        });
        QContextMenuEvent image_editor_context_event(
            QContextMenuEvent::Mouse,
            image_context_position,
            image_context_widget.mapToGlobal(image_context_position),
            Qt::NoModifier);
        QApplication::sendEvent(&image_context_widget, &image_editor_context_event);
        require(image_editor_action_found && image_edit_requested,
                "The image clip context menu lost its Image Editor action.");
        image_context_widget.close();

        timeline::TimelineWidget video_context_widget;
        video_context_widget.resize(900, 180);
        video_context_widget.setTimelineViewportWidth(900);
        auto video_context_clip = makeClip("video.mp4", 0, 100, "Video clip");
        video_context_clip.clip_id = 923;
        video_context_clip.kind = timeline::ClipKind::Video;
        video_context_widget.setTracks({timeline::TimelineTrack{
            721, "Video 1", 1.0, false, {video_context_clip}}});
        video_context_widget.show();
        application.processEvents();
        bool video_clip_selected = false;
        bool video_fusion_requested = false;
        bool video_fusion_action_found = false;
        QObject::connect(
            &video_context_widget,
            &timeline::TimelineWidget::clipSelected,
            [&video_clip_selected](timeline::TrackId track_id, timeline::ClipId clip_id) {
                video_clip_selected = track_id == 721 && clip_id == 923;
            });
        QObject::connect(
            &video_context_widget,
            &timeline::TimelineWidget::openFusionClipRequested,
            [&video_fusion_requested](timeline::ClipId clip_id) {
                video_fusion_requested = clip_id == 923;
            });
        const QPoint video_context_position(
            static_cast<int>(video_context_widget.contentXForFrame(15)), 100);
        QTimer::singleShot(0, [&video_context_widget, &video_fusion_action_found]() {
            auto* menu = video_context_widget.findChild<QMenu*>();
            if (menu == nullptr) return;
            for (auto* action : menu->actions()) {
                if (action->text() == QStringLiteral("Open in Fusion")) {
                    video_fusion_action_found = true;
                    action->trigger();
                    break;
                }
            }
            menu->close();
        });
        QContextMenuEvent video_context_event(
            QContextMenuEvent::Mouse,
            video_context_position,
            video_context_widget.mapToGlobal(video_context_position),
            Qt::NoModifier);
        QApplication::sendEvent(&video_context_widget, &video_context_event);
        require(video_clip_selected && video_fusion_action_found && video_fusion_requested,
                "The video clip context menu did not select and route its stable ID to Fusion.");
        video_context_widget.close();

        timeline::TimelineWidget linked_audio_context_widget;
        linked_audio_context_widget.resize(900, 240);
        linked_audio_context_widget.setTimelineViewportWidth(900);
        auto linked_video_clip = makeClip(
            "linked-video.mkv", 0, 100, "Video with audio");
        linked_video_clip.clip_id = 921;
        linked_video_clip.kind = timeline::ClipKind::Video;
        linked_video_clip.linked_clip_id = 922;
        linked_video_clip.audio_extracted = true;
        auto linked_audio_clip = makeClip(
            "linked-video.mkv", 0, 100, "Audio companion");
        linked_audio_clip.clip_id = 922;
        linked_audio_clip.kind = timeline::ClipKind::Audio;
        linked_audio_clip.linked_clip_id = 921;
        linked_audio_clip.frame_rate.reset();
        linked_audio_clip.frame_count.reset();
        const std::vector<timeline::TimelineTrack> linked_audio_tracks{
            timeline::TimelineTrack{719, "Video 1", 1.0, false, {linked_video_clip}},
            timeline::TimelineTrack{720, "Audio 1", 1.0, false, {linked_audio_clip},
                                    {}, timeline::TrackKind::Audio}};
        linked_audio_context_widget.setTracks(linked_audio_tracks);
        linked_audio_context_widget.show();
        application.processEvents();
        timeline::ClipId unlinked_clip_id = 0;
        bool linked_video_selected = false;
        bool linked_audio_menu_found = false;
        bool linked_audio_action_found = false;
        bool linked_fusion_action_found = false;
        timeline::ClipId linked_fusion_clip_id = 0;
        QObject::connect(
            &linked_audio_context_widget,
            &timeline::TimelineWidget::audioUnlinkRequested,
            [&unlinked_clip_id](timeline::ClipId clip_id) {
                unlinked_clip_id = clip_id;
            });
        QObject::connect(
            &linked_audio_context_widget,
            &timeline::TimelineWidget::openFusionClipRequested,
            [&linked_fusion_clip_id](timeline::ClipId clip_id) {
                linked_fusion_clip_id = clip_id;
            });
        QObject::connect(
            &linked_audio_context_widget,
            &timeline::TimelineWidget::clipSelected,
            [&linked_video_selected](timeline::TrackId track_id, timeline::ClipId clip_id) {
                linked_video_selected = track_id == 719 && clip_id == 921;
            });
        const timeline::TimelineGeometry linked_audio_geometry(
            linked_audio_tracks, QSizeF(linked_audio_context_widget.size()),
            linked_audio_context_widget.trackRowHeight(),
            linked_audio_context_widget.zoomFactor(), std::nullopt, 30.0);
        const QPoint linked_video_context_position(
            linked_audio_geometry.clipRect(linked_video_clip, 0).center().toPoint());
        QTimer::singleShot(0, [&linked_audio_context_widget, &linked_audio_menu_found,
                               &linked_audio_action_found,
                               &linked_fusion_action_found]() {
            auto* menu = linked_audio_context_widget.findChild<QMenu*>();
            if (menu == nullptr) return;
            linked_audio_menu_found = true;
            for (auto* action : menu->actions()) {
                if (action->text() == QStringLiteral("Open in Fusion")) {
                    linked_fusion_action_found = true;
                    action->trigger();
                } else if (action->text() == QStringLiteral("Unlink Audio")) {
                    linked_audio_action_found = true;
                    action->trigger();
                }
            }
            menu->close();
        });
        QContextMenuEvent linked_audio_context_event(
            QContextMenuEvent::Mouse,
            linked_video_context_position,
            linked_audio_context_widget.mapToGlobal(linked_video_context_position),
            Qt::NoModifier);
        QApplication::sendEvent(
            &linked_audio_context_widget, &linked_audio_context_event);
        require(unlinked_clip_id == 921 && linked_fusion_clip_id == 921 &&
                    linked_fusion_action_found && linked_audio_action_found,
                "The linked video context menu did not retain Unlink Audio and add Fusion for its stable clip ID (received " +
                    std::to_string(unlinked_clip_id) + ", selected=" +
                    std::to_string(linked_video_selected) + ", menu=" +
                    std::to_string(linked_audio_menu_found) + ", unlink=" +
                    std::to_string(linked_audio_action_found) + ", Fusion=" +
                    std::to_string(linked_fusion_clip_id) + ").");

        bool linked_audio_fusion_action_found = false;
        bool linked_audio_unlink_action_found = false;
        QTimer::singleShot(0, [&linked_audio_context_widget,
                               &linked_audio_fusion_action_found,
                               &linked_audio_unlink_action_found]() {
            auto* menu = linked_audio_context_widget.findChild<QMenu*>();
            if (menu == nullptr) return;
            for (auto* action : menu->actions()) {
                if (action->text() == QStringLiteral("Open in Fusion")) {
                    linked_audio_fusion_action_found = true;
                } else if (action->text() == QStringLiteral("Unlink Audio")) {
                    linked_audio_unlink_action_found = true;
                    action->trigger();
                }
            }
            menu->close();
        });
        const QPoint linked_audio_context_position(
            linked_audio_geometry.clipRect(linked_audio_clip, 1).center().toPoint());
        QContextMenuEvent linked_audio_companion_context_event(
            QContextMenuEvent::Mouse,
            linked_audio_context_position,
            linked_audio_context_widget.mapToGlobal(linked_audio_context_position),
            Qt::NoModifier);
        QApplication::sendEvent(
            &linked_audio_context_widget, &linked_audio_companion_context_event);
        require(unlinked_clip_id == 922 && linked_audio_unlink_action_found &&
                    !linked_audio_fusion_action_found,
                "The linked audio companion must retain Unlink Audio and omit Open in Fusion.");
        linked_audio_context_widget.close();

        timeline::TimelineWidget read_only_widget;
        read_only_widget.resize(900, 180);
        read_only_widget.setTimelineViewportWidth(900);
        auto read_only_clip = makeClip(
            "read-only-still.png", 0, 12000, "Read-only still");
        read_only_clip.clip_id = 920;
        read_only_clip.kind = timeline::ClipKind::Image;
        read_only_widget.setTracks({timeline::TimelineTrack{
            718, "Images", 1.0, false, {read_only_clip}}});
        read_only_widget.show();
        application.processEvents();

        int read_only_interaction_count = 0;
        const auto count_read_only_interaction =
            [&read_only_interaction_count](auto&&...) {
                ++read_only_interaction_count;
            };
        QObject::connect(
            &read_only_widget,
            &timeline::TimelineWidget::clipSelected,
            count_read_only_interaction);
        QObject::connect(
            &read_only_widget,
            &timeline::TimelineWidget::clipSelectionCleared,
            count_read_only_interaction);
        QObject::connect(
            &read_only_widget,
            &timeline::TimelineWidget::editImageClipRequested,
            count_read_only_interaction);
        QObject::connect(
            &read_only_widget,
            &timeline::TimelineWidget::clipMoveRequested,
            count_read_only_interaction);
        QObject::connect(
            &read_only_widget,
            &timeline::TimelineWidget::clipSplitRequested,
            count_read_only_interaction);
        QObject::connect(
            &read_only_widget,
            &timeline::TimelineWidget::clipEdgeTrimRequested,
            count_read_only_interaction);
        QObject::connect(
            &read_only_widget,
            &timeline::TimelineWidget::mediaDropRequested,
            count_read_only_interaction);
        QObject::connect(
            &read_only_widget,
            &timeline::TimelineWidget::effectDropRequested,
            count_read_only_interaction);
        QObject::connect(
            &read_only_widget,
            &timeline::TimelineWidget::transitionSelected,
            count_read_only_interaction);
        QObject::connect(
            &read_only_widget,
            &timeline::TimelineWidget::transitionAddRequested,
            count_read_only_interaction);
        QObject::connect(
            &read_only_widget,
            &timeline::TimelineWidget::transitionRemoveRequested,
            count_read_only_interaction);
        QObject::connect(
            &read_only_widget,
            &timeline::TimelineWidget::seekStarted,
            count_read_only_interaction);
        QObject::connect(
            &read_only_widget,
            &timeline::TimelineWidget::seekRequested,
            count_read_only_interaction);

        read_only_widget.setZoomFactor(2.0);
        const auto read_only_zoom = read_only_widget.zoomFactor();
        const auto read_only_row_height = read_only_widget.trackRowHeight();
        read_only_widget.setReadOnly(true);
        require(read_only_widget.isReadOnly(),
                "The Timeline did not expose its read-only state.");

        const auto read_only_clip_position = QPointF(
            read_only_widget.contentXForFrame(3000), 83);
        sendMouse(
            read_only_widget,
            QEvent::MouseButtonPress,
            read_only_clip_position,
            Qt::LeftButton);
        sendMouse(
            read_only_widget,
            QEvent::MouseMove,
            read_only_clip_position + QPointF(80, 0),
            Qt::LeftButton);
        sendMouse(
            read_only_widget,
            QEvent::MouseButtonRelease,
            read_only_clip_position + QPointF(80, 0),
            Qt::NoButton);
        sendMouse(
            read_only_widget,
            QEvent::MouseButtonPress,
            QPointF(300, 25),
            Qt::LeftButton);
        sendMouse(
            read_only_widget,
            QEvent::MouseButtonRelease,
            QPointF(300, 25),
            Qt::NoButton);

        QTimer::singleShot(0, [&application]() {
            for (auto* window : application.topLevelWidgets()) {
                if (auto* menu = qobject_cast<QMenu*>(window)) menu->close();
            }
        });
        const QPoint read_only_context_position(
            static_cast<int>(read_only_widget.contentXForFrame(3000)), 83);
        QContextMenuEvent read_only_context_event(
            QContextMenuEvent::Mouse,
            read_only_context_position,
            read_only_widget.mapToGlobal(read_only_context_position),
            Qt::NoModifier);
        QApplication::sendEvent(&read_only_widget, &read_only_context_event);

        QMimeData read_only_media_mime;
        read_only_media_mime.setData(
            ui::kMediaPathMimeType,
            QByteArrayLiteral("read-only-drop.mp4"));
        const auto read_only_drop_position = read_only_clip_position.toPoint();
        QDragEnterEvent read_only_drag_enter(
            read_only_drop_position,
            Qt::CopyAction,
            &read_only_media_mime,
            Qt::LeftButton,
            Qt::NoModifier);
        QApplication::sendEvent(&read_only_widget, &read_only_drag_enter);
        QDragMoveEvent read_only_drag_move(
            read_only_drop_position,
            Qt::CopyAction,
            &read_only_media_mime,
            Qt::LeftButton,
            Qt::NoModifier);
        QApplication::sendEvent(&read_only_widget, &read_only_drag_move);
        QDropEvent read_only_drop(
            QPointF(read_only_drop_position),
            Qt::CopyAction,
            &read_only_media_mime,
            Qt::LeftButton,
            Qt::NoModifier);
        QApplication::sendEvent(&read_only_widget, &read_only_drop);
        sendWheel(
            read_only_widget,
            QPointF(300, 83),
            120,
            Qt::ControlModifier);
        sendWheel(
            read_only_widget,
            QPointF(300, 83),
            120,
            Qt::ShiftModifier);
        require(read_only_interaction_count == 0 &&
                    read_only_widget.zoomFactor() == read_only_zoom &&
                    read_only_widget.trackRowHeight() == read_only_row_height &&
                    !read_only_drop.isAccepted(),
                "Read-only Timeline input selected, sought, edited, dropped, opened a menu, or changed zoom/row height.");

        QScrollArea read_only_drop_scroll;
        read_only_drop_scroll.resize(500, 150);
        read_only_drop_scroll.setWidgetResizable(true);
        read_only_drop_scroll.setAcceptDrops(true);
        read_only_drop_scroll.viewport()->setAcceptDrops(true);
        auto* read_only_viewport_timeline = new timeline::TimelineWidget;
        read_only_viewport_timeline->setTracks({timeline::TimelineTrack{
            719, "Video", 1.0, false,
            {makeClip("read-only-viewport.mp4", 0, 12000, "Viewport clip")}}});
        read_only_viewport_timeline->setAcceptDrops(false);
        read_only_viewport_timeline->setReadOnly(true);
        read_only_drop_scroll.setWidget(read_only_viewport_timeline);
        read_only_drop_scroll.viewport()->installEventFilter(
            read_only_viewport_timeline);
        read_only_drop_scroll.show();
        application.processEvents();
        int read_only_viewport_drop_count = 0;
        QObject::connect(
            read_only_viewport_timeline,
            &timeline::TimelineWidget::mediaDropRequested,
            [&read_only_viewport_drop_count](const QString&, qint64, qint64) {
                ++read_only_viewport_drop_count;
            });
        const QPoint read_only_viewport_position(100, 80);
        QDragEnterEvent read_only_viewport_enter(
            read_only_viewport_position,
            Qt::CopyAction,
            &read_only_media_mime,
            Qt::LeftButton,
            Qt::NoModifier);
        QApplication::sendEvent(
            read_only_drop_scroll.viewport(), &read_only_viewport_enter);
        QDragMoveEvent read_only_viewport_move(
            read_only_viewport_position,
            Qt::CopyAction,
            &read_only_media_mime,
            Qt::LeftButton,
            Qt::NoModifier);
        QApplication::sendEvent(
            read_only_drop_scroll.viewport(), &read_only_viewport_move);
        QDropEvent read_only_viewport_drop(
            QPointF(read_only_viewport_position),
            Qt::CopyAction,
            &read_only_media_mime,
            Qt::LeftButton,
            Qt::NoModifier);
        QApplication::sendEvent(
            read_only_drop_scroll.viewport(), &read_only_viewport_drop);
        require(read_only_viewport_drop_count == 0 &&
                    !read_only_viewport_drop.isAccepted(),
                "The Timeline viewport accepted a media drop while read-only.");
        read_only_drop_scroll.close();

        read_only_widget.setReadOnly(false);
        sendMouse(
            read_only_widget,
            QEvent::MouseButtonPress,
            read_only_clip_position,
            Qt::LeftButton);
        sendMouse(
            read_only_widget,
            QEvent::MouseButtonRelease,
            read_only_clip_position,
            Qt::NoButton);
        require(!read_only_widget.isReadOnly() &&
                    read_only_interaction_count > 0,
                "Returning the Timeline to Edit did not restore normal interaction.");
        read_only_widget.close();

        widget.close();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
