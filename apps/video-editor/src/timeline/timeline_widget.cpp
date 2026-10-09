#include "timeline_widget.h"
#include "ui/media_browser/external_file_urls.h"
#include "timeline_time.h"

#include "../ui/media_browser/media_drag_mime.h"

#include <creative_suite/effects/effects.h>

#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QContextMenuEvent>
#include <QEvent>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QMimeData>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>

namespace timeline {
namespace {

constexpr double edge_width = 8.0;
constexpr double shared_roll_half_width = 4.0;
constexpr double shared_single_clip_handle_width = 8.0;
constexpr double track_group_splitter_height = 18.0;
constexpr double ruler_minor_target_spacing_pixels = 8.0;

TrimPointerPosition trimPointer(const QPointF& position) noexcept {
    return {position.x(), position.y()};
}

std::int64_t rulerMinorStep(double pixels_per_frame) noexcept {
    if (!std::isfinite(pixels_per_frame) || pixels_per_frame <= 0.0 ||
        pixels_per_frame >= 1.0) {
        return 1;
    }

    const auto raw_step = std::max<long double>(
        1.0L,
        std::ceil(static_cast<long double>(ruler_minor_target_spacing_pixels) /
                  static_cast<long double>(pixels_per_frame)));
    const auto max_step = std::numeric_limits<std::int64_t>::max();
    std::int64_t magnitude = 1;
    while (magnitude <= max_step / 10 &&
           static_cast<long double>(magnitude) * 10.0L < raw_step) {
        magnitude *= 10;
    }

    for (const auto multiplier : {1, 2, 5}) {
        if (magnitude <= max_step / multiplier) {
            const auto candidate = magnitude * multiplier;
            if (static_cast<long double>(candidate) >= raw_step) {
                return candidate;
            }
        }
    }
    if (magnitude <= max_step / 10) return magnitude * 10;
    return max_step;
}

QString text(const std::string& value) {
    return QString::fromUtf8(value.data(), static_cast<int>(value.size()));
}

QString clipDuration(const TimelineClip& clip, double timeline_frame_rate) {
    if (std::isfinite(timeline_frame_rate) && timeline_frame_rate > 0.0 &&
        clip.timeline_duration_frames > 0) {
        return QString::number(
            static_cast<double>(clip.timeline_duration_frames) / timeline_frame_rate,
            'f',
            3) + " s";
    }
    return "Unknown duration";
}

std::optional<ClipLocation> locationForClip(
    const std::vector<TimelineTrack>& tracks,
    ClipId clip_id) noexcept {
    for (std::size_t track_index = 0; track_index < tracks.size(); ++track_index) {
        for (std::size_t clip_index = 0;
             clip_index < tracks[track_index].clips.size(); ++clip_index) {
            if (tracks[track_index].clips[clip_index].clip_id == clip_id) {
                return ClipLocation{track_index, clip_index};
            }
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> indexForTrack(
    const std::vector<TimelineTrack>& tracks,
    TrackId track_id) noexcept {
    for (std::size_t index = 0; index < tracks.size(); ++index) {
        if (tracks[index].track_id == track_id) return index;
    }
    return std::nullopt;
}

} // namespace

TimelineWidget::TimelineWidget(QWidget* parent)
    : QWidget(parent) {
    setMinimumHeight(100);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAcceptDrops(true);
    setMouseTracking(true);
    setContextMenuPolicy(Qt::DefaultContextMenu);
}

void TimelineWidget::setFrameRate(FrameRate frame_rate) noexcept {
    if (!validFrameRate(frame_rate)) return;
    frame_rate_ = reducedFrameRate(frame_rate);
    update();
    emit playheadVisualChanged();
}

void TimelineWidget::setTracks(const std::vector<TimelineTrack>& tracks) {
    // Model changes can arrive while this widget owns the mouse grab for a
    // seek, move, trim, or blade gesture. Resetting the gesture flags alone
    // would leave the grab active and route every subsequent click back to
    // the timeline instead of the rest of the editor.
    if (QWidget::mouseGrabber() == this) releaseMouse();
    if (split_drag_active_ || splitter_hover_active_) {
        unsetCursor();
    }
    split_drag_active_ = false;
    splitter_hover_active_ = false;
    if (interaction_controller_.trimGesture().active()) unsetCursor();

    tracks_ = tracks;
    if (tracks_.empty()) tracks_.push_back(TimelineTrack{1, "Video 1", 1.0, false, {}});
    std::erase_if(selected_clip_ids_, [this](ClipId clip_id) {
        return !locationForClip(tracks_, clip_id).has_value();
    });
    if (active_clip_.has_value() &&
        (active_clip_->track_index >= tracks_.size() ||
         active_clip_->clip_index >= tracks_[active_clip_->track_index].clips.size())) {
        active_clip_.reset();
        playhead_frame_ = 0;
    }
    if (selected_transition_.has_value()) {
        const auto& selection = *selected_transition_;
        bool exists = false;
        if (selection.track_index < tracks_.size() &&
            selection.from_clip_index < tracks_[selection.track_index].clips.size() &&
            selection.to_clip_index < tracks_[selection.track_index].clips.size()) {
            const auto from_id = tracks_[selection.track_index]
                .clips[selection.from_clip_index].clip_id;
            const auto to_id = tracks_[selection.track_index]
                .clips[selection.to_clip_index].clip_id;
            exists = std::any_of(
                tracks_[selection.track_index].transitions.begin(),
                tracks_[selection.track_index].transitions.end(),
                [from_id, to_id](const TimelineTransition& transition) {
                    return transition.from_clip_id == from_id &&
                        transition.to_clip_id == to_id;
                });
        }
        if (!exists) selected_transition_.reset();
    }
    updateVerticalExtent();
    updateHorizontalExtent();
    interaction_controller_.cancelAll();
    clearDragPreview();
    emit trackHeaderVisualsChanged();
    emit playheadVisualChanged();
    update();
}

void TimelineWidget::clearAudioWaveforms() {
    audio_waveforms_.clear();
    update();
}

void TimelineWidget::setAudioWaveform(
    const std::filesystem::path& source_path,
    const std::shared_ptr<const media::AudioWaveform>& waveform) {
    if (source_path.empty() || waveform == nullptr) return;
    audio_waveforms_[source_path] = waveform;
    update();
}

void TimelineWidget::setStereoWaveformDisplayEnabled(bool enabled) noexcept {
    if (stereo_waveform_display_enabled_ == enabled) return;
    stereo_waveform_display_enabled_ = enabled;
    update();
}

bool TimelineWidget::stereoWaveformDisplayEnabled() const noexcept {
    return stereo_waveform_display_enabled_;
}

void TimelineWidget::setClips(const std::vector<TimelineClip>& clips) {
    TimelineTrack track{1, "Video 1", 1.0, false, clips};
    setTracks({track});
}

void TimelineWidget::clearClips() {
    if (QWidget::mouseGrabber() == this) releaseMouse();
    if (interaction_controller_.trimGesture().active()) unsetCursor();
    tracks_.clear();
    tracks_.push_back(TimelineTrack{1, "Video 1", 1.0, false, {}});
    updateVerticalExtent();
    updateHorizontalExtent();
    active_clip_.reset();
    selected_clip_ids_.clear();
    playhead_frame_ = 0;
    interaction_controller_.cancelAll();
    clearDragPreview();
    selected_transition_.reset();
    emit trackHeaderVisualsChanged();
    emit playheadVisualChanged();
    update();
}

void TimelineWidget::setActiveClip(std::optional<ClipLocation> location) {
    if (location.has_value() &&
        (location->track_index >= tracks_.size() ||
         location->clip_index >= tracks_[location->track_index].clips.size())) {
        location.reset();
    }
    active_clip_ = location;
    if (!location.has_value()) {
        selected_clip_ids_.clear();
    } else {
        const auto clip_id = tracks_[location->track_index]
            .clips[location->clip_index].clip_id;
        if (std::find(selected_clip_ids_.begin(), selected_clip_ids_.end(), clip_id) ==
            selected_clip_ids_.end()) {
            selected_clip_ids_ = {clip_id};
        }
    }
    interaction_controller_.clearTransientPreview();
    emit trackHeaderVisualsChanged();
    emit playheadVisualChanged();
    update();
}

std::vector<ClipId> TimelineWidget::selectedClipIds() const {
    return selected_clip_ids_;
}

void TimelineWidget::setSelectedClipIds(const std::vector<ClipId>& clip_ids) {
    selected_clip_ids_.clear();
    for (const auto clip_id : clip_ids) {
        if (locationForClip(tracks_, clip_id).has_value() &&
            std::find(selected_clip_ids_.begin(), selected_clip_ids_.end(), clip_id) ==
                selected_clip_ids_.end()) {
            selected_clip_ids_.push_back(clip_id);
        }
    }
    update();
}

void TimelineWidget::setActiveClipIndex(std::optional<std::size_t> clip_index) {
    if (!clip_index.has_value()) {
        setActiveClip(std::nullopt);
    } else {
        setActiveClip(ClipLocation{0, *clip_index});
    }
}

void TimelineWidget::setPlayheadFrame(std::int64_t frame_index) {
    playhead_frame_ = std::max<std::int64_t>(0, frame_index);
    const auto total = totalDuration();
    if (total > 0) {
        playhead_frame_ = std::min(playhead_frame_, total - 1);
    }
    interaction_controller_.clearTransientPreview();
    // The ruler position is only a transient visual override while the user
    // is dragging it. Once an external playback/seek update arrives, the
    // live playhead must win even if the decoder skipped over that exact
    // frame.
    update();
    emit playheadVisualChanged();
}

std::int64_t TimelineWidget::playheadFrame() const noexcept {
    return playhead_frame_;
}

std::int64_t TimelineWidget::displayedPlayheadFrame() const noexcept {
    const auto ruler_frame = interaction_controller_.rulerPreviewFrame();
    auto frame = ruler_frame.value_or(playhead_frame_);
    const auto drag_frame = interaction_controller_.seekPreviewLocalFrame();
    const bool active_clip_valid = active_clip_.has_value() &&
        active_clip_->track_index < tracks_.size() &&
        active_clip_->clip_index < tracks_[active_clip_->track_index].clips.size();
    if (!ruler_frame.has_value() && active_clip_valid && drag_frame.has_value()) {
        const auto& clip = tracks_[active_clip_->track_index]
            .clips[active_clip_->clip_index];
        if (*drag_frame >= 0 && clip.timeline_start_frame <=
            std::numeric_limits<std::int64_t>::max() - *drag_frame) {
            frame = clip.timeline_start_frame + *drag_frame;
        }
    }
    const auto duration = totalDuration();
    if (duration > 0) frame = std::clamp<std::int64_t>(frame, 0, duration - 1);
    return std::max<std::int64_t>(0, frame);
}

QString TimelineWidget::playheadTimecode() const {
    return QString::fromStdString(
        timeline::formatTimelineTimecode(displayedPlayheadFrame(), frame_rate_));
}

void TimelineWidget::setRazorMode(bool enabled) {
    if (interaction_controller_.splitPending() && QWidget::mouseGrabber() == this) {
        releaseMouse();
    }
    razor_mode_ = enabled;
    if (enabled) volume_mode_ = false;
    interaction_controller_.cancelSplit();
    unsetCursor();
    update();
}

bool TimelineWidget::razorMode() const noexcept {
    return razor_mode_;
}

void TimelineWidget::setVolumeMode(bool enabled) {
    if (enabled) setRazorMode(false);
    volume_mode_ = enabled;
    if (volume_mode_) setCursor(Qt::CrossCursor);
    else if (!razor_mode_) unsetCursor();
    update();
}

bool TimelineWidget::volumeMode() const noexcept {
    return volume_mode_;
}

void TimelineWidget::setMoveRequiresAlt(bool enabled) {
    move_requires_alt_ = enabled;
    if (interaction_controller_.moveActive() || interaction_controller_.movePending()) {
        interaction_controller_.cancelMove();
        clearDragPreview();
        if (QWidget::mouseGrabber() == this) releaseMouse();
    }
    update();
}

bool TimelineWidget::moveRequiresAlt() const noexcept {
    return move_requires_alt_;
}

void TimelineWidget::setReadOnly(bool read_only) {
    if (read_only_ == read_only) return;
    read_only_ = read_only;
    if (read_only_) {
        if (audio_gain_envelope_drag_.has_value()) {
            audio_gain_envelope_drag_.reset();
            emit audioGainEnvelopeEditFinished();
        }
        interaction_controller_.cancelAll();
        if (QWidget::mouseGrabber() == this) releaseMouse();
        unsetCursor();
        suppress_next_context_menu_ = false;
    }
    update();
    emit playheadVisualChanged();
}

bool TimelineWidget::isReadOnly() const noexcept {
    return read_only_;
}

void TimelineWidget::setSnapEnabled(bool enabled) {
    if (snap_enabled_ == enabled) return;
    snap_enabled_ = enabled;
    const auto move_target = interaction_controller_.moveTarget();
    if (interaction_controller_.moveActive() && move_target.has_value()) {
        const auto raw_frame = globalFrameAt(move_preview_position_.x());
        const auto source_location = locationForClip(
            tracks_, interaction_controller_.movingClipId());
        const auto target_index = indexForTrack(tracks_, move_target->track_id);
        if (raw_frame.has_value() && source_location.has_value() &&
            target_index.has_value()) {
            const auto& clip = tracks_[source_location->track_index]
                .clips[source_location->clip_index];
            const auto snapped = snapPlacement(
                *target_index, *raw_frame, clip.timeline_duration_frames,
                source_location);
            interaction_controller_.setMoveTarget(TimelineMoveTarget{
                move_target->track_id, snapped.start_frame, snapped.guide_frame});
        }
    } else if (interaction_controller_.dropPreview().hovering &&
               interaction_controller_.dropPreview().media &&
               interaction_controller_.dropPreview().target_track_index.has_value()) {
        auto drop = interaction_controller_.dropPreview();
        const auto raw_frame = globalFrameAt(drop.pointer_position.x());
        if (raw_frame.has_value()) {
            const auto snapped = snapPlacement(
                *drop.target_track_index,
                *raw_frame,
                drop.duration_frames);
            drop.target_frame = snapped.start_frame;
            drop.snap_guide_frame = snapped.guide_frame;
            drop.valid = !placementOverlaps(
                *drop.target_track_index, *drop.target_frame, drop.duration_frames);
            interaction_controller_.setDropPreview(std::move(drop));
        }
    }
    update();
    emit snapEnabledChanged(snap_enabled_);
}

bool TimelineWidget::snapEnabled() const noexcept {
    return snap_enabled_;
}

void TimelineWidget::setTrackScrollOffset(TrackKind kind, int offset) {
    const auto maximum = trackScrollMaximum(kind);
    const auto normalized = std::clamp(offset, 0, maximum);
    auto& current = kind == TrackKind::Audio
        ? audio_scroll_offset_ : video_scroll_offset_;
    if (std::abs(current - static_cast<double>(normalized)) < 0.001) return;
    current = static_cast<double>(normalized);
    update();
    emit trackHeaderVisualsChanged();
}

int TimelineWidget::trackScrollOffset(TrackKind kind) const noexcept {
    return static_cast<int>(std::lround(
        kind == TrackKind::Audio ? audio_scroll_offset_ : video_scroll_offset_));
}

int TimelineWidget::trackScrollMaximum(TrackKind kind) const noexcept {
    return static_cast<int>(std::ceil(
        geometry().trackGroupScrollMaximum(kind)));
}

QRectF TimelineWidget::trackGroupViewportRect(TrackKind kind) const noexcept {
    return geometry().trackGroupViewportRect(kind);
}

QRectF TimelineWidget::trackBounds(std::size_t track_index) const noexcept {
    return trackRect(track_index);
}

QRectF TimelineWidget::clipBounds(const ClipLocation& location) const noexcept {
    return clipRect(location);
}

QRectF TimelineWidget::trackSplitterRect() const noexcept {
    return trackViewLayout().splitter_rect;
}

void TimelineWidget::updateTrackSplitterHoverState(const QPointF& position) {
    const auto splitter_hovered = trackSplitterRect().contains(position);
    const auto hover_changed = splitter_hover_active_ != splitter_hovered;
    splitter_hover_active_ = splitter_hovered;
    if (hover_changed) update();

    if (split_drag_active_ || splitter_hovered) {
        setCursor(Qt::SplitVCursor);
    } else if (cursor().shape() == Qt::SplitVCursor) {
        unsetCursor();
    }
}

double TimelineWidget::trackGroupSplitRatio() const noexcept {
    return track_group_split_ratio_;
}

void TimelineWidget::setTrackGroupSplitRatio(double ratio) {
    if (!std::isfinite(ratio)) return;
    const auto normalized = std::clamp(ratio, 0.2, 0.8);
    if (std::abs(normalized - track_group_split_ratio_) < 0.0001) return;
    const auto video_anchor = captureTrackScrollAnchor(TrackKind::Video);
    const auto audio_anchor = captureTrackScrollAnchor(TrackKind::Audio);
    const auto anchor_edge_y = [this](
        TrackKind kind,
        const std::optional<TrackScrollAnchor>& anchor) -> std::optional<double> {
        if (!anchor.has_value()) return std::nullopt;
        const auto index = indexForTrack(tracks_, anchor->track_id);
        if (!index.has_value() || tracks_[*index].kind != kind) {
            return std::nullopt;
        }
        const auto row = trackRect(*index);
        return anchor->bottom_edge ? row.bottom() : row.top();
    };
    const auto video_edge_before = anchor_edge_y(TrackKind::Video, video_anchor);
    const auto audio_edge_before = anchor_edge_y(TrackKind::Audio, audio_anchor);
    const auto splitter_y_before = trackSplitterRect().center().y();

    track_group_split_ratio_ = normalized;
    updateVerticalExtent();
    // Keep both stacks attached to the divider as their pane boundary moves.
    // This also compensates for scroll clamping and for Video's bottom alignment
    // when its content fits inside the upper pane.
    const auto splitter_delta =
        trackSplitterRect().center().y() - splitter_y_before;
    if (video_edge_before.has_value()) {
        const auto index = indexForTrack(tracks_, video_anchor->track_id);
        if (index.has_value()) {
            const auto row = trackRect(*index);
            const auto edge_after = video_anchor->bottom_edge
                ? row.bottom() : row.top();
            video_splitter_translation_ +=
                *video_edge_before + splitter_delta - edge_after;
        }
    }
    if (audio_edge_before.has_value()) {
        const auto index = indexForTrack(tracks_, audio_anchor->track_id);
        if (index.has_value()) {
            const auto row = trackRect(*index);
            const auto edge_after = audio_anchor->bottom_edge
                ? row.bottom() : row.top();
            audio_splitter_translation_ +=
                *audio_edge_before + splitter_delta - edge_after;
        }
    }
    update();
    emit trackHeaderVisualsChanged();
    emit trackGroupSplitRatioChanged(track_group_split_ratio_);
}

double TimelineWidget::zoomFactor() const noexcept {
    return zoom_factor_;
}

void TimelineWidget::setZoomFactor(double factor) {
    if (!std::isfinite(factor)) return;
    const auto normalized = std::clamp(
        factor, kMinTimelineZoomFactor, kMaxTimelineZoomFactor);
    if (std::abs(normalized - zoom_factor_) < 0.000001) return;
    zoom_factor_ = normalized;
    updateHorizontalExtent();
    update();
    emit zoomChanged(zoom_factor_);
}

double TimelineWidget::trackRowHeight() const noexcept {
    return video_track_row_height_;
}

double TimelineWidget::trackRowHeight(TrackKind kind) const noexcept {
    return kind == TrackKind::Audio
        ? audio_track_row_height_ : video_track_row_height_;
}

void TimelineWidget::setTrackRowHeight(double height) {
    setTrackRowHeights(height, height);
}

void TimelineWidget::setTrackRowHeight(TrackKind kind, double height) {
    if (!std::isfinite(height)) return;
    if (track_row_height_adjustment_mode_ ==
        TrackRowHeightAdjustmentMode::Together) {
        setTrackRowHeights(height, height);
        return;
    }
    if (kind == TrackKind::Audio) {
        setTrackRowHeights(video_track_row_height_, height);
    } else {
        setTrackRowHeights(height, audio_track_row_height_);
    }
}

void TimelineWidget::setTrackRowHeights(
    double video_height,
    double audio_height) {
    if (!std::isfinite(video_height) || !std::isfinite(audio_height)) return;
    const auto normalized_video = std::clamp(
        video_height, kMinimumTrackRowHeight, kMaximumTrackRowHeight);
    const auto normalized_audio = std::clamp(
        audio_height, kMinimumTrackRowHeight, kMaximumTrackRowHeight);
    if (std::abs(normalized_video - video_track_row_height_) < 0.000001 &&
        std::abs(normalized_audio - audio_track_row_height_) < 0.000001) {
        return;
    }

    const auto video_anchor = captureTrackScrollAnchor(TrackKind::Video);
    const auto audio_anchor = captureTrackScrollAnchor(TrackKind::Audio);
    video_track_row_height_ = normalized_video;
    audio_track_row_height_ = normalized_audio;

    // Divider adjustments can leave pixel compensation after one group's
    // independent scroll offset reaches its limit. Re-anchor each group to
    // its visible track and derive fresh offsets for the new row pitch.
    video_splitter_translation_ = 0.0;
    audio_splitter_translation_ = 0.0;
    updateVerticalExtent();
    restoreTrackScrollAnchor(TrackKind::Video, video_anchor);
    restoreTrackScrollAnchor(TrackKind::Audio, audio_anchor);
    emit trackScrollMetricsChanged();
    emit trackHeaderVisualsChanged();
    update();
    emit trackRowHeightsChanged(
        video_track_row_height_, audio_track_row_height_);
}

TrackRowHeightAdjustmentMode
TimelineWidget::trackRowHeightAdjustmentMode() const noexcept {
    return track_row_height_adjustment_mode_;
}

void TimelineWidget::setTrackRowHeightAdjustmentMode(
    TrackRowHeightAdjustmentMode mode) {
    switch (mode) {
    case TrackRowHeightAdjustmentMode::Together:
    case TrackRowHeightAdjustmentMode::IndependentlyByGroup:
        break;
    default:
        return;
    }
    if (mode == track_row_height_adjustment_mode_) return;
    track_row_height_adjustment_mode_ = mode;
    if (mode == TrackRowHeightAdjustmentMode::Together) {
        // Video is the canonical group when independent sizes are joined.
        setTrackRowHeights(video_track_row_height_, video_track_row_height_);
    }
}

double TimelineWidget::nextZoomFactor(int direction) const noexcept {
    if (direction == 0) return zoom_factor_;
    if (direction > 0) {
        for (const auto level : kTimelineZoomLevels) {
            if (level > zoom_factor_ + 0.000001) return level;
        }
        return kTimelineZoomLevels.back();
    }
    for (auto index = kTimelineZoomLevels.size(); index-- > 0;) {
        if (kTimelineZoomLevels[index] < zoom_factor_ - 0.000001) {
            return kTimelineZoomLevels[index];
        }
    }
    return kTimelineZoomLevels.front();
}

bool TimelineWidget::canZoomIn() const noexcept {
    return zoom_factor_ < kMaxTimelineZoomFactor - 0.000001;
}

bool TimelineWidget::canZoomOut() const noexcept {
    return zoom_factor_ > kMinTimelineZoomFactor + 0.000001;
}

QString TimelineWidget::formatTimecode(std::int64_t frame, double frame_rate) {
    if (!std::isfinite(frame_rate) || frame_rate <= 0.0) frame_rate = 30.0;
    const auto nonnegative_frame = std::max<std::int64_t>(0, frame);
    const auto milliseconds_value = std::clamp<long double>(
        std::round(
            static_cast<long double>(nonnegative_frame) * 1000.0L /
            static_cast<long double>(frame_rate)),
        0.0L,
        static_cast<long double>(std::numeric_limits<std::int64_t>::max()));
    const auto milliseconds = static_cast<std::int64_t>(milliseconds_value);
    const auto hours = milliseconds / (60 * 60 * 1000);
    const auto minutes = (milliseconds / (60 * 1000)) % 60;
    const auto seconds = (milliseconds / 1000) % 60;
    const auto remainder = milliseconds % 1000;
    return QString("%1:%2:%3.%4")
        .arg(static_cast<qlonglong>(hours), 2, 10, QChar('0'))
        .arg(static_cast<int>(minutes), 2, 10, QChar('0'))
        .arg(static_cast<int>(seconds), 2, 10, QChar('0'))
        .arg(static_cast<int>(remainder), 3, 10, QChar('0'));
}

void TimelineWidget::setTimelineViewportWidth(int width) {
    const auto normalized_width = std::max(0, width);
    if (timeline_viewport_width_ == normalized_width) return;
    timeline_viewport_width_ = normalized_width;
    updateHorizontalExtent();
}

int TimelineWidget::trackHeaderOverlayWidth() const noexcept {
    return static_cast<int>(std::ceil(
        TimelineGeometry::left_margin + TimelineGeometry::track_header_width));
}

void TimelineWidget::paintTrackHeaderCell(
    QPainter& painter,
    std::size_t track_index,
    const QRectF& row) const {
    if (track_index >= tracks_.size()) return;

    const auto header = QRectF(
        row.left(),
        row.top(),
        TimelineGeometry::track_header_width,
        row.height());
    const bool active_track = active_clip_.has_value() &&
        active_clip_->track_index == track_index;

    painter.setPen(active_track ? QColor("#d5a94b") : QColor("#3d4654"));
    painter.setBrush(active_track ? QColor("#252d3a") : QColor("#202631"));
    QPainterPath header_path;
    constexpr double header_corner_radius = 4.0;
    header_path.moveTo(header.right(), header.top());
    header_path.lineTo(
        header.left() + header_corner_radius,
        header.top());
    header_path.quadTo(
        header.left(),
        header.top(),
        header.left(),
        header.top() + header_corner_radius);
    header_path.lineTo(
        header.left(),
        header.bottom() - header_corner_radius);
    header_path.quadTo(
        header.left(),
        header.bottom(),
        header.left() + header_corner_radius,
        header.bottom());
    header_path.lineTo(header.right(), header.bottom());
    header_path.closeSubpath();
    painter.drawPath(header_path);

    std::size_t track_kind_number = 0;
    for (std::size_t index = 0; index <= track_index; ++index) {
        if (tracks_[index].kind == tracks_[track_index].kind) {
            ++track_kind_number;
        }
    }
    const auto track_label = QString("%1%2  %3")
        .arg(tracks_[track_index].kind == TrackKind::Audio ? "A" : "V")
        .arg(track_kind_number)
        .arg(text(tracks_[track_index].name));
    const auto clip_count = QString("%1 clip%2")
        .arg(tracks_[track_index].clips.size())
        .arg(tracks_[track_index].clips.size() == 1 ? "" : "s");
    const auto text_bounds = header.adjusted(10.0, 2.0, -8.0, -2.0);
    painter.save();
    painter.setClipRect(header, Qt::IntersectClip);

    if (header.height() < 44.0) {
        auto compact_font = painter.font();
        compact_font.setPointSize(8);
        painter.setFont(compact_font);
        painter.setPen(active_track ? QColor("#ffcf5c") : QColor("#b8c2d1"));
        const auto compact_label = QStringLiteral("%1  ·  %2")
            .arg(track_label, clip_count);
        painter.drawText(
            text_bounds,
            Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
            QFontMetrics(compact_font).elidedText(
                compact_label, Qt::ElideRight,
                std::max(0, static_cast<int>(std::floor(text_bounds.width())))));
    } else {
        const auto title_height = std::min(
            33.0, std::max(18.0, header.height() * 0.45));
        const auto title_bounds = QRectF(
            text_bounds.left(), header.top() + 7.0,
            text_bounds.width(), title_height);
        const auto count_bounds = QRectF(
            text_bounds.left(), title_bounds.bottom() + 1.0,
            text_bounds.width(), std::max(0.0,
                header.bottom() - 3.0 - title_bounds.bottom()));
        painter.setPen(active_track ? QColor("#ffcf5c") : QColor("#b8c2d1"));
        painter.drawText(
            title_bounds,
            Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
            QFontMetrics(painter.font()).elidedText(
                track_label, Qt::ElideRight,
                std::max(0, static_cast<int>(std::floor(title_bounds.width())))));

        auto count_font = painter.font();
        count_font.setPointSize(8);
        painter.setFont(count_font);
        painter.setPen(QColor("#7e8999"));
        painter.drawText(
            count_bounds,
            Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
            QFontMetrics(count_font).elidedText(
                clip_count, Qt::ElideRight,
                std::max(0, static_cast<int>(std::floor(count_bounds.width())))));
    }
    painter.restore();

    painter.setPen(QColor("#384250"));
    painter.drawLine(
        QPointF(row.left() + TimelineGeometry::track_header_width, row.top() + 4),
        QPointF(row.left() + TimelineGeometry::track_header_width, row.bottom() - 4));
}

void TimelineWidget::paintTrackHeaderOverlay(
    QPainter& painter) const {
    const auto overlay_width = static_cast<double>(trackHeaderOverlayWidth());
    const auto view = trackViewLayout();

    painter.save();
    painter.setClipRect(QRectF(
        0.0,
        0.0,
        overlay_width,
        static_cast<double>(painter.viewport().height())));
    painter.fillRect(
        QRectF(0.0, 0.0, overlay_width, TimelineGeometry::top_margin),
        QColor("#171a20"));
    painter.setPen(QColor("#384250"));
    painter.drawLine(
        QPointF(overlay_width - 1.0, 12.0),
        QPointF(overlay_width - 1.0, 37.0));
    painter.fillRect(view.video_viewport, QColor("#171a20"));
    painter.fillRect(view.audio_viewport, QColor("#171a20"));

    for (std::size_t track_index = 0;
         track_index < tracks_.size();
         ++track_index) {
        const auto row = trackRect(track_index);
        const auto group = tracks_[track_index].kind;
        painter.save();
        painter.setClipRect(trackGroupViewportRect(group));
        paintTrackHeaderCell(painter, track_index, row);
        painter.setPen(QColor("#384250"));
        painter.drawLine(
            QPointF(overlay_width - 1.0, row.top() + 4),
            QPointF(overlay_width - 1.0, row.bottom() - 4));
        painter.restore();
    }
    const auto paint_empty_group = [&painter](
        const QRectF& viewport, const QString& title, const QString& hint) {
        if (viewport.height() < 18.0) return;
        painter.save();
        painter.setClipRect(viewport);
        painter.setPen(QColor("#9aa4b2"));
        painter.setFont(QFont(painter.font().family(), 8, QFont::DemiBold));
        if (viewport.height() < 38.0) {
            painter.drawText(viewport.adjusted(10.0, 1.0, -5.0, -1.0),
                Qt::AlignLeft | Qt::AlignVCenter, hint);
            painter.restore();
            return;
        }
        painter.drawText(
            viewport.adjusted(10.0, 2.0, -5.0, -viewport.height() / 2.0),
            Qt::AlignLeft | Qt::AlignVCenter,
            title);
        painter.setPen(QColor("#707b8b"));
        painter.setFont(QFont(painter.font().family(), 7));
        painter.drawText(
            viewport.adjusted(10.0, viewport.height() / 2.0, -5.0, -2.0),
            Qt::AlignLeft | Qt::AlignVCenter,
            hint);
        painter.restore();
    };
    const auto current_geometry = geometry();
    if (current_geometry.trackGroupCount(TrackKind::Video) == 0) {
        paint_empty_group(
            view.video_viewport,
            QStringLiteral("No video tracks"),
            QStringLiteral("Drop video or image media"));
    }
    if (current_geometry.trackGroupCount(TrackKind::Audio) == 0) {
        paint_empty_group(
            view.audio_viewport,
            QStringLiteral("No audio tracks"),
            QStringLiteral("Drop audio media"));
    }
    painter.restore();
}

bool TimelineWidget::eventFilter(QObject* watched, QEvent* event) {
    if (event != nullptr && event->type() == QEvent::Resize) {
        const auto* resize_event = static_cast<const QResizeEvent*>(event);
        setTimelineViewportWidth(resize_event->size().width());
        updateVerticalExtent();
    }

    auto* watched_widget = qobject_cast<QWidget*>(watched);
    if (watched_widget != nullptr && watched != this && event != nullptr) {
        if (event->type() == QEvent::Wheel) {
            auto* wheel_event = static_cast<QWheelEvent*>(event);
            const auto position = mapFrom(
                watched_widget, wheel_event->position().toPoint());
            if (handleWheel(
                    position, wheel_event->pixelDelta(), wheel_event->angleDelta(),
                    wheel_event->modifiers())) {
                wheel_event->accept();
                return true;
            }
        }
        if (read_only_) {
            switch (event->type()) {
            case QEvent::DragEnter:
            case QEvent::DragMove:
            case QEvent::DragLeave:
            case QEvent::Drop:
                event->ignore();
                return true;
            default:
                break;
            }
        }
        switch (event->type()) {
        case QEvent::DragEnter: {
            auto* drag_event = static_cast<QDragEnterEvent*>(event);
            if (isSupportedDrop(drag_event->mimeData())) {
                drag_event->acceptProposedAction();
            } else {
                drag_event->ignore();
            }
            return true;
        }
        case QEvent::DragMove: {
            auto* drag_event = static_cast<QDragMoveEvent*>(event);
            const auto position = mapFrom(
                watched_widget,
                drag_event->position().toPoint());
            if (updateDropHover(drag_event->mimeData(), position)) {
                drag_event->acceptProposedAction();
            } else {
                drag_event->ignore();
            }
            return true;
        }
        case QEvent::DragLeave: {
            auto* drag_event = static_cast<QDragLeaveEvent*>(event);
            clearDropHover();
            drag_event->accept();
            return true;
        }
        case QEvent::Drop: {
            auto* drop_event = static_cast<QDropEvent*>(event);
            const auto position = mapFrom(
                watched_widget,
                drop_event->position().toPoint());
            if (processDrop(drop_event->mimeData(), position)) {
                drop_event->acceptProposedAction();
            } else {
                drop_event->ignore();
            }
            return true;
        }
        default:
            break;
        }
    }

    return QWidget::eventFilter(watched, event);
}

QRectF TimelineWidget::trackRect(std::size_t index) const noexcept {
    return geometry().trackRect(index);
}

TimelineGeometry TimelineWidget::geometry() const noexcept {
    const auto fixed_duration = interaction_controller_.trimGesture().active() &&
            interaction_controller_.trimGesture().scaleDuration() > 0
        ? std::optional<std::int64_t>{interaction_controller_.trimGesture().scaleDuration()}
        : std::nullopt;
    return TimelineGeometry(
        tracks_, QSizeF(width(), height()),
        video_track_row_height_, audio_track_row_height_, zoom_factor_,
        fixed_duration, frame_rate_.asDouble(), trackViewLayout());
}

TimelineTrackViewLayout TimelineWidget::trackViewLayout() const noexcept {
    constexpr double bottom_margin = 12.0;
    const auto available_height = std::max(
        0.0,
        static_cast<double>(height()) - TimelineGeometry::top_margin -
            bottom_margin - track_group_splitter_height);
    const auto minimum_height = std::min(
        kMinimumTrackRowHeight, available_height / 2.0);
    const auto minimum_ratio = available_height > 0.0
        ? minimum_height / available_height : 0.5;
    const auto ratio = std::clamp(
        track_group_split_ratio_, minimum_ratio, 1.0 - minimum_ratio);
    auto video_height = available_height * ratio;
    auto audio_height = available_height - video_height;

    const auto audio_top = TimelineGeometry::top_margin + video_height +
        track_group_splitter_height;
    const auto width = std::max(
        0.0,
        static_cast<double>(this->width()) - TimelineGeometry::left_margin -
            TimelineGeometry::right_margin);
    TimelineTrackViewLayout layout;
    layout.video_viewport = QRectF(
        TimelineGeometry::left_margin,
        TimelineGeometry::top_margin,
        width,
        video_height);
    layout.splitter_rect = QRectF(
        TimelineGeometry::left_margin,
        TimelineGeometry::top_margin + video_height,
        width,
        track_group_splitter_height);
    layout.audio_viewport = QRectF(
        TimelineGeometry::left_margin,
        audio_top,
        width,
        audio_height);
    layout.video_scroll_offset = video_scroll_offset_;
    layout.audio_scroll_offset = audio_scroll_offset_;
    const auto video_count = static_cast<double>(std::count_if(
        tracks_.begin(), tracks_.end(), [](const TimelineTrack& track) {
            return track.kind == TrackKind::Video;
        }));
    const auto video_content_height = video_count <= 0.0
        ? 0.0
        : video_count * video_track_row_height_ +
            (video_count - 1.0) * TimelineGeometry::row_gap;
    const auto video_alignment_offset = std::max(
        0.0, video_height - video_content_height);
    layout.video_track_translation = video_splitter_translation_ +
        video_alignment_offset;
    layout.audio_track_translation = audio_splitter_translation_;
    return layout;
}

std::optional<TimelineWidget::TrackScrollAnchor>
TimelineWidget::captureTrackScrollAnchor(TrackKind kind) const noexcept {
    const auto viewport = trackGroupViewportRect(kind);
    if (viewport.height() <= 0.0) return std::nullopt;
    std::optional<TrackScrollAnchor> visible_anchor;
    for (std::size_t index = 0; index < tracks_.size(); ++index) {
        if (tracks_[index].kind != kind) continue;
        const auto row = trackRect(index);
        if (row.bottom() > viewport.top() && row.top() < viewport.bottom()) {
            const auto anchor = kind == TrackKind::Video
                ? TrackScrollAnchor{
                    tracks_[index].track_id,
                    row.bottom() - viewport.bottom(), true}
                : TrackScrollAnchor{
                    tracks_[index].track_id,
                    row.top() - viewport.top(), false};
            if (kind == TrackKind::Audio) return anchor;
            visible_anchor = anchor;
        }
    }
    if (visible_anchor.has_value()) return visible_anchor;

    for (std::size_t index = 0; index < tracks_.size(); ++index) {
        if (tracks_[index].kind != kind) continue;
        const auto row = trackRect(index);
        if (kind == TrackKind::Video) {
            visible_anchor = TrackScrollAnchor{
                tracks_[index].track_id,
                row.bottom() - viewport.bottom(), true};
        } else {
            return TrackScrollAnchor{
                tracks_[index].track_id,
                row.top() - viewport.top(), false};
        }
    }
    return visible_anchor;
}

void TimelineWidget::restoreTrackScrollAnchor(
    TrackKind kind,
    const std::optional<TrackScrollAnchor>& anchor) noexcept {
    auto& scroll_offset = kind == TrackKind::Audio
        ? audio_scroll_offset_ : video_scroll_offset_;
    const auto group_count = geometry().trackGroupCount(kind);
    if (group_count == 0) {
        scroll_offset = 0.0;
        return;
    }

    std::size_t anchor_index = tracks_.size();
    if (anchor.has_value()) {
        const auto index = indexForTrack(tracks_, anchor->track_id);
        if (index.has_value() && tracks_[*index].kind == kind) {
            anchor_index = *index;
        }
    }
    if (anchor_index == tracks_.size()) {
        const auto first = std::find_if(
            tracks_.begin(), tracks_.end(), [kind](const TimelineTrack& track) {
                return track.kind == kind;
            });
        if (first == tracks_.end()) {
            scroll_offset = 0.0;
            return;
        }
        anchor_index = static_cast<std::size_t>(
            std::distance(tracks_.begin(), first));
    }

    std::size_t group_index = 0;
    for (std::size_t index = 0; index < anchor_index; ++index) {
        if (tracks_[index].kind == kind) ++group_index;
    }
    const auto layout = trackViewLayout();
    const auto track_translation = kind == TrackKind::Audio
        ? layout.audio_track_translation : layout.video_track_translation;
    const auto viewport = kind == TrackKind::Audio
        ? layout.audio_viewport : layout.video_viewport;
    const auto row_height = trackRowHeight(kind);
    const auto row_edge_offset = anchor.has_value()
        ? std::clamp(
            anchor->row_edge_offset,
            1.0 - row_height,
            anchor->bottom_edge
                ? 0.0 : std::max(0.0, viewport.height() - 1.0))
        : 0.0;
    const auto row_pitch_offset = static_cast<double>(group_index) *
        (row_height + TimelineGeometry::row_gap);
    const auto desired_offset = anchor.has_value() && anchor->bottom_edge
        ? row_pitch_offset + row_height + track_translation -
            viewport.height() - row_edge_offset
        : row_pitch_offset + track_translation - row_edge_offset;
    scroll_offset = std::clamp(
        desired_offset, 0.0, geometry().trackGroupScrollMaximum(kind));
}

QRectF TimelineWidget::rulerRect() const noexcept {
    return geometry().rulerRect();
}

void TimelineWidget::updateVerticalExtent() {
    constexpr double bottom_margin = 12.0;
    const auto minimum_height = static_cast<int>(std::ceil(
        TimelineGeometry::top_margin + video_track_row_height_ +
        audio_track_row_height_ +
        track_group_splitter_height + bottom_margin));
    setMinimumHeight(std::max(100, minimum_height));
    updateGeometry();
    const auto current_geometry = geometry();
    video_scroll_offset_ = std::clamp(
        video_scroll_offset_, 0.0,
        current_geometry.trackGroupScrollMaximum(TrackKind::Video));
    audio_scroll_offset_ = std::clamp(
        audio_scroll_offset_, 0.0,
        current_geometry.trackGroupScrollMaximum(TrackKind::Audio));
    emit trackScrollMetricsChanged();
}

std::optional<TrackKind> TimelineWidget::emptyTrackGroupAt(double y) const noexcept {
    const auto current_geometry = geometry();
    if (current_geometry.trackGroupCount(TrackKind::Video) == 0 &&
        current_geometry.trackGroupViewportRect(TrackKind::Video)
            .contains(QPointF(TimelineGeometry::left_margin, y))) {
        return TrackKind::Video;
    }
    if (current_geometry.trackGroupCount(TrackKind::Audio) == 0 &&
        current_geometry.trackGroupViewportRect(TrackKind::Audio)
            .contains(QPointF(TimelineGeometry::left_margin, y))) {
        return TrackKind::Audio;
    }
    return std::nullopt;
}

std::optional<TrackKind> TimelineWidget::trackGroupAt(double y) const noexcept {
    const auto current_geometry = geometry();
    if (current_geometry.trackGroupViewportRect(TrackKind::Video)
            .contains(QPointF(TimelineGeometry::left_margin, y))) {
        return TrackKind::Video;
    }
    if (current_geometry.trackGroupViewportRect(TrackKind::Audio)
            .contains(QPointF(TimelineGeometry::left_margin, y))) {
        return TrackKind::Audio;
    }
    return std::nullopt;
}

void TimelineWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    updateVerticalExtent();
}

QRectF TimelineWidget::trackContentRect(std::size_t index) const noexcept {
    // Clips use the complete vertical extent of the track row. The header
    // remains reserved horizontally, while vertical insets would make a clip
    // appear shorter than its Timeline track for no functional reason.
    return geometry().trackContentRect(index);
}

QRectF TimelineWidget::clipRect(const ClipLocation& location) const noexcept {
    if (location.track_index >= tracks_.size() ||
        location.clip_index >= tracks_[location.track_index].clips.size()) {
        return {};
    }
    return geometry().clipRect(displayedClip(location), location.track_index);
}

const TimelineClip& TimelineWidget::displayedClip(
    const ClipLocation& location) const noexcept {
    const auto& preview = interaction_controller_.trimGesture().preview();
    if (preview.has_value()) {
        if (preview->clip_location == location) return preview->clip;
        if (preview->neighbor_location == location &&
            preview->neighbor_clip.has_value()) {
            return *preview->neighbor_clip;
        }
    }
    static const TimelineClip empty_clip;
    if (location.track_index >= tracks_.size() ||
        location.clip_index >= tracks_[location.track_index].clips.size()) {
        return empty_clip;
    }
    return tracks_[location.track_index].clips[location.clip_index];
}

std::int64_t TimelineWidget::mediaDropDuration(
    const QMimeData* mime_data) const noexcept {
    if (mime_data == nullptr) return 1;

    const auto readInteger = [mime_data](const char* mime_type)
        -> std::optional<std::int64_t> {
        if (!mime_data->hasFormat(mime_type)) return std::nullopt;
        bool ok = false;
        const auto value = QString::fromUtf8(mime_data->data(mime_type))
            .toLongLong(&ok);
        return ok && value > 0 ? std::optional<std::int64_t>(value) : std::nullopt;
    };
    const auto readDouble = [mime_data](const char* mime_type)
        -> std::optional<double> {
        if (!mime_data->hasFormat(mime_type)) return std::nullopt;
        bool ok = false;
        const auto value = QString::fromUtf8(mime_data->data(mime_type))
            .toDouble(&ok);
        return ok && std::isfinite(value) && value > 0.0
            ? std::optional<double>(value)
            : std::nullopt;
    };

    const auto source_frame_rate = readDouble(ui::kMediaFrameRateMimeType)
        .value_or(frame_rate_.asDouble());
    if (const auto frame_count = readInteger(ui::kMediaFrameCountMimeType);
        frame_count.has_value()) {
        return timelineFramesForSourceDuration(
            *frame_count, source_frame_rate, frame_rate_).value_or(1);
    }
    const auto duration_seconds = readDouble(ui::kMediaDurationSecondsMimeType);
    if (!duration_seconds.has_value()) return 1;

    const auto estimated = static_cast<long double>(*duration_seconds) *
        static_cast<long double>(frame_rate_.asDouble());
    if (!std::isfinite(estimated) ||
        estimated >= static_cast<long double>(std::numeric_limits<std::int64_t>::max())) {
        return 1;
    }
    return std::max<std::int64_t>(
        1,
        static_cast<std::int64_t>(std::ceil(estimated)));
}

QString TimelineWidget::mediaDropLabel(const QMimeData* mime_data) const {
    if (mime_data == nullptr) return "Media";
    const auto name = QString::fromUtf8(
        mime_data->data(ui::kMediaDisplayNameMimeType));
    if (!name.isEmpty()) return name;
    const auto path = QString::fromUtf8(
        mime_data->data(ui::kMediaPathMimeType));
    const auto file_name = QFileInfo(path).fileName();
    return file_name.isEmpty() ? QStringLiteral("Media") : file_name;
}

bool TimelineWidget::placementOverlaps(
    std::size_t track_index,
    std::int64_t start_frame,
    std::int64_t duration_frames,
    std::optional<ClipLocation> excluded) const noexcept {
    return TimelineDropValidator::overlaps(
        tracks_, track_index, start_frame, duration_frames, excluded);
}

QRectF TimelineWidget::previewRect(
    std::size_t track_index,
    std::int64_t start_frame,
    std::int64_t duration_frames) const noexcept {
    if (track_index >= tracks_.size() || duration_frames <= 0) return {};
    const auto content = trackContentRect(track_index);
    const auto max_frame = std::numeric_limits<std::int64_t>::max();
    const auto bounded_start = std::max<std::int64_t>(0, start_frame);
    const auto bounded_end = bounded_start > max_frame - duration_frames
        ? max_frame
        : bounded_start + duration_frames;
    const auto left = contentXForFrame(bounded_start);
    const auto right = contentXForFrame(bounded_end);
    return QRectF(
        left,
        content.top(),
        std::max(2.0, right - left),
        content.height());
}

timeline::SnapPlacement TimelineWidget::snapPlacement(
    std::size_t track_index,
    std::int64_t raw_start_frame,
    std::int64_t duration_frames,
    std::optional<ClipLocation> excluded) const noexcept {
    return TimelineDropValidator::snap(
        tracks_, geometry(), snap_enabled_, track_index, raw_start_frame,
        duration_frames, excluded);
}

double TimelineWidget::frameRate() const noexcept {
    return frame_rate_.asDouble();
}

std::int64_t TimelineWidget::standardDuration() const noexcept {
    return geometry().standardDuration();
}

std::int64_t TimelineWidget::displayDuration() const noexcept {
    return geometry().displayDuration();
}

std::int64_t TimelineWidget::totalDuration() const noexcept {
    return geometry().totalDuration();
}

double TimelineWidget::pixelsPerFrame() const noexcept {
    return geometry().pixelsPerFrame();
}

void TimelineWidget::updateHorizontalExtent() {
    const auto base_width = std::max(
        1,
        timeline_viewport_width_ > 0 ? timeline_viewport_width_ : width());
    const auto standard = standardDuration();
    const auto base_duration = std::max(totalDuration(), standard);
    const auto scaled_width = std::ceil(
        static_cast<long double>(base_width) *
        static_cast<long double>(base_duration) *
        static_cast<long double>(zoom_factor_) /
        static_cast<long double>(standard));
    const auto max_width = static_cast<long double>(std::numeric_limits<int>::max());
    const auto required_width = static_cast<int>(std::min(scaled_width, max_width));
    setMinimumWidth(std::max(base_width, required_width));
    updateGeometry();
}

std::optional<std::int64_t> TimelineWidget::frameAtContentX(double x) const noexcept {
    return globalFrameAt(x);
}

double TimelineWidget::contentXForFrame(std::int64_t frame) const noexcept {
    return geometry().contentXForFrame(frame);
}

std::optional<std::size_t> TimelineWidget::trackAt(double y) const noexcept {
    return TimelineHitTester::trackAt(geometry(), tracks_.size(), y);
}

std::optional<ClipLocation> TimelineWidget::clipAt(double x, double y) const noexcept {
    return TimelineHitTester::clipAt(
        tracks_, geometry(), x, y, interaction_controller_.trimGesture().preview());
}

std::optional<std::int64_t> TimelineWidget::globalFrameAt(double x) const noexcept {
    return geometry().frameAtContentX(x);
}

std::optional<std::int64_t> TimelineWidget::playheadFrameAtRulerX(
    double x) const noexcept {
    return geometry().playheadFrameAtRulerX(x);
}

std::optional<std::int64_t> TimelineWidget::localFrameAt(
    const ClipLocation& location, double x) const noexcept {
    const auto rect = clipRect(location);
    if (rect.width() <= 0.0) return std::nullopt;
    const auto& clip = tracks_[location.track_index].clips[location.clip_index];
    const double fraction = std::clamp((x - rect.left()) / rect.width(), 0.0, 1.0);
    return std::clamp<std::int64_t>(
        static_cast<std::int64_t>(std::llround(
            fraction * std::max<std::int64_t>(0, clip.timeline_duration_frames - 1))),
        0,
        std::max<std::int64_t>(0, clip.timeline_duration_frames - 1));
}

std::optional<ClipEdge> TimelineWidget::trimEdgeAt(
    const ClipLocation& location, double x) const noexcept {
    const auto rect = clipRect(location);
    if (rect.width() <= 0.0 || !rect.contains(QPointF(x, rect.center().y()))) {
        return std::nullopt;
    }
    const auto left_distance = x - rect.left();
    const auto right_distance = rect.right() - x;
    const auto& track = tracks_[location.track_index];
    const auto& clip = track.clips[location.clip_index];
    const bool shared_left = location.clip_index > 0 &&
        track.clips[location.clip_index - 1].timeline_start_frame >= 0 &&
        track.clips[location.clip_index - 1].timeline_duration_frames > 0 &&
        track.clips[location.clip_index - 1].timeline_start_frame <=
            std::numeric_limits<std::int64_t>::max() -
                track.clips[location.clip_index - 1].timeline_duration_frames &&
        clip.timeline_start_frame >= 0 &&
        track.clips[location.clip_index - 1].timeline_start_frame +
                track.clips[location.clip_index - 1].timeline_duration_frames ==
            clip.timeline_start_frame;
    const bool shared_right = location.clip_index + 1 < track.clips.size() &&
        clip.timeline_start_frame >= 0 && clip.timeline_duration_frames > 0 &&
        clip.timeline_start_frame <= std::numeric_limits<std::int64_t>::max() -
            clip.timeline_duration_frames &&
        clip.timeline_start_frame + clip.timeline_duration_frames ==
            track.clips[location.clip_index + 1].timeline_start_frame;
    const double left_hit_width = shared_left
        ? shared_roll_half_width + shared_single_clip_handle_width
        : edge_width;
    const double right_hit_width = shared_right
        ? shared_roll_half_width + shared_single_clip_handle_width
        : edge_width;
    const bool near_left = left_distance <= left_hit_width;
    const bool near_right = right_distance <= right_hit_width;
    if (near_left && near_right) {
        return left_distance <= right_distance ? ClipEdge::Left : ClipEdge::Right;
    }
    if (near_left) return ClipEdge::Left;
    if (near_right) return ClipEdge::Right;
    return std::nullopt;
}

ClipEdgeEditMode TimelineWidget::trimEditModeAt(
    const ClipLocation& location, ClipEdge edge, double x) const noexcept {
    if (location.track_index >= tracks_.size() ||
        location.clip_index >= tracks_[location.track_index].clips.size()) {
        return ClipEdgeEditMode::Individual;
    }
    const auto& track = tracks_[location.track_index];
    const auto& clip = track.clips[location.clip_index];
    std::optional<std::int64_t> shared_boundary;
    if (edge == ClipEdge::Left && location.clip_index > 0) {
        const auto& previous = track.clips[location.clip_index - 1];
        if (previous.timeline_start_frame >= 0 &&
            previous.timeline_duration_frames > 0 &&
            clip.timeline_start_frame >= 0 &&
            previous.timeline_start_frame <= std::numeric_limits<std::int64_t>::max() -
                previous.timeline_duration_frames &&
            previous.timeline_start_frame + previous.timeline_duration_frames ==
                clip.timeline_start_frame) {
            shared_boundary = clip.timeline_start_frame;
        }
    } else if (edge == ClipEdge::Right &&
               location.clip_index + 1 < track.clips.size() &&
               clip.timeline_start_frame >= 0 &&
               clip.timeline_duration_frames > 0 &&
               clip.timeline_start_frame <= std::numeric_limits<std::int64_t>::max() -
                   clip.timeline_duration_frames) {
        const auto boundary = clip.timeline_start_frame + clip.timeline_duration_frames;
        if (boundary == track.clips[location.clip_index + 1].timeline_start_frame) {
            shared_boundary = boundary;
        }
    }
    if (!shared_boundary.has_value()) return ClipEdgeEditMode::Individual;

    const auto total = displayDuration();
    const auto content = trackContentRect(location.track_index);
    if (total <= 0 || content.width() <= 0.0) return ClipEdgeEditMode::Individual;
    const auto boundary_x = content.left() + content.width() *
        static_cast<double>(*shared_boundary) / static_cast<double>(total);
    return std::abs(x - boundary_x) <= shared_roll_half_width
        ? ClipEdgeEditMode::Rolling
        : ClipEdgeEditMode::Individual;
}

void TimelineWidget::updateTrimHoverCursor(const QPointF& position) {
    if (!interaction_controller_.trimGesture().active() && !razor_mode_) {
        const auto location = clipAt(position.x(), position.y());
        if (location.has_value() &&
            trimEdgeAt(*location, position.x()).has_value()) {
            if (cursor().shape() != Qt::SizeHorCursor) {
                setCursor(Qt::SizeHorCursor);
            }
            return;
        }
    }
    if (testAttribute(Qt::WA_SetCursor)) unsetCursor();
}

std::optional<std::int64_t> TimelineWidget::trimBoundaryAt(double x) const noexcept {
    if (tracks_.empty()) return std::nullopt;
    const auto content = trackContentRect(
        interaction_controller_.trimGesture().location().track_index);
    if (content.width() <= 0.0) return std::nullopt;
    const auto clamped_x = std::clamp(x, content.left(), content.right());
    return globalFrameAt(clamped_x);
}

std::optional<std::pair<std::size_t, std::size_t>>
TimelineWidget::transitionClipIndexesAt(double x, double y) const noexcept {
    return TimelineHitTester::transitionPairAt(
        tracks_, geometry(), x, y, interaction_controller_.trimGesture().preview());
}

void TimelineWidget::emitSelected(
    const ClipLocation& location,
    bool collapse_multi_selection) {
    active_clip_ = location;
    if (collapse_multi_selection) collapseSelectionTo(location);
    selected_transition_.reset();
    emit transitionSelectionCleared();
    const auto& clip = tracks_[location.track_index].clips[location.clip_index];
    emit clipSelected(tracks_[location.track_index].track_id, clip.clip_id);
}

void TimelineWidget::collapseSelectionTo(
    std::optional<ClipLocation> location) {
    if (!location.has_value() || location->track_index >= tracks_.size() ||
        location->clip_index >= tracks_[location->track_index].clips.size()) {
        selected_clip_ids_.clear();
        return;
    }
    selected_clip_ids_ = {
        tracks_[location->track_index].clips[location->clip_index].clip_id};
}

void TimelineWidget::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor("#171a20"));

