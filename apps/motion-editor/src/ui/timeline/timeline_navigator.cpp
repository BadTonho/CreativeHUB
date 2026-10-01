#include "timeline_navigator.h"
#include "timeline_navigator_math.h"
#include "timeline_controls.h"
#include "timeline_layer_tracks.h"
#include "timeline_ruler.h"
#include "timeline_view_mapping.h"

#include <QAction>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <utility>

namespace motion::ui {
namespace {

constexpr std::int64_t kMaximumFrame = std::numeric_limits<std::int64_t>::max();
constexpr std::int64_t kSecondsPerHour = 60 * 60;

QString utf8Text(const std::string& value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

QString formatFrameRate(model::FrameRate frame_rate)
{
    return QString::number(frame_rate.asDouble(), 'g', 6);
}

QString formatTimelinePosition(
    std::int64_t frame,
    model::FrameRate frame_rate,
    TimelineDisplayMode display_mode)
{
    if (display_mode == TimelineDisplayMode::Frames) {
        return QString::number(static_cast<qlonglong>(frame));
    }
    return utf8Text(detail::formatElapsedTime(
        frame, frame_rate.numerator, frame_rate.denominator));
}

std::int64_t framesPerHour(model::FrameRate frame_rate) noexcept
{
    const auto numerator = frame_rate.numerator * kSecondsPerHour;
    const auto frame_count = (numerator + frame_rate.denominator - 1) /
        frame_rate.denominator;
    return std::max<std::int64_t>(1, frame_count);
}

std::int64_t initialVisibleEndFrame(model::FrameRate frame_rate) noexcept
{
    return framesPerHour(frame_rate) - 1;
}

std::int64_t roundedFrameClamped(long double value, std::int64_t maximum) noexcept
{
    if (value <= 0.0L) {
        return 0;
    }
    if (value >= static_cast<long double>(maximum)) {
        return maximum;
    }
    const auto rounded = std::round(value);
    if (rounded >= static_cast<long double>(maximum)) {
        return maximum;
    }
    return static_cast<std::int64_t>(rounded);
}

} // namespace

TimelineNavigator::TimelineNavigator(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("motion-timeline"));
    setMinimumHeight(196);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 8);
    layout->setSpacing(4);

    controls_ = new TimelineControls(this);
    layout->addWidget(controls_);

    ruler_ = new TimelineRuler(this);
    ruler_->setHeaderWidth(kTimelineHeaderWidth);
    ruler_->setToolTip(QStringLiteral(
        "At 100%, the initial one-hour range fits the timeline. Use Ctrl+wheel or the zoom "
        "controls to change scale. Drag past the actual range end to extend by one hour per "
        "gesture; scroll to the end first if it is offscreen. This range is not the composition end."));
    layout->addWidget(ruler_);

    layer_scroll_area_ = new QScrollArea(this);
    layer_scroll_area_->setObjectName(QStringLiteral("motion-timeline-layer-scroll"));
    layer_scroll_area_->setWidgetResizable(true);
    layer_scroll_area_->setFrameShape(QFrame::NoFrame);
    layer_scroll_area_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    layer_tracks_ = new TimelineLayerTracks(layer_scroll_area_);
    layer_scroll_area_->setWidget(layer_tracks_);
    layout->addWidget(layer_scroll_area_, 1);

    horizontal_scroll_bar_ = new QScrollBar(Qt::Horizontal, this);
    horizontal_scroll_bar_->setObjectName(QStringLiteral("motion-timeline-horizontal-scroll"));
    horizontal_scroll_bar_->setRange(0, detail::kTimelineScrollResolution);
    horizontal_scroll_bar_->setPageStep(detail::kTimelineScrollResolution);
    layout->addWidget(horizontal_scroll_bar_);

    playback_timer_ = new QTimer(this);
    playback_timer_->setObjectName(QStringLiteral("motion-timeline-playback-timer"));
    playback_timer_->setTimerType(Qt::PreciseTimer);
    connect(playback_timer_, &QTimer::timeout, this, [this] { playbackTick(); });

