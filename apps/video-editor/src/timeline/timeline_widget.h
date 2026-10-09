#pragma once

#include "media/audio_waveform.h"
#include "timeline_model.h"
#include "timeline_geometry.h"
#include "timeline_drop_validation.h"
#include "timeline_interaction_painter.h"
#include "timeline_interaction_controller.h"
#include "timeline_layout.h"
#include "timeline_row_height_mode.h"
#include "timeline_zoom.h"

#include <QString>
#include <QStringList>
#include <QPointF>
#include <QRectF>
#include <QWidget>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

class QDragEnterEvent;
class QDragLeaveEvent;
class QDragMoveEvent;
class QDropEvent;
class QMimeData;
class QMouseEvent;
class QPaintEvent;
class QPainter;
class QContextMenuEvent;
class QResizeEvent;
class QWheelEvent;
class QPoint;
class QEvent;
class QObject;

namespace timeline {

class TimelineTrackHeaderOverlay;

class TimelineWidget final : public QWidget {
    Q_OBJECT

public:
    explicit TimelineWidget(QWidget* parent = nullptr);

    void setTracks(const std::vector<TimelineTrack>& tracks);
    void clearAudioWaveforms();
    void setAudioWaveform(
        const std::filesystem::path& source_path,
        const std::shared_ptr<const media::AudioWaveform>& waveform);
    void setStereoWaveformDisplayEnabled(bool enabled) noexcept;
    [[nodiscard]] bool stereoWaveformDisplayEnabled() const noexcept;
    void setFrameRate(FrameRate frame_rate) noexcept;
    void setClips(const std::vector<TimelineClip>& clips);
    void clearClips();
    void setActiveClip(std::optional<ClipLocation> location);
    void setSelectedClipIds(const std::vector<ClipId>& clip_ids);
    [[nodiscard]] std::vector<ClipId> selectedClipIds() const;
    void setActiveClipIndex(std::optional<std::size_t> clip_index);
    void setPlayheadFrame(std::int64_t frame_index);
    [[nodiscard]] std::int64_t playheadFrame() const noexcept;
    [[nodiscard]] std::int64_t displayedPlayheadFrame() const noexcept;
    [[nodiscard]] QString playheadTimecode() const;
    void setRazorMode(bool enabled);
    [[nodiscard]] bool razorMode() const noexcept;
    void setVolumeMode(bool enabled);
    [[nodiscard]] bool volumeMode() const noexcept;
    void setMoveRequiresAlt(bool enabled);
    [[nodiscard]] bool moveRequiresAlt() const noexcept;
    void setReadOnly(bool read_only);
    [[nodiscard]] bool isReadOnly() const noexcept;
    void setSnapEnabled(bool enabled);
    [[nodiscard]] bool snapEnabled() const noexcept;
    void setTrackScrollOffset(TrackKind kind, int offset);
    [[nodiscard]] int trackScrollOffset(TrackKind kind) const noexcept;
    [[nodiscard]] int trackScrollMaximum(TrackKind kind) const noexcept;
    [[nodiscard]] QRectF trackGroupViewportRect(TrackKind kind) const noexcept;
    [[nodiscard]] QRectF trackBounds(std::size_t track_index) const noexcept;
    [[nodiscard]] QRectF clipBounds(const ClipLocation& location) const noexcept;
    [[nodiscard]] QRectF trackSplitterRect() const noexcept;
    [[nodiscard]] double trackGroupSplitRatio() const noexcept;
    void setTrackGroupSplitRatio(double ratio);
    // Rendering bridge used by the fixed header overlay hosted by the
    // Timeline scroll area's viewport.
    [[nodiscard]] int trackHeaderOverlayWidth() const noexcept;
    void paintTrackHeaderOverlay(QPainter& painter) const;
    void setTimelineViewportWidth(int width);
    [[nodiscard]] double zoomFactor() const noexcept;
    void setZoomFactor(double factor);
    [[nodiscard]] double trackRowHeight() const noexcept;
    [[nodiscard]] double trackRowHeight(TrackKind kind) const noexcept;
    void setTrackRowHeight(double height);
    void setTrackRowHeight(TrackKind kind, double height);
    void setTrackRowHeights(double video_height, double audio_height);
    [[nodiscard]] TrackRowHeightAdjustmentMode
    trackRowHeightAdjustmentMode() const noexcept;
    void setTrackRowHeightAdjustmentMode(
        TrackRowHeightAdjustmentMode mode);
    [[nodiscard]] double nextZoomFactor(int direction) const noexcept;
    [[nodiscard]] bool canZoomIn() const noexcept;
    [[nodiscard]] bool canZoomOut() const noexcept;
    [[nodiscard]] std::optional<std::int64_t> frameAtContentX(double x) const noexcept;
    [[nodiscard]] double contentXForFrame(std::int64_t frame) const noexcept;
    [[nodiscard]] static QString formatTimecode(
        std::int64_t frame,
        double frame_rate);

signals:
    void clipSelected(timeline::TrackId track_id, timeline::ClipId clip_id);
    void openFusionClipRequested(timeline::ClipId clip_id);
    void editImageClipRequested(timeline::ClipId clip_id);
    void openMotionClipRequested(timeline::ClipId clip_id);
    void clipSelectionCleared();
    void clipMoveRequested(
        timeline::ClipId clip_id,
        timeline::TrackId target_track_id,
        qint64 timeline_start_frame);
    void clipSplitRequested(timeline::ClipId clip_id, qint64 local_frame);
    void clipEdgeTrimRequested(
        timeline::ClipId clip_id,
        qint64 edge,
        qint64 boundary_frame,
        qint64 mode);
    void mediaDropRequested(
        const QString& source_path,
        timeline::TrackId track_id,
        qint64 timeline_frame);
    void externalFilesDropRequested(
        const QStringList& source_paths,
        timeline::TrackId track_id,
        qint64 timeline_frame);
    void effectDropRequested(
        const QString& effect_id,
        timeline::TrackId track_id,
        qint64 timeline_frame);
    void transitionSelected(
        timeline::TrackId track_id,
        timeline::ClipId from_clip_id,
        timeline::ClipId to_clip_id);
    void transitionSelectionCleared();
    void transitionAddRequested(
        timeline::TrackId track_id,
        timeline::ClipId from_clip_id,
        timeline::ClipId to_clip_id,
        qint64 kind);
    void transitionRemoveRequested(
        timeline::TrackId track_id,
        timeline::ClipId from_clip_id,
        timeline::ClipId to_clip_id);
    void audioUnlinkRequested(timeline::ClipId clip_id);
    void audioGainEnvelopeEditStarted();
    void audioGainEnvelopeChanged(
        timeline::ClipId clip_id,
        const std::vector<timeline::AudioGainKeyframe>& keyframes);
    void audioGainEnvelopeEditFinished();
    void trimStarted();
    void seekStarted();
    void seekRequested(qint64 frame_index);
    void zoomRequested(double factor);
    void zoomChanged(double factor);
    void trackRowHeightsChanged(double video_height, double audio_height);
    void snapEnabledChanged(bool enabled);
    void trackHeaderVisualsChanged();
    void trackScrollMetricsChanged();
    void trackGroupSplitRatioChanged(double ratio);
    void playheadVisualChanged();
    void mediaGroupDropRequested(
        const QString& source_path,
        timeline::TrackKind track_kind,
        qint64 timeline_frame);
    void externalFilesGroupDropRequested(
        const QStringList& source_paths,
        timeline::TrackKind track_kind,
        qint64 timeline_frame);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    struct TrackScrollAnchor final {
        TrackId track_id = 0;
        double row_edge_offset = 0.0;
        bool bottom_edge = false;
    };