    const auto track_view = trackViewLayout();
    const auto current_geometry = geometry();
    painter.fillRect(track_view.video_viewport, QColor("#1b2028"));
    painter.fillRect(track_view.audio_viewport, QColor("#1b2028"));
    const auto splitter_highlighted = splitter_hover_active_ || split_drag_active_;
    const auto splitter_background = split_drag_active_
        ? QColor("#214463")
        : splitter_highlighted ? QColor("#26394b") : QColor("#202630");
    const auto splitter_border = split_drag_active_
        ? QColor("#80c7ff")
        : splitter_highlighted ? QColor("#66b7ff") : QColor("#566476");
    const auto splitter_grip = split_drag_active_
        ? QColor("#b8e2ff")
        : splitter_hover_active_ ? QColor("#9bd5ff") : QColor("#aab6c5");
    painter.fillRect(track_view.splitter_rect, splitter_background);
    painter.setPen(QPen(splitter_border, 1.0));
    painter.drawLine(track_view.splitter_rect.topLeft(),
                     track_view.splitter_rect.topRight());
    painter.drawLine(track_view.splitter_rect.bottomLeft(),
                     track_view.splitter_rect.bottomRight());
    const auto grip_center = track_view.splitter_rect.center();
    const QRectF grip_bounds(
        grip_center.x() - 18.0,
        grip_center.y() - 6.0,
        36.0,
        12.0);
    const auto grip_border = split_drag_active_
        ? QColor("#80c7ff")
        : splitter_hover_active_ ? QColor("#66b7ff") : QColor("#566476");
    painter.setPen(QPen(grip_border, 1.0));
    painter.setBrush(split_drag_active_
        ? QColor("#214463")
        : splitter_hover_active_ ? QColor("#26394b") : QColor("#2a303a"));
    painter.drawRoundedRect(grip_bounds, 5.0, 5.0);
    painter.setPen(QPen(splitter_grip, 1.4, Qt::SolidLine, Qt::RoundCap));
    for (const auto offset : {-3.0, 0.0, 3.0}) {
        painter.drawLine(
            QPointF(grip_center.x() - 6.0, grip_center.y() + offset),
            QPointF(grip_center.x() + 6.0, grip_center.y() + offset));
    }
    const auto paint_empty_group = [&painter](
        const QRectF& viewport, const QString& message) {
        if (viewport.height() < 18.0) return;
        painter.save();
        painter.setClipRect(viewport);
        painter.setPen(QColor("#727e8e"));
        painter.setFont(QFont(painter.font().family(), 9));
        painter.drawText(viewport.adjusted(
            TimelineGeometry::track_header_width + 18.0, 0.0, -22.0, 0.0),
            Qt::AlignCenter, message);
        painter.restore();
    };
    if (current_geometry.trackGroupCount(TrackKind::Video) == 0) {
        paint_empty_group(track_view.video_viewport,
                          QStringLiteral("Drop video or image media here"));
    }
    if (current_geometry.trackGroupCount(TrackKind::Audio) == 0) {
        paint_empty_group(track_view.audio_viewport,
                          QStringLiteral("Drop audio media here"));
    }

