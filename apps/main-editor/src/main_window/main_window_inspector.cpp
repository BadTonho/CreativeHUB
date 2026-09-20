#include "main_window.h"
#include "main_window_support.h"

#include "logging/logger.h"
#include "preview_widget.h"
#include "project/project_file.h"
#include "timeline/timeline_widget.h"
#include "ui/media_browser_list_widget.h"

#include <QAction>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QDesktopServices>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
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
#include <QPushButton>
#include <QSignalBlocker>
#include <QScrollArea>
#include <QSlider>
#include <QStatusBar>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>

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

QWidget* MainWindow::createInspector() {
    auto* container = new QWidget(this);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto* title = new QLabel("Transform", container);
    title->setStyleSheet("font-weight: 600; font-size: 14px;");
    layout->addWidget(title);

    auto* description = new QLabel(
        "Values are normalized to the 1920×1080 project canvas.", container);
    description->setWordWrap(true);
    description->setStyleSheet("color: #9aa4b2;");
    layout->addWidget(description);

    auto* form = new QFormLayout;
    form->setHorizontalSpacing(8);
    form->setVerticalSpacing(6);
    const std::array<QString, 5> labels{
        "Position X", "Position Y", "Scale", "Rotation", "Opacity"};
    const std::array<double, 5> minimums{-10.0, -10.0, 0.01, -3600.0, 0.0};
    const std::array<double, 5> maximums{10.0, 10.0, 20.0, 3600.0, 1.0};
    const std::array<double, 5> steps{0.01, 0.01, 0.01, 1.0, 0.01};
    for (int index = 0; index < 5; ++index) {
        auto* row = new QWidget(container);
        auto* row_layout = new QHBoxLayout(row);
        row_layout->setContentsMargins(0, 0, 0, 0);
        row_layout->setSpacing(4);
        auto* spin = new QDoubleSpinBox(row);
        spin->setRange(minimums[index], maximums[index]);
        spin->setSingleStep(steps[index]);
        spin->setDecimals(index == 3 ? 1 : 3);
        spin->setEnabled(false);
        transform_spin_boxes_[static_cast<std::size_t>(index)] = spin;
        auto* key = new QPushButton("◇", row);
        key->setCheckable(true);
        key->setEnabled(false);
        key->setFixedWidth(30);
        key->setStyleSheet(
            "QPushButton { font-size: 16px; font-weight: 600; padding: 0px; }"
            "QPushButton:checked { color: #171a20; background: #e8b94f; }");
        key->setToolTip("Add keyframe at the current frame");
        transform_key_buttons_[static_cast<std::size_t>(index)] = key;
        row_layout->addWidget(spin, 1);
        row_layout->addWidget(key);
        form->addRow(labels[index], row);

        connect(spin, &QDoubleSpinBox::editingFinished, this, [this, index]() {
            if (transform_spin_boxes_[static_cast<std::size_t>(index)] != nullptr) {
                applyTransformProperty(
                    index,
                    transform_spin_boxes_[static_cast<std::size_t>(index)]->value());
            }
        });
        connect(key, &QPushButton::clicked, this, [this, index]() {
            toggleTransformKeyframe(index);
        });
    }
    layout->addLayout(form);
    layout->addStretch();
    updateInspector();
    return container;
}
void MainWindow::updateInspector() {
    const auto location = selectedTimelineClipLocation();
    const bool enabled = location.has_value() &&
        location->track_index < timeline_model_.trackCount() &&
        location->clip_index < timeline_model_.clipCount(location->track_index);
    const std::array<QDoubleSpinBox*, 5> spins = transform_spin_boxes_;
    for (auto* spin : spins) if (spin != nullptr) spin->setEnabled(enabled);
    for (auto* button : transform_key_buttons_) {
        if (button != nullptr) button->setEnabled(enabled);
    }
    if (location.has_value() && enabled) {
        const auto& clip = timeline_model_.tracks()[location->track_index]
            .clips[location->clip_index];
        const auto evaluated = timeline::evaluateTransform(
            clip.transform,
            clip.keyframes,
            std::max<std::int64_t>(0, playback_frame_index_));
        const std::array<double, 5> values{
            evaluated.position_x,
            evaluated.position_y,
            evaluated.scale,
            evaluated.rotation_degrees,
            evaluated.opacity};
        for (std::size_t index = 0; index < spins.size(); ++index) {
            if (spins[index] != nullptr) {
                const QSignalBlocker blocker(spins[index]);
                spins[index]->setValue(values[index]);
            }
            if (transform_key_buttons_[index] != nullptr) {
                const auto property = static_cast<timeline::TransformProperty>(index);
                const auto& keys = timeline::keyframesFor(clip.keyframes, property);
                const auto current_frame = std::max<std::int64_t>(0, playback_frame_index_);
                const bool has_key = std::any_of(keys.begin(), keys.end(),
                    [current_frame](const auto& key) {
                        return key.frame == current_frame;
                    });
                const QSignalBlocker blocker(transform_key_buttons_[index]);
                transform_key_buttons_[index]->setChecked(has_key);
                transform_key_buttons_[index]->setText(has_key ? QStringLiteral("◆") : QStringLiteral("◇"));
                transform_key_buttons_[index]->setToolTip(
                    has_key ? QStringLiteral("Remove keyframe at the current frame")
                            : QStringLiteral("Add keyframe at the current frame"));
            }
        }
    } else {
        for (auto* button : transform_key_buttons_) {
            if (button != nullptr) {
                const QSignalBlocker blocker(button);
                button->setChecked(false);
                button->setText(QStringLiteral("◇"));
                button->setToolTip(QStringLiteral("Add keyframe at the current frame"));
            }
        }
    }
}