    [[nodiscard]] QRectF trackRect(std::size_t index) const noexcept;
    [[nodiscard]] TimelineGeometry geometry() const noexcept;
    [[nodiscard]] TimelineTrackViewLayout trackViewLayout() const noexcept;
    [[nodiscard]] std::optional<TrackScrollAnchor> captureTrackScrollAnchor(
        TrackKind kind) const noexcept;
    void restoreTrackScrollAnchor(
        TrackKind kind,
        const std::optional<TrackScrollAnchor>& anchor) noexcept;
    void updateTrackSplitterHoverState(const QPointF& position);
    [[nodiscard]] QRectF rulerRect() const noexcept;
    [[nodiscard]] QRectF trackContentRect(std::size_t index) const noexcept;
    [[nodiscard]] QRectF clipRect(const ClipLocation& location) const noexcept;
    [[nodiscard]] const TimelineClip& displayedClip(
        const ClipLocation& location) const noexcept;
    void paintTrackHeaderCell(
        QPainter& painter,
        std::size_t track_index,
        const QRectF& row) const;
    [[nodiscard]] double frameRate() const noexcept;
    [[nodiscard]] std::int64_t standardDuration() const noexcept;
    [[nodiscard]] std::int64_t displayDuration() const noexcept;
    [[nodiscard]] std::int64_t totalDuration() const noexcept;
    [[nodiscard]] double pixelsPerFrame() const noexcept;
    void updateVerticalExtent();
    [[nodiscard]] std::optional<TrackKind> emptyTrackGroupAt(double y) const noexcept;
    [[nodiscard]] std::optional<TrackKind> trackGroupAt(double y) const noexcept;
    [[nodiscard]] bool handleWheel(
        QPointF position,
        QPoint pixel_delta,
        QPoint angle_delta,
        Qt::KeyboardModifiers modifiers);
    void updateHorizontalExtent();
    [[nodiscard]] std::optional<std::size_t> trackAt(double y) const noexcept;
    [[nodiscard]] std::optional<ClipLocation> clipAt(double x, double y) const noexcept;
    [[nodiscard]] std::optional<std::int64_t> globalFrameAt(double x) const noexcept;
    [[nodiscard]] std::optional<std::int64_t> playheadFrameAtRulerX(
        double x) const noexcept;
    [[nodiscard]] std::optional<std::int64_t> localFrameAt(const ClipLocation&, double x) const noexcept;
    [[nodiscard]] std::optional<ClipEdge> trimEdgeAt(const ClipLocation&, double x) const noexcept;
    [[nodiscard]] ClipEdgeEditMode trimEditModeAt(
        const ClipLocation&, ClipEdge, double x) const noexcept;
    void updateTrimHoverCursor(const QPointF& position);
    [[nodiscard]] std::optional<std::int64_t> trimBoundaryAt(double x) const noexcept;
    [[nodiscard]] std::optional<std::pair<std::size_t, std::size_t>>
    transitionClipIndexesAt(double x, double y) const noexcept;
    void showTransitionMenu(const QPoint& position, const QPoint& global_position);
    void showVisualClipMenu(const ClipLocation& location, const QPoint& global_position);
    void showAudioLinkMenu(const ClipLocation& location, const QPoint& global_position);
    void showAudioGainKeyframeMenu(
        const ClipLocation& location,
        const QPoint& position,
        const QPoint& global_position);
    [[nodiscard]] std::optional<std::size_t> audioGainKeyframeAt(
        const ClipLocation& location,
        const QPointF& position) const noexcept;
    void emitSelected(const ClipLocation& location, bool collapse_multi_selection = true);
    void collapseSelectionTo(std::optional<ClipLocation> location);
    [[nodiscard]] bool isSupportedDrop(
        const QMimeData* mime_data) const noexcept;
    [[nodiscard]] std::int64_t mediaDropDuration(
        const QMimeData* mime_data) const noexcept;
    [[nodiscard]] QString mediaDropLabel(
        const QMimeData* mime_data) const;
    [[nodiscard]] bool placementOverlaps(
        std::size_t track_index,
        std::int64_t start_frame,
        std::int64_t duration_frames,
        std::optional<ClipLocation> excluded = std::nullopt) const noexcept;
    [[nodiscard]] QRectF previewRect(
        std::size_t track_index,
        std::int64_t start_frame,
        std::int64_t duration_frames) const noexcept;
    [[nodiscard]] timeline::SnapPlacement snapPlacement(
        std::size_t track_index,
        std::int64_t raw_start_frame,
        std::int64_t duration_frames,
        std::optional<ClipLocation> excluded = std::nullopt) const noexcept;
    void clearDragPreview();
    void clearDropHover();
    [[nodiscard]] bool updateDropHover(
        const QMimeData* mime_data,
        const QPointF& position);
    [[nodiscard]] bool processDrop(
        const QMimeData* mime_data,
        const QPointF& position);