    const auto total = displayDuration();
    const auto ruler = rulerRect();
    painter.setPen(QColor("#4b5565"));
    painter.drawLine(ruler.bottomLeft(), ruler.bottomRight());
    const auto ruler_end = std::max<std::int64_t>(1, total);
    const auto tick_count = std::clamp<std::int64_t>(
        static_cast<std::int64_t>(std::ceil(static_cast<double>(ruler_end) / 120.0)),
        1,
        12);
    const auto tick_step = std::max<std::int64_t>(
        1,
        (ruler_end + tick_count - 1) / tick_count);
    const auto ruler_content = trackContentRect(0);
    const auto minor_frame_width = pixelsPerFrame();
    const auto minor_step = rulerMinorStep(minor_frame_width);
    if (minor_frame_width > 0.0 && minor_frame_width < 1.0 &&
        ruler_content.width() > 0.0) {
        const auto dirty = event != nullptr
            ? QRectF(event->rect())
            : QRectF(rect());
        const auto first_x = std::max({
            ruler_content.left(), ruler.left(), dirty.left()});
        const auto last_x = std::min({
            ruler_content.right(), ruler.right(), dirty.right()});
        if (last_x >= first_x) {
            const auto first_visible_frame = std::max<std::int64_t>(
                0,
                static_cast<std::int64_t>(std::floor(
                    (first_x - ruler_content.left()) / minor_frame_width)) - 1);
            const auto last_visible_frame = std::min<std::int64_t>(
                ruler_end,
                static_cast<std::int64_t>(std::ceil(
                    (last_x - ruler_content.left()) / minor_frame_width)) + 1);
            const auto first_frame = first_visible_frame -
                first_visible_frame % minor_step;

            painter.save();
            painter.setPen(QPen(QColor(82, 94, 110, 100), 1.0));
            for (auto frame = first_frame;
                 frame <= last_visible_frame;
                 frame += minor_step) {
                if (frame % tick_step != 0) {
                    const auto x = contentXForFrame(frame);
                    painter.drawLine(
                        QPointF(x, ruler.top()),
                        QPointF(x, ruler.bottom()));
                }
                if (frame > last_visible_frame - minor_step) break;
            }
            painter.restore();
        }
    }
    const auto fps = frameRate();
    painter.setFont(QFont(painter.font().family(), 8));
    for (std::int64_t frame = 0; frame <= ruler_end; frame += tick_step) {
        const auto x = contentXForFrame(frame);
        painter.setPen(QColor("#252d38"));
        painter.drawLine(QPointF(x, ruler.top()), QPointF(x, ruler.bottom()));
        painter.setPen(QColor("#566171"));
        painter.drawLine(QPointF(x, ruler.bottom() - 5), ruler.bottomRight() +
            QPointF(x - ruler.right(), 0));
        painter.setPen(QColor("#9aa4b2"));
        painter.drawText(
            QRectF(x + 4, ruler.top(), 96, ruler.height()),
            Qt::AlignLeft | Qt::AlignVCenter,
            TimelineWidget::formatTimecode(frame, fps));
        if (frame > ruler_end - tick_step) break;
    }

