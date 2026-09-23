#include "main_window.h"
#include "main_window_support.h"

#include "logging/logger.h"
#include "preview_widget.h"
#include "project/project_file.h"
#include "timeline/timeline_widget.h"
#include "ui/media_browser_list_widget.h"

#include <QAction>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QDesktopServices>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFontComboBox>
#include <QGroupBox>
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
#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <QSettings>
#include <QSpinBox>
#include <QScrollArea>
#include <QSlider>
#include <QStatusBar>
#include <QTabWidget>
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

namespace {

constexpr int transform_slider_resolution = 1000;
constexpr std::array<double, 5> transform_minimums{
    -10.0, -10.0, 0.01, -3600.0, 0.0};
constexpr std::array<double, 5> transform_maximums{
    10.0, 10.0, 20.0, 3600.0, 1.0};
constexpr std::array<double, 5> transform_steps{
    0.01, 0.01, 0.01, 1.0, 0.01};

int transformSliderValue(double value, double minimum, double maximum) {
    if (maximum <= minimum) return 0;
    const auto fraction = std::clamp(
        (value - minimum) / (maximum - minimum), 0.0, 1.0);
    return static_cast<int>(std::lround(
        fraction * transform_slider_resolution));
}

double transformValueFromSlider(int slider_value, double minimum, double maximum) {
    if (maximum <= minimum) return minimum;
    const auto fraction = std::clamp(
        static_cast<double>(slider_value) /
            transform_slider_resolution,
        0.0,
        1.0);
    return minimum + fraction * (maximum - minimum);
}

} // namespace