    connect(controls_->previousFrameButton(), &QPushButton::clicked, this, [this] {
        if (previous_frame_action_ != nullptr) previous_frame_action_->trigger();
        else seekToFrame(current_frame_ - (current_frame_ > 0 ? 1 : 0));
    });
    connect(controls_->playPauseButton(), &QPushButton::clicked, this, [this] {
        if (play_pause_action_ != nullptr) play_pause_action_->trigger();
        else if (playing_) pausePlayback();
        else startPlayback();
    });
    connect(controls_->loopButton(), &QPushButton::toggled, this, [this](bool enabled) {
        if (loop_action_ != nullptr) loop_action_->setChecked(enabled);
        else loop_enabled_ = enabled;
    });
    connect(controls_->nextFrameButton(), &QPushButton::clicked, this, [this] {
        if (next_frame_action_ != nullptr) next_frame_action_->trigger();
        else if (current_frame_ < visible_end_frame_) {
            seekToFrame(current_frame_ + 1);
        }
    });
    connect(controls_->displayModeCombo(), qOverload<int>(&QComboBox::currentIndexChanged),
        this, [this](int index) {
            display_mode_ = index == static_cast<int>(TimelineDisplayMode::Frames)
                ? TimelineDisplayMode::Frames : TimelineDisplayMode::Time;
            updateViewWidgets();
            updateControls();
        });
    connect(controls_->graphEditorButton(), &QPushButton::toggled,
            this, &TimelineNavigator::graphEditorToggled);
    connect(ruler_, &TimelineRuler::seekRequested, this, [this](qint64 frame) {
        seekToFrame(static_cast<std::int64_t>(frame));
    });
    connect(ruler_, &TimelineRuler::extendRangeRequested, this, [this] {
        extendViewByOneHour();
    });
    connect(ruler_, &TimelineRuler::zoomStepRequested, this, [this](int direction) {
        applyZoomLevel(zoom_level_index_ + (direction > 0 ? 1 : -1));
    });
    layer_tracks_->setZoomStepHandler([this](int direction) {
        applyZoomLevel(zoom_level_index_ + direction);
    });
    layer_tracks_->setViewportWidthChangedHandler([this](int width) {
        ruler_->setMappingWidth(width);
    });
    connect(controls_->zoomSlider(), &QSlider::valueChanged, this, [this](int index) {
        applyZoomLevel(index);
    });
    connect(controls_->zoomOutButton(), &QPushButton::clicked, this, [this] {
        if (zoom_out_action_ != nullptr) zoom_out_action_->trigger();
        else applyZoomLevel(zoom_level_index_ - 1);
    });
    connect(controls_->zoomInButton(), &QPushButton::clicked, this, [this] {
        if (zoom_in_action_ != nullptr) zoom_in_action_->trigger();
        else applyZoomLevel(zoom_level_index_ + 1);
    });
    connect(horizontal_scroll_bar_, &QScrollBar::valueChanged, this, [this](int value) {
        const auto maximum_start = maximumViewStartFrame();
        const long double fraction = static_cast<long double>(value) /
            static_cast<long double>(detail::kTimelineScrollResolution);
        view_start_frame_ = roundedFrameClamped(
            fraction * static_cast<long double>(maximum_start), maximum_start);
        updateViewWidgets();
    });

    frames_per_view_ = calculateFramesPerView();
    updateHorizontalScrollBar();
    updateViewWidgets();
    updateControls();
}

void TimelineNavigator::setShortcutActions(
    QAction* play_pause,
    QAction* previous_frame,
    QAction* next_frame,
    QAction* loop,
    QAction* zoom_in,
    QAction* zoom_out)
{
    play_pause_action_ = play_pause;
    previous_frame_action_ = previous_frame;
    next_frame_action_ = next_frame;
    loop_action_ = loop;
    zoom_in_action_ = zoom_in;
    zoom_out_action_ = zoom_out;

    if (play_pause_action_ != nullptr) {
        connect(play_pause_action_, &QAction::triggered, this, [this] {
            if (playing_) pausePlayback();
            else startPlayback();
        });
    }
    if (previous_frame_action_ != nullptr) {
        connect(previous_frame_action_, &QAction::triggered, this, [this] {
            seekToFrame(current_frame_ - (current_frame_ > 0 ? 1 : 0));
        });
    }
    if (next_frame_action_ != nullptr) {
        connect(next_frame_action_, &QAction::triggered, this, [this] {
            if (current_frame_ < visible_end_frame_) seekToFrame(current_frame_ + 1);
        });
    }
    if (loop_action_ != nullptr) {
        connect(loop_action_, &QAction::toggled, this, [this](bool enabled) {
            loop_enabled_ = enabled;
            const QSignalBlocker blocker(controls_->loopButton());
            controls_->loopButton()->setChecked(enabled);
        });
    }
    if (zoom_in_action_ != nullptr) {
        connect(zoom_in_action_, &QAction::triggered, this, [this] {
            applyZoomLevel(zoom_level_index_ + 1);
        });
    }
    if (zoom_out_action_ != nullptr) {
        connect(zoom_out_action_, &QAction::triggered, this, [this] {
            applyZoomLevel(zoom_level_index_ - 1);
        });
    }
    updateControls();
}

