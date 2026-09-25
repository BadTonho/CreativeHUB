#include "main_window.h"
#include "main_window_support.h"

#include "logging/logger.h"
#include "preview_widget.h"
#include "project/project_file.h"
#include "settings/user_preferences.h"
#include "timeline/timeline_clip_edge_command.h"
#include "timeline/timeline_track_header_overlay.h"
#include "timeline/timeline_zoom.h"
#include "timeline/timeline_widget.h"
#include "ui/media_browser_list_widget.h"
#include "ui/system_memory_indicator.h"
#include "ui/timeline_end_buttons.h"

#include <QAction>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QDesktopServices>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QIcon>
#include <QKeySequence>
#include <QLabel>
#include <QAbstractItemView>
#include <QListWidget>
#include <QListWidgetItem>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMetaObject>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QStatusBar>
#include <QStyle>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <iterator>
#include <limits>
#include <string_view>
#include <system_error>
#include <utility>


using namespace main_window_detail;

namespace {

int timelineZoomLevelIndex(double factor) {
    const auto match = std::min_element(
        timeline::kTimelineZoomLevels.begin(), timeline::kTimelineZoomLevels.end(),
        [factor](double left, double right) {
            return std::abs(left - factor) < std::abs(right - factor);
        });
    return static_cast<int>(std::distance(
        timeline::kTimelineZoomLevels.begin(), match));
}

std::int64_t localFrameAtTimelinePlayhead(
    const timeline::TimelineClip& clip,
    std::int64_t playhead_frame) {
    if (playhead_frame < clip.timeline_start_frame) return 0;
    const auto local_frame = playhead_frame - clip.timeline_start_frame;
    return local_frame >= 0 && local_frame < clip.timeline_duration_frames
        ? local_frame
        : 0;
}

QIcon timelineToolIcon(bool blade) {
    QPixmap pixmap(20, 20);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor("#f2f2f2"), 1.6, Qt::SolidLine, Qt::RoundCap,
                        Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);

    if (blade) {
        painter.drawLine(QPointF(4.0, 15.5), QPointF(15.5, 4.0));
        painter.drawLine(QPointF(3.5, 16.0), QPointF(8.0, 16.5));
        painter.drawLine(QPointF(3.5, 16.0), QPointF(4.0, 11.5));
        painter.drawLine(QPointF(7.0, 13.0), QPointF(10.0, 16.0));
    } else {
        painter.drawRoundedRect(QRectF(6.0, 2.0, 8.0, 16.0), 4.0, 4.0);
        painter.drawLine(QPointF(10.0, 2.5), QPointF(10.0, 7.0));
        painter.drawLine(QPointF(8.0, 4.5), QPointF(8.0, 7.0));
        painter.drawLine(QPointF(12.0, 4.5), QPointF(12.0, 7.0));
        painter.drawLine(QPointF(10.0, 7.0), QPointF(10.0, 9.5));
    }

    return QIcon(pixmap);
}

QIcon timelineSnapIcon() {
    QPixmap pixmap(20, 20);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor("#f2f2f2"), 1.8, Qt::SolidLine,
                        Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawLine(QPointF(5.0, 3.0), QPointF(5.0, 9.0));
    painter.drawLine(QPointF(15.0, 3.0), QPointF(15.0, 9.0));
    painter.drawArc(QRectF(5.0, 4.0, 10.0, 12.0), 180 * 16, 180 * 16);
    painter.drawLine(QPointF(3.0, 3.0), QPointF(7.0, 3.0));
    painter.drawLine(QPointF(13.0, 3.0), QPointF(17.0, 3.0));
    return QIcon(pixmap);
}

} // namespace

void MainWindow::addVideoTrack() {
    bool accepted = false;
    const auto name = QInputDialog::getText(
        this,
        "Add Video Track",
        "Track name:",
        QLineEdit::Normal,
        QString("Video %1").arg(timeline_model_.trackCount() + 1),
        &accepted);
    if (!accepted) return;
    try {
        const auto result = executeTimelineCommand(application::AddTrackCommand{
            name.toUtf8().toStdString()});
        if (!result.changed()) {
            statusBar()->showMessage("The track name is invalid.");
            return;
        }
        applyTimelineEditResult(result);
        updateTimelineState();
        updatePlaybackControls();
        statusBar()->showMessage("Video track added.");
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "timeline",
            "add_track",
            error.what(),
            {});
        statusBar()->showMessage("Could not add the video track.");
    }
}
void MainWindow::renameActiveTrack() {
    synchronizeActiveTimelineSelection();
    if (!active_timeline_track_id_.has_value()) return;
    const auto track_index = timeline_model_.locateTrack(*active_timeline_track_id_);
    if (!track_index.has_value()) return;
    bool accepted = false;
    const auto current = fromUtf8(timeline_model_.tracks()[*track_index].name);
    const auto name = QInputDialog::getText(
        this,
        "Rename Track",
        "Track name:",
        QLineEdit::Normal,
        current,
        &accepted);
    if (!accepted) return;
    try {
        const auto result = executeTimelineCommand(application::RenameTrackCommand{
            *active_timeline_track_id_, name.toUtf8().toStdString()});
        if (result.status == application::EditStatus::Rejected) {
            statusBar()->showMessage("The track name is invalid.");
            return;
        }
        if (!result.changed()) return;
        applyTimelineEditResult(result);
        updateTimelineState();
        statusBar()->showMessage("Track renamed.");
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "timeline",
            "rename_track",
            error.what(),
            {{"track_index", std::to_string(*track_index)}});
        statusBar()->showMessage("Could not rename the track.");
    }
}

void MainWindow::moveActiveTrack(int direction) {
    synchronizeActiveTimelineSelection();
    if (timeline_model_.trackCount() < 2) return;
    if (!active_timeline_track_id_.has_value()) return;
    const auto track_location = timeline_model_.locateTrack(*active_timeline_track_id_);
    if (!track_location.has_value()) return;
    const auto from = *track_location;
    if (direction < 0 && from == 0) return;
    if (direction > 0 && from + 1 >= timeline_model_.trackCount()) return;
    const auto to = direction < 0 ? from - 1 : from + 1;
    try {
        const auto result = executeTimelineCommand(application::MoveTrackCommand{
            *active_timeline_track_id_, to});
        if (!result.changed()) return;
        applyTimelineEditResult(result);
        updateTimelineState();
        refreshPlaybackComposition();
        statusBar()->showMessage("Track order updated.");
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "timeline",
            "move_track",
            error.what(),
            {{"from_index", std::to_string(from)},
             {"to_index", std::to_string(to)}});
        statusBar()->showMessage("Could not move the track.");
    }
}

void MainWindow::removeActiveTrack() {
    synchronizeActiveTimelineSelection();
    if (!active_timeline_track_id_.has_value()) return;
    const auto track_index = timeline_model_.locateTrack(*active_timeline_track_id_);
    if (!track_index.has_value()) return;
    try {
        const auto result = executeTimelineCommand(application::RemoveTrackCommand{
            *active_timeline_track_id_});
        if (result.reason == application::EditReason::TrackNotEmpty) {
            statusBar()->showMessage("Only empty tracks can be removed.");
            return;
        }
        if (!result.changed()) return;
        applyTimelineEditResult(result);
        updateTimelineState();
        refreshPlaybackComposition();
        updatePlaybackControls();
        statusBar()->showMessage("Video track removed.");
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "timeline",
            "remove_track",
            error.what(),
            {{"track_index", std::to_string(*track_index)}});
        statusBar()->showMessage("Could not remove the video track.");
    }
}

void MainWindow::addTextClipAt(timeline::TrackId track_id, qint64 requested_frame) {
    const auto track_index = timeline_model_.locateTrack(track_id);
    if (!track_index.has_value() || requested_frame < 0) {
        statusBar()->showMessage("No video track is available for text.");
        return;
    }

    double frame_rate = 30.0;
    if (const auto selected_index = selectedMediaIndex(); selected_index.has_value()) {
        const auto& metadata = media_items_[*selected_index].metadata;
        if (metadata.frame_rate.has_value() &&
            std::isfinite(*metadata.frame_rate) && *metadata.frame_rate > 0.0) {
            frame_rate = *metadata.frame_rate;
        }
    }
    const auto duration_frames = std::max<std::int64_t>(
        1, static_cast<std::int64_t>(std::ceil(frame_rate * 5.0)));
    const auto start_frame = std::max<std::int64_t>(0, requested_frame);

    try {
        const auto result = executeTimelineCommand(application::AddTextClipCommand{
            track_id,
            start_frame,
            duration_frames,
            frame_rate});
        if (!result.changed() && result.reason == application::EditReason::Overlap) {
            statusBar()->showMessage("A text clip already occupies that range.");
            return;
        }
        if (!result.changed()) {
            statusBar()->showMessage("The text clip could not be added.");
            return;
        }

        applyTimelineEditResult(result, false);
        updateTimelineState();
        updatePlaybackControls();
        updatePlaybackStatus();
        refreshPlaybackComposition();
        if (playback_controller_ != nullptr && playback_controller_->available() &&
            active_timeline_track_index_cache_.has_value() &&
            active_timeline_clip_index_cache_.has_value() &&
            timeline_model_.tracks()[*active_timeline_track_index_cache_]
                .clips[*active_timeline_clip_index_cache_].kind == timeline::ClipKind::Text) {
            playback_controller_->renderCompositionFrame(
                timelinePlayheadFrame(), playback_frame_index_);
        } else if (playback_controller_ != nullptr && canPlaybackSelectedMedia()) {
            playback_controller_->seekActiveClip(playback_frame_index_);
        }
        statusBar()->showMessage("Text clip added.");
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "timeline",
            "add_text",
            error.what(),
            {{"track_index", std::to_string(*track_index)},
             {"timeline_frame", std::to_string(start_frame)}});
        statusBar()->showMessage("Could not add the text clip.");
        QMessageBox::warning(this, "Timeline error", "The text clip could not be added.");
    }
}

struct MainWindow::TimelineControls {
    QPushButton* add_track_button;
    QPushButton* rename_track_button;
    QPushButton* move_track_up_button;
    QPushButton* move_track_down_button;
    QPushButton* remove_track_button;
    QPushButton* zoom_out_button;
    QSlider* zoom_slider;
    QLabel* zoom_indicator;
    QPushButton* zoom_in_button;
};

