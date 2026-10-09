#pragma once

#include "model/composition_document.h"
#include "timeline_types.h"

#include <QElapsedTimer>
#include <QPoint>
#include <QWidget>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <vector>

class QAction;
class QScrollArea;
class QScrollBar;
class QTimer;

namespace motion::ui {

class TimelineControls;
class TimelineLayerTracks;
class TimelineRuler;

class TimelineNavigator final : public QWidget {
    Q_OBJECT

public:
    explicit TimelineNavigator(QWidget* parent = nullptr);

    void setCompositionTiming(model::FrameRate frame_rate);
    void setShortcutActions(QAction* play_pause,
                           QAction* previous_frame,
                           QAction* next_frame,
                           QAction* loop,
                           QAction* zoom_in,
                           QAction* zoom_out);
    void setCurrentFrame(std::int64_t frame);
    void setLayers(const std::vector<model::CompositionLayer>& layers);
    void setSelectedLayerId(model::LayerId id);
    void setLayerExpanded(model::LayerId id, bool expanded);
    void setTransformGroupExpanded(model::LayerId id, bool expanded);
    void setGraphEditorOpen(bool open);
    [[nodiscard]] std::int64_t currentFrame() const noexcept;
    [[nodiscard]] bool isPlaying() const noexcept;
    [[nodiscard]] bool isLoopEnabled() const noexcept;
    [[nodiscard]] std::int64_t visibleEndFrame() const noexcept;
    [[nodiscard]] double zoomFactor() const noexcept;
    [[nodiscard]] int zoomLevelIndex() const noexcept;
    [[nodiscard]] std::int64_t viewStartFrame() const noexcept;
    [[nodiscard]] std::int64_t framesPerView() const noexcept;
    [[nodiscard]] int frameToViewportX(std::int64_t frame) const noexcept;
    [[nodiscard]] std::int64_t frameAtViewportX(int x) const noexcept;

    void setMediaDropHandler(
        std::function<void(const std::filesystem::path&,
                           std::int64_t,
                           model::LayerId)> handler);
    void setLayerSelectedHandler(std::function<void(model::LayerId)> handler);
    void setLayerContextMenuHandler(
        std::function<void(model::LayerId, const QPoint&)> handler);
    void setLayerMoveHandler(
        std::function<void(model::LayerId, std::int64_t)> handler);
    void setLayerResizeHandler(
        std::function<void(model::LayerId, std::int64_t)> handler);
    void setLayerReorderHandler(
        std::function<void(model::LayerId, std::size_t)> handler);
    void setLayerVisibilityHandler(
        std::function<void(model::LayerId, bool)> handler);
    void setLayerRemoveHandler(std::function<void(model::LayerId)> handler);
    void setKeyframeSelectedHandler(
        std::function<void(model::LayerId,
                           creative_suite::animation::TransformProperty,
                           std::int64_t)> handler);
    void setKeyframeMoveHandler(
        std::function<bool(model::LayerId,
                           creative_suite::animation::TransformProperty,
                           std::int64_t,
                           std::int64_t)> handler);
    void setCurveSegmentSelectedHandler(
        std::function<void(model::LayerId,
                           creative_suite::animation::TransformProperty,
                           std::int64_t)> handler);

signals:
    void currentFrameChanged(qint64 frame);
    void graphEditorToggled(bool open);

private:
    void seekToFrame(std::int64_t frame);
    void startPlayback();
    void pausePlayback(bool update_to_clock = true);
    void playbackTick();
    void setPlayheadFrame(std::int64_t frame);
    void ensureFrameInNavigationRange(std::int64_t frame);
    void extendViewByOneHour() noexcept;
    void applyZoomLevel(int index);
    void updateHorizontalScrollBar();
    void setViewStartFrame(std::int64_t frame);
    void updateViewWidgets();
    void ensureCurrentFrameVisible();
    [[nodiscard]] std::int64_t maximumViewStartFrame() const noexcept;
    [[nodiscard]] std::int64_t calculateFramesPerView() const noexcept;
    void updateControls();

    model::FrameRate frame_rate_{};
    std::int64_t visible_end_frame_ = 0;
    std::int64_t current_frame_ = 0;
    std::int64_t view_start_frame_ = 0;
    std::int64_t frames_per_view_ = 1;
    double zoom_factor_ = 1.0;
    int zoom_level_index_ = 3;
    TimelineDisplayMode display_mode_ = TimelineDisplayMode::Time;
    TimelineControls* controls_ = nullptr;
    QAction* play_pause_action_ = nullptr;
    QAction* previous_frame_action_ = nullptr;
    QAction* next_frame_action_ = nullptr;
    QAction* loop_action_ = nullptr;
    QAction* zoom_in_action_ = nullptr;
    QAction* zoom_out_action_ = nullptr;
    TimelineRuler* ruler_ = nullptr;
    TimelineLayerTracks* layer_tracks_ = nullptr;
    QScrollArea* layer_scroll_area_ = nullptr;
    QScrollBar* horizontal_scroll_bar_ = nullptr;
    QTimer* playback_timer_ = nullptr;
    QElapsedTimer playback_clock_;
    std::int64_t playback_start_frame_ = 0;
    std::int64_t playback_end_frame_exclusive_ = 0;
    bool playing_ = false;
    bool loop_enabled_ = false;
};

} // namespace motion::ui