void TimelineNavigator::setCompositionTiming(
    model::FrameRate frame_rate)
{
    pausePlayback(false);
    layer_tracks_->collapseAllLayerTracks();
    frame_rate_ = frame_rate;
    current_frame_ = 0;
    playback_start_frame_ = 0;
    playback_end_frame_exclusive_ = 0;
    loop_enabled_ = false;
    {
        const QSignalBlocker blocker(controls_->loopButton());
        controls_->loopButton()->setChecked(false);
    }
    if (loop_action_ != nullptr) loop_action_->setChecked(false);
    display_mode_ = TimelineDisplayMode::Time;
    {
        const QSignalBlocker blocker(controls_->displayModeCombo());
        controls_->displayModeCombo()->setCurrentIndex(static_cast<int>(display_mode_));
    }
    visible_end_frame_ = initialVisibleEndFrame(frame_rate);
    zoom_level_index_ = detail::kTimelineZoomDefaultIndex;
    zoom_factor_ = detail::kTimelineZoomLevels[static_cast<std::size_t>(zoom_level_index_)];
    frames_per_view_ = calculateFramesPerView();
    view_start_frame_ = 0;
    updateHorizontalScrollBar();
    updateViewWidgets();
    updateControls();
}

void TimelineNavigator::setCurrentFrame(std::int64_t frame)
{
    seekToFrame(frame);
}

std::int64_t TimelineNavigator::currentFrame() const noexcept
{
    return current_frame_;
}

bool TimelineNavigator::isPlaying() const noexcept
{
    return playing_;
}

bool TimelineNavigator::isLoopEnabled() const noexcept
{
    return loop_enabled_;
}

std::int64_t TimelineNavigator::visibleEndFrame() const noexcept
{
    return visible_end_frame_;
}

double TimelineNavigator::zoomFactor() const noexcept
{
    return zoom_factor_;
}

int TimelineNavigator::zoomLevelIndex() const noexcept
{
    return zoom_level_index_;
}

std::int64_t TimelineNavigator::viewStartFrame() const noexcept
{
    return view_start_frame_;
}

std::int64_t TimelineNavigator::framesPerView() const noexcept
{
    return frames_per_view_;
}

int TimelineNavigator::frameToViewportX(std::int64_t frame) const noexcept
{
    if (ruler_ == nullptr) {
        return 0;
    }
    return TimelineViewMapping{ruler_->mappingWidth(), kTimelineHeaderWidth, visible_end_frame_,
        view_start_frame_, frames_per_view_}.xForFrame(frame);
}

std::int64_t TimelineNavigator::frameAtViewportX(int x) const noexcept
{
    if (ruler_ == nullptr) {
        return 0;
    }
    return TimelineViewMapping{ruler_->mappingWidth(), kTimelineHeaderWidth, visible_end_frame_,
        view_start_frame_, frames_per_view_}.frameAtX(x);
}

void TimelineNavigator::setLayers(const std::vector<model::CompositionLayer>& layers)
{
    std::vector<TimelineLayerRow> rows;
    rows.reserve(layers.size());
    playback_end_frame_exclusive_ = 0;
    const auto maximum_frame = std::numeric_limits<std::int64_t>::max();
    for (auto layer = layers.rbegin(); layer != layers.rend(); ++layer) {
        rows.push_back(TimelineLayerRow{
            layer->id,
            layer->kind,
            utf8Text(layer->name),
            layer->visible,
            layer->timeline_start_frame,
            layer->duration_frames,
            layer->maximum_timeline_duration_frames,
            layer->keyframes});
        if (layer->timeline_start_frame >= 0 && layer->duration_frames > 0) {
            const auto end_frame = layer->duration_frames >
                    maximum_frame - layer->timeline_start_frame
                ? maximum_frame
                : layer->timeline_start_frame + layer->duration_frames;
            playback_end_frame_exclusive_ = std::max(
                playback_end_frame_exclusive_, end_frame);
        }
    }
    layer_tracks_->setRows(std::move(rows));
    if (playing_ && (playback_end_frame_exclusive_ == 0 ||
        current_frame_ >= playback_end_frame_exclusive_ - 1)) {
        pausePlayback(false);
        if (playback_end_frame_exclusive_ > 0) {
            setPlayheadFrame(playback_end_frame_exclusive_ - 1);
        }
    }
    updateViewWidgets();
    updateControls();
}