MainWindow::TimelineControls MainWindow::createTimelineControls(
    QWidget* container,
    QVBoxLayout* layout) {
    auto* controls = new QHBoxLayout;
    controls->setSpacing(6);

    auto* playback_label = new QLabel("Playback", container);
    playback_label->setStyleSheet("color: #9aa4b2; font-weight: 600;");
    controls->addWidget(playback_label);

    previous_frame_button_ = new QPushButton(container);
    play_pause_button_ = new QPushButton(container);
    next_frame_button_ = new QPushButton(container);
    previous_frame_button_->setIcon(
        style()->standardIcon(QStyle::SP_MediaSeekBackward));
    play_pause_button_->setIcon(
        style()->standardIcon(QStyle::SP_MediaPlay));
    next_frame_button_->setIcon(
        style()->standardIcon(QStyle::SP_MediaSeekForward));
    previous_frame_button_->setIconSize(QSize(16, 16));
    play_pause_button_->setIconSize(QSize(16, 16));
    next_frame_button_->setIconSize(QSize(16, 16));
    previous_frame_button_->setFixedSize(32, 28);
    play_pause_button_->setFixedSize(32, 28);
    next_frame_button_->setFixedSize(32, 28);
    clear_timeline_button_ = new QPushButton("Clear Timeline", container);
    selection_button_ = new QPushButton(container);
    razor_button_ = new QPushButton(container);
    snap_button_ = new QPushButton(container);
    selection_button_->setIcon(timelineToolIcon(false));
    razor_button_->setIcon(timelineToolIcon(true));
    snap_button_->setIcon(timelineSnapIcon());
    selection_button_->setIconSize(QSize(16, 16));
    razor_button_->setIconSize(QSize(16, 16));
    snap_button_->setIconSize(QSize(16, 16));
    selection_button_->setFixedSize(32, 28);
    razor_button_->setFixedSize(32, 28);
    snap_button_->setFixedSize(32, 28);
    selection_button_->setCheckable(true);
    razor_button_->setCheckable(true);
    snap_button_->setCheckable(true);
    selection_button_->setAutoExclusive(true);
    razor_button_->setAutoExclusive(true);
    selection_button_->setChecked(true);
    controls->addWidget(previous_frame_button_);
    controls->addWidget(play_pause_button_);
    controls->addWidget(next_frame_button_);
    controls->addSpacing(6);
    auto* monitor_volume_label = new QLabel("Volume", container);
    monitor_volume_label->setStyleSheet("color: #9aa4b2; font-weight: 600;");
    monitor_volume_slider_ = new QSlider(Qt::Horizontal, container);
    monitor_volume_indicator_ = new QLabel(container);
    monitor_volume_slider_->setObjectName("monitorVolumeSlider");
    monitor_volume_indicator_->setObjectName("monitorVolumeIndicator");
    monitor_volume_slider_->setRange(
        settings::kMinimumMonitorVolumePercent,
        settings::kMaximumMonitorVolumePercent);
    monitor_volume_slider_->setSingleStep(5);
    monitor_volume_slider_->setPageStep(10);
    monitor_volume_slider_->setFixedWidth(96);
    monitor_volume_slider_->setToolTip(
        "Editor monitoring volume (0% to 200%)");
    monitor_volume_slider_->setAccessibleName("Monitor Volume");
    monitor_volume_indicator_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    monitor_volume_indicator_->setMinimumWidth(42);
    monitor_volume_indicator_->setStyleSheet("color: #9aa4b2;");
    controls->addWidget(monitor_volume_label);
    controls->addWidget(monitor_volume_slider_);
    controls->addWidget(monitor_volume_indicator_);
    controls->addWidget(clear_timeline_button_);
    controls->addWidget(selection_button_);
    controls->addWidget(razor_button_);
    controls->addWidget(snap_button_);
    controls->addSpacing(10);
    auto* tracks_label = new QLabel("Tracks", container);
    tracks_label->setStyleSheet("color: #9aa4b2; font-weight: 600;");
    controls->addWidget(tracks_label);
    auto* add_track_button = new QPushButton("Add Video Track", container);
    auto* rename_track_button = new QPushButton("Rename Track", container);
    auto* move_track_up_button = new QPushButton("Track Up", container);
    auto* move_track_down_button = new QPushButton("Track Down", container);
    auto* remove_track_button = new QPushButton("Remove Track", container);
    controls->addWidget(add_track_button);
    controls->addWidget(rename_track_button);
    controls->addWidget(move_track_up_button);
    controls->addWidget(move_track_down_button);
    controls->addWidget(remove_track_button);
    controls->addSpacing(10);
    auto* zoom_out_button = new QPushButton("−", container);
    auto* zoom_control = new QWidget(container);
    auto* zoom_layout = new QVBoxLayout(zoom_control);
    auto* zoom_slider = new QSlider(Qt::Horizontal, zoom_control);
    auto* zoom_indicator = new QLabel("100%", zoom_control);
    auto* zoom_in_button = new QPushButton("+", container);
    zoom_out_button->setFixedWidth(28);
    zoom_in_button->setFixedWidth(28);
    zoom_layout->setContentsMargins(0, 0, 0, 0);
    zoom_layout->setSpacing(0);
    zoom_control->setFixedWidth(92);
    zoom_slider->setRange(
        0, static_cast<int>(timeline::kTimelineZoomLevels.size()) - 1);
    zoom_slider->setValue(timelineZoomLevelIndex(1.0));
    zoom_slider->setFixedWidth(92);
    zoom_slider->setSingleStep(1);
    zoom_slider->setPageStep(1);
    zoom_slider->setToolTip("Timeline zoom level");
    zoom_slider->setStyleSheet(
        "QSlider::groove:horizontal { height: 2px; background: #3b4553; }"
        "QSlider::sub-page:horizontal { height: 2px; background: #8b98aa; }"
        "QSlider::add-page:horizontal { height: 2px; background: #252d38; }"
        "QSlider::handle:horizontal { width: 10px; height: 10px; "
        "margin: -4px 0; border-radius: 5px; background: #d5a94b; }");
    zoom_indicator->setAlignment(Qt::AlignCenter);
    zoom_indicator->setFixedWidth(92);
    zoom_out_button->setToolTip("Zoom out of the timeline");
    zoom_in_button->setToolTip("Zoom in on the timeline");
    zoom_indicator->setToolTip("Current timeline zoom");
    controls->addWidget(zoom_out_button);
    zoom_layout->addWidget(zoom_indicator);
    zoom_layout->addWidget(zoom_slider);
    controls->addWidget(zoom_control);
    controls->addWidget(zoom_in_button);
    controls->addStretch();
    const auto workspace_buttons = ui::createTimelineEndButtons(container);
    workspace_buttons_container_ = workspace_buttons.container;
    edit_workspace_button_ = workspace_buttons.edit;
    fusion_workspace_button_ = workspace_buttons.fusion;
    connect(
        edit_workspace_button_,
        &QPushButton::clicked,
        this,
        [this]() { setWorkspacePage(WorkspacePage::Edit); });
    connect(
        fusion_workspace_button_,
        &QPushButton::clicked,
        this,
        [this]() { setWorkspacePage(WorkspacePage::Fusion); });
    layout->addLayout(controls);

    previous_frame_button_->setToolTip("Step one frame backward");
    play_pause_button_->setToolTip("Play or pause the active clip");
    next_frame_button_->setToolTip("Step one frame forward");
    previous_frame_button_->setAccessibleName("Previous Frame");
    play_pause_button_->setAccessibleName("Play or Pause");
    next_frame_button_->setAccessibleName("Next Frame");
    monitor_volume_label->setToolTip(
        "Volume heard during editor playback; does not modify the project.");
    clear_timeline_button_->setToolTip("Remove all clips from every track");
    selection_button_->setToolTip("Select and move timeline clips");
    selection_button_->setAccessibleName("Selection Tool");
    razor_button_->setToolTip("Split a clip where you click");
    razor_button_->setAccessibleName("Blade Tool");
    snap_button_->setToolTip(
        "Toggle magnetic snapping for clips and media drops");
    snap_button_->setAccessibleName("Magnetic Snap");
    add_track_button->setToolTip("Create a new empty video track");
    rename_track_button->setToolTip("Rename the active track");
    move_track_up_button->setToolTip("Move the active track toward the top");
    move_track_down_button->setToolTip("Move the active track toward the bottom");
    remove_track_button->setToolTip("Remove the active track when it is empty");

    return {
        add_track_button,
        rename_track_button,
        move_track_up_button,
        move_track_down_button,
        remove_track_button,
        zoom_out_button,
        zoom_slider,
        zoom_indicator,
        zoom_in_button};
}

void MainWindow::createTimelineViewport(QWidget* container, QVBoxLayout* layout) {
    timeline_widget_ = new timeline::TimelineWidget(container);
    timeline_scroll_ = new QScrollArea(container);
    timeline_scroll_->setWidgetResizable(true);
    timeline_scroll_->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
    timeline_scroll_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    timeline_scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    timeline_scroll_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    timeline_scroll_->setAcceptDrops(true);
    timeline_scroll_->viewport()->setAcceptDrops(true);
    timeline_scroll_->setFrameShape(QFrame::NoFrame);
    timeline_scroll_->setStyleSheet(
        "QScrollArea { background: transparent; border: none; }"
        "QScrollArea > QWidget > QWidget { background: transparent; }");
    timeline_scroll_->setWidget(timeline_widget_);
    timeline_widget_->setAcceptDrops(false);
    timeline_scroll_->viewport()->installEventFilter(timeline_widget_);
    timeline_widget_->setTimelineViewportWidth(timeline_scroll_->viewport()->width());
    timeline_header_overlay_ = new timeline::TimelineTrackHeaderOverlay(
        timeline_widget_,
        timeline_scroll_->viewport());
    timeline_header_overlay_->setVerticalScrollOffset(
        timeline_scroll_->verticalScrollBar()->value());
    connect(
        timeline_scroll_->verticalScrollBar(),
        &QScrollBar::valueChanged,
        timeline_header_overlay_,
        &timeline::TimelineTrackHeaderOverlay::setVerticalScrollOffset);
    layout->addWidget(timeline_scroll_, 1);
}