    for (std::size_t track_index = 0; track_index < tracks_.size(); ++track_index) {
        const auto row = trackRect(track_index);
        const auto content = trackContentRect(track_index);
        painter.save();
        painter.setClipRect(
            trackGroupViewportRect(tracks_[track_index].kind), Qt::IntersectClip);
        const bool active_track = active_clip_.has_value() &&
            active_clip_->track_index == track_index;
        painter.setPen(active_track ? QColor("#d5a94b") : QColor("#3d4654"));
        painter.setBrush(active_track ? QColor("#252d3a") : QColor("#202631"));
        painter.drawRoundedRect(row, 4, 4);
        paintTrackHeaderCell(painter, track_index, row);

        for (std::size_t clip_index = 0;
             clip_index < tracks_[track_index].clips.size(); ++clip_index) {
            const ClipLocation location{track_index, clip_index};
            const auto rect = clipRect(location);
            const auto& clip = displayedClip(location);
            const bool active = active_clip_.has_value() &&
                *active_clip_ == location;
            const bool selected = std::find(
                selected_clip_ids_.begin(), selected_clip_ids_.end(), clip.clip_id) !=
                selected_clip_ids_.end();
            const auto clip_id = clip.clip_id;
            const bool moving = interaction_controller_.moveActive() &&
                interaction_controller_.movingClipId() == clip_id;
            const bool trimming = interaction_controller_.trimGesture().active() &&
                interaction_controller_.trimGesture().location() == location;
            const QColor track_colors[] = {
                QColor("#3c75ae"), QColor("#357f70"),
                QColor("#6d5ca8"), QColor("#9b6943")};
            const auto clip_color = clip.kind == ClipKind::Text
                ? QColor("#8c5fb3")
                : clip.kind == ClipKind::Audio
                    ? QColor("#2d8b91")
                    : track_colors[track_index % 4];
            if (moving) {
                painter.setPen(QColor(255, 255, 255, 90));
                painter.setBrush(QColor(
                    clip_color.red(), clip_color.green(), clip_color.blue(), 55));
            } else {
                painter.setPen(active ? QColor("#ffcf5c")
                    : selected ? QColor("#70d8ce") : clip_color.lighter(135));
                painter.setBrush(trimming
                    ? QColor("#8a5a2f")
                    : active ? clip_color.lighter(115)
                    : selected ? clip_color.lighter(125) : clip_color);
            }
            painter.drawRoundedRect(rect, 3, 3);

            if (clip.kind == ClipKind::Audio &&
                clip.source_duration_time_us > 0 && rect.width() > 2.0 &&
                rect.height() > 10.0) {
                const auto waveform_entry = audio_waveforms_.find(clip.source_path);
                const auto waveform = waveform_entry == audio_waveforms_.end()
                    ? std::shared_ptr<const media::AudioWaveform>{}
                    : waveform_entry->second.lock();
                if (waveform != nullptr && !waveform->peaks.empty()) {
                    const auto waveform_bounds = rect.adjusted(1.0, 3.0, -1.0, -3.0);
                    const bool stereo_channels =
                        stereo_waveform_display_enabled_ &&
                        waveform->source_channel_count > 1;
                    auto waveform_color = QColor("#a9e8e2");
                    waveform_color.setAlpha(active ? 215 : 180);
                    const auto draw_channel = [&](const QRectF& channel_bounds,
                                                  int channel) {
                        const auto visible = channel_bounds.intersected(
                            QRectF(event->rect()));
                        if (visible.isEmpty()) return;
                        const auto first_x = std::max(
                            static_cast<int>(std::floor(visible.left())),
                            static_cast<int>(std::floor(channel_bounds.left())));
                        const auto last_x = std::min(
                            static_cast<int>(std::ceil(visible.right())),
                            static_cast<int>(std::ceil(channel_bounds.right())));
                        const auto center_y = channel_bounds.center().y();
                        const auto maximum_amplitude = std::max(
                            0.0, channel_bounds.height() * 0.44);
                        painter.save();
                        painter.setClipRect(channel_bounds, Qt::IntersectClip);
                        painter.setPen(QPen(waveform_color, 1.0));
                        for (int x = first_x; x <= last_x; ++x) {
                            const auto first_fraction = std::clamp(
                                (static_cast<double>(x) - rect.left()) / rect.width(),
                                0.0,
                                1.0);
                            const auto end_fraction = std::clamp(
                                (static_cast<double>(x) + 1.0 - rect.left()) /
                                    rect.width(),
                                0.0,
                                1.0);
                            const auto first_source_time_us =
                                static_cast<long double>(clip.source_start_time_us) +
                                first_fraction * static_cast<long double>(
                                    clip.source_duration_time_us);
                            const auto end_source_time_us =
                                static_cast<long double>(clip.source_start_time_us) +
                                end_fraction * static_cast<long double>(
                                    clip.source_duration_time_us);
                            const auto maximum_waveform_time_us =
                                static_cast<long double>(waveform->peaks.size()) *
                                media::kAudioWaveformBucketDurationUs;
                            if (end_source_time_us <= 0.0L ||
                                first_source_time_us >= maximum_waveform_time_us) {
                                continue;
                            }
                            const auto first_bucket = static_cast<std::size_t>(
                                std::max(0.0L, first_source_time_us) /
                                media::kAudioWaveformBucketDurationUs);
                            const auto end_bucket = static_cast<std::size_t>(
                                std::ceil(std::min(
                                    end_source_time_us, maximum_waveform_time_us) /
                                    media::kAudioWaveformBucketDurationUs));
                            const auto bounded_end_bucket = std::min(
                                end_bucket, waveform->peaks.size());
                            auto peak = std::uint8_t{0};
                            for (auto bucket = first_bucket;
                                 bucket < bounded_end_bucket; ++bucket) {
                                if (channel < 0) {
                                    peak = std::max({
                                        peak,
                                        waveform->peaks[bucket].left,
                                        waveform->peaks[bucket].right});
                                } else if (channel == 0) {
                                    peak = std::max(
                                        peak, waveform->peaks[bucket].left);
                                } else {
                                    peak = std::max(
                                        peak, waveform->peaks[bucket].right);
                                }
                            }
                            if (peak == 0) continue;
                            const auto amplitude = std::max(
                                1.0,
                                maximum_amplitude * static_cast<double>(peak) / 255.0);
                            painter.drawLine(
                                QPointF(x + 0.5, center_y - amplitude),
                                QPointF(x + 0.5, center_y + amplitude));
                        }
                        painter.restore();
                    };
                    painter.save();
                    painter.setClipRect(waveform_bounds, Qt::IntersectClip);
                    if (stereo_channels) {
                        const auto channel_height = waveform_bounds.height() / 2.0;
                        const QRectF left_bounds(
                            waveform_bounds.left(), waveform_bounds.top(),
                            waveform_bounds.width(), channel_height);
                        const QRectF right_bounds(
                            waveform_bounds.left(),
                            waveform_bounds.top() + channel_height,
                            waveform_bounds.width(),
                            waveform_bounds.height() - channel_height);
                        draw_channel(left_bounds, 0);
                        draw_channel(right_bounds, 1);
                        painter.setPen(QPen(QColor(169, 232, 226, 80), 1.0));
                        painter.drawLine(
                            QPointF(waveform_bounds.left(), waveform_bounds.center().y()),
                            QPointF(waveform_bounds.right(), waveform_bounds.center().y()));
                    } else {
                        draw_channel(waveform_bounds, -1);
                    }
                    painter.restore();
                }
            }

            if (clip.kind == ClipKind::Audio && rect.width() > 2.0 &&
                rect.height() > 12.0) {
                const auto& gain_points = audio_gain_envelope_drag_.has_value() &&
                        audio_gain_envelope_drag_->clip_id == clip.clip_id
                    ? audio_gain_envelope_drag_->keyframes
                    : clip.audio_gain_keyframes;
                const auto bounds = rect.adjusted(1.0, 6.0, -1.0, -6.0);
                const auto duration = std::max<std::int64_t>(
                    1, clip.timeline_duration_frames);
                const auto point = [&](std::int64_t frame, double gain) {
                    const auto x = bounds.left() + bounds.width() *
                        std::clamp(static_cast<double>(frame) / duration, 0.0, 1.0);
                    const auto y = bounds.bottom() - bounds.height() *
                        std::clamp(gain / 2.0, 0.0, 1.0);
                    return QPointF(x, y);
                };
                std::vector<AudioGainKeyframe> visible_points = gain_points;
                if (visible_points.empty()) {
                    visible_points = {{0, 1.0}, {duration, 1.0}};
                } else {
                    if (visible_points.front().frame > 0) {
                        visible_points.insert(visible_points.begin(), {
                            0, evaluateAudioGainEnvelope(visible_points, 0.0)});
                    }
                    if (visible_points.back().frame < duration) {
                        visible_points.push_back({duration,
                            evaluateAudioGainEnvelope(visible_points,
                                static_cast<double>(duration))});
                    }
                }
                QPainterPath envelope_path;
                envelope_path.moveTo(point(
                    visible_points.front().frame,
                    visible_points.front().gain));
                for (std::size_t index = 1; index < visible_points.size(); ++index) {
                    envelope_path.lineTo(point(
                        visible_points[index].frame,
                        visible_points[index].gain));
                }
                painter.save();
                painter.setClipRect(bounds, Qt::IntersectClip);
                painter.setPen(QPen(QColor("#ffd56a"), 2.0,
                                    Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                painter.setBrush(Qt::NoBrush);
                painter.drawPath(envelope_path);
                if (volume_mode_ || !gain_points.empty()) {
                    for (const auto& keyframe : gain_points) {
                        painter.setPen(QPen(QColor("#382e17"), 1.0));
                        painter.setBrush(QColor("#ffe39a"));
                        painter.drawEllipse(point(keyframe.frame, keyframe.gain), 3.2, 3.2);
                    }
                }
                painter.restore();
            }

            painter.setPen(moving ? QColor(244, 247, 251, 100) : QColor("#f4f7fb"));
            const auto label = QString("%1  %2%3")
                .arg(clip_index + 1)
                .arg(clip.kind == ClipKind::Text ? "[Text] "
                    : clip.kind == ClipKind::Audio ? "[Audio] " : "")
                .arg(text(clip.display_name))
                + " - " + clipDuration(clip, frame_rate_.asDouble());
            painter.drawText(rect.adjusted(6, 0, -6, 0),
                Qt::AlignVCenter,
                QFontMetrics(painter.font()).elidedText(
                    label, Qt::ElideRight, std::max(1, static_cast<int>(rect.width() - 12))));

            if (active && clip.timeline_duration_frames > 0) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor("#ffe08a"));
                for (const auto property : {
                         timeline::TransformProperty::PositionX,
                         timeline::TransformProperty::PositionY,
                         timeline::TransformProperty::Scale,
                         timeline::TransformProperty::Rotation,
                         timeline::TransformProperty::Opacity}) {
                    for (const auto& keyframe : timeline::keyframesFor(
                             clip.keyframes, property)) {
                        const double fraction = static_cast<double>(keyframe.frame) /
                            std::max<std::int64_t>(1, clip.timeline_duration_frames - 1);
                        const auto key_x = rect.left() + rect.width() *
                            std::clamp(fraction, 0.0, 1.0);
                        const auto key_y = rect.top() + 7.0;
                        painter.drawPolygon({
                            QPointF(key_x, key_y - 4.0),
                            QPointF(key_x + 4.0, key_y),
                            QPointF(key_x, key_y + 4.0),
                            QPointF(key_x - 4.0, key_y)});
                    }
                }
            }
        }

        const auto& trim_preview = interaction_controller_.trimGesture().preview();
        if (interaction_controller_.trimGesture().active() && trim_preview.has_value() &&
            trim_preview->clip_location.track_index == track_index &&
            !trim_preview->neighbor_clip.has_value()) {
            const auto rect = clipRect(trim_preview->clip_location);
            const auto edge_x = interaction_controller_.trimGesture().edge() == ClipEdge::Left
                ? rect.left()
                : rect.right();
            painter.setPen(QPen(QColor("#fff0a3"), 2.0, Qt::SolidLine));
            painter.drawLine(
                QPointF(edge_x, row.top() + 2.0),
                QPointF(edge_x, row.bottom() - 2.0));
        }

        for (const auto& transition : tracks_[track_index].transitions) {
            const auto indexes = [&]() -> std::optional<std::pair<std::size_t, std::size_t>> {
                std::optional<std::size_t> from;
                std::optional<std::size_t> to;
                for (std::size_t index = 0; index < tracks_[track_index].clips.size(); ++index) {
                    if (displayedClip(ClipLocation{track_index, index}).clip_id ==
                        transition.from_clip_id) {
                        from = index;
                    }
                    if (displayedClip(ClipLocation{track_index, index}).clip_id ==
                        transition.to_clip_id) {
                        to = index;
                    }
                }
                if (!from.has_value() || !to.has_value()) return std::nullopt;
                return std::make_pair(*from, *to);
            }();
            if (!indexes.has_value() || indexes->second != indexes->first + 1 || total <= 0) {
                continue;
            }
            const auto& from = displayedClip(
                ClipLocation{track_index, indexes->first});
            const auto& to = displayedClip(
                ClipLocation{track_index, indexes->second});
            if (transition.duration_frames > std::min(
                    from.timeline_duration_frames,
                    to.timeline_duration_frames)) {
                continue;
            }
            const auto boundary_frame = from.timeline_start_frame +
                from.timeline_duration_frames;
            const auto boundary = content.left() + content.width() *
                static_cast<double>(boundary_frame) / total;
            const auto transition_width = content.width() *
                static_cast<double>(transition.duration_frames) / total;
            const auto selected = selected_transition_.has_value() &&
                selected_transition_->track_index == track_index &&
                selected_transition_->from_clip_index == indexes->first &&
                selected_transition_->to_clip_index == indexes->second;
            const auto left = boundary - transition_width;
            const auto right = transition.kind == TransitionKind::FadeToBlack
                ? boundary + transition_width
                : boundary;
            const auto transition_color =
                transition.kind == TransitionKind::AudioCrossfade
                    ? QColor("#70d7cd") : QColor("#d5a94b");
            painter.setPen(QPen(
                selected ? QColor("#fff0a3") : transition_color,
                selected ? 2.0 : 1.0,
                Qt::DashLine));
            painter.setBrush(transition.kind == TransitionKind::AudioCrossfade
                ? QColor(74, 185, 175, selected ? 105 : 55)
                : QColor(213, 169, 75, selected ? 90 : 45));
            painter.drawRect(QRectF(
                std::max(content.left(), left),
                content.top() + 2,
                std::min(content.right(), right) - std::max(content.left(), left),
                content.height() - 4));
            painter.setPen(QColor("#ffe08a"));
            painter.drawText(
                QRectF(std::max(content.left(), left), content.top() + 4,
                       std::max(0.0, std::min(content.right(), right) -
                           std::max(content.left(), left)), 18),
                Qt::AlignCenter,
                transition.kind == TransitionKind::AudioCrossfade
                    ? "Crossfade"
                    : transition.kind == TransitionKind::FadeToBlack
                        ? "Fade" : "Dissolve");
        }
        painter.restore();
    }

