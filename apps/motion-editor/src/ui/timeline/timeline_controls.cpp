#include "timeline_controls.h"

#include "timeline_navigator_math.h"
#include "timeline_types.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>

namespace motion::ui {

TimelineControls::TimelineControls(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("motion-timeline-controls"));

    auto* controls = new QHBoxLayout(this);
    controls->setContentsMargins(0, 0, 0, 0);

    previous_frame_button_ = new QPushButton(QStringLiteral("Previous frame"), this);
    previous_frame_button_->setObjectName(QStringLiteral("motion-timeline-previous-frame"));
    play_pause_button_ = new QPushButton(QStringLiteral("Play"), this);
    play_pause_button_->setObjectName(QStringLiteral("motion-timeline-play-pause"));
    play_pause_button_->setToolTip(QStringLiteral("Play or pause the composition"));
    loop_button_ = new QPushButton(QStringLiteral("Loop"), this);
    loop_button_->setObjectName(QStringLiteral("motion-timeline-loop"));
    loop_button_->setCheckable(true);
    loop_button_->setToolTip(QStringLiteral("Restart playback from frame 0 at the end"));
    display_mode_combo_ = new QComboBox(this);
    display_mode_combo_->setObjectName(QStringLiteral("motion-timeline-display-mode"));
    display_mode_combo_->addItem(QStringLiteral("Time"),
        static_cast<int>(TimelineDisplayMode::Time));
    display_mode_combo_->addItem(QStringLiteral("Frames"),
        static_cast<int>(TimelineDisplayMode::Frames));
    display_mode_combo_->setToolTip(QStringLiteral("Timeline ruler and playhead display"));
    frame_label_ = new QLabel(this);
    frame_label_->setObjectName(QStringLiteral("motion-timeline-position-readout"));
    zoom_out_button_ = new QPushButton(QStringLiteral("\u2212"), this);
    zoom_out_button_->setObjectName(QStringLiteral("motion-timeline-zoom-out"));
    zoom_out_button_->setToolTip(QStringLiteral("Zoom out"));
    zoom_slider_ = new QSlider(Qt::Horizontal, this);
    zoom_slider_->setObjectName(QStringLiteral("motion-timeline-zoom-slider"));
    zoom_slider_->setRange(0, static_cast<int>(detail::kTimelineZoomLevels.size()) - 1);
    zoom_slider_->setValue(detail::kTimelineZoomDefaultIndex);
    zoom_slider_->setFixedWidth(150);
    zoom_slider_->setToolTip(QStringLiteral("Timeline zoom"));
    zoom_level_label_ = new QLabel(this);
    zoom_level_label_->setObjectName(QStringLiteral("motion-timeline-zoom-level"));
    zoom_level_label_->setMinimumWidth(52);
    zoom_level_label_->setAlignment(Qt::AlignCenter);
    zoom_in_button_ = new QPushButton(QStringLiteral("+"), this);
    zoom_in_button_->setObjectName(QStringLiteral("motion-timeline-zoom-in"));
    zoom_in_button_->setToolTip(QStringLiteral("Zoom in"));
    frame_rate_label_ = new QLabel(this);
    frame_rate_label_->setObjectName(QStringLiteral("motion-timeline-frame-rate"));
    next_frame_button_ = new QPushButton(QStringLiteral("Next frame"), this);
    next_frame_button_->setObjectName(QStringLiteral("motion-timeline-next-frame"));
    graph_editor_button_ = new QPushButton(QStringLiteral("Graph Editor"), this);
    graph_editor_button_->setObjectName(QStringLiteral("motion-timeline-graph-editor-toggle"));
    graph_editor_button_->setCheckable(true);
    graph_editor_button_->setToolTip(QStringLiteral("Select the Graph Editor tab"));

    controls->addWidget(previous_frame_button_);
    controls->addWidget(play_pause_button_);
    controls->addWidget(loop_button_);
    controls->addWidget(display_mode_combo_);
    controls->addWidget(graph_editor_button_);
    controls->addWidget(frame_label_);
    controls->addStretch(1);
    controls->addWidget(zoom_out_button_);
    controls->addWidget(zoom_slider_);
    controls->addWidget(zoom_level_label_);
    controls->addWidget(zoom_in_button_);
    controls->addStretch(1);
    controls->addWidget(frame_rate_label_);
    controls->addWidget(next_frame_button_);
}

QPushButton* TimelineControls::previousFrameButton() const noexcept
{
    return previous_frame_button_;
}

QPushButton* TimelineControls::playPauseButton() const noexcept
{
    return play_pause_button_;
}

QPushButton* TimelineControls::loopButton() const noexcept
{
    return loop_button_;
}

QComboBox* TimelineControls::displayModeCombo() const noexcept
{
    return display_mode_combo_;
}

QLabel* TimelineControls::frameLabel() const noexcept
{
    return frame_label_;
}

QPushButton* TimelineControls::zoomOutButton() const noexcept
{
    return zoom_out_button_;
}

QSlider* TimelineControls::zoomSlider() const noexcept
{
    return zoom_slider_;
}

QLabel* TimelineControls::zoomLevelLabel() const noexcept
{
    return zoom_level_label_;
}

QPushButton* TimelineControls::zoomInButton() const noexcept
{
    return zoom_in_button_;
}

QLabel* TimelineControls::frameRateLabel() const noexcept
{
    return frame_rate_label_;
}

QPushButton* TimelineControls::nextFrameButton() const noexcept
{
    return next_frame_button_;
}

QPushButton* TimelineControls::graphEditorButton() const noexcept
{
    return graph_editor_button_;
}

} // namespace motion::ui