void MainWindow::createTimelineFooter(QWidget* container, QVBoxLayout* layout) {
    // Keep the playback status as a compact footer while giving the timeline
    // the expandable space in the dock.
    auto* playback_footer = new QWidget(container);
    auto* playback_footer_layout = new QHBoxLayout(playback_footer);
    playback_footer_layout->setContentsMargins(0, 0, 0, 0);
    playback_footer_layout->setSpacing(12);

    playback_status_label_ = new QLabel("No media selected.", playback_footer);
    playback_status_label_->setStyleSheet("color: #9aa4b2;");
    playback_status_label_->setSizePolicy(
        QSizePolicy::Preferred,
        QSizePolicy::Fixed);
    playback_footer_layout->addWidget(playback_status_label_);

    timeline_message_label_ = new QLabel(playback_footer);
    timeline_message_label_->setStyleSheet("color: #9aa4b2;");
    timeline_message_label_->setSizePolicy(
        QSizePolicy::Preferred,
        QSizePolicy::Fixed);
    playback_footer_layout->addWidget(timeline_message_label_);
    playback_footer_layout->addStretch(1);
    system_memory_indicator_ = new SystemMemoryIndicator(playback_footer);
    playback_footer_layout->addWidget(system_memory_indicator_);
    playback_footer->setFixedHeight(playback_footer->sizeHint().height());
    layout->addWidget(playback_footer);

    connect(
        statusBar(),
        &QStatusBar::messageChanged,
        this,
        [this](const QString& message) {
            if (timeline_message_label_ == nullptr) return;
            timeline_message_label_->setText(message);
            timeline_message_label_->setVisible(!message.isEmpty());
        });
    timeline_message_label_->setText(statusBar()->currentMessage());
    timeline_message_label_->setVisible(!statusBar()->currentMessage().isEmpty());
    statusBar()->setVisible(false);
}

void MainWindow::connectTimelineSignals(const TimelineControls& controls) {
    auto* add_track_button = controls.add_track_button;
    auto* rename_track_button = controls.rename_track_button;
    auto* move_track_up_button = controls.move_track_up_button;
    auto* move_track_down_button = controls.move_track_down_button;
    auto* remove_track_button = controls.remove_track_button;
    auto* zoom_out_button = controls.zoom_out_button;
    auto* zoom_slider = controls.zoom_slider;
    auto* zoom_indicator = controls.zoom_indicator;
    auto* zoom_in_button = controls.zoom_in_button;

    connect(previous_frame_button_, &QPushButton::clicked, this, [this]() {
        sendPlaybackCommand(playback::PlaybackCommand::StepBackward);
    });
    connect(play_pause_button_, &QPushButton::clicked, this, [this]() {
        sendPlaybackCommand(playback_is_playing_ ? playback::PlaybackCommand::Pause : playback::PlaybackCommand::Play);
    });
    connect(next_frame_button_, &QPushButton::clicked, this, [this]() {
        sendPlaybackCommand(playback::PlaybackCommand::StepForward);
    });
    connect(
        monitor_volume_slider_,
        &QSlider::valueChanged,
        this,
        &MainWindow::applyMonitorVolumePercent);
    connect(clear_timeline_button_, &QPushButton::clicked, this, [this]() {
        clearTimeline();
    });
    connect(selection_button_, &QPushButton::clicked, this, [this]() {
        if (razor_tool_action_ != nullptr) {
            razor_tool_action_->setChecked(false);
        }
        if (timeline_widget_ != nullptr) timeline_widget_->setRazorMode(false);
    });
    connect(razor_button_, &QPushButton::toggled, this, [this](bool enabled) {
        if (razor_tool_action_ != nullptr &&
            razor_tool_action_->isChecked() != enabled) {
            razor_tool_action_->setChecked(enabled);
        }
        if (selection_button_ != nullptr &&
            selection_button_->isChecked() == enabled) {
            selection_button_->setChecked(!enabled);
        }
        if (timeline_widget_ != nullptr) timeline_widget_->setRazorMode(enabled);
    });
    connect(snap_button_, &QPushButton::toggled, this, [this](bool enabled) {
        if (timeline_widget_ != nullptr &&
            timeline_widget_->snapEnabled() != enabled) {
            timeline_widget_->setSnapEnabled(enabled);
        }
    });
    connect(
        timeline_widget_,
        &timeline::TimelineWidget::snapEnabledChanged,
        this,
        [this](bool enabled) {
            if (snap_button_ == nullptr || snap_button_->isChecked() == enabled) {
                return;
            }
            const QSignalBlocker blocker(snap_button_);
            snap_button_->setChecked(enabled);
        });
    connect(add_track_button, &QPushButton::clicked,
            this, &MainWindow::addVideoTrack);
    connect(rename_track_button, &QPushButton::clicked,
            this, &MainWindow::renameActiveTrack);
    connect(move_track_up_button, &QPushButton::clicked,
            this, [this]() { moveActiveTrack(-1); });
    connect(move_track_down_button, &QPushButton::clicked,
            this, [this]() { moveActiveTrack(1); });
    connect(remove_track_button, &QPushButton::clicked,
            this, &MainWindow::removeActiveTrack);
    connect(
        timeline_widget_,
        &timeline::TimelineWidget::zoomRequested,
        this,
        [this](double factor) {
            applyTimelineZoom(factor);
        });
    connect(
        timeline_widget_,
        &timeline::TimelineWidget::zoomChanged,
        this,
        [this, zoom_slider, zoom_indicator, zoom_out_button, zoom_in_button](double factor) {
            const QSignalBlocker blocker(zoom_slider);
            zoom_slider->setValue(timelineZoomLevelIndex(factor));
            zoom_indicator->setText(
                QString::number(static_cast<int>(std::lround(factor * 100.0))) + "%");
            zoom_out_button->setEnabled(timeline_widget_->canZoomOut());
            zoom_in_button->setEnabled(timeline_widget_->canZoomIn());
            updateProjectDirtyState();
        });
    connect(
        timeline_widget_,
        &timeline::TimelineWidget::trackRowHeightChanged,
        this,
        [this](double) { updateProjectDirtyState(); });
    connect(zoom_slider, &QSlider::valueChanged, this, [this](int level) {
        if (timeline_widget_ == nullptr ||
            level < 0 || level >= static_cast<int>(timeline::kTimelineZoomLevels.size())) {
            return;
        }
        applyTimelineZoom(
            timeline::kTimelineZoomLevels[static_cast<std::size_t>(level)]);
    });
    connect(zoom_out_button, &QPushButton::clicked, this, [this]() {
        if (timeline_widget_ == nullptr) return;
        applyTimelineZoom(timeline_widget_->nextZoomFactor(-1));
    });
    connect(zoom_in_button, &QPushButton::clicked, this, [this]() {
        if (timeline_widget_ == nullptr) return;
        applyTimelineZoom(timeline_widget_->nextZoomFactor(1));
    });
    zoom_out_button->setEnabled(timeline_widget_->canZoomOut());
    zoom_in_button->setEnabled(timeline_widget_->canZoomIn());
    connect(
        timeline_widget_,
        &timeline::TimelineWidget::clipSelected,
        this,
        &MainWindow::handleTimelineClipSelectionChanged);
    connect(
        timeline_widget_,
        &timeline::TimelineWidget::editImageClipRequested,
        this,
        &MainWindow::editTimelineImageClip);
    connect(
        timeline_widget_,
        &timeline::TimelineWidget::clipSelectionCleared,
        this,
        &MainWindow::handleTimelineClipSelectionCleared);
    connect(
        timeline_widget_,
        &timeline::TimelineWidget::transitionSelected,
        this,
        &MainWindow::handleTimelineTransitionSelected);
    connect(
        timeline_widget_,
        &timeline::TimelineWidget::transitionSelectionCleared,
        this,
        &MainWindow::handleTimelineTransitionSelectionCleared);
    connect(
        timeline_widget_,
        &timeline::TimelineWidget::transitionAddRequested,
        this,
        &MainWindow::handleTimelineTransitionAddRequested);
    connect(
        timeline_widget_,
        &timeline::TimelineWidget::transitionRemoveRequested,
        this,
        &MainWindow::handleTimelineTransitionRemoveRequested);
    connect(
        timeline_widget_,
        &timeline::TimelineWidget::clipMoveRequested,
        this,
        &MainWindow::handleTimelineClipMove);
    connect(
        timeline_widget_,
        &timeline::TimelineWidget::clipSplitRequested,
        this,
        &MainWindow::handleTimelineClipSplit);
    connect(
        timeline_widget_,
        &timeline::TimelineWidget::trimStarted,
        this,
        &MainWindow::handleTimelineTrimStarted);
    connect(
        timeline_widget_,
        &timeline::TimelineWidget::clipEdgeTrimRequested,
        this,
        &MainWindow::handleTimelineClipTrim);
    connect(
        timeline_widget_,
        &timeline::TimelineWidget::seekStarted,
        this,
        &MainWindow::handleTimelineSeekStarted);
    connect(
        timeline_widget_,
        &timeline::TimelineWidget::seekRequested,
        this,
        &MainWindow::handleTimelineSeek);
    connect(
        timeline_widget_,
        &timeline::TimelineWidget::mediaDropRequested,
        this,
        &MainWindow::handleMediaDropAt);
    connect(
        timeline_widget_,
        &timeline::TimelineWidget::effectDropRequested,
        this,
        &MainWindow::handleEffectDropAt);
}

QWidget* MainWindow::createTimeline() {
    auto* container = new QWidget;
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    const auto controls = createTimelineControls(container, layout);
    monitor_volume_slider_->setValue(settings::monitorVolumePercent());
    applyMonitorVolumePercent(monitor_volume_slider_->value());
    createTimelineViewport(container, layout);
    snap_button_->setChecked(timeline_widget_->snapEnabled());
    createTimelineFooter(container, layout);
    connectTimelineSignals(controls);

    updateTimelineState();
    updatePlaybackControls();
    return container;
}

void MainWindow::applyTimelineZoom(double factor) {
    if (timeline_widget_ == nullptr || timeline_scroll_ == nullptr) return;

    auto* scroll_bar = timeline_scroll_->horizontalScrollBar();
    const auto old_scroll = scroll_bar->value();
    const auto anchor_frame = timelinePlayheadFrame();
    const auto anchor_content_x = timeline_widget_->contentXForFrame(anchor_frame);
    const auto raw_anchor_viewport_x = anchor_content_x - static_cast<double>(old_scroll);
    const auto viewport_width = static_cast<double>(timeline_scroll_->viewport()->width());
    const auto anchor_viewport_x = raw_anchor_viewport_x >= 0.0 &&
            raw_anchor_viewport_x <= viewport_width
        ? raw_anchor_viewport_x
        : viewport_width / 2.0;
    timeline_widget_->setZoomFactor(factor);

    QTimer::singleShot(0, timeline_scroll_, [this, anchor_frame, anchor_viewport_x]() {
        if (timeline_widget_ == nullptr || timeline_scroll_ == nullptr) return;
        auto* bar = timeline_scroll_->horizontalScrollBar();
        const auto new_anchor_content_x = timeline_widget_->contentXForFrame(anchor_frame);
        const auto target = static_cast<int>(std::llround(
            new_anchor_content_x - anchor_viewport_x));
        bar->setValue(target);
    });
}