void MainWindow::applyTransformProperty(int property_index, double value) {
    if (property_index < 0 || property_index >= 5 ||
        !active_timeline_track_index_.has_value() ||
        !active_timeline_clip_index_.has_value()) {
        return;
    }
    const auto track_index = *active_timeline_track_index_;
    const auto clip_index = *active_timeline_clip_index_;
    if (track_index >= timeline_model_.trackCount() ||
        clip_index >= timeline_model_.clipCount(track_index)) return;

    const auto before = captureTimelineEditState();
    const auto property = static_cast<timeline::TransformProperty>(property_index);
    const auto local_frame = std::max<std::int64_t>(0, playback_frame_index_);
    timeline::TransformParameterResult result = timeline::TransformParameterResult::NoChange;
    pending_clip_activation_.reset();
    ++playback_generation_;
    playback_is_playing_ = false;
    if (playback_worker_ != nullptr) {
        QMetaObject::invokeMethod(playback_worker_, "pause", Qt::QueuedConnection);
    }

    const auto& clip = timeline_model_.tracks()[track_index].clips[clip_index];
    const auto& keys = timeline::keyframesFor(clip.keyframes, property);
    const bool has_key_at_frame = std::any_of(keys.begin(), keys.end(),
        [local_frame](const auto& key) {
            return key.frame == local_frame;
        });
    if (has_key_at_frame) {
        result = timeline_model_.setClipKeyframe(
            track_index, clip_index, property, local_frame, value);
    } else {
        auto transform = timeline_model_.tracks()[track_index].clips[clip_index].transform;
        switch (property) {
        case timeline::TransformProperty::PositionX: transform.position_x = value; break;
        case timeline::TransformProperty::PositionY: transform.position_y = value; break;
        case timeline::TransformProperty::Scale: transform.scale = value; break;
        case timeline::TransformProperty::Rotation: transform.rotation_degrees = value; break;
        case timeline::TransformProperty::Opacity: transform.opacity = value; break;
        }
        result = timeline_model_.setClipTransform(track_index, clip_index, transform);
    }
    if (result != timeline::TransformParameterResult::Changed) {
        return;
    }
    recordTimelineEdit(before);
    updateProjectDirtyState();
    updateTimelineState();
    sendCompositionToWorker();
    if (playback_worker_ != nullptr && canPlaybackSelectedMedia()) {
        playback_worker_->requestSeek(local_frame, playback_generation_);
    }
    statusBar()->showMessage("Transform updated.");
}

void MainWindow::toggleTransformKeyframe(int property_index) {
    if (property_index < 0 || property_index >= 5 ||
        !active_timeline_track_index_.has_value() ||
        !active_timeline_clip_index_.has_value()) return;
    const auto track_index = *active_timeline_track_index_;
    const auto clip_index = *active_timeline_clip_index_;
    if (track_index >= timeline_model_.trackCount() ||
        clip_index >= timeline_model_.clipCount(track_index)) return;
    const auto& clip = timeline_model_.tracks()[track_index].clips[clip_index];
    const auto frame = std::clamp<std::int64_t>(
        playback_frame_index_, 0, std::max<std::int64_t>(0, clip.timeline_duration_frames - 1));
    const auto property = static_cast<timeline::TransformProperty>(property_index);
    const auto& keys = timeline::keyframesFor(clip.keyframes, property);
    const bool has_key = std::any_of(keys.begin(), keys.end(),
        [frame](const auto& key) {
            return key.frame == frame;
        });
    const auto before = captureTimelineEditState();
    pending_clip_activation_.reset();
    ++playback_generation_;
    playback_is_playing_ = false;
    if (playback_worker_ != nullptr) {
        QMetaObject::invokeMethod(playback_worker_, "pause", Qt::QueuedConnection);
    }
    timeline::TransformParameterResult result = timeline::TransformParameterResult::NoChange;
    if (has_key) {
        result = timeline_model_.removeClipKeyframe(
            track_index, clip_index, property, frame);
    } else {
        const auto evaluated = timeline::evaluateTransform(
            clip.transform, clip.keyframes, frame);
        const std::array<double, 5> values{
            evaluated.position_x, evaluated.position_y, evaluated.scale,
            evaluated.rotation_degrees, evaluated.opacity};
        result = timeline_model_.setClipKeyframe(
            track_index, clip_index, property, frame,
            values[static_cast<std::size_t>(property_index)]);
    }
    if (result == timeline::TransformParameterResult::Changed) {
        recordTimelineEdit(before);
        updateTimelineState();
        updateProjectDirtyState();
        sendCompositionToWorker();
        if (playback_worker_ != nullptr && canPlaybackSelectedMedia()) {
            playback_worker_->requestSeek(frame, playback_generation_);
        }
        statusBar()->showMessage(has_key ? "Keyframe removed." : "Keyframe added.");
    } else {
        updateInspector();
    }
}