void TimelineNavigator::setSelectedLayerId(model::LayerId id)
{
    layer_tracks_->setSelectedLayerId(id);
}

void TimelineNavigator::setLayerExpanded(model::LayerId id, bool expanded)
{
    layer_tracks_->setLayerExpanded(id, expanded);
}

void TimelineNavigator::setTransformGroupExpanded(model::LayerId id, bool expanded)
{
    layer_tracks_->setTransformGroupExpanded(id, expanded);
}

void TimelineNavigator::setGraphEditorOpen(bool open)
{
    if (controls_->graphEditorButton() == nullptr || controls_->graphEditorButton()->isChecked() == open) return;
    const QSignalBlocker blocker(controls_->graphEditorButton());
    controls_->graphEditorButton()->setChecked(open);
}

void TimelineNavigator::setMediaDropHandler(
    std::function<void(const std::filesystem::path&, std::int64_t, model::LayerId)> handler)
{
    layer_tracks_->setMediaDropHandler(std::move(handler));
}

void TimelineNavigator::setLayerSelectedHandler(
    std::function<void(model::LayerId)> handler)
{
    layer_tracks_->setLayerSelectedHandler(std::move(handler));
}

void TimelineNavigator::setLayerMoveHandler(
    std::function<void(model::LayerId, std::int64_t)> handler)
{
    layer_tracks_->setLayerMoveHandler(std::move(handler));
}

void TimelineNavigator::setLayerResizeHandler(
    std::function<void(model::LayerId, std::int64_t)> handler)
{
    layer_tracks_->setLayerResizeHandler(std::move(handler));
}

void TimelineNavigator::setLayerReorderHandler(
    std::function<void(model::LayerId, std::size_t)> handler)
{
    layer_tracks_->setLayerReorderHandler(std::move(handler));
}

void TimelineNavigator::setLayerVisibilityHandler(
    std::function<void(model::LayerId, bool)> handler)
{
    layer_tracks_->setLayerVisibilityHandler(std::move(handler));
}

void TimelineNavigator::setLayerRemoveHandler(
    std::function<void(model::LayerId)> handler)
{
    layer_tracks_->setLayerRemoveHandler(std::move(handler));
}

void TimelineNavigator::setKeyframeSelectedHandler(
    std::function<void(model::LayerId,
                       creative_suite::animation::TransformProperty,
                       std::int64_t)> handler)
{
    layer_tracks_->setKeyframeSelectedHandler(std::move(handler));
}

void TimelineNavigator::setKeyframeMoveHandler(
    std::function<bool(model::LayerId,
                       creative_suite::animation::TransformProperty,
                       std::int64_t,
                       std::int64_t)> handler)
{
    layer_tracks_->setKeyframeMoveHandler(std::move(handler));
}

void TimelineNavigator::setCurveSegmentSelectedHandler(
    std::function<void(model::LayerId,
                       creative_suite::animation::TransformProperty,
                       std::int64_t)> handler)
{
    layer_tracks_->setCurveSegmentSelectedHandler(std::move(handler));
}

void TimelineNavigator::seekToFrame(std::int64_t frame)
{
    if (playing_) pausePlayback(false);
    const auto bounded_frame = std::clamp(frame, std::int64_t{0}, visible_end_frame_);
    setPlayheadFrame(bounded_frame);
}