void MainWindow::applyMonitorVolumePercent(int percent) {
    const auto normalized = std::clamp(
        percent,
        settings::kMinimumMonitorVolumePercent,
        settings::kMaximumMonitorVolumePercent);
    settings::setMonitorVolumePercent(normalized);
    if (monitor_volume_slider_ != nullptr &&
        monitor_volume_slider_->value() != normalized) {
        const QSignalBlocker blocker(monitor_volume_slider_);
        monitor_volume_slider_->setValue(normalized);
    }
    if (monitor_volume_indicator_ != nullptr) {
        monitor_volume_indicator_->setText(
            QString::number(normalized) + "%");
    }
    if (playback_controller_ != nullptr) {
        playback_controller_->setMonitorVolume(static_cast<double>(normalized) / 100.0);
    }
}

bool MainWindow::hasSelectedMedia() const noexcept {
    return selectedMediaIndex().has_value();
}

std::optional<timeline::ClipLocation>
MainWindow::selectedTimelineClipLocation() const noexcept {
    if (!timeline_model_.hasClip()) return std::nullopt;
    if (active_timeline_clip_id_.has_value()) {
        if (const auto location = timeline_model_.locateClip(*active_timeline_clip_id_);
            location.has_value()) {
            const auto& clip = timeline_model_.tracks()[location->track_index]
                .clips[location->clip_index];
            if (clip.kind == timeline::ClipKind::Text) return location;
            const auto selected_index = selectedMediaIndex();
            if (selected_index.has_value() &&
                normalizedPath(clip.source_path) == normalizedPath(
                    media_items_[*selected_index].metadata.source_path)) {
                return location;
            }
        }
    }
    if (active_timeline_track_index_cache_.has_value() &&
        active_timeline_clip_index_cache_.has_value() &&
        *active_timeline_track_index_cache_ < timeline_model_.trackCount() &&
        *active_timeline_clip_index_cache_ < timeline_model_.clipCount(
            *active_timeline_track_index_cache_)) {
        const auto& active_clip = timeline_model_.tracks()
            [*active_timeline_track_index_cache_].clips[*active_timeline_clip_index_cache_];
        if (active_clip.kind == timeline::ClipKind::Text) {
            return timeline::ClipLocation{
                *active_timeline_track_index_cache_, *active_timeline_clip_index_cache_};
        }
        const auto selected_index = selectedMediaIndex();
        if (selected_index.has_value() &&
            active_clip.source_path == media_items_[*selected_index].metadata.source_path) {
            return timeline::ClipLocation{
                *active_timeline_track_index_cache_, *active_timeline_clip_index_cache_};
        }
    }
    const auto selected_index = selectedMediaIndex();
    if (!selected_index.has_value()) return std::nullopt;
    const auto& selected = media_items_[*selected_index];
    for (std::size_t track = 0; track < timeline_model_.trackCount(); ++track) {
        const auto& clips = timeline_model_.tracks()[track].clips;
        for (std::size_t index = 0; index < clips.size(); ++index) {
            if (timeline::isMediaClipKind(clips[index].kind) &&
                clips[index].source_path == selected.metadata.source_path) {
                return timeline::ClipLocation{track, index};
            }
        }
    }
    return std::nullopt;
}

void MainWindow::synchronizeActiveTimelineSelection() noexcept {
    if (active_timeline_clip_id_.has_value()) {
        const auto location = timeline_model_.locateClip(*active_timeline_clip_id_);
        if (!location.has_value()) {
            clearActiveTimelineSelection();
            return;
        }
        const auto& track = timeline_model_.tracks()[location->track_index];
        active_timeline_track_id_ = track.track_id;
        active_timeline_track_index_cache_ = location->track_index;
        active_timeline_clip_index_cache_ = location->clip_index;
        return;
    }
    if (active_timeline_track_id_.has_value()) {
        const auto track_index = timeline_model_.locateTrack(*active_timeline_track_id_);
        if (track_index.has_value()) {
            active_timeline_track_index_cache_ = *track_index;
            return;
        }
        active_timeline_track_id_.reset();
        active_timeline_track_index_cache_.reset();
    }
    if (active_timeline_track_index_cache_.has_value() &&
        *active_timeline_track_index_cache_ < timeline_model_.trackCount()) {
        const auto& track = timeline_model_.tracks()[*active_timeline_track_index_cache_];
        active_timeline_track_id_ = track.track_id;
        if (active_timeline_clip_index_cache_.has_value() &&
            *active_timeline_clip_index_cache_ < track.clips.size()) {
            active_timeline_clip_id_ = track.clips[*active_timeline_clip_index_cache_].clip_id;
        }
    } else if (!active_timeline_clip_index_cache_.has_value()) {
        active_timeline_track_index_cache_.reset();
    }
}

void MainWindow::setActiveTimelineSelection(timeline::ClipLocation location) noexcept {
    if (location.track_index >= timeline_model_.trackCount() ||
        location.clip_index >= timeline_model_.clipCount(location.track_index)) {
        clearActiveTimelineSelection();
        return;
    }
    const auto& track = timeline_model_.tracks()[location.track_index];
    active_timeline_track_id_ = track.track_id;
    active_timeline_clip_id_ = track.clips[location.clip_index].clip_id;
    active_timeline_track_index_cache_ = location.track_index;
    active_timeline_clip_index_cache_ = location.clip_index;
}

void MainWindow::clearActiveTimelineSelection() noexcept {
    active_timeline_track_id_.reset();
    active_timeline_clip_id_.reset();
    active_timeline_track_index_cache_.reset();
    active_timeline_clip_index_cache_.reset();
}

std::optional<std::size_t> MainWindow::selectedTimelineClipIndex() const noexcept {
    const auto location = selectedTimelineClipLocation();
    return location.has_value()
        ? std::optional<std::size_t>{location->clip_index}
        : std::nullopt;
}

bool MainWindow::selectedMediaMatchesTimeline() const noexcept {
    return selectedTimelineClipIndex().has_value();
}

bool MainWindow::canPreviewSelectedMedia() const noexcept {
    return hasSelectedMedia() &&
        !media_items_[*selectedMediaIndex()].offline &&
        (!timeline_model_.hasClip() || selectedMediaMatchesTimeline());
}

bool MainWindow::canPlaybackSelectedMedia() const noexcept {
    if (!timeline_model_.hasClip() ||
        !active_timeline_track_index_cache_.has_value() ||
        !active_timeline_clip_index_cache_.has_value() ||
        *active_timeline_track_index_cache_ >= timeline_model_.trackCount() ||
        *active_timeline_clip_index_cache_ >= timeline_model_.clipCount(
            *active_timeline_track_index_cache_)) {
        return false;
    }

    const auto& clip = timeline_model_.tracks()[*active_timeline_track_index_cache_]
        .clips[*active_timeline_clip_index_cache_];
    if (clip.kind == timeline::ClipKind::Text) {
        return playback_controller_ != nullptr && playback_controller_->available();
    }

    const auto media = std::find_if(
        media_items_.begin(),
        media_items_.end(),
        [&clip](const ImportedMedia& item) {
            return normalizedPath(item.metadata.source_path) ==
                normalizedPath(clip.source_path);
        });
    return media != media_items_.end() && !media->offline &&
        playback_controller_ != nullptr && playback_controller_->available();
}

std::optional<timeline::ClipLocation>
MainWindow::timelineClipAtPlayhead() const noexcept {
    if (!timeline_model_.hasClip()) return std::nullopt;
    return timeline_model_.topClipAt(timelinePlayheadFrame());
}

bool MainWindow::canPlaybackTimelineAtPlayhead() const noexcept {
    const auto location = timelineClipAtPlayhead();
    if (!location.has_value() || playback_controller_ == nullptr ||
        !playback_controller_->available()) return false;

    const auto& clip = timeline_model_.tracks()[location->track_index]
        .clips[location->clip_index];
    if (clip.kind == timeline::ClipKind::Text) return true;

    const auto media = std::find_if(
        media_items_.begin(),
        media_items_.end(),
        [&clip](const ImportedMedia& item) {
            return normalizedPath(item.metadata.source_path) ==
                normalizedPath(clip.source_path);
        });
    return media != media_items_.end() && !media->offline;
}

std::int64_t MainWindow::timelinePlayheadFrame() const noexcept {
    if (preserved_timeline_playhead_frame_.has_value()) {
        return std::max<std::int64_t>(
            0, *preserved_timeline_playhead_frame_);
    }
    if (!active_timeline_track_index_cache_.has_value() ||
        !active_timeline_clip_index_cache_.has_value() ||
        *active_timeline_track_index_cache_ >= timeline_model_.trackCount() ||
        *active_timeline_clip_index_cache_ >= timeline_model_.clipCount(
            *active_timeline_track_index_cache_)) {
        return timeline_widget_ != nullptr
            ? timeline_widget_->playheadFrame()
            : 0;
    }
    const auto& clip = timeline_model_.tracks()[*active_timeline_track_index_cache_]
        .clips[*active_timeline_clip_index_cache_];
    if (clip.timeline_duration_frames <= 0) return clip.timeline_start_frame;
    const auto local_frame = std::clamp<std::int64_t>(
        playback_frame_index_, 0, clip.timeline_duration_frames - 1);
    if (clip.timeline_start_frame >
        std::numeric_limits<std::int64_t>::max() - local_frame) {
        return clip.timeline_start_frame;
    }
    return clip.timeline_start_frame + local_frame;
}