    std::vector<TimelineTrack> tracks_;
    std::unordered_map<
        std::filesystem::path,
        std::weak_ptr<const media::AudioWaveform>> audio_waveforms_;
    bool stereo_waveform_display_enabled_ = false;
    int timeline_viewport_width_ = 0;
    double zoom_factor_ = 1.0;
    double video_track_row_height_ = kDefaultTrackRowHeight;
    double audio_track_row_height_ = kDefaultTrackRowHeight;
    TrackRowHeightAdjustmentMode track_row_height_adjustment_mode_ =
        TrackRowHeightAdjustmentMode::Together;
    double track_group_split_ratio_ = 0.5;
    double video_scroll_offset_ = 0.0;
    double audio_scroll_offset_ = 0.0;
    double video_splitter_translation_ = 0.0;
    double audio_splitter_translation_ = 0.0;
    bool split_drag_active_ = false;
    bool splitter_hover_active_ = false;
    bool snap_enabled_ = true;
    std::optional<ClipLocation> active_clip_;
    std::vector<ClipId> selected_clip_ids_;
    std::int64_t playhead_frame_ = 0;
    bool move_requires_alt_ = false;
    bool read_only_ = false;
    FrameRate frame_rate_;
    TimelineInteractionController interaction_controller_;
    bool razor_mode_ = false;
    bool volume_mode_ = false;
    struct AudioGainEnvelopeDrag {
        ClipId clip_id = 0;
        std::size_t keyframe_index = 0;
        std::vector<AudioGainKeyframe> keyframes;
        bool changed = false;
    };
    std::optional<AudioGainEnvelopeDrag> audio_gain_envelope_drag_;
    QPointF move_preview_position_{};
    struct SelectedTransition {
        std::size_t track_index = 0;
        std::size_t from_clip_index = 0;
        std::size_t to_clip_index = 0;

        friend bool operator==(const SelectedTransition&, const SelectedTransition&) = default;
    };
    std::optional<SelectedTransition> selected_transition_;
    bool suppress_next_context_menu_ = false;
};

} // namespace timeline