    // At frame-level zoom, draw only the frame boundaries that intersect the
    // current paint region. Keep these guides inside the time ruler so clip
    // content remains visually clear at high zoom levels.
    const auto frame_grid_content = trackContentRect(0);
    const auto frame_width = pixelsPerFrame();
    if (ruler_end > 0 && frame_width >= 1.0 &&
        frame_grid_content.width() > 0.0) {
        const auto dirty = event != nullptr
            ? QRectF(event->rect())
            : QRectF(rect());
        const auto first_x = std::max({
            frame_grid_content.left(), ruler.left(), dirty.left()});
        const auto last_x = std::min({
            frame_grid_content.right(), ruler.right(), dirty.right()});
        if (last_x >= first_x) {
            const auto first_frame = std::max<std::int64_t>(
                0,
                static_cast<std::int64_t>(std::floor(
                    (first_x - frame_grid_content.left()) / frame_width)) - 1);
            const auto last_frame = std::min<std::int64_t>(
                ruler_end,
                static_cast<std::int64_t>(std::ceil(
                    (last_x - frame_grid_content.left()) / frame_width)) + 1);

            painter.save();
            painter.setPen(QPen(QColor(92, 105, 122, 105), 1.0));
            for (auto frame = first_frame; frame <= last_frame; ++frame) {
                const auto x = contentXForFrame(frame);
                painter.drawLine(
                    QPointF(x, ruler.top()),
                    QPointF(x, ruler.bottom()));
                if (frame == last_frame) break;
            }
            painter.restore();
        }
    }