void MainWindow::synchronizeTimelineSessionSelection() {
    if (!active_timeline_clip_id_.has_value() && active_timeline_track_index_cache_.has_value() &&
        active_timeline_clip_index_cache_.has_value() &&
        *active_timeline_track_index_cache_ < timeline_model_.trackCount() &&
        *active_timeline_clip_index_cache_ < timeline_model_.clipCount(*active_timeline_track_index_cache_)) {
        const auto& track = timeline_model_.tracks()[*active_timeline_track_index_cache_];
        active_timeline_track_id_ = track.track_id;
        active_timeline_clip_id_ = track.clips[*active_timeline_clip_index_cache_].clip_id;
    }
    editor_session_.selectionForUi().selected_source_path.reset();
    if (hasSelectedMedia()) {
        editor_session_.selectionForUi().selected_source_path = normalizedPath(
            media_items_[*selectedMediaIndex()].metadata.source_path);
    }
}

timeline::EditState MainWindow::captureTimelineEditState() {
    synchronizeTimelineSessionSelection();
    return editor_session_.captureEditState();
}

void MainWindow::recordTimelineEdit(timeline::EditState state) {
    try {
        timeline_command_service_.recordLegacyEdit(std::move(state));
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "timeline",
            "record_history",
            error.what(),
            {});
    }
    updateHistoryActions();
}

void MainWindow::updateHistoryActions() {
    if (undo_action_ != nullptr) undo_action_->setEnabled(timeline_command_service_.canUndo());
    if (redo_action_ != nullptr) redo_action_->setEnabled(timeline_command_service_.canRedo());
}

void MainWindow::applyTimelineEditResult(
    const application::TimelineEditResult& result,
    bool stop_playback) {
    if (!result.changed()) return;
    active_timeline_track_id_ = result.selection.active_track_id;
    active_timeline_clip_id_ = result.selection.active_clip_id;
    active_transition_ = result.selection.active_transition;
    playback_frame_index_ = std::max<std::int64_t>(0, result.playhead_frame);
    preserved_timeline_playhead_frame_ = result.preserved_playhead_frame;
    synchronizeActiveTimelineSelection();
    if (result.selection.selected_source_path.has_value() && media_list_ != nullptr) {
        const auto selected_path = normalizedPath(*result.selection.selected_source_path);
        const auto selected_media = std::find_if(
            media_items_.begin(), media_items_.end(),
            [&selected_path](const ImportedMedia& item) {
                return normalizedPath(item.metadata.source_path) == selected_path;
            });
        if (selected_media != media_items_.end()) {
            const auto media_index = static_cast<qint64>(
                std::distance(media_items_.begin(), selected_media));
            const QSignalBlocker blocker(media_list_);
            for (int row = 0; row < media_list_->count(); ++row) {
                auto* item = media_list_->item(row);
                if (item != nullptr &&
                    item->data(media_browser_ui::kMediaIndexRole).toLongLong() == media_index) {
                    media_list_->setCurrentItem(item);
                    break;
                }
            }
        }
    }
    if (result.invalidate_playback) {
        if (playback_controller_ != nullptr) {
            playback_controller_->invalidate(stop_playback);
        }
        playback_is_playing_ = false;
    }
    updateHistoryActions();
    updateProjectDirtyState();
}

void MainWindow::beginAudioEdit() {
    if (!pending_audio_edit_batch_id_.has_value() &&
        active_timeline_track_id_.has_value() &&
        active_timeline_clip_id_.has_value()) {
        pending_audio_edit_batch_id_ = timeline_command_service_.beginEditBatch();
    }
}

void MainWindow::finishAudioEdit() {
    if (!pending_audio_edit_batch_id_.has_value()) return;
    static_cast<void>(timeline_command_service_.finishEditBatch(*pending_audio_edit_batch_id_));
    pending_audio_edit_batch_id_.reset();
    updateHistoryActions();
    updateTimelineState();
    updatePlaybackControls();
    updatePlaybackStatus();
}

void MainWindow::applyClipAudioControls() {
    if (!active_timeline_clip_id_.has_value() ||
        clip_volume_slider_ == nullptr || clip_mute_check_ == nullptr) {
        return;
    }
    beginAudioEdit();
    const auto result = executeTimelineCommand(application::SetClipAudioCommand{
        *active_timeline_clip_id_,
        static_cast<double>(clip_volume_slider_->value()) / 100.0,
        clip_mute_check_->isChecked()});
    if (result.changed()) {
        applyTimelineEditResult(result, false);
        updatePlaybackAudioParameters();
        updateProjectDirtyState();
        statusBar()->showMessage("Clip audio settings changed.");
    }
}

void MainWindow::applyTrackAudioControls() {
    if (!active_timeline_track_id_.has_value() ||
        track_volume_slider_ == nullptr || track_mute_check_ == nullptr) {
        return;
    }
    beginAudioEdit();
    const auto result = executeTimelineCommand(application::SetTrackAudioCommand{
        *active_timeline_track_id_,
        static_cast<double>(track_volume_slider_->value()) / 100.0,
        track_mute_check_->isChecked()});
    if (result.changed()) {
        applyTimelineEditResult(result, false);
        updatePlaybackAudioParameters();
        updateProjectDirtyState();
        statusBar()->showMessage("Track audio settings changed.");
    }
}

void MainWindow::beginTransformEdit() {
    if (!pending_transform_edit_batch_id_.has_value() &&
        active_timeline_clip_id_.has_value()) {
        pending_transform_edit_batch_id_ = timeline_command_service_.beginEditBatch();
    }
}

void MainWindow::finishTransformEdit() {
    if (!pending_transform_edit_batch_id_.has_value()) return;
    static_cast<void>(timeline_command_service_.finishEditBatch(*pending_transform_edit_batch_id_));
    pending_transform_edit_batch_id_.reset();
    updateHistoryActions();
    updateProjectDirtyState();
}

void MainWindow::updatePlaybackAudioParameters() {
    if (playback_controller_ == nullptr ||
        !active_timeline_track_index_cache_.has_value() ||
        !active_timeline_clip_index_cache_.has_value() ||
        *active_timeline_track_index_cache_ >= timeline_model_.trackCount() ||
        *active_timeline_clip_index_cache_ >= timeline_model_.clipCount(
            *active_timeline_track_index_cache_)) {
        return;
    }
    playback_controller_->setAudioParametersForActiveClip();
}

void MainWindow::updateTimelineState() {
    synchronizeActiveTimelineSelection();
    const bool occupied = timeline_model_.hasClip();

    if (clear_timeline_button_ != nullptr) {
        clear_timeline_button_->setEnabled(occupied);
    }

    if (timeline_widget_ != nullptr) {
        timeline_widget_->setTracks(timeline_model_.tracks());
        if (const auto location = selectedTimelineClipLocation(); location.has_value()) {
            timeline_widget_->setActiveClip(location);
        } else {
            timeline_widget_->setActiveClip(std::nullopt);
        }
        timeline_widget_->setPlayheadFrame(timelinePlayheadFrame());
    }

    const bool audio_enabled = canPlaybackSelectedMedia() &&
        active_timeline_track_index_cache_.has_value() &&
        active_timeline_clip_index_cache_.has_value() &&
        timeline_model_.tracks()[*active_timeline_track_index_cache_]
            .clips[*active_timeline_clip_index_cache_].kind == timeline::ClipKind::Video;
    if (clip_volume_slider_ != nullptr && clip_mute_check_ != nullptr &&
        track_volume_slider_ != nullptr && track_mute_check_ != nullptr) {
        clip_volume_slider_->setEnabled(audio_enabled);
        clip_mute_check_->setEnabled(audio_enabled);
        track_volume_slider_->setEnabled(audio_enabled);
        track_mute_check_->setEnabled(audio_enabled);
        if (audio_enabled) {
            const auto& track = timeline_model_.tracks()[*active_timeline_track_index_cache_];
            const auto& clip = track.clips[*active_timeline_clip_index_cache_];
            const QSignalBlocker clip_slider_blocker(clip_volume_slider_);
            const QSignalBlocker clip_mute_blocker(clip_mute_check_);
            const QSignalBlocker track_slider_blocker(track_volume_slider_);
            const QSignalBlocker track_mute_blocker(track_mute_check_);
            clip_volume_slider_->setValue(static_cast<int>(std::lround(
                std::clamp(clip.audio_gain, 0.0, 2.0) * 100.0)));
            clip_mute_check_->setChecked(clip.audio_muted);
            track_volume_slider_->setValue(static_cast<int>(std::lround(
                std::clamp(track.audio_gain, 0.0, 2.0) * 100.0)));
            track_mute_check_->setChecked(track.audio_muted);
        }
    }

    updateHistoryActions();
    updateProjectDirtyState();
    updateInspector();
}

void MainWindow::addSelectedMediaToTimeline() {
    if (!hasSelectedMedia()) return;

    const auto selected_index = selectedMediaIndex();
    if (!selected_index.has_value()) return;
    const auto& selected = media_items_[*selected_index];
    if (selected.offline) {
        statusBar()->showMessage("Offline media cannot be added to the timeline.");
        return;
    }
    try {
        const auto track_index = active_timeline_track_index_cache_.value_or(0);
        if (track_index >= timeline_model_.trackCount()) {
            statusBar()->showMessage("The selected track is unavailable.");
            return;
        }
        const auto result = executeTimelineCommand(application::AddMediaClipCommand{
            selected.metadata.source_path,
            timeline_model_.tracks()[track_index].track_id,
            std::nullopt});
        if (result.changed()) {
            applyTimelineEditResult(result);
            updateTimelineState();
            updatePlaybackControls();
            updatePlaybackStatus();
            updateMediaDetails(static_cast<int>(*selected_index));
            if (track_index == 0 && active_timeline_clip_index_cache_.has_value()) {
                activateTimelineClipAt(
                    track_index, *active_timeline_clip_index_cache_, 0, false);
            } else {
                preview_widget_->setFrame(selected.first_frame);
                refreshPlaybackComposition();
            }
            statusBar()->showMessage("Media added to the timeline.");
            return;
        }
        if (result.reason == application::EditReason::InvalidTimingMetadata) {
            const auto path = pathToUtf8(selected.metadata.source_path);
            logging::Logger::instance().log(
                logging::Level::Error,
                "timeline",
                "add_clip",
                "Media does not contain enough valid timing metadata for timeline placement.",
                {{"path", path}});
            QMessageBox::warning(
                this,
                "Could not add media",
                "This media does not contain enough timing metadata for the timeline.");
            statusBar()->showMessage("Could not add media to the timeline.");
            return;
        }
        if (result.reason == application::EditReason::OfflineMedia) {
            statusBar()->showMessage("Offline media cannot be added to the timeline.");
            return;
        }
        statusBar()->showMessage("Could not add media to the timeline.");
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "timeline",
            "add_clip",
            error.what(),
            {{"path", pathToUtf8(selected.metadata.source_path)}});
        statusBar()->showMessage("Could not add media to the timeline.");
        QMessageBox::warning(
            this,
            "Timeline error",
            "The media could not be added to the timeline.");
    }
}