void TimelineNavigator::startPlayback()
{
    if (playing_ || playback_end_frame_exclusive_ <= 0 ||
        frame_rate_.numerator <= 0 || frame_rate_.denominator <= 0) {
        updateControls();
        return;
    }

    if (current_frame_ >= playback_end_frame_exclusive_ - 1) {
        setPlayheadFrame(0);
    }
    playback_start_frame_ = current_frame_;
    playback_clock_.start();
    const auto frame_interval_ms = static_cast<int>(std::clamp(
        std::ceil(1000.0L * static_cast<long double>(frame_rate_.denominator) /
                  static_cast<long double>(frame_rate_.numerator)),
        1.0L, static_cast<long double>(std::numeric_limits<int>::max())));
    playing_ = true;
    playback_timer_->start(frame_interval_ms);
    updateControls();
}

void TimelineNavigator::pausePlayback(bool update_to_clock)
{
    if (!playing_) return;

    const auto frame_before_pause = current_frame_;
    std::int64_t target_frame = current_frame_;
    if (update_to_clock && playback_end_frame_exclusive_ > 0) {
        const auto elapsed_frames = detail::framesElapsedForNanoseconds(
            playback_clock_.nsecsElapsed(),
            frame_rate_.numerator,
            frame_rate_.denominator);
        if (loop_enabled_) {
            target_frame = detail::loopFrameForElapsed(
                playback_start_frame_, elapsed_frames, playback_end_frame_exclusive_);
        } else {
            const auto frames_until_end = playback_end_frame_exclusive_ - playback_start_frame_;
            target_frame = elapsed_frames >= frames_until_end
                ? playback_end_frame_exclusive_ - 1
                : playback_start_frame_ + elapsed_frames;
        }
    }

    playing_ = false;
    playback_timer_->stop();
    setPlayheadFrame(target_frame);
    if (current_frame_ == frame_before_pause) {
        // The active playback decode may still be in flight and will be
        // discarded once playback stops. Request the frozen frame again even
        // when the timer did not advance the playhead since its last tick.
        emit currentFrameChanged(static_cast<qint64>(current_frame_));
    }
    updateControls();
}

void TimelineNavigator::playbackTick()
{
    if (!playing_ || playback_end_frame_exclusive_ <= 0) return;
    const auto elapsed_frames = detail::framesElapsedForNanoseconds(
        playback_clock_.nsecsElapsed(),
        frame_rate_.numerator,
        frame_rate_.denominator);

    std::int64_t target_frame = 0;
    bool reached_end = false;
    if (loop_enabled_) {
        target_frame = detail::loopFrameForElapsed(
            playback_start_frame_, elapsed_frames, playback_end_frame_exclusive_);
    } else {
        const auto frames_until_end = playback_end_frame_exclusive_ - playback_start_frame_;
        reached_end = elapsed_frames >= frames_until_end;
        target_frame = reached_end
            ? playback_end_frame_exclusive_ - 1
            : playback_start_frame_ + elapsed_frames;
    }

    const auto frame_before_tick = current_frame_;
    if (reached_end) {
        playing_ = false;
        playback_timer_->stop();
    }
    setPlayheadFrame(target_frame);
    if (reached_end && current_frame_ == frame_before_tick) {
        emit currentFrameChanged(static_cast<qint64>(current_frame_));
    }
    updateControls();
}

void TimelineNavigator::setPlayheadFrame(std::int64_t frame)
{
    const auto bounded_frame = std::max<std::int64_t>(0, frame);
    ensureFrameInNavigationRange(bounded_frame);
    if (bounded_frame == current_frame_) {
        updateViewWidgets();
        updateControls();
        return;
    }

    current_frame_ = bounded_frame;
    ensureCurrentFrameVisible();
    updateViewWidgets();
    updateControls();
    emit currentFrameChanged(static_cast<qint64>(current_frame_));
}

void TimelineNavigator::ensureFrameInNavigationRange(std::int64_t frame)
{
    const auto extended_end = detail::extendRangeEndToInclude(
        visible_end_frame_, frame, framesPerHour(frame_rate_));
    if (extended_end == visible_end_frame_) return;
    visible_end_frame_ = extended_end;
    updateHorizontalScrollBar();
}

void TimelineNavigator::extendViewByOneHour() noexcept
{
    if (playing_) pausePlayback(false);
    const auto extended_end = detail::saturatingFrameAdd(
        visible_end_frame_, framesPerHour(frame_rate_));
    if (extended_end == visible_end_frame_) {
        return;
    }

    visible_end_frame_ = extended_end;
    current_frame_ = visible_end_frame_;
    updateHorizontalScrollBar();
    setViewStartFrame(maximumViewStartFrame());
    updateViewWidgets();
    updateControls();
    emit currentFrameChanged(static_cast<qint64>(current_frame_));
}