    TimelineInteractionPaintState interaction_paint;
    interaction_paint.tracks = &tracks_;
    const auto paint_geometry = geometry();
    interaction_paint.geometry = &paint_geometry;
    const auto move_target = interaction_controller_.moveTarget();
    if (interaction_controller_.moveActive()) {
        interaction_paint.moving_clip = locationForClip(
            tracks_, interaction_controller_.movingClipId());
        if (move_target.has_value()) {
            interaction_paint.move_target_track =
                indexForTrack(tracks_, move_target->track_id);
            interaction_paint.move_target_frame = move_target->timeline_start_frame;
            interaction_paint.snap_guide_frame = move_target->snap_guide_frame;
        }
    }
    const auto& drop = interaction_controller_.dropPreview();
    interaction_paint.invalid_marker_position =
        interaction_controller_.moveActive() ? move_preview_position_ : drop.pointer_position;
    interaction_paint.media_drop_hovered = drop.hovering && drop.media;
    interaction_paint.drop_hover_track = drop.hovering
        ? drop.target_track_index : std::nullopt;
    interaction_paint.drop_hover_frame = drop.hovering
        ? drop.target_frame : std::nullopt;
    interaction_paint.drop_empty_group = drop.hovering
        ? drop.target_empty_group : std::nullopt;
    interaction_paint.drop_duration_frames = drop.duration_frames;
    interaction_paint.drop_label = drop.label;
    interaction_paint.drop_valid = drop.valid;
    if (!interaction_paint.moving_clip.has_value()) {
        interaction_paint.snap_guide_frame = drop.snap_guide_frame;
    }
    TimelineInteractionPainter::paint(painter, interaction_paint);