void MainWindow::handleMediaDrop(const QString& source_path) {
    const std::string source_text = source_path.toUtf8().toStdString();
    try {
        const auto dropped_path = normalizedPath(
            QFileInfo(source_path).filesystemFilePath());
        const auto existing = std::find_if(
            media_items_.begin(),
            media_items_.end(),
            [&dropped_path](const ImportedMedia& item) {
                return item.metadata.source_path == dropped_path;
            });

        if (existing == media_items_.end()) {
            statusBar()->showMessage("Import this media before adding it to the timeline.");
            return;
        }

        populateMediaBrowser(dropped_path);
        const auto index = selectedMediaIndex();
        if (!index.has_value()) return;
        addSelectedMediaToTimeline();
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "ui",
            "timeline_drop",
            error.what(),
            {{"path", source_text}});
        statusBar()->showMessage("Could not add dropped media to the timeline.");
        QMessageBox::warning(
            this,
            "Could not add media",
            "The dropped media could not be added to the timeline.");
    }
}

void MainWindow::handleMediaDropAt(
    const QString& source_path,
    timeline::TrackId track_id,
    qint64 timeline_frame) {
    const auto target_track = timeline_model_.locateTrack(track_id);
    if (!target_track.has_value() || timeline_frame < 0) return;
    const auto path = normalizedPath(QFileInfo(source_path).filesystemFilePath());
    const auto media = std::find_if(
        media_items_.begin(),
        media_items_.end(),
        [&path](const ImportedMedia& item) {
            return normalizedPath(item.metadata.source_path) == path;
        });
    if (media == media_items_.end()) {
        statusBar()->showMessage("Import this media before adding it to the timeline.");
        return;
    }
    const auto media_index = static_cast<std::size_t>(
        std::distance(media_items_.begin(), media));
    if (media->offline) {
        statusBar()->showMessage("Offline media cannot be added to the timeline.");
        return;
    }

    try {
        const auto result = executeTimelineCommand(application::AddMediaClipCommand{
            media->metadata.source_path,
            track_id,
            timeline_frame});
        if (!result.changed() &&
            (result.reason == application::EditReason::Overlap ||
             result.reason == application::EditReason::InvalidPosition ||
             result.reason == application::EditReason::InvalidTarget)) {
            statusBar()->showMessage("The media cannot be placed at that position.");
            return;
        }
        if (result.reason == application::EditReason::InvalidTimingMetadata) {
            logging::Logger::instance().log(
                logging::Level::Error,
                "timeline",
                "add_clip",
                "Media does not contain enough timing metadata for timeline placement.",
                {{"path", pathToUtf8(path)},
                 {"track_id", std::to_string(track_id)},
                 {"timeline_frame", std::to_string(timeline_frame)}});
            QMessageBox::warning(
                this,
                "Could not add media",
                "This media does not contain enough timing metadata for the timeline.");
            return;
        }
        if (!result.changed()) return;

        applyTimelineEditResult(result);
        {
            const QSignalBlocker blocker(media_list_);
            media_list_->setCurrentRow(static_cast<int>(media_index));
        }
        updateMediaDetails(static_cast<int>(media_index));
        updateTimelineState();
        updatePlaybackControls();
        updatePlaybackStatus();
        refreshPlaybackComposition();
        if (*target_track == 0) {
            activateTimelineClipAt(
                *target_track,
                *active_timeline_clip_index_cache_,
                0,
                false);
        } else {
            preview_widget_->setFrame(media->first_frame);
        }
        statusBar()->showMessage("Media added to the timeline.");
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "ui",
            "timeline_drop",
            error.what(),
            {{"path", pathToUtf8(path)},
             {"track_id", std::to_string(track_id)},
             {"timeline_frame", std::to_string(timeline_frame)}});
        statusBar()->showMessage("Could not add dropped media to the timeline.");
        QMessageBox::warning(this, "Could not add media",
                             "The dropped media could not be added to the timeline.");
    }
}

void MainWindow::handleEffectDropAt(
    const QString& effect_id,
    timeline::TrackId track_id,
    qint64 timeline_frame) {
    if (effect_id != QStringLiteral("text.text")) {
        statusBar()->showMessage("This effect cannot be added to the timeline.");
        return;
    }
    addTextClipAt(track_id, timeline_frame);
}

void MainWindow::handleTimelineClipSelectionChanged(
    timeline::TrackId track_id,
    timeline::ClipId clip_id) {
    active_transition_.reset();
    const auto location = timeline_model_.locateClip(clip_id);
    if (!location.has_value() ||
        timeline_model_.tracks()[location->track_index].track_id != track_id) {
        return;
    }
    const auto selected_track = location->track_index;
    const auto& selected_clip = timeline_model_.tracks()[selected_track]
        .clips[location->clip_index];
    if (selected_track == 0 && timeline::isMediaClipKind(selected_clip.kind)) {
        handleTimelineClipSelected(clip_id);
        return;
    }

    const auto previous_playhead = timelinePlayheadFrame();
    const bool move_playhead =
        move_playhead_on_clip_selection_action_ != nullptr &&
        move_playhead_on_clip_selection_action_->isChecked();
    setActiveTimelineSelection(*location);
    const auto selected_local_frame = move_playhead
        ? std::int64_t{0}
        : localFrameAtTimelinePlayhead(selected_clip, previous_playhead);
    if (move_playhead) {
        preserved_timeline_playhead_frame_.reset();
    } else if (selected_clip.timeline_start_frame + selected_local_frame !=
               previous_playhead) {
        preserved_timeline_playhead_frame_ = previous_playhead;
    } else {
        preserved_timeline_playhead_frame_.reset();
    }
    if (selected_clip.kind == timeline::ClipKind::Text) {
        playback_is_playing_ = false;
        playback_frame_index_ = selected_local_frame;
        if (move_playhead) {
            preserved_timeline_playhead_frame_.reset();
        }
        updateTimelineState();
        updatePlaybackControls();
        updatePlaybackStatus();
        if (playback_controller_ != nullptr) {
            (void)playback_controller_->activateClip(
                selected_clip.clip_id, selected_local_frame, false,
                !move_playhead);
        }
        statusBar()->showMessage("Text clip selected.");
        return;
    }
    playback_is_playing_ = false;
    if (playback_controller_ != nullptr) playback_controller_->invalidate(true);
    const auto& clip = timeline_model_.tracks()[*active_timeline_track_index_cache_]
        .clips[location->clip_index];
    const auto media = std::find_if(
        media_items_.begin(),
        media_items_.end(),
        [&clip](const ImportedMedia& item) {
            return normalizedPath(item.metadata.source_path) ==
                   normalizedPath(clip.source_path);
        });
    if (media == media_items_.end()) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "timeline",
            "select_clip",
            "The selected timeline clip has no matching imported media item.",
            {{"path", pathToUtf8(clip.source_path)},
             {"track_id", std::to_string(track_id)},
             {"clip_id", std::to_string(clip_id)}});
        return;
    }
    populateMediaBrowser(clip.source_path);
    if (media->offline) {
        preview_widget_->clearFrame("Preview area\n\nThe selected media is offline.");
    } else {
        preview_widget_->setFrame(media->first_frame);
        activateTimelineClipAt(
            location->track_index,
            location->clip_index,
            selected_local_frame,
            false,
            !move_playhead);
    }
    playback_frame_index_ = selected_local_frame;
    updateTimelineState();
    updatePlaybackControls();
    updatePlaybackStatus();
    statusBar()->showMessage("Timeline clip selected.");
}

void MainWindow::handleTimelineClipSelectionCleared() {
    const auto previous_playhead = timelinePlayheadFrame();
    const bool move_playhead =
        move_playhead_on_clip_selection_action_ != nullptr &&
        move_playhead_on_clip_selection_action_->isChecked();
    if (playback_controller_ != nullptr) playback_controller_->invalidate(true);
    playback_is_playing_ = false;
    clearActiveTimelineSelection();
    if (media_list_ != nullptr) {
        const QSignalBlocker blocker(media_list_);
        media_list_->clearSelection();
        media_list_->setCurrentRow(-1);
    }
    playback_frame_index_ = 0;
    if (move_playhead || previous_playhead == 0) {
        preserved_timeline_playhead_frame_.reset();
    } else {
        preserved_timeline_playhead_frame_ = previous_playhead;
    }
    updateTimelineState();
    updatePlaybackControls();
    updatePlaybackStatus();
    statusBar()->showMessage("Gap in timeline.");
}

void MainWindow::handleTimelineTransitionSelected(
    timeline::TrackId track_id,
    timeline::ClipId from_clip_id,
    timeline::ClipId to_clip_id) {
    active_transition_.reset();
    const auto track = timeline_model_.locateTrack(track_id);
    const auto from = timeline_model_.locateClip(from_clip_id);
    const auto to = timeline_model_.locateClip(to_clip_id);
    if (!track.has_value() || !from.has_value() || !to.has_value() ||
        from->track_index != *track || to->track_index != *track ||
        timeline_model_.transitionBetween(*track, from->clip_index, to->clip_index) == nullptr) {
        updateInspector();
        return;
    }
    active_transition_ = ActiveTransition{track_id, from_clip_id, to_clip_id};
    updateInspector();
    statusBar()->showMessage("Timeline transition selected.");
}

void MainWindow::handleTimelineTransitionSelectionCleared() {
    active_transition_.reset();
    updateInspector();
}

void MainWindow::handleTimelineTransitionAddRequested(
    timeline::TrackId track_id,
    timeline::ClipId from_clip_id,
    timeline::ClipId to_clip_id,
    qint64 kind) {
    if (kind < 0 || kind > 1) return;

    try {
        const auto result = executeTimelineCommand(application::AddTransitionCommand{
            track_id,
            from_clip_id,
            to_clip_id,
            kind == 0 ? timeline::TransitionKind::CrossDissolve
                      : timeline::TransitionKind::FadeToBlack,
            15});
        if (!result.changed()) {
            if (result.status == application::EditStatus::NoChange) {
                statusBar()->showMessage("A transition is already present here.");
                return;
            }
            statusBar()->showMessage("The transition cannot be added here.");
            return;
        }
        applyTimelineEditResult(result, false);
        updateTimelineState();
        updatePlaybackControls();
        updatePlaybackStatus();
        refreshPlaybackComposition();
        if (playback_controller_ != nullptr) {
            playback_controller_->renderCompositionFrame(
                timelinePlayheadFrame(), playback_frame_index_);
        }
        statusBar()->showMessage("Timeline transition added.");
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "timeline",
            "add_transition",
            error.what(),
            {{"track_id", std::to_string(track_id)},
             {"from_clip_id", std::to_string(from_clip_id)},
             {"to_clip_id", std::to_string(to_clip_id)},
             });
        statusBar()->showMessage("Could not add the timeline transition.");
    }
}