void TimelineNavigator::applyZoomLevel(int index)
{
    const int last_index = static_cast<int>(detail::kTimelineZoomLevels.size()) - 1;
    const int bounded_index = std::clamp(index, 0, last_index);
    const auto next_factor = detail::kTimelineZoomLevels[
        static_cast<std::size_t>(bounded_index)];
    if (bounded_index == zoom_level_index_ && next_factor == zoom_factor_) {
        updateControls();
        return;
    }

    const TimelineViewMapping old_mapping{
        ruler_->mappingWidth(), kTimelineHeaderWidth, visible_end_frame_,
        view_start_frame_, frames_per_view_};
    const bool playhead_visible = current_frame_ >= view_start_frame_ &&
        current_frame_ <= old_mapping.viewEndFrame();
    const int anchor_x = playhead_visible
        ? old_mapping.xForFrame(current_frame_)
        : old_mapping.axisLeft() + old_mapping.axisWidth() / 2;
    const int axis_width = std::max(1, old_mapping.axisWidth());
    const long double anchor_fraction = std::clamp(
        static_cast<long double>(anchor_x - old_mapping.axisLeft()) /
            static_cast<long double>(axis_width),
        0.0L, 1.0L);

    zoom_level_index_ = bounded_index;
    zoom_factor_ = next_factor;
    frames_per_view_ = calculateFramesPerView();
    const long double frame_offset = anchor_fraction *
        static_cast<long double>(std::max<std::int64_t>(0, frames_per_view_ - 1));
    const long double requested_start =
        static_cast<long double>(current_frame_) - frame_offset;
    const auto maximum_start = maximumViewStartFrame();
    const auto new_start = roundedFrameClamped(
        std::clamp(requested_start, 0.0L, static_cast<long double>(maximum_start)),
        maximum_start);
    updateHorizontalScrollBar();
    setViewStartFrame(new_start);
    updateControls();
}

void TimelineNavigator::updateHorizontalScrollBar()
{
    if (horizontal_scroll_bar_ == nullptr) {
        return;
    }
    const auto maximum_start = maximumViewStartFrame();
    const long double total_frames = static_cast<long double>(visible_end_frame_) + 1.0L;
    const long double page_fraction = total_frames > 0.0L
        ? static_cast<long double>(frames_per_view_) / total_frames : 1.0L;
    const int page_step = std::clamp(static_cast<int>(std::llround(
        page_fraction * detail::kTimelineScrollResolution)),
        1, detail::kTimelineScrollResolution);
    const int value = maximum_start == 0 ? 0 : static_cast<int>(std::llround(
        static_cast<long double>(std::clamp(view_start_frame_, std::int64_t{0}, maximum_start)) /
        static_cast<long double>(maximum_start) * detail::kTimelineScrollResolution));
    const QSignalBlocker blocker(horizontal_scroll_bar_);
    horizontal_scroll_bar_->setRange(0, detail::kTimelineScrollResolution);
    horizontal_scroll_bar_->setPageStep(page_step);
    horizontal_scroll_bar_->setSingleStep(std::max(1, page_step / 10));
    horizontal_scroll_bar_->setEnabled(maximum_start > 0);
    horizontal_scroll_bar_->setValue(value);
    const long double fraction = static_cast<long double>(value) /
        detail::kTimelineScrollResolution;
    view_start_frame_ = roundedFrameClamped(
        fraction * static_cast<long double>(maximum_start), maximum_start);
}

void TimelineNavigator::setViewStartFrame(std::int64_t frame)
{
    const auto maximum_start = maximumViewStartFrame();
    const auto bounded = std::clamp(frame, std::int64_t{0}, maximum_start);
    const int value = maximum_start == 0 ? 0 : static_cast<int>(std::llround(
        static_cast<long double>(bounded) / static_cast<long double>(maximum_start) *
        detail::kTimelineScrollResolution));
    const QSignalBlocker blocker(horizontal_scroll_bar_);
    horizontal_scroll_bar_->setValue(value);
    const long double fraction = static_cast<long double>(value) /
        detail::kTimelineScrollResolution;
    view_start_frame_ = roundedFrameClamped(
        fraction * static_cast<long double>(maximum_start), maximum_start);
    updateViewWidgets();
}