    const bool active_clip_valid = active_clip_.has_value() &&
        active_clip_->track_index < tracks_.size() &&
        active_clip_->clip_index < tracks_[active_clip_->track_index].clips.size();
    // The playhead is independent from clip selection. Keep it visible while
    // the timeline has content, including after a gap click clears selection.
    if (interaction_controller_.rulerSeekPending() || active_clip_valid || totalDuration() > 0) {
        const auto content = trackContentRect(0);
        const auto content_duration = std::max<std::int64_t>(1, totalDuration());
        const auto visual_duration = std::max<std::int64_t>(1, displayDuration());
        const auto ruler_frame = interaction_controller_.rulerPreviewFrame();
        auto global_frame = ruler_frame.value_or(playhead_frame_);
        const auto drag_frame = interaction_controller_.seekPreviewLocalFrame();
        if (!ruler_frame.has_value() && active_clip_valid && drag_frame.has_value()) {
            const auto& clip = tracks_[active_clip_->track_index]
                .clips[active_clip_->clip_index];
            global_frame = clip.timeline_start_frame + *drag_frame;
        }
        global_frame = std::clamp<std::int64_t>(global_frame, 0, content_duration - 1);
        const auto x = std::clamp(
            interaction_controller_.rulerContentX().value_or(content.left() + content.width() *
                static_cast<double>(global_frame) / visual_duration),
            content.left(),
            content.right());
        painter.setPen(QPen(QColor("#ffcf5c"), 2));
        painter.drawLine(
            QPointF(x, track_view.video_viewport.top()),
            QPointF(x, track_view.audio_viewport.bottom()));
        painter.setBrush(QColor("#ffcf5c"));
        painter.drawPolygon({
            QPointF(x - 4, ruler.top() + 1),
            QPointF(x + 4, ruler.top() + 1),
            QPointF(x, ruler.bottom() + 4)});
    }
}

void TimelineWidget::dragEnterEvent(QDragEnterEvent* event) {
    if (read_only_) {
        event->ignore();
        return;
    }
    if (isSupportedDrop(event->mimeData())) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void TimelineWidget::dragLeaveEvent(QDragLeaveEvent* event) {
    if (read_only_) {
        event->ignore();
        return;
    }
    clearDropHover();
    event->accept();
}

void TimelineWidget::dragMoveEvent(QDragMoveEvent* event) {
    if (read_only_) {
        event->ignore();
        return;
    }
    if (updateDropHover(event->mimeData(), event->position())) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void TimelineWidget::dropEvent(QDropEvent* event) {
    if (read_only_) {
        event->ignore();
        return;
    }
    if (processDrop(event->mimeData(), event->position())) {
        event->acceptProposedAction();
        update();
    } else {
        clearDropHover();
        event->ignore();
    }
}

bool TimelineWidget::isSupportedDrop(const QMimeData* mime_data) const noexcept {
    return mime_data != nullptr &&
        (mime_data->hasFormat(ui::kMediaPathMimeType) ||
         mime_data->hasFormat(ui::kEffectIdMimeType) ||
         !media_browser_ui::localFilesFromUrls(mime_data).isEmpty());
}

void TimelineWidget::clearDragPreview() {
    interaction_controller_.clearDropGhost();
}

void TimelineWidget::clearDropHover() {
    interaction_controller_.clearDropPreview();
    update();
}

bool TimelineWidget::updateDropHover(
    const QMimeData* mime_data,
    const QPointF& position) {
    const auto track = trackAt(position.y());
    const auto empty_group = emptyTrackGroupAt(position.y());
    const auto frame = globalFrameAt(position.x());
    const bool supported = isSupportedDrop(mime_data);
    bool accepted = supported &&
        (track.has_value() || empty_group.has_value()) && frame.has_value();
    bool media_target_valid = true;
    TimelineDropPreview preview;
    preview.hovering = supported;
    preview.pointer_position = position;
    if (accepted) {
        preview.target_track_index = track;
        preview.target_frame = frame;
        preview.target_empty_group = empty_group;
    }
    if (supported && mime_data->hasFormat(ui::kMediaPathMimeType)) {
        preview.media = true;
        preview.duration_frames = mediaDropDuration(mime_data);
        preview.label = mediaDropLabel(mime_data);
        if (accepted && track.has_value()) {
            const auto snapped = snapPlacement(
                *track,
                *frame,
                preview.duration_frames);
            preview.target_frame = snapped.start_frame;
            preview.snap_guide_frame = snapped.guide_frame;
            const auto media_kind = mime_data->hasFormat(ui::kMediaKindMimeType)
                ? QString::fromUtf8(mime_data->data(ui::kMediaKindMimeType))
                : QStringLiteral("video");
            const bool audio_drop = media_kind == QStringLiteral("audio");
            const bool target_is_audio = tracks_[*track].kind == TrackKind::Audio;
            if (audio_drop) {
                preview.valid = !target_is_audio || !placementOverlaps(
                    *track, *preview.target_frame, preview.duration_frames);
                media_target_valid = preview.valid;
            } else {
                preview.valid = !target_is_audio && !placementOverlaps(
                    *track, *preview.target_frame, preview.duration_frames);
                media_target_valid = !target_is_audio;
            }
        } else if (accepted && empty_group.has_value()) {
            const auto media_kind = mime_data->hasFormat(ui::kMediaKindMimeType)
                ? QString::fromUtf8(mime_data->data(ui::kMediaKindMimeType))
                : QStringLiteral("video");
            const auto destination_kind = media_kind == QStringLiteral("audio")
                ? TrackKind::Audio : TrackKind::Video;
            preview.valid = destination_kind == *empty_group;
            media_target_valid = preview.valid;
        }
    } else if (supported && !media_browser_ui::localFilesFromUrls(mime_data).isEmpty()) {
        preview.media = true;
        preview.duration_frames = 1;
        preview.label = QStringLiteral("Import media");
        media_target_valid = accepted && (empty_group.has_value() ||
            (track.has_value() &&
             (tracks_[*track].kind == TrackKind::Video ||
              tracks_[*track].kind == TrackKind::Audio)));
        preview.valid = media_target_valid;
    } else if (supported && mime_data->hasFormat(ui::kEffectIdMimeType)) {
        const auto effect_id = QString::fromUtf8(
            mime_data->data(ui::kEffectIdMimeType));
        const auto effect_utf8 = effect_id.toUtf8();
        const bool is_clip_effect = creative_suite::effects::findDefinition(
            std::string_view(effect_utf8.constData(),
                             static_cast<std::size_t>(effect_utf8.size()))) != nullptr;
        const bool is_transition_effect =
            effect_id == QStringLiteral("transitions.cross_dissolve") ||
            effect_id == QStringLiteral("transitions.fade_to_black");
        if (is_transition_effect) {
            const auto indexes = transitionClipIndexesAt(
                position.x(), position.y());
            if (!indexes.has_value() || !track.has_value()) {
                accepted = false;
                preview.target_track_index.reset();
                preview.target_frame.reset();
            } else {
                const auto& clips = tracks_[*track].clips;
                preview.target_track_index = track;
                preview.target_frame = clips[indexes->second].timeline_start_frame;
            }
        } else if (is_clip_effect && track.has_value() && frame.has_value()) {
            const auto clip = tracks_[*track].kind == TrackKind::Video
                ? clipAt(position.x(), position.y())
                : std::optional<ClipLocation>{};
            const bool compatible = clip.has_value() &&
                (tracks_[*track].clips[clip->clip_index].kind == ClipKind::Video ||
                 tracks_[*track].clips[clip->clip_index].kind == ClipKind::Image);
            preview.valid = compatible;
            media_target_valid = compatible;
            if (!compatible) accepted = false;
        }
    }
    interaction_controller_.setDropPreview(std::move(preview));
    update();
    return accepted && media_target_valid;
}

bool TimelineWidget::processDrop(
    const QMimeData* mime_data,
    const QPointF& position) {
    const auto track = trackAt(position.y());
    const auto empty_group = emptyTrackGroupAt(position.y());
    const auto frame = globalFrameAt(position.x());
    const bool is_media_drop = mime_data != nullptr &&
        mime_data->hasFormat(ui::kMediaPathMimeType);
    const auto external_paths = media_browser_ui::localFilesFromUrls(mime_data);
    const bool is_external_file_drop = !external_paths.isEmpty();
    const bool is_effect_drop = mime_data != nullptr &&
        mime_data->hasFormat(ui::kEffectIdMimeType);
    if ((!is_media_drop && !is_external_file_drop && !is_effect_drop) ||
        (!track.has_value() && !empty_group.has_value()) || !frame.has_value()) {
        return false;
    }

    if (is_external_file_drop) {
        QStringList paths = external_paths;
        clearDropHover();
        if (empty_group.has_value()) {
            emit externalFilesGroupDropRequested(paths, *empty_group, *frame);
        } else {
            const auto& target_track = tracks_[*track];
            if (target_track.kind != TrackKind::Video &&
                target_track.kind != TrackKind::Audio) return false;
            emit externalFilesDropRequested(paths, target_track.track_id, *frame);
        }
        return true;
    }

    auto target_frame = *frame;
    if (is_media_drop) {
        const auto media_kind = mime_data->hasFormat(ui::kMediaKindMimeType)
            ? QString::fromUtf8(mime_data->data(ui::kMediaKindMimeType))
            : QStringLiteral("video");
        const bool audio_drop = media_kind == QStringLiteral("audio");
        if (empty_group.has_value()) {
            const auto target_kind = audio_drop ? TrackKind::Audio : TrackKind::Video;
            if (target_kind != *empty_group) return false;
        } else {
            const auto duration = mediaDropDuration(mime_data);
            target_frame = snapPlacement(*track, *frame, duration).start_frame;
            const bool target_is_audio = tracks_[*track].kind == TrackKind::Audio;
            if ((target_is_audio && !audio_drop) ||
                (target_is_audio && audio_drop &&
                 placementOverlaps(*track, target_frame, duration))) {
                return false;
            }
        }
    }
    if (empty_group.has_value() && is_effect_drop) return false;
    if (is_effect_drop) {
        const auto effect_id = QString::fromUtf8(
            mime_data->data(ui::kEffectIdMimeType));
        const bool is_cross_dissolve = effect_id ==
            QStringLiteral("transitions.cross_dissolve");
        const bool is_fade_to_black = effect_id ==
            QStringLiteral("transitions.fade_to_black");
        if (is_cross_dissolve || is_fade_to_black) {
            const auto indexes = transitionClipIndexesAt(
                position.x(), position.y());
            if (!indexes.has_value()) return false;
            clearDropHover();
            const auto& track_value = tracks_[*track];
            emit transitionAddRequested(
                track_value.track_id,
                track_value.clips[indexes->first].clip_id,
                track_value.clips[indexes->second].clip_id,
                is_cross_dissolve ? 0 : 1);
            return true;
        }
        const auto effect_utf8 = effect_id.toUtf8();
        if (creative_suite::effects::findDefinition(
                std::string_view(effect_utf8.constData(),
                                 static_cast<std::size_t>(effect_utf8.size()))) != nullptr) {
            if (tracks_[*track].kind != TrackKind::Video) return false;
            const auto clip = clipAt(position.x(), position.y());
            if (!clip.has_value()) return false;
            const auto kind = tracks_[*track].clips[clip->clip_index].kind;
            if (kind != ClipKind::Video && kind != ClipKind::Image) return false;
        }
    }
    clearDropHover();
    if (is_media_drop) {
        const auto path = QString::fromUtf8(
            mime_data->data(ui::kMediaPathMimeType));
        if (path.isEmpty()) return false;
        if (empty_group.has_value()) {
            emit mediaGroupDropRequested(path, *empty_group, target_frame);
        } else {
            emit mediaDropRequested(
                path,
                tracks_[*track].track_id,
                target_frame);
        }
    } else {
        const auto effect_id = QString::fromUtf8(
            mime_data->data(ui::kEffectIdMimeType));
        if (effect_id.isEmpty()) return false;
        if (!track.has_value()) return false;
        emit effectDropRequested(effect_id, tracks_[*track].track_id, *frame);
    }
    return true;
}

void TimelineWidget::showTransitionMenu(
    const QPoint& position,
    const QPoint& global_position) {
    const auto indexes = transitionClipIndexesAt(position.x(), position.y());
    const auto track_index = trackAt(position.y());
    if (!indexes.has_value() || !track_index.has_value()) return;

    selected_transition_ = SelectedTransition{
        *track_index, indexes->first, indexes->second};
    const auto& track = tracks_[*track_index];
    const auto& from = track.clips[indexes->first];
    const auto& to = track.clips[indexes->second];
    emit transitionSelected(track.track_id, from.clip_id, to.clip_id);
    const auto* existing = [&]() -> const TimelineTransition* {
        for (const auto& transition : track.transitions) {
            if (transition.from_clip_id == from.clip_id &&
                transition.to_clip_id == to.clip_id) {
                return &transition;
            }
        }
        return nullptr;
    }();

    QMenu menu(this);
    QAction* dissolve = nullptr;
    QAction* fade = nullptr;
    QAction* crossfade = nullptr;
    if (track.kind == TrackKind::Audio) {
        crossfade = menu.addAction("Add Audio Crossfade");
        crossfade->setEnabled(existing == nullptr &&
            !from.linked_clip_id.has_value() && !to.linked_clip_id.has_value());
    } else {
        dissolve = menu.addAction("Add Cross Dissolve");
        fade = menu.addAction("Add Fade to Black");
        dissolve->setEnabled(existing == nullptr);
        fade->setEnabled(existing == nullptr);
    }
    menu.addSeparator();
    auto* remove = menu.addAction("Remove Transition");
    remove->setEnabled(existing != nullptr);
    const auto* chosen = menu.exec(global_position);
    if (chosen == dissolve) {
        emit transitionAddRequested(track.track_id, from.clip_id, to.clip_id, 0);
    } else if (chosen == fade) {
        emit transitionAddRequested(track.track_id, from.clip_id, to.clip_id, 1);
    } else if (chosen == crossfade) {
        emit transitionAddRequested(track.track_id, from.clip_id, to.clip_id, 2);
    } else if (chosen == remove) {
        emit transitionRemoveRequested(track.track_id, from.clip_id, to.clip_id);
    }
    update();
}

void TimelineWidget::showVisualClipMenu(
    const ClipLocation& location,
    const QPoint& global_position) {
    if (location.track_index >= tracks_.size() ||
        location.clip_index >= tracks_[location.track_index].clips.size()) return;
    const auto& clip = tracks_[location.track_index].clips[location.clip_index];
    if (clip.kind != ClipKind::Video && clip.kind != ClipKind::Image) return;
    const auto clip_id = clip.clip_id;
    const auto clip_kind = clip.kind;
    const bool has_linked_audio = clip.linked_clip_id.has_value();
    emitSelected(location);
    QMenu menu(this);
    auto* open_fusion = menu.addAction(QStringLiteral("Open in Fusion"));
    connect(open_fusion, &QAction::triggered, this, [this, clip_id]() {
        emit openFusionClipRequested(clip_id);
    });
    if (clip_kind == ClipKind::Image) {
        auto* edit = menu.addAction(QStringLiteral("Edit Clip Image in Image Editor"));
        connect(edit, &QAction::triggered, this, [this, clip_id]() {
            emit editImageClipRequested(clip_id);
        });
    }
    if (has_linked_audio) {
        auto* unlink = menu.addAction(QStringLiteral("Unlink Audio"));
        connect(unlink, &QAction::triggered, this, [this, clip_id]() {
            emit audioUnlinkRequested(clip_id);
        });
    }
    menu.exec(global_position);
}

void TimelineWidget::showAudioLinkMenu(
    const ClipLocation& location,
    const QPoint& global_position) {
    if (location.track_index >= tracks_.size() ||
        location.clip_index >= tracks_[location.track_index].clips.size()) return;
    const auto& clip = tracks_[location.track_index].clips[location.clip_index];
    if (!clip.linked_clip_id.has_value()) return;
    const auto clip_id = clip.clip_id;
    emitSelected(location);
    QMenu menu(this);
    auto* unlink = menu.addAction(QStringLiteral("Unlink Audio"));
    connect(unlink, &QAction::triggered, this, [this, clip_id]() {
        emit audioUnlinkRequested(clip_id);
    });
    menu.exec(global_position);
}

std::optional<std::size_t> TimelineWidget::audioGainKeyframeAt(
    const ClipLocation& location,
    const QPointF& position) const noexcept {
    if (location.track_index >= tracks_.size() ||
        location.clip_index >= tracks_[location.track_index].clips.size()) {
        return std::nullopt;
    }
    const auto& clip = tracks_[location.track_index].clips[location.clip_index];
    if (clip.kind != ClipKind::Audio || clip.audio_gain_keyframes.empty()) {
        return std::nullopt;
    }
    const auto rect = clipRect(location).adjusted(1.0, 6.0, -1.0, -6.0);
    const auto duration = std::max<std::int64_t>(1, clip.timeline_duration_frames);
    double nearest_distance = 8.0 * 8.0;
    std::optional<std::size_t> nearest;
    for (std::size_t index = 0; index < clip.audio_gain_keyframes.size(); ++index) {
        const auto& keyframe = clip.audio_gain_keyframes[index];
        const auto x = rect.left() + rect.width() *
            std::clamp(static_cast<double>(keyframe.frame) / duration, 0.0, 1.0);
        const auto y = rect.bottom() - rect.height() *
            std::clamp(keyframe.gain / 2.0, 0.0, 1.0);
        const auto dx = position.x() - x;
        const auto dy = position.y() - y;
        const auto distance = dx * dx + dy * dy;
        if (distance <= nearest_distance) {
            nearest_distance = distance;
            nearest = index;
        }
    }
    return nearest;
}

void TimelineWidget::showAudioGainKeyframeMenu(
    const ClipLocation& location,
    const QPoint& position,
    const QPoint& global_position) {
    if (location.track_index >= tracks_.size() ||
        location.clip_index >= tracks_[location.track_index].clips.size()) return;
    const auto& clip = tracks_[location.track_index].clips[location.clip_index];
    if (clip.kind != ClipKind::Audio) return;
    const auto keyframe_index = audioGainKeyframeAt(location, position);
    if (!keyframe_index.has_value()) return;
    const auto keyframe = clip.audio_gain_keyframes[*keyframe_index];
    const auto duration = clip.timeline_duration_frames;
    const auto clip_id = clip.clip_id;
    const auto original_points = clip.audio_gain_keyframes;
    emitSelected(location);
    QMenu menu(this);
    QAction* edit_action = nullptr;
    const bool edge = keyframe.frame == 0 || keyframe.frame == duration;
    edit_action = menu.addAction(edge
        ? QStringLiteral("Reset Edge Point to 100%")
        : QStringLiteral("Remove Volume Point"));
    if (menu.exec(global_position) != edit_action) return;
    auto points = original_points;
    if (edge) {
        const auto found = std::find_if(points.begin(), points.end(),
            [keyframe](const AudioGainKeyframe& point) {
                return point.frame == keyframe.frame;
            });
        if (found != points.end()) found->gain = 1.0;
    } else if (*keyframe_index < points.size()) {
        points.erase(points.begin() + static_cast<std::ptrdiff_t>(*keyframe_index));
    }
    emit audioGainEnvelopeEditStarted();
    emit audioGainEnvelopeChanged(clip_id, points);
    emit audioGainEnvelopeEditFinished();
}

void TimelineWidget::contextMenuEvent(QContextMenuEvent* event) {
    if (read_only_) {
        event->ignore();
        return;
    }
    if (suppress_next_context_menu_) {
        suppress_next_context_menu_ = false;
        event->accept();
        return;
    }
    const auto indexes = transitionClipIndexesAt(
        event->pos().x(), event->pos().y());
    const auto track_index = trackAt(event->pos().y());
    if (!indexes.has_value() || !track_index.has_value()) {
        const auto location = clipAt(event->pos().x(), event->pos().y());
        if (location.has_value()) {
            const auto& clip = tracks_[location->track_index].clips[location->clip_index];
            if (clip.kind == ClipKind::Video || clip.kind == ClipKind::Image) {
                showVisualClipMenu(*location, event->globalPos());
                event->accept();
                return;
            }
            if (clip.kind == ClipKind::Audio && clip.linked_clip_id.has_value()) {
                showAudioLinkMenu(*location, event->globalPos());
                event->accept();
                return;
            }
        }
        event->ignore(); return;
    }
    showTransitionMenu(event->pos(), event->globalPos());
    event->accept();
}

void TimelineWidget::leaveEvent(QEvent* event) {
    if (splitter_hover_active_) {
        splitter_hover_active_ = false;
        update();
    }
    if (!split_drag_active_ && !interaction_controller_.trimGesture().active()) {
        unsetCursor();
    }
    QWidget::leaveEvent(event);
}

void TimelineWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton &&
        trackSplitterRect().contains(event->position())) {
        splitter_hover_active_ = true;
        split_drag_active_ = true;
        setCursor(Qt::SplitVCursor);
        grabMouse();
        update();
        event->accept();
        return;
    }
    if (read_only_) {
        event->ignore();
        return;
    }
    if (event->button() == Qt::RightButton) {
        const auto indexes = transitionClipIndexesAt(
            event->position().x(), event->position().y());
        if (indexes.has_value() && trackAt(event->position().y()).has_value()) {
            suppress_next_context_menu_ = true;
            showTransitionMenu(
                event->position().toPoint(),
                event->globalPosition().toPoint());
            event->accept();
        } else {
            const auto location = clipAt(
                event->position().x(), event->position().y());
            if (volume_mode_ && location.has_value() &&
                tracks_[location->track_index].clips[location->clip_index].kind ==
                    ClipKind::Audio &&
                audioGainKeyframeAt(*location, event->position()).has_value()) {
                suppress_next_context_menu_ = true;
                showAudioGainKeyframeMenu(
                    *location, event->position().toPoint(),
                    event->globalPosition().toPoint());
                event->accept();
            } else if (location.has_value() &&
                (tracks_[location->track_index].clips[location->clip_index].kind ==
                     ClipKind::Video ||
                 tracks_[location->track_index].clips[location->clip_index].kind ==
                     ClipKind::Image)) {
                suppress_next_context_menu_ = true;
                showVisualClipMenu(*location, event->globalPosition().toPoint());
                event->accept();
            } else if (location.has_value() &&
                tracks_[location->track_index].clips[location->clip_index].kind ==
                    ClipKind::Audio &&
                tracks_[location->track_index].clips[location->clip_index]
                    .linked_clip_id.has_value()) {
                suppress_next_context_menu_ = true;
                showAudioLinkMenu(*location, event->globalPosition().toPoint());
                event->accept();
            } else {
                event->ignore();
            }
        }
        return;
    }
    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    if (rulerRect().contains(event->position())) {
        const auto ruler = rulerRect();
        const auto ruler_x = std::clamp(
            event->position().x(), ruler.left(), ruler.right());
        const auto frame = playheadFrameAtRulerX(ruler_x);
        if (!frame.has_value()) {
            event->ignore();
            return;
        }
        interaction_controller_.beginRulerSeek(*frame, ruler_x);
        emit seekStarted();
        grabMouse();
        update();
        emit playheadVisualChanged();
        event->accept();
        return;
    }
    const auto location = clipAt(event->position().x(), event->position().y());
    if (!location.has_value()) {
        if (trackAt(event->position().y()).has_value() &&
            globalFrameAt(event->position().x()).has_value()) {
            active_clip_.reset();
            selected_clip_ids_.clear();
            selected_transition_.reset();
            emit clipSelectionCleared();
            emit transitionSelectionCleared();
            update();
            event->accept();
        } else {
            event->ignore();
        }
        return;
    }
    const bool alt_pressed = event->modifiers().testFlag(Qt::AltModifier);
    if (volume_mode_) {
        const auto& source_clip = tracks_[location->track_index]
            .clips[location->clip_index];
        if (source_clip.kind != ClipKind::Audio) {
            event->ignore();
            return;
        }
        const auto clip = source_clip;
        const auto bounds = clipRect(*location).adjusted(1.0, 6.0, -1.0, -6.0);
        const auto duration = std::max<std::int64_t>(1, clip.timeline_duration_frames);
        const auto frame_from_x = [&]() {
            const auto fraction = bounds.width() <= 0.0 ? 0.0 :
                std::clamp((event->position().x() - bounds.left()) /
                               bounds.width(), 0.0, 1.0);
            return static_cast<std::int64_t>(std::llround(fraction * duration));
        };
        const auto gain_from_y = [&]() {
            if (bounds.height() <= 0.0) return 1.0;
            return std::clamp(2.0 * (bounds.bottom() - event->position().y()) /
                                  bounds.height(), 0.0, 2.0);
        };
        emitSelected(*location);
        auto points = clip.audio_gain_keyframes;
        auto selected_point = audioGainKeyframeAt(*location, event->position());
        if (!selected_point.has_value()) {
            auto frame = frame_from_x();
            if (points.empty()) {
                points = {{0, 1.0}, {duration, 1.0}};
                if (duration > 1) {
                    frame = std::clamp<std::int64_t>(frame, 1, duration - 1);
                } else {
                    frame = frame <= 0 ? 0 : duration;
                }
            }
            const auto gain = gain_from_y();
            const auto found = std::lower_bound(points.begin(), points.end(), frame,
                [](const AudioGainKeyframe& point, std::int64_t target) {
                    return point.frame < target;
                });
            if (found != points.end() && found->frame == frame) {
                selected_point = static_cast<std::size_t>(
                    std::distance(points.begin(), found));
                found->gain = gain;
            } else {
                selected_point = static_cast<std::size_t>(
                    std::distance(points.begin(), found));
                points.insert(found, AudioGainKeyframe{frame, gain});
            }
        }
        if (!selected_point.has_value() || *selected_point >= points.size()) {
            event->ignore();
            return;
        }
        emit audioGainEnvelopeEditStarted();
        audio_gain_envelope_drag_ = AudioGainEnvelopeDrag{
            clip.clip_id, *selected_point, std::move(points), false};
        if (audio_gain_envelope_drag_->keyframes != clip.audio_gain_keyframes) {
            audio_gain_envelope_drag_->changed = true;
            emit audioGainEnvelopeChanged(
                clip.clip_id, audio_gain_envelope_drag_->keyframes);
        }
        grabMouse();
        update();
        event->accept();
        return;
    }
    if (razor_mode_) {
        const auto frame = localFrameAt(*location, event->position().x());
        if (!frame.has_value()) {
            event->ignore();
            return;
        }
        emitSelected(*location);
        const auto& clip = tracks_[location->track_index].clips[location->clip_index];
        interaction_controller_.beginSplit(
            clip.clip_id, *frame, event->position());
        grabMouse();
        event->accept();
        return;
    }
    if (event->modifiers().testFlag(Qt::ControlModifier)) {
        const auto& clicked_clip = tracks_[location->track_index]
            .clips[location->clip_index];
        const auto existing = std::find(
            selected_clip_ids_.begin(), selected_clip_ids_.end(), clicked_clip.clip_id);
        if (existing == selected_clip_ids_.end()) {
            selected_clip_ids_.push_back(clicked_clip.clip_id);
            emitSelected(*location, false);
        } else {
            const bool was_primary = active_clip_.has_value() &&
                tracks_[active_clip_->track_index]
                    .clips[active_clip_->clip_index].clip_id == clicked_clip.clip_id;
            selected_clip_ids_.erase(existing);
            if (selected_clip_ids_.empty()) {
                active_clip_.reset();
                selected_transition_.reset();
                emit clipSelectionCleared();
                emit transitionSelectionCleared();
            } else if (was_primary) {
                const auto next_primary = locationForClip(
                    tracks_, selected_clip_ids_.back());
                if (next_primary.has_value()) emitSelected(*next_primary, false);
            }
            emit trackHeaderVisualsChanged();
        }
        update();
        event->accept();
        return;
    }
    const auto edge = trimEdgeAt(*location, event->position().x());
    const auto transition_indexes = transitionClipIndexesAt(
        event->position().x(), event->position().y());
    if (transition_indexes.has_value()) {
        collapseSelectionTo(active_clip_);
    } else {
        collapseSelectionTo(location);
    }
    const auto trim_mode = edge.has_value()
        ? trimEditModeAt(*location, *edge, event->position().x())
        : ClipEdgeEditMode::Individual;
    if (transition_indexes.has_value() && edge.has_value()) {
        const auto& clip = tracks_[location->track_index].clips[location->clip_index];
        const auto scale_duration = displayDuration();
        const auto original_boundary = *edge == ClipEdge::Left
            ? clip.timeline_start_frame
            : clip.timeline_start_frame + clip.timeline_duration_frames;
        interaction_controller_.trimGesture().begin(
            tracks_, *location, *edge, trim_mode, original_boundary,
            scale_duration, trimPointer(event->position()), *transition_indexes,
            frame_rate_);
        grabMouse();
        setCursor(Qt::SizeHorCursor);
        event->accept();
        update();
        return;
    }
    if (transition_indexes.has_value()) {
        selected_transition_ = SelectedTransition{
            trackAt(event->position().y()).value(),
            transition_indexes->first,
            transition_indexes->second};
        const auto& track = tracks_[selected_transition_->track_index];
        emit transitionSelected(
            track.track_id,
            track.clips[transition_indexes->first].clip_id,
            track.clips[transition_indexes->second].clip_id);
        event->accept();
        update();
        return;
    }
    if (edge.has_value()) {
        const auto scale_duration = displayDuration();
        const auto& clip = tracks_[location->track_index].clips[location->clip_index];
        const auto original_boundary = *edge == ClipEdge::Left
            ? clip.timeline_start_frame
            : clip.timeline_start_frame + clip.timeline_duration_frames;
        emitSelected(*location);
        emit trimStarted();
        interaction_controller_.trimGesture().begin(
            tracks_, *location, *edge, trim_mode, original_boundary,
            scale_duration, trimPointer(event->position()), std::nullopt,
            frame_rate_);
        grabMouse();
        setCursor(Qt::SizeHorCursor);
        event->accept();
        return;
    }
    if ((alt_pressed == move_requires_alt_) &&
        !transitionClipIndexesAt(event->position().x(), event->position().y()).has_value() &&
        !trimEdgeAt(*location, event->position().x()).has_value()) {
        if (!active_clip_.has_value() || *active_clip_ != *location) {
            emitSelected(*location);
        }
        const auto& clip = tracks_[location->track_index].clips[location->clip_index];
        interaction_controller_.beginMove(
            clip.clip_id, tracks_[location->track_index].track_id,
            clip.timeline_start_frame, event->position());
        move_preview_position_ = event->position();
        grabMouse();
        event->accept();
        update();
        return;
    }
    if (!active_clip_.has_value() || *active_clip_ != *location) {
        emitSelected(*location);
        event->accept();
        return;
    }
    const auto frame = localFrameAt(*location, event->position().x());
    if (!frame.has_value()) {
        event->ignore();
        return;
    }
    const auto& clip = tracks_[location->track_index].clips[location->clip_index];
    interaction_controller_.beginSeek(
        clip.clip_id, clip.timeline_start_frame, *frame, event->position());
    grabMouse();
    event->accept();
    update();
}

void TimelineWidget::mouseMoveEvent(QMouseEvent* event) {
    if (split_drag_active_) {
        constexpr double bottom_margin = 12.0;
        const auto available_height = std::max(
            1.0,
            static_cast<double>(height()) - TimelineGeometry::top_margin -
                bottom_margin - track_group_splitter_height);
        setTrackGroupSplitRatio(
            (event->position().y() - TimelineGeometry::top_margin -
             track_group_splitter_height / 2.0) / available_height);
        event->accept();
        return;
    }
    if (trackSplitterRect().contains(event->position())) {
        updateTrackSplitterHoverState(event->position());
        event->accept();
        return;
    }
    updateTrackSplitterHoverState(event->position());
    if (read_only_) {
        event->ignore();
        return;
    }
    if (audio_gain_envelope_drag_.has_value()) {
        const auto location = locationForClip(
            tracks_, audio_gain_envelope_drag_->clip_id);
        if (!location.has_value()) {
            event->accept();
            return;
        }
        const auto rect = clipRect(*location).adjusted(1.0, 6.0, -1.0, -6.0);
        auto& drag = *audio_gain_envelope_drag_;
        if (drag.keyframe_index >= drag.keyframes.size()) {
            event->accept();
            return;
        }
        const auto& clip = tracks_[location->track_index].clips[location->clip_index];
        const auto duration = std::max<std::int64_t>(1, clip.timeline_duration_frames);
        auto frame = static_cast<std::int64_t>(std::llround(
            std::clamp((event->position().x() - rect.left()) /
                           std::max(1.0, rect.width()), 0.0, 1.0) * duration));
        const auto gain = rect.height() <= 0.0 ? 1.0 : std::clamp(
            2.0 * (rect.bottom() - event->position().y()) / rect.height(), 0.0, 2.0);
        auto& keyframe = drag.keyframes[drag.keyframe_index];
        if (drag.keyframe_index == 0) {
            frame = 0;
        } else if (drag.keyframe_index + 1 == drag.keyframes.size()) {
            frame = duration;
        } else {
            const auto minimum_frame =
                drag.keyframes[drag.keyframe_index - 1].frame + 1;
            const auto maximum_frame =
                drag.keyframes[drag.keyframe_index + 1].frame - 1;
            frame = minimum_frame <= maximum_frame
                ? std::clamp(frame, minimum_frame, maximum_frame)
                : keyframe.frame;
        }
        if (keyframe.frame != frame || std::abs(keyframe.gain - gain) > 1e-9) {
            keyframe.frame = frame;
            keyframe.gain = gain;
            drag.changed = true;
            emit audioGainEnvelopeChanged(drag.clip_id, drag.keyframes);
            update();
        }
        event->accept();
        return;
    }
    if (interaction_controller_.rulerSeekPending()) {
        const auto ruler = rulerRect();
        const auto ruler_x = std::clamp(
            event->position().x(), ruler.left(), ruler.right());
        if (const auto frame = playheadFrameAtRulerX(ruler_x);
            frame.has_value()) {
            interaction_controller_.updateRulerSeek(*frame, ruler_x);
            update();
            emit playheadVisualChanged();
        }
        event->accept();
        return;
    }
    if (interaction_controller_.movePending()) {
        if (!interaction_controller_.updateMoveActivation(event->position())) {
            event->accept();
            return;
        }
        emit trimStarted();
    }
    if (interaction_controller_.moveActive()) {
        move_preview_position_ = event->position();
        const auto move_target_index = trackAt(event->position().y());
        std::optional<TimelineMoveTarget> target;
        if (move_target_index.has_value()) {
            const auto raw_frame = globalFrameAt(event->position().x());
            if (raw_frame.has_value()) {
                const auto source_location = locationForClip(
                    tracks_, interaction_controller_.movingClipId());
                if (!source_location.has_value()) {
                    interaction_controller_.setMoveTarget(std::nullopt);
                    update();
                    event->accept();
                    return;
                }
                const auto& clip = tracks_[source_location->track_index]
                    .clips[source_location->clip_index];
                const auto snapped = snapPlacement(
                    *move_target_index, *raw_frame, clip.timeline_duration_frames,
                    source_location);
                target = TimelineMoveTarget{
                    tracks_[*move_target_index].track_id,
                    snapped.start_frame, snapped.guide_frame};
            } else {
                target = TimelineMoveTarget{
                    tracks_[*move_target_index].track_id, 0, std::nullopt};
            }
        }
        interaction_controller_.setMoveTarget(target);
        update();
        event->accept();
        return;
    }
    if (interaction_controller_.trimGesture().active()) {
        const auto movement = interaction_controller_.trimGesture().move(
            tracks_, trimBoundaryAt(event->position().x()),
            trimPointer(event->position()));
        if (movement.started) {
            const auto active_gesture = interaction_controller_.trimGesture();
            emitSelected(active_gesture.location());
            emit trimStarted();
            // Selection can synchronously refresh the widget's tracks and
            // cancel the gesture. The old path resumed this trim afterward.
            interaction_controller_.trimGesture() = active_gesture;
            grabMouse();
            setCursor(Qt::SizeHorCursor);
        }
        if (movement.repaint) update();
        event->accept();
        return;
    }
    if (interaction_controller_.splitPending()) {
        interaction_controller_.updateSplit(event->position());
        update();
        event->accept();
        return;
    }
    if (interaction_controller_.seekPending()) {
        std::optional<std::int64_t> local_frame;
        if (const auto location = locationForClip(
                tracks_, interaction_controller_.seekClipId());
            location.has_value()) {
            local_frame = localFrameAt(*location, event->position().x());
        }
        const auto started = interaction_controller_.updateSeek(
            event->position(), local_frame);
        if (started) {
            emit seekStarted();
        } else {
            event->accept();
            return;
        }
    }
    if (interaction_controller_.seekDragging()) {
        std::optional<std::int64_t> local_frame;
        if (const auto location = locationForClip(
                tracks_, interaction_controller_.seekClipId());
            location.has_value()) {
            local_frame = localFrameAt(*location, event->position().x());
        }
        static_cast<void>(interaction_controller_.updateSeek(
            event->position(), local_frame));
        update();
        emit playheadVisualChanged();
        event->accept();
    } else {
        updateTrimHoverCursor(event->position());
        event->ignore();
    }
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (split_drag_active_ && event->button() == Qt::LeftButton) {
        split_drag_active_ = false;
        releaseMouse();
        updateTrackSplitterHoverState(event->position());
        update();
        event->accept();
        return;
    }
    if (read_only_) {
        event->ignore();
        return;
    }
    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    if (audio_gain_envelope_drag_.has_value()) {
        audio_gain_envelope_drag_.reset();
        releaseMouse();
        emit audioGainEnvelopeEditFinished();
        update();
        event->accept();
        return;
    }
    if (interaction_controller_.rulerSeekPending()) {
        const auto frame = interaction_controller_.finishRulerSeek();
        releaseMouse();
        if (frame.has_value()) emit seekRequested(*frame);
        update();
        emit playheadVisualChanged();
        event->accept();
        return;
    }
    if (interaction_controller_.moveActive() || interaction_controller_.movePending()) {
        const auto request = interaction_controller_.finishMove();
        clearDragPreview();
        releaseMouse();
        if (request.has_value()) {
            emit clipMoveRequested(
                request->clip_id, request->target_track_id,
                request->timeline_start_frame);
        }
        updateTrimHoverCursor(event->position());
        update();
        event->accept();
        return;
    }
    if (interaction_controller_.trimGesture().active()) {
        const auto result = interaction_controller_.trimGesture().finish(
            tracks_, trimBoundaryAt(event->position().x()),
            trimPointer(event->position()));
        unsetCursor();
        releaseMouse();
        if (result.kind == TrimGestureFinish::Kind::SelectTransition &&
            result.transition_pair.has_value()) {
            const auto& pair = *result.transition_pair;
            selected_transition_ = SelectedTransition{
                result.location.track_index, pair.first, pair.second};
            const auto& track = tracks_[result.location.track_index];
            emit transitionSelected(
                track.track_id,
                track.clips[pair.first].clip_id,
                track.clips[pair.second].clip_id);
        } else if (result.kind == TrimGestureFinish::Kind::RequestTrim) {
            const auto clip_id = tracks_[result.location.track_index]
                .clips[result.location.clip_index].clip_id;
            emit clipEdgeTrimRequested(
                clip_id,
                static_cast<qint64>(result.edge),
                result.boundary_frame,
                static_cast<qint64>(result.mode));
        }
        updateTrimHoverCursor(event->position());
        update();
        event->accept();
        return;
    }
    if (interaction_controller_.splitPending()) {
        const auto request = interaction_controller_.finishSplit();
        releaseMouse();
        if (request.has_value()) {
            emit clipSplitRequested(request->clip_id, request->local_frame);
        }
        update();
        event->accept();
        return;
    }
    if (interaction_controller_.seekDragging()) {
        const auto frame = interaction_controller_.finishSeek();
        releaseMouse();
        if (frame.has_value()) emit seekRequested(*frame);
        updateTrimHoverCursor(event->position());
        update();
        emit playheadVisualChanged();
        event->accept();
        return;
    }
    if (interaction_controller_.seekPending()) {
        static_cast<void>(interaction_controller_.finishSeek());
        releaseMouse();
        updateTrimHoverCursor(event->position());
        update();
        event->accept();
        return;
    }
    event->ignore();
}

void TimelineWidget::wheelEvent(QWheelEvent* event) {
    if (event == nullptr) return;
    if (handleWheel(event->position(), event->pixelDelta(),
                    event->angleDelta(), event->modifiers())) {
        event->accept();
    } else {
        event->ignore();
    }
}

bool TimelineWidget::handleWheel(
    QPointF position,
    QPoint pixel_delta,
    QPoint angle_delta,
    Qt::KeyboardModifiers modifiers) {
    if (read_only_ && (modifiers.testFlag(Qt::ControlModifier) ||
                       modifiers.testFlag(Qt::ShiftModifier))) {
        return true;
    }
    if (modifiers.testFlag(Qt::ControlModifier)) {
        const auto vertical_delta = angle_delta.y() != 0
            ? angle_delta.y() : pixel_delta.y();
        if (vertical_delta == 0) return false;
        const auto next_factor = nextZoomFactor(vertical_delta > 0 ? 1 : -1);
        if (std::abs(next_factor - zoom_factor_) >= 0.000001) {
            emit zoomRequested(next_factor);
        }
        return true;
    }
    if (modifiers.testFlag(Qt::ShiftModifier)) {
        const auto height_delta = pixel_delta.y() != 0
            ? static_cast<double>(pixel_delta.y())
            : static_cast<double>(angle_delta.y()) / 8.0;
        if (std::abs(height_delta) < 0.000001) return false;
        if (track_row_height_adjustment_mode_ ==
            TrackRowHeightAdjustmentMode::Together) {
            setTrackRowHeights(
                video_track_row_height_ + height_delta,
                audio_track_row_height_ + height_delta);
        } else if (const auto group = trackGroupAt(position.y());
                   group.has_value()) {
            setTrackRowHeight(
                *group, trackRowHeight(*group) + height_delta);
        }
        return true;
    }

    const auto group = trackGroupAt(position.y());
    if (!group.has_value()) return false;
    double scroll_delta = static_cast<double>(pixel_delta.y());
    if (std::abs(scroll_delta) < 0.000001) {
        const auto steps = static_cast<double>(angle_delta.y()) / 120.0;
        if (std::abs(steps) < 0.000001) return false;
        scroll_delta = steps * 3.0 *
            (trackRowHeight(*group) + TimelineGeometry::row_gap);
    }
    setTrackScrollOffset(*group, trackScrollOffset(*group) -
        static_cast<int>(std::lround(scroll_delta)));
    return true;
}

} // namespace timeline