void MainWindow::handleTimelineTransitionRemoveRequested(
    timeline::TrackId track_id,
    timeline::ClipId from_clip_id,
    timeline::ClipId to_clip_id) {
    try {
        const auto result = executeTimelineCommand(application::RemoveTransitionCommand{
            track_id, from_clip_id, to_clip_id});
        if (!result.changed()) {
            statusBar()->showMessage("No transition is present at this junction.");
            return;
        }
        applyTimelineEditResult(result, false);
        updateTimelineState();
        updatePlaybackControls();
        updatePlaybackStatus();
        refreshPlaybackComposition();
        if (playback_controller_ != nullptr) {
            playback_controller_->renderCompositionFrame(
                timelinePlayheadFrame(), playback_frame_index_);
        }
        statusBar()->showMessage("Timeline transition removed.");
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "timeline",
            "remove_transition",
            error.what(),
            {{"track_id", std::to_string(track_id)},
             {"from_clip_id", std::to_string(from_clip_id)},
             {"to_clip_id", std::to_string(to_clip_id)},
             });
        statusBar()->showMessage("Could not remove the timeline transition.");
    }
}

void MainWindow::applyTransitionSettings() {
    if (!active_transition_.has_value() || transition_type_combo_ == nullptr ||
        transition_duration_spin_ == nullptr) {
        return;
    }

    const auto selection = *active_transition_;
    const auto track_index = timeline_model_.locateTrack(selection.track_id);
    const auto from_location = timeline_model_.locateClip(selection.from_clip_id);
    const auto to_location = timeline_model_.locateClip(selection.to_clip_id);
    if (!track_index.has_value() || !from_location.has_value() ||
        !to_location.has_value() ||
        from_location->track_index != *track_index ||
        to_location->track_index != *track_index ||
        timeline_model_.transitionBetween(
            *track_index,
            from_location->clip_index,
            to_location->clip_index) == nullptr) {
        active_transition_.reset();
        updateInspector();
        return;
    }

    try {
        const auto kind = transition_type_combo_->currentData().toInt() == 1
            ? timeline::TransitionKind::FadeToBlack
            : timeline::TransitionKind::CrossDissolve;
        const auto result = executeTimelineCommand(application::UpdateTransitionCommand{
            selection.track_id,
            selection.from_clip_id,
            selection.to_clip_id,
            kind,
            transition_duration_spin_->value()});
        if (!result.changed()) {
            statusBar()->showMessage("The transition settings were not changed.");
            updateInspector();
            return;
        }

        applyTimelineEditResult(result, false);
        updateTimelineState();
        updatePlaybackControls();
        updatePlaybackStatus();
        refreshPlaybackComposition();
        if (playback_controller_ != nullptr) {
            playback_controller_->renderCompositionFrame(
                timelinePlayheadFrame(), playback_frame_index_);
        }
        statusBar()->showMessage("Timeline transition updated.");
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "timeline",
            "update_transition",
            error.what(),
            {{"track_id", std::to_string(selection.track_id)},
             {"from_clip_id", std::to_string(selection.from_clip_id)},
             {"to_clip_id", std::to_string(selection.to_clip_id)},
             });
        statusBar()->showMessage("Could not update the timeline transition.");
    }
}

void MainWindow::removeSelectedTransition() {
    if (!active_transition_.has_value()) return;
    const auto selection = *active_transition_;
    const auto track_index = timeline_model_.locateTrack(selection.track_id);
    const auto from_location = timeline_model_.locateClip(selection.from_clip_id);
    const auto to_location = timeline_model_.locateClip(selection.to_clip_id);
    if (!track_index.has_value() || !from_location.has_value() ||
        !to_location.has_value() ||
        from_location->track_index != *track_index ||
        to_location->track_index != *track_index) {
        active_transition_.reset();
        updateInspector();
        return;
    }
    handleTimelineTransitionRemoveRequested(
        selection.track_id, selection.from_clip_id, selection.to_clip_id);
}

void MainWindow::handleTimelineClipMove(
    timeline::ClipId clip_id,
    timeline::TrackId target_track_id,
    qint64 timeline_start_frame) {
    if (timeline_start_frame < 0) return;
    try {
        const auto result = executeTimelineCommand(application::MoveClipCommand{
            clip_id, target_track_id, timeline_start_frame});
        if (!result.changed()) {
            if (result.status == application::EditStatus::NoChange) return;
            statusBar()->showMessage("The clip cannot be moved to that position.");
            return;
        }
        applyTimelineEditResult(result);
        updateTimelineState();
        updatePlaybackControls();
        updatePlaybackStatus();
        refreshPlaybackComposition();
        statusBar()->showMessage("Timeline clip moved.");
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "timeline",
            "move_clip",
            error.what(),
            {{"clip_id", std::to_string(clip_id)},
             {"target_track_id", std::to_string(target_track_id)},
             {"timeline_frame", std::to_string(timeline_start_frame)}});
        statusBar()->showMessage("Could not move the timeline clip.");
    }
}

void MainWindow::handleTimelineClipSplit(
    timeline::ClipId clip_id,
    qint64 local_frame) {
    if (local_frame < 0) return;
    const auto location = timeline_model_.locateClip(clip_id);
    if (!location.has_value()) return;
    const auto source_clip = timeline_model_.tracks()[location->track_index]
        .clips[location->clip_index];
    try {
        const auto result = executeTimelineCommand(application::SplitClipCommand{
            clip_id, local_frame});
        if (!result.changed()) {
            statusBar()->showMessage("A clip cannot be split at its boundary.");
            return;
        }
        applyTimelineEditResult(result);
        updateTimelineState();
        updatePlaybackControls();
        updatePlaybackStatus();
        refreshPlaybackComposition();
        if (!active_timeline_track_index_cache_.has_value() ||
            !active_timeline_clip_index_cache_.has_value()) {
            return;
        }
        const auto& right_clip = timeline_model_.tracks()[*active_timeline_track_index_cache_]
            .clips[*active_timeline_clip_index_cache_];
        const auto media_item = std::find_if(
            media_items_.begin(),
            media_items_.end(),
            [&right_clip](const ImportedMedia& item) {
                return normalizedPath(item.metadata.source_path) ==
                       normalizedPath(right_clip.source_path);
            });
        if (media_item != media_items_.end() && !media_item->offline) {
            const auto media_index = static_cast<int>(
                std::distance(media_items_.begin(), media_item));
            {
                const QSignalBlocker blocker(media_list_);
                media_list_->setCurrentRow(media_index);
            }
            updateMediaDetails(media_index);
            activateTimelineClipAt(
                *active_timeline_track_index_cache_,
                *active_timeline_clip_index_cache_, 0, false);
        } else if (source_clip.kind == timeline::ClipKind::Text &&
                   playback_controller_ != nullptr) {
            playback_controller_->renderCompositionFrame(
                timelinePlayheadFrame(), playback_frame_index_);
        }
        statusBar()->showMessage("Clip split at the playhead.");
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "timeline",
            "split_clip",
            error.what(),
            {{"track_id", std::to_string(source_clip.track_id)},
             {"clip_id", std::to_string(clip_id)},
             {"local_frame", std::to_string(local_frame)}});
        statusBar()->showMessage("Could not split the timeline clip.");
    }
}

void MainWindow::handleTimelineClipTrim(
    timeline::ClipId clip_id,
    qint64 edge_value,
    qint64 boundary_frame,
    qint64 mode_value) {
    const auto location = timeline_model_.locateClip(clip_id);
    if (!location.has_value()) return;
    if (edge_value != static_cast<qint64>(timeline::ClipEdge::Left) &&
        edge_value != static_cast<qint64>(timeline::ClipEdge::Right)) {
        return;
    }
    if (mode_value != static_cast<qint64>(timeline::ClipEdgeEditMode::Rolling) &&
        mode_value != static_cast<qint64>(timeline::ClipEdgeEditMode::Individual)) {
        return;
    }

    const auto edge = static_cast<timeline::ClipEdge>(edge_value);
    const auto mode = static_cast<timeline::ClipEdgeEditMode>(mode_value);
    try {
        const auto result = executeTimelineCommand(application::TrimClipEdgeCommand{
            clip_id,
            edge,
            boundary_frame,
            mode,
            timelinePlayheadFrame(),
            playback_frame_index_});
        if (result.status == application::EditStatus::NoChange) return;
        if (!result.changed()) {
            statusBar()->showMessage("The clip edge cannot move any farther.");
            return;
        }
        applyTimelineEditResult(result);
        if (!active_timeline_track_index_cache_.has_value() ||
            !active_timeline_clip_index_cache_.has_value()) return;
        const auto edited_location = timeline::ClipLocation{
            *active_timeline_track_index_cache_, *active_timeline_clip_index_cache_};
        const auto& edited_clip = timeline_model_.tracks()[edited_location.track_index]
            .clips[edited_location.clip_index];
        updateTimelineState();
        updatePlaybackControls();
        updatePlaybackStatus();
        if (edited_clip.kind == timeline::ClipKind::Text ||
            edited_clip.kind == timeline::ClipKind::Image) {
            activateTimelineClipAt(
                edited_location.track_index,
                edited_location.clip_index,
                playback_frame_index_,
                false,
                true);
        } else {
            const auto media_item = std::find_if(
                media_items_.begin(),
                media_items_.end(),
                [&edited_clip](const ImportedMedia& item) {
                    return normalizedPath(item.metadata.source_path) ==
                        normalizedPath(edited_clip.source_path);
                });
            if (media_item != media_items_.end() && !media_item->offline) {
                activateTimelineClipAt(
                    edited_location.track_index,
                    edited_location.clip_index,
                    playback_frame_index_,
                    false,
                    true);
            } else {
                refreshPlaybackComposition();
            }
        }
        statusBar()->showMessage("Timeline clip edge adjusted.");
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "timeline",
            "trim_clip_edge",
            error.what(),
            {{"track_id", std::to_string(timeline_model_.tracks()[location->track_index].track_id)},
             {"clip_id", std::to_string(clip_id)},
             {"edge", std::to_string(edge_value)},
             {"boundary_frame", std::to_string(boundary_frame)},
             {"mode", std::to_string(mode_value)}});
        statusBar()->showMessage("Could not adjust the timeline clip edge.");
    }
}