QWidget* MainWindow::createInspector() {
    auto* container = new QWidget(this);
    auto* outer_layout = new QVBoxLayout(container);
    outer_layout->setContentsMargins(0, 0, 0, 0);
    outer_layout->setSpacing(0);

    inspector_tabs_ = new QTabWidget(container);
    auto* inspector_page = new QWidget(inspector_tabs_);
    auto* layout = new QVBoxLayout(inspector_page);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto* title = new QLabel("Transform", inspector_page);
    title->setStyleSheet("font-weight: 600; font-size: 14px;");
    layout->addWidget(title);

    auto* description = new QLabel(
        "Values are normalized to the 1920×1080 project canvas.", inspector_page);
    description->setWordWrap(true);
    description->setStyleSheet("color: #9aa4b2;");
    layout->addWidget(description);

    auto* form = new QFormLayout;
    form->setHorizontalSpacing(8);
    form->setVerticalSpacing(6);
    const std::array<QString, 5> labels{
        "Position X", "Position Y", "Scale", "Rotation", "Opacity"};
    for (int index = 0; index < 5; ++index) {
        auto* row = new QWidget(inspector_page);
        auto* row_layout = new QHBoxLayout(row);
        row_layout->setContentsMargins(0, 0, 0, 0);
        row_layout->setSpacing(4);
        auto* spin = new QDoubleSpinBox(row);
        spin->setRange(transform_minimums[index], transform_maximums[index]);
        spin->setSingleStep(transform_steps[index]);
        spin->setDecimals(index == 3 ? 1 : 3);
        spin->setEnabled(false);
        spin->setFixedWidth(82);
        transform_spin_boxes_[static_cast<std::size_t>(index)] = spin;
        auto* key = new QPushButton("◇", row);
        key->setCheckable(true);
        auto* slider = new QSlider(Qt::Horizontal, row);
        slider->setRange(0, transform_slider_resolution);
        slider->setValue(transformSliderValue(
            index == 2 ? 1.0 : index == 3 ? 0.0 : index == 4 ? 1.0 : 0.5,
            transform_minimums[index], transform_maximums[index]));
        slider->setSingleStep(1);
        slider->setPageStep(100);
        slider->setTracking(true);
        slider->setEnabled(false);
        slider->setToolTip(QString("Adjust %1").arg(labels[index]));
        slider->setStyleSheet(
            "QSlider::groove:horizontal { height: 4px; background: #303844; "
            "border-radius: 2px; }"
            "QSlider::sub-page:horizontal { height: 4px; background: #8b98aa; "
            "border-radius: 2px; }"
            "QSlider::add-page:horizontal { height: 4px; background: #252d38; "
            "border-radius: 2px; }"
            "QSlider::handle:horizontal { width: 10px; height: 10px; "
            "margin: -3px 0; border-radius: 5px; background: #d5a94b; }");
        transform_sliders_[static_cast<std::size_t>(index)] = slider;
        key->setEnabled(false);
        key->setFixedWidth(30);
        key->setStyleSheet(
            "QPushButton { font-size: 16px; font-weight: 600; padding: 0px; }"
            "QPushButton:checked { color: #171a20; background: #e8b94f; }");
        key->setToolTip("Add keyframe at the current frame");
        transform_key_buttons_[static_cast<std::size_t>(index)] = key;
        row_layout->addWidget(slider, 1);
        row_layout->addWidget(spin);
        row_layout->addWidget(key);
        form->addRow(labels[index], row);

        connect(spin, &QDoubleSpinBox::editingFinished, this, [this, index]() {
            if (transform_spin_boxes_[static_cast<std::size_t>(index)] != nullptr) {
                applyTransformProperty(
                    index,
                    transform_spin_boxes_[static_cast<std::size_t>(index)]->value());
            }
        });
        connect(slider, &QSlider::sliderPressed,
                this, &MainWindow::beginTransformEdit);
        connect(slider, &QSlider::valueChanged, this,
                [this, index, minimum = transform_minimums[index],
                 maximum = transform_maximums[index]](int value) {
                    applyTransformProperty(
                        index,
                        transformValueFromSlider(value, minimum, maximum));
                });
        connect(slider, &QSlider::sliderReleased,
                this, &MainWindow::finishTransformEdit);
        connect(key, &QPushButton::clicked, this, [this, index]() {
            toggleTransformKeyframe(index);
        });
    }
    layout->addLayout(form);

    text_controls_ = new QWidget(inspector_page);
    auto* text_layout = new QVBoxLayout(text_controls_);
    text_layout->setContentsMargins(0, 8, 0, 0);
    text_layout->setSpacing(6);
    auto* text_title = new QLabel("Text", text_controls_);
    text_title->setStyleSheet("font-weight: 600;");
    text_layout->addWidget(text_title);
    text_content_editor_ = new QPlainTextEdit(text_controls_);
    text_content_editor_->setPlaceholderText("Text content");
    text_content_editor_->setFixedHeight(70);
    text_layout->addWidget(text_content_editor_);

    auto* text_form = new QFormLayout;
    text_font_combo_ = new QFontComboBox(text_controls_);
    text_font_size_spin_ = new QSpinBox(text_controls_);
    text_font_size_spin_->setRange(1, 512);
    text_font_size_spin_->setValue(48);
    text_color_button_ = new QPushButton("Color", text_controls_);
    text_alignment_combo_ = new QComboBox(text_controls_);
    text_alignment_combo_->addItems({"Left", "Center", "Right"});
    text_form->addRow("Font", text_font_combo_);
    text_form->addRow("Size", text_font_size_spin_);
    text_form->addRow("Color", text_color_button_);
    text_form->addRow("Alignment", text_alignment_combo_);
    text_layout->addLayout(text_form);
    apply_text_button_ = new QPushButton("Apply Text", text_controls_);
    text_layout->addWidget(apply_text_button_);
    layout->addWidget(text_controls_);

    transition_controls_ = new QWidget(inspector_page);
    auto* transition_layout = new QVBoxLayout(transition_controls_);
    transition_layout->setContentsMargins(0, 8, 0, 0);
    transition_layout->setSpacing(6);
    auto* transition_title = new QLabel("Transition", transition_controls_);
    transition_title->setStyleSheet("font-weight: 600;");
    transition_layout->addWidget(transition_title);
    auto* transition_form = new QFormLayout;
    transition_type_combo_ = new QComboBox(transition_controls_);
    transition_type_combo_->addItem("Cross Dissolve", 0);
    transition_type_combo_->addItem("Fade to Black", 1);
    transition_duration_spin_ = new QSpinBox(transition_controls_);
    transition_duration_spin_->setRange(1, 1);
    transition_duration_spin_->setSuffix(" frames");
    transition_form->addRow("Type", transition_type_combo_);
    transition_form->addRow("Duration", transition_duration_spin_);
    transition_layout->addLayout(transition_form);
    apply_transition_button_ = new QPushButton(
        "Apply Transition", transition_controls_);
    remove_transition_button_ = new QPushButton(
        "Remove Transition", transition_controls_);
    transition_layout->addWidget(apply_transition_button_);
    transition_layout->addWidget(remove_transition_button_);
    transition_controls_->setVisible(false);
    layout->addWidget(transition_controls_);

    auto* audio_page = new QWidget(inspector_tabs_);
    auto* audio_layout = new QVBoxLayout(audio_page);
    audio_layout->setContentsMargins(12, 12, 12, 12);
    audio_layout->setSpacing(8);

    auto* clip_audio_group = new QGroupBox("Clip", audio_page);
    auto* clip_audio_layout = new QFormLayout(clip_audio_group);
    clip_volume_slider_ = new QSlider(Qt::Horizontal, clip_audio_group);
    clip_volume_slider_->setRange(0, 200);
    clip_volume_slider_->setValue(100);
    clip_volume_slider_->setToolTip("Active clip volume (0% to 200%)");
    clip_volume_slider_->setEnabled(false);
    clip_mute_check_ = new QCheckBox("Mute clip", clip_audio_group);
    clip_mute_check_->setEnabled(false);
    clip_audio_layout->addRow("Volume", clip_volume_slider_);
    clip_audio_layout->addRow("Mute", clip_mute_check_);
    audio_layout->addWidget(clip_audio_group);

    auto* track_audio_group = new QGroupBox("Track", audio_page);
    auto* track_audio_layout = new QFormLayout(track_audio_group);
    track_volume_slider_ = new QSlider(Qt::Horizontal, track_audio_group);
    track_volume_slider_->setRange(0, 200);
    track_volume_slider_->setValue(100);
    track_volume_slider_->setToolTip("Active track volume (0% to 200%)");
    track_volume_slider_->setEnabled(false);
    track_mute_check_ = new QCheckBox("Mute track", track_audio_group);
    track_mute_check_->setEnabled(false);
    track_audio_layout->addRow("Volume", track_volume_slider_);
    track_audio_layout->addRow("Mute", track_mute_check_);
    audio_layout->addWidget(track_audio_group);
    audio_layout->addStretch();

    inspector_tabs_->addTab(inspector_page, "Inspector");
    inspector_tabs_->addTab(audio_page, "Audio");
    outer_layout->addWidget(inspector_tabs_);

    QSettings settings;
    const auto saved_tab = settings.value("inspector/active_tab", 0).toInt();
    inspector_tabs_->setCurrentIndex(std::clamp(
        saved_tab, 0, inspector_tabs_->count() - 1));
    connect(inspector_tabs_, &QTabWidget::currentChanged, this, [](int index) {
        QSettings tab_settings;
        tab_settings.setValue("inspector/active_tab", index);
    });

    connect(text_color_button_, &QPushButton::clicked, this, [this]() {
        const QColor current(
            text_color_[0], text_color_[1], text_color_[2], text_color_[3]);
        const auto chosen = QColorDialog::getColor(
            current, this, "Text color", QColorDialog::ShowAlphaChannel);
        if (!chosen.isValid()) return;
        text_color_ = {
            static_cast<std::uint8_t>(chosen.red()),
            static_cast<std::uint8_t>(chosen.green()),
            static_cast<std::uint8_t>(chosen.blue()),
            static_cast<std::uint8_t>(chosen.alpha())};
        text_color_button_->setStyleSheet(
            QString("background-color: rgba(%1, %2, %3, %4);")
                .arg(chosen.red()).arg(chosen.green())
                .arg(chosen.blue()).arg(chosen.alpha()));
    });
    connect(apply_text_button_, &QPushButton::clicked,
            this, &MainWindow::applyTextStyle);
    connect(apply_transition_button_, &QPushButton::clicked,
            this, &MainWindow::applyTransitionSettings);
    connect(remove_transition_button_, &QPushButton::clicked,
            this, &MainWindow::removeSelectedTransition);
    connect(clip_volume_slider_, &QSlider::sliderPressed,
            this, &MainWindow::beginAudioEdit);
    connect(clip_volume_slider_, &QSlider::valueChanged, this,
            [this](int) { applyClipAudioControls(); });
    connect(clip_volume_slider_, &QSlider::sliderReleased,
            this, &MainWindow::finishAudioEdit);
    connect(track_volume_slider_, &QSlider::sliderPressed,
            this, &MainWindow::beginAudioEdit);
    connect(track_volume_slider_, &QSlider::valueChanged, this,
            [this](int) { applyTrackAudioControls(); });
    connect(track_volume_slider_, &QSlider::sliderReleased,
            this, &MainWindow::finishAudioEdit);
    connect(clip_mute_check_, &QCheckBox::toggled, this, [this](bool) {
        beginAudioEdit();
        applyClipAudioControls();
        finishAudioEdit();
    });
    connect(track_mute_check_, &QCheckBox::toggled, this, [this](bool) {
        beginAudioEdit();
        applyTrackAudioControls();
        finishAudioEdit();
    });
    layout->addStretch();
    updateInspector();
    return container;
}
void MainWindow::updateInspector() {
    bool transition_enabled = false;
    const timeline::TimelineTransition* selected_transition = nullptr;
    std::optional<std::size_t> transition_track_index;
    std::optional<std::size_t> transition_from_clip_index;
    std::optional<std::size_t> transition_to_clip_index;
    if (active_transition_.has_value()) {
        const auto& selection = *active_transition_;
        if (selection.track_id != 0) {
            const auto track_index = timeline_model_.locateTrack(selection.track_id);
            const auto from_location = timeline_model_.locateClip(selection.from_clip_id);
            const auto to_location = timeline_model_.locateClip(selection.to_clip_id);
            if (track_index.has_value() && from_location.has_value() &&
                to_location.has_value() &&
                from_location->track_index == *track_index &&
                to_location->track_index == *track_index) {
                transition_track_index = *track_index;
                transition_from_clip_index = from_location->clip_index;
                transition_to_clip_index = to_location->clip_index;
            }
        }
        if (transition_track_index.has_value() &&
            transition_from_clip_index.has_value() &&
            transition_to_clip_index.has_value()) {
            selected_transition = timeline_model_.transitionBetween(
                *transition_track_index,
                *transition_from_clip_index,
                *transition_to_clip_index);
            transition_enabled = selected_transition != nullptr;
        }
    }
    if (!transition_enabled) active_transition_.reset();

    const auto location = selectedTimelineClipLocation();
    const bool enabled = location.has_value() &&
        location->track_index < timeline_model_.trackCount() &&
        location->clip_index < timeline_model_.clipCount(location->track_index);
    const std::array<QDoubleSpinBox*, 5> spins = transform_spin_boxes_;
    const std::array<QSlider*, 5> sliders = transform_sliders_;
    for (auto* spin : spins) {
        if (spin != nullptr) spin->setEnabled(enabled && !transition_enabled);
    }
    for (auto* slider : sliders) {
        if (slider != nullptr) slider->setEnabled(enabled && !transition_enabled);
    }
    for (auto* button : transform_key_buttons_) {
        if (button != nullptr) button->setEnabled(enabled && !transition_enabled);
    }
    const bool text_enabled = enabled &&
        timeline_model_.tracks()[location->track_index]
            .clips[location->clip_index].kind == timeline::ClipKind::Text;
    if (text_controls_ != nullptr) {
        text_controls_->setVisible(text_enabled && !transition_enabled);
    }
    if (text_content_editor_ != nullptr) text_content_editor_->setEnabled(text_enabled);
    if (text_font_combo_ != nullptr) text_font_combo_->setEnabled(text_enabled);
    if (text_font_size_spin_ != nullptr) text_font_size_spin_->setEnabled(text_enabled);
    if (text_color_button_ != nullptr) text_color_button_->setEnabled(text_enabled);
    if (text_alignment_combo_ != nullptr) text_alignment_combo_->setEnabled(text_enabled);
    if (apply_text_button_ != nullptr) {
        apply_text_button_->setEnabled(text_enabled && !transition_enabled);
    }
    if (transition_controls_ != nullptr) {
        transition_controls_->setVisible(transition_enabled);
    }
    if (transition_type_combo_ != nullptr) {
        transition_type_combo_->setEnabled(transition_enabled);
    }
    if (transition_duration_spin_ != nullptr) {
        transition_duration_spin_->setEnabled(transition_enabled);
    }
    if (apply_transition_button_ != nullptr) {
        apply_transition_button_->setEnabled(transition_enabled);
    }
    if (remove_transition_button_ != nullptr) {
        remove_transition_button_->setEnabled(transition_enabled);
    }
    if (transition_enabled && selected_transition != nullptr) {
        for (auto* button : transform_key_buttons_) {
            if (button != nullptr) {
                const QSignalBlocker blocker(button);
                button->setChecked(false);
                button->setText(QStringLiteral("◇"));
                button->setToolTip(QStringLiteral("Select a clip to edit keyframes"));
            }
        }
        const auto& track = timeline_model_.tracks()[*transition_track_index];
        const auto& from = track.clips[*transition_from_clip_index];
        const auto& to = track.clips[*transition_to_clip_index];
        const auto maximum = std::min(
            from.timeline_duration_frames,
            to.timeline_duration_frames);
        if (transition_type_combo_ != nullptr) {
            const QSignalBlocker blocker(transition_type_combo_);
            transition_type_combo_->setCurrentIndex(
                selected_transition->kind == timeline::TransitionKind::FadeToBlack
                    ? 1
                    : 0);
        }
        if (transition_duration_spin_ != nullptr) {
            const QSignalBlocker blocker(transition_duration_spin_);
            transition_duration_spin_->setRange(
                1,
                static_cast<int>(std::min<std::int64_t>(
                    maximum, std::numeric_limits<int>::max())));
            transition_duration_spin_->setValue(
                static_cast<int>(selected_transition->duration_frames));
        }
        return;
    }
    if (location.has_value() && enabled) {
        const auto& clip = timeline_model_.tracks()[location->track_index]
            .clips[location->clip_index];
        if (clip.kind == timeline::ClipKind::Text) {
            if (text_content_editor_ != nullptr) {
                const QSignalBlocker blocker(text_content_editor_);
                text_content_editor_->setPlainText(QString::fromUtf8(
                    clip.text.content.data(),
                    static_cast<qsizetype>(clip.text.content.size())));
            }
            if (text_font_combo_ != nullptr) {
                const QSignalBlocker blocker(text_font_combo_);
                text_font_combo_->setCurrentFont(QFont(QString::fromUtf8(
                    clip.text.font_family.data(),
                    static_cast<qsizetype>(clip.text.font_family.size()))));
            }
            if (text_font_size_spin_ != nullptr) {
                const QSignalBlocker blocker(text_font_size_spin_);
                text_font_size_spin_->setValue(
                    static_cast<int>(std::lround(clip.text.font_size_pixels)));
            }
            text_color_ = clip.text.color;
            if (text_color_button_ != nullptr) {
                text_color_button_->setStyleSheet(
                    QString("background-color: rgba(%1, %2, %3, %4);")
                        .arg(text_color_[0]).arg(text_color_[1])
                        .arg(text_color_[2]).arg(text_color_[3]));
            }
            if (text_alignment_combo_ != nullptr) {
                const QSignalBlocker blocker(text_alignment_combo_);
                text_alignment_combo_->setCurrentIndex(
                    clip.text.alignment == timeline::TextAlignment::Left
                        ? 0
                        : clip.text.alignment == timeline::TextAlignment::Right
                            ? 2
                            : 1);
            }
        }
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
            if (sliders[index] != nullptr) {
                const QSignalBlocker blocker(sliders[index]);
                sliders[index]->setValue(transformSliderValue(
                    values[index],
                    transform_minimums[index],
                    transform_maximums[index]));
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

void MainWindow::applyTextStyle() {
    if (!active_timeline_track_index_cache_.has_value() ||
        !active_timeline_clip_index_cache_.has_value() ||
        text_content_editor_ == nullptr || text_font_combo_ == nullptr ||
        text_font_size_spin_ == nullptr || text_alignment_combo_ == nullptr) {
        return;
    }
    const auto track_index = *active_timeline_track_index_cache_;
    const auto clip_index = *active_timeline_clip_index_cache_;
    if (track_index >= timeline_model_.trackCount() ||
        clip_index >= timeline_model_.clipCount(track_index) ||
        timeline_model_.tracks()[track_index].clips[clip_index].kind !=
            timeline::ClipKind::Text) {
        return;
    }

    timeline::TextStyle text;
    text.content = text_content_editor_->toPlainText().toUtf8().toStdString();
    text.font_family = text_font_combo_->currentFont().family().toUtf8().toStdString();
    text.font_size_pixels = text_font_size_spin_->value();
    text.color = text_color_;
    text.alignment = text_alignment_combo_->currentIndex() == 0
        ? timeline::TextAlignment::Left
        : text_alignment_combo_->currentIndex() == 2
            ? timeline::TextAlignment::Right
            : timeline::TextAlignment::Center;

    const auto before = captureTimelineEditState();
    pending_clip_activation_.reset();
    ++playback_generation_;
    playback_is_playing_ = false;
    if (playback_worker_ != nullptr) {
        QMetaObject::invokeMethod(playback_worker_, "pause", Qt::QueuedConnection);
    }
    const auto result = timeline_model_.setClipText(track_index, clip_index, text);
    if (result != timeline::TextParameterResult::Changed) {
        updateInspector();
        return;
    }
    recordTimelineEdit(before);
    updateTimelineState();
    updateProjectDirtyState();
    sendCompositionToWorker();
    if (playback_worker_ != nullptr &&
        timeline_model_.tracks()[track_index].clips[clip_index].kind ==
            timeline::ClipKind::Text) {
        QMetaObject::invokeMethod(
            playback_worker_,
            "renderCompositionFrame",
            Qt::QueuedConnection,
            Q_ARG(qint64, static_cast<qint64>(timelinePlayheadFrame())),
            Q_ARG(qint64, static_cast<qint64>(playback_frame_index_)),
            Q_ARG(quint64, playback_generation_));
    } else if (playback_worker_ != nullptr && canPlaybackSelectedMedia()) {
        playback_worker_->requestSeek(playback_frame_index_, playback_generation_);
    }
    statusBar()->showMessage("Text style updated.");
}

void MainWindow::applyTransformProperty(int property_index, double value) {
    if (property_index < 0 || property_index >= 5 ||
        !active_timeline_track_index_cache_.has_value() ||
        !active_timeline_clip_index_cache_.has_value()) {
        return;
    }
    const auto track_index = *active_timeline_track_index_cache_;
    const auto clip_index = *active_timeline_clip_index_cache_;
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
    if (!pending_transform_edit_.has_value()) {
        recordTimelineEdit(before);
    }
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
        !active_timeline_track_index_cache_.has_value() ||
        !active_timeline_clip_index_cache_.has_value()) return;
    const auto track_index = *active_timeline_track_index_cache_;
    const auto clip_index = *active_timeline_clip_index_cache_;
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