void TimelineNavigator::updateViewWidgets()
{
    if (ruler_ == nullptr || layer_tracks_ == nullptr) {
        return;
    }
    ruler_->setViewState(
        visible_end_frame_, current_frame_, view_start_frame_, frames_per_view_,
        frame_rate_, display_mode_);
    layer_tracks_->setViewState(
        visible_end_frame_, current_frame_, view_start_frame_, frames_per_view_);
}

void TimelineNavigator::ensureCurrentFrameVisible()
{
    const TimelineViewMapping mapping{
        ruler_->mappingWidth(), kTimelineHeaderWidth, visible_end_frame_,
        view_start_frame_, frames_per_view_};
    if (current_frame_ >= view_start_frame_ && current_frame_ <= mapping.viewEndFrame()) {
        return;
    }
    const auto centered_offset = std::max<std::int64_t>(0, (frames_per_view_ - 1) / 2);
    const auto desired_start = current_frame_ > centered_offset
        ? current_frame_ - centered_offset : 0;
    setViewStartFrame(std::min(desired_start, maximumViewStartFrame()));
}

std::int64_t TimelineNavigator::maximumViewStartFrame() const noexcept
{
    const auto last_visible_offset = std::max<std::int64_t>(0, frames_per_view_ - 1);
    return visible_end_frame_ >= last_visible_offset
        ? visible_end_frame_ - last_visible_offset : 0;
}

std::int64_t TimelineNavigator::calculateFramesPerView() const noexcept
{
    const long double raw = std::ceil(
        static_cast<long double>(framesPerHour(frame_rate_)) /
        static_cast<long double>(zoom_factor_));
    return std::max<std::int64_t>(1,
        raw >= static_cast<long double>(kMaximumFrame)
            ? kMaximumFrame : static_cast<std::int64_t>(raw));
}

void TimelineNavigator::updateControls()
{
    controls_->frameLabel()->setText(display_mode_ == TimelineDisplayMode::Frames
        ? QStringLiteral("Frame %1").arg(static_cast<qlonglong>(current_frame_))
        : formatTimelinePosition(current_frame_, frame_rate_, display_mode_));
    controls_->frameRateLabel()->setText(QStringLiteral("FPS: %1").arg(formatFrameRate(frame_rate_)));
    controls_->previousFrameButton()->setEnabled(current_frame_ > 0);
    controls_->nextFrameButton()->setEnabled(current_frame_ < visible_end_frame_);
    controls_->playPauseButton()->setText(playing_ ? QStringLiteral("Pause") : QStringLiteral("Play"));
    controls_->playPauseButton()->setEnabled(playing_ || playback_end_frame_exclusive_ > 0);
    controls_->loopButton()->setEnabled(playing_ || playback_end_frame_exclusive_ > 0);
    controls_->zoomOutButton()->setEnabled(zoom_level_index_ > 0);
    controls_->zoomInButton()->setEnabled(
        zoom_level_index_ < static_cast<int>(detail::kTimelineZoomLevels.size()) - 1);
    controls_->zoomLevelLabel()->setText(QStringLiteral("%1%")
        .arg(static_cast<int>(std::lround(zoom_factor_ * 100.0))));
    const QSignalBlocker blocker(controls_->zoomSlider());
    controls_->zoomSlider()->setValue(zoom_level_index_);
    if (previous_frame_action_ != nullptr) {
        previous_frame_action_->setEnabled(controls_->previousFrameButton()->isEnabled());
    }
    if (next_frame_action_ != nullptr) {
        next_frame_action_->setEnabled(controls_->nextFrameButton()->isEnabled());
    }
    if (play_pause_action_ != nullptr) {
        play_pause_action_->setEnabled(controls_->playPauseButton()->isEnabled());
    }
    if (loop_action_ != nullptr) {
        loop_action_->setEnabled(controls_->loopButton()->isEnabled());
        const QSignalBlocker loop_blocker(loop_action_);
        loop_action_->setChecked(loop_enabled_);
    }
    if (zoom_in_action_ != nullptr) {
        zoom_in_action_->setEnabled(controls_->zoomInButton()->isEnabled());
    }
    if (zoom_out_action_ != nullptr) {
        zoom_out_action_->setEnabled(controls_->zoomOutButton()->isEnabled());
    }
}

} // namespace motion::ui