void MainWindow::handleTimelineClipSelected(timeline::ClipId clip_id) {
    const auto location = timeline_model_.locateClip(clip_id);
    if (!location.has_value() || location->track_index != 0) return;
    const auto clip_index = location->clip_index;

    const auto previous_playhead = timelinePlayheadFrame();
    const bool move_playhead =
        move_playhead_on_clip_selection_action_ != nullptr &&
        move_playhead_on_clip_selection_action_->isChecked();
    const bool had_pending_activation = playback_activation_loading_;
    if (had_pending_activation) {
        playback_controller_->invalidate(true);
        playback_is_playing_ = false;
    }
    setActiveTimelineSelection(*location);

    const auto& clip = timeline_model_.tracks()[0].clips[clip_index];
    const auto media_item = std::find_if(
        media_items_.begin(),
        media_items_.end(),
        [&clip](const ImportedMedia& item) {
            return item.metadata.source_path == clip.source_path;
        });
    if (media_item == media_items_.end()) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "timeline",
            "select_clip",
            "The selected timeline clip has no matching imported media item.",
            {{"path", pathToUtf8(clip.source_path)},
             {"clip_id", std::to_string(clip_id)}});
        statusBar()->showMessage("Could not select the timeline clip.");
        return;
    }

    const auto media_index = static_cast<std::size_t>(
        std::distance(media_items_.begin(), media_item));
    populateMediaBrowser(clip.source_path);
    const bool selected_after_filter = selectedMediaIndex().has_value() &&
        *selectedMediaIndex() == media_index;
    if (!selected_after_filter && had_pending_activation) updateMediaDetails(-1);

    const auto& selected_clip =
        timeline_model_.tracks()[0].clips[clip_index];
    const auto selected_local_frame = move_playhead
        ? std::int64_t{0}
        : localFrameAtTimelinePlayhead(selected_clip, previous_playhead);
    if (move_playhead) {
        preserved_timeline_playhead_frame_.reset();
    } else if (selected_clip.timeline_start_frame + selected_local_frame !=
               previous_playhead) {
        preserved_timeline_playhead_frame_ = previous_playhead;
    } else {
        preserved_timeline_playhead_frame_.reset();
    }
    playback_frame_index_ = selected_local_frame;
    if (!media_item->offline) {
        preview_widget_->setFrame(media_item->first_frame);
    }
    updateTimelineState();
    updatePlaybackControls();
    updatePlaybackStatus();
    if (!media_item->offline) {
        activateTimelineClipAt(
            0,
            clip_index,
            selected_local_frame,
            false,
            !move_playhead);
    }
    statusBar()->showMessage("Timeline clip selected.");
}

void MainWindow::clearTimeline() {
    if (!timeline_model_.hasClip()) return;

    try {
        const auto result = executeTimelineCommand(application::ClearTimelineCommand{});
        if (!result.changed()) return;
        applyTimelineEditResult(result);
        refreshPlaybackComposition();
        const int selected_row = media_list_ != nullptr ? media_list_->currentRow() : -1;
        if (selected_row >= 0) {
            updateMediaDetails(selected_row);
        } else {
            updateTimelineState();
            updatePlaybackControls();
            updatePlaybackStatus();
        }
        statusBar()->showMessage("Timeline cleared.");
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "timeline",
            "clear_timeline",
            error.what(),
            {});
        updateTimelineState();
        updatePlaybackControls();
        updatePlaybackStatus();
        statusBar()->showMessage("Could not clear the timeline.");
        QMessageBox::warning(
            this,
            "Timeline error",
            "The timeline could not be cleared.");
    }
}

void MainWindow::undoTimelineEdit() {
    if (!timeline_command_service_.canUndo()) return;
    try {
        synchronizeTimelineSessionSelection();
        const auto result = timeline_command_service_.undo();
        if (!result.changed()) return;
        applyTimelineEditResult(result);
        updateTimelineState();
        updatePlaybackControls();
        updatePlaybackStatus();
        refreshPlaybackComposition();
        statusBar()->showMessage("Timeline edit undone.");
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "timeline",
            "undo",
            error.what(),
            {});
        updateHistoryActions();
        statusBar()->showMessage("Could not undo the timeline edit.");
    }
}

void MainWindow::redoTimelineEdit() {
    if (!timeline_command_service_.canRedo()) return;
    try {
        synchronizeTimelineSessionSelection();
        const auto result = timeline_command_service_.redo();
        if (!result.changed()) return;
        applyTimelineEditResult(result);
        updateTimelineState();
        updatePlaybackControls();
        updatePlaybackStatus();
        refreshPlaybackComposition();
        statusBar()->showMessage("Timeline edit redone.");
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "timeline",
            "redo",
            error.what(),
            {});
        updateHistoryActions();
        statusBar()->showMessage("Could not redo the timeline edit.");
    }
}

void MainWindow::moveActiveTimelineClip(int direction) {
    if (!active_timeline_track_index_cache_.has_value() ||
        !active_timeline_clip_index_cache_.has_value() ||
        *active_timeline_track_index_cache_ >= timeline_model_.trackCount() ||
        *active_timeline_clip_index_cache_ >= timeline_model_.clipCount(
            *active_timeline_track_index_cache_)) {
        return;
    }
    if (direction == 0) return;
    const auto track = *active_timeline_track_index_cache_;
    const auto clip = *active_timeline_clip_index_cache_;
    const auto& active_clip = timeline_model_.tracks()[track].clips[clip];
    if (direction < 0 && active_clip.timeline_start_frame == 0) return;
    const auto new_start = active_clip.timeline_start_frame + direction;
    try {
        const auto result = executeTimelineCommand(application::MoveClipCommand{
            active_clip.clip_id, active_clip.track_id, new_start});
        if (!result.changed()) {
            if (result.status == application::EditStatus::NoChange) return;
            statusBar()->showMessage("The clip cannot move to that frame.");
            return;
        }
        applyTimelineEditResult(result);
        refreshPlaybackComposition();
        updateTimelineState();
        updateProjectDirtyState();
        statusBar()->showMessage("Timeline clip moved by one frame.");
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "timeline",
            "move_clip",
            error.what(),
            {{"track_index", std::to_string(track)},
             {"clip_index", std::to_string(clip)},
             {"timeline_frame", std::to_string(new_start)}});
        statusBar()->showMessage("Could not move the timeline clip.");
    }
}

void MainWindow::deleteActiveTimelineClip() {
    if (!active_timeline_track_index_cache_.has_value() ||
        !active_timeline_clip_index_cache_.has_value() ||
        *active_timeline_track_index_cache_ >= timeline_model_.trackCount() ||
        *active_timeline_clip_index_cache_ >= timeline_model_.clipCount(
            *active_timeline_track_index_cache_) ||
        playback_activation_loading_) {
        return;
    }

    const auto track_index = *active_timeline_track_index_cache_;
    const auto clip_index = *active_timeline_clip_index_cache_;
    const auto clip = timeline_model_.tracks()[track_index].clips[clip_index];
    if (timeline::isMediaClipKind(clip.kind) && !canPlaybackSelectedMedia()) return;

    try {
        const auto result = executeTimelineCommand(
            application::DeleteClipCommand{clip.clip_id});
        if (!result.changed()) return;
        applyTimelineEditResult(result);
        if (!timeline_model_.hasClip()) {
            refreshPlaybackComposition();
            updateTimelineState();
            updatePlaybackControls();
            updatePlaybackStatus();
            statusBar()->showMessage("Timeline clip deleted.");
            return;
        }
        refreshPlaybackComposition();
        updateTimelineState();
        updatePlaybackControls();
        updatePlaybackStatus();
        if (active_timeline_track_index_cache_.has_value() &&
            active_timeline_clip_index_cache_.has_value() &&
            timeline::isMediaClipKind(timeline_model_.tracks()[
                *active_timeline_track_index_cache_].clips[
                *active_timeline_clip_index_cache_].kind)) {
            activateTimelineClipAt(*active_timeline_track_index_cache_,
                                   *active_timeline_clip_index_cache_, 0, false);
        } else {
            refreshPlaybackComposition();
            updateTimelineState();
            updatePlaybackControls();
            updatePlaybackStatus();
        }
        statusBar()->showMessage("Timeline clip deleted.");
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "timeline",
            "delete_clip",
            error.what(),
            {{"path", pathToUtf8(clip.source_path)},
             {"clip_index", std::to_string(clip_index)},
             {"source_start_frame", std::to_string(clip.source_start_frame)},
             {"frame_count", std::to_string(clip.timeline_duration_frames)}});
        playback_is_playing_ = false;
        updateTimelineState();
        updatePlaybackControls();
        updatePlaybackStatus();
        statusBar()->showMessage("Could not delete the timeline clip.");
        QMessageBox::warning(
            this,
            "Timeline error",
            "The timeline clip could not be deleted.");
    }
}

void MainWindow::splitActiveClipAtPlayhead() {
    if (!active_timeline_clip_index_cache_.has_value() ||
        !active_timeline_track_index_cache_.has_value() ||
        *active_timeline_track_index_cache_ >= timeline_model_.trackCount() ||
        *active_timeline_clip_index_cache_ >= timeline_model_.clipCount(
            *active_timeline_track_index_cache_) ||
        playback_activation_loading_) {
        return;
    }

    const auto& active_clip = timeline_model_.tracks()
        [*active_timeline_track_index_cache_].clips[*active_timeline_clip_index_cache_];
    if (timeline::isMediaClipKind(active_clip.kind) &&
        !canPlaybackSelectedMedia()) {
        return;
    }

    if (active_timeline_clip_id_.has_value()) {
        handleTimelineClipSplit(
            *active_timeline_clip_id_, playback_frame_index_);
    }
}

void MainWindow::handleTimelineTrimStarted() {
    if (!timeline_model_.hasClip()) return;
    if (playback_controller_ != nullptr) {
        playback_controller_->invalidate(true);
    }
    playback_is_playing_ = false;
    updatePlaybackControls();
    updatePlaybackStatus();
}
