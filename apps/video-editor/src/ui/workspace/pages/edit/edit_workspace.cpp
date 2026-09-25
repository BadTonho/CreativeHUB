#include "ui/workspace/pages/edit/edit_workspace.h"
#include "settings/user_preferences.h"
#include "timeline/timeline_track_header_overlay.h"
#include "timeline/timeline_zoom.h"
#include "timeline/timeline_widget.h"
#include "ui/system/system_memory_indicator.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFontComboBox>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QSettings>
#include <QStyle>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <iterator>

namespace ui {

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


int timelineZoomLevelIndex(double factor) {
    const auto match = std::min_element(
        timeline::kTimelineZoomLevels.begin(), timeline::kTimelineZoomLevels.end(),
        [factor](double left, double right) {
            return std::abs(left - factor) < std::abs(right - factor);
        });
    return static_cast<int>(std::distance(
        timeline::kTimelineZoomLevels.begin(), match));
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


}

EditWorkspace::EditWorkspace(
    application::EditorSession& session,
    application::TimelineCommandService& command_service,
    QWidget* preview_widget,
    QObject* parent)
    : QObject(parent),
      preview_widget_(preview_widget),
      controller_(new EditWorkspaceController(session, command_service, this)) {}

void EditWorkspace::createPanels(QWidget* parent) {
    inspector_panel_ = createInspector(parent);
    timeline_panel_ = createTimeline(parent);
    ui_.inspector_panel = inspector_panel_;
    ui_.timeline_panel = timeline_panel_;
    controller_->setUi(ui_);
    controller_->updateTimelineState();
    controller_->updateInspector();
}

QWidget* EditWorkspace::createInspector(QWidget* parent) {
    auto* edit_controller = controller_;
    auto* container = new QWidget(parent);
    auto* outer_layout = new QVBoxLayout(container);
    outer_layout->setContentsMargins(0, 0, 0, 0);
    outer_layout->setSpacing(0);

    ui_.inspector_tabs = new QTabWidget(container);
    auto* inspector_page = new QWidget(ui_.inspector_tabs);
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
        ui_.transform_spins[static_cast<std::size_t>(index)] = spin;
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
        ui_.transform_sliders[static_cast<std::size_t>(index)] = slider;
        key->setEnabled(false);
        key->setFixedWidth(30);
        key->setStyleSheet(
            "QPushButton { font-size: 16px; font-weight: 600; padding: 0px; }"
            "QPushButton:checked { color: #171a20; background: #e8b94f; }");
        key->setToolTip("Add keyframe at the current frame");
        ui_.transform_keys[static_cast<std::size_t>(index)] = key;
        row_layout->addWidget(slider, 1);
        row_layout->addWidget(spin);
        row_layout->addWidget(key);
        form->addRow(labels[index], row);

        connect(spin, &QDoubleSpinBox::editingFinished, edit_controller,
                [edit_controller, spin, index]() {
            edit_controller->applyTransformProperty(index, spin->value());
        });
        connect(slider, &QSlider::sliderPressed,
                edit_controller, &ui::EditWorkspaceController::beginTransformEdit);
        connect(slider, &QSlider::valueChanged, edit_controller,
                [edit_controller, index, minimum = transform_minimums[index],
                 maximum = transform_maximums[index]](int value) {
                    edit_controller->applyTransformProperty(
                        index,
                        transformValueFromSlider(value, minimum, maximum));
                });
        connect(slider, &QSlider::sliderReleased,
                edit_controller, &ui::EditWorkspaceController::finishTransformEdit);
        connect(key, &QPushButton::clicked, edit_controller, [edit_controller, index]() {
            edit_controller->toggleTransformKeyframe(index);
        });
    }
    layout->addLayout(form);

    ui_.text_controls = new QWidget(inspector_page);
    auto* text_layout = new QVBoxLayout(ui_.text_controls);
    text_layout->setContentsMargins(0, 8, 0, 0);
    text_layout->setSpacing(6);
    auto* text_title = new QLabel("Text", ui_.text_controls);
    text_title->setStyleSheet("font-weight: 600;");
    text_layout->addWidget(text_title);
    ui_.text_content = new QPlainTextEdit(ui_.text_controls);
    ui_.text_content->setPlaceholderText("Text content");
    ui_.text_content->setFixedHeight(70);
    text_layout->addWidget(ui_.text_content);

    auto* text_form = new QFormLayout;
    ui_.text_font = new QFontComboBox(ui_.text_controls);
    ui_.text_font_size = new QSpinBox(ui_.text_controls);
    ui_.text_font_size->setRange(1, 512);
    ui_.text_font_size->setValue(48);
    ui_.text_color = new QPushButton("Color", ui_.text_controls);
    ui_.text_alignment = new QComboBox(ui_.text_controls);
    ui_.text_alignment->addItems({"Left", "Center", "Right"});
    text_form->addRow("Font", ui_.text_font);
    text_form->addRow("Size", ui_.text_font_size);
    text_form->addRow("Color", ui_.text_color);
    text_form->addRow("Alignment", ui_.text_alignment);
    text_layout->addLayout(text_form);
    ui_.apply_text = new QPushButton("Apply Text", ui_.text_controls);
    text_layout->addWidget(ui_.apply_text);
    layout->addWidget(ui_.text_controls);

    ui_.transition_controls = new QWidget(inspector_page);
    auto* transition_layout = new QVBoxLayout(ui_.transition_controls);
    transition_layout->setContentsMargins(0, 8, 0, 0);
    transition_layout->setSpacing(6);
    auto* transition_title = new QLabel("Transition", ui_.transition_controls);
    transition_title->setStyleSheet("font-weight: 600;");
    transition_layout->addWidget(transition_title);
    auto* transition_form = new QFormLayout;
    ui_.transition_type = new QComboBox(ui_.transition_controls);
    ui_.transition_type->addItem("Cross Dissolve", 0);
    ui_.transition_type->addItem("Fade to Black", 1);
    ui_.transition_duration = new QSpinBox(ui_.transition_controls);
    ui_.transition_duration->setRange(1, 1);
    ui_.transition_duration->setSuffix(" frames");
    transition_form->addRow("Type", ui_.transition_type);
    transition_form->addRow("Duration", ui_.transition_duration);
    transition_layout->addLayout(transition_form);
    ui_.apply_transition = new QPushButton(
        "Apply Transition", ui_.transition_controls);
    ui_.remove_transition = new QPushButton(
        "Remove Transition", ui_.transition_controls);
    transition_layout->addWidget(ui_.apply_transition);
    transition_layout->addWidget(ui_.remove_transition);
    ui_.transition_controls->setVisible(false);
    layout->addWidget(ui_.transition_controls);

    auto* audio_page = new QWidget(ui_.inspector_tabs);
    auto* audio_layout = new QVBoxLayout(audio_page);
    audio_layout->setContentsMargins(12, 12, 12, 12);
    audio_layout->setSpacing(8);

    auto* clip_audio_group = new QGroupBox("Clip", audio_page);
    auto* clip_audio_layout = new QFormLayout(clip_audio_group);
    ui_.clip_volume = new QSlider(Qt::Horizontal, clip_audio_group);
    ui_.clip_volume->setRange(0, 200);
    ui_.clip_volume->setValue(100);
    ui_.clip_volume->setToolTip("Active clip volume (0% to 200%)");
    ui_.clip_volume->setEnabled(false);
    ui_.clip_mute = new QCheckBox("Mute clip", clip_audio_group);
    ui_.clip_mute->setEnabled(false);
    clip_audio_layout->addRow("Volume", ui_.clip_volume);
    clip_audio_layout->addRow("Mute", ui_.clip_mute);
    audio_layout->addWidget(clip_audio_group);

    auto* track_audio_group = new QGroupBox("Track", audio_page);
    auto* track_audio_layout = new QFormLayout(track_audio_group);
    ui_.track_volume = new QSlider(Qt::Horizontal, track_audio_group);
    ui_.track_volume->setRange(0, 200);
    ui_.track_volume->setValue(100);
    ui_.track_volume->setToolTip("Active track volume (0% to 200%)");
    ui_.track_volume->setEnabled(false);
    ui_.track_mute = new QCheckBox("Mute track", track_audio_group);
    ui_.track_mute->setEnabled(false);
    track_audio_layout->addRow("Volume", ui_.track_volume);
    track_audio_layout->addRow("Mute", ui_.track_mute);
    audio_layout->addWidget(track_audio_group);
    audio_layout->addStretch();

    ui_.inspector_tabs->addTab(inspector_page, "Inspector");
    ui_.inspector_tabs->addTab(audio_page, "Audio");
    outer_layout->addWidget(ui_.inspector_tabs);

    QSettings settings;
    const auto saved_tab = settings.value("inspector/active_tab", 0).toInt();
    ui_.inspector_tabs->setCurrentIndex(std::clamp(
        saved_tab, 0, ui_.inspector_tabs->count() - 1));
    connect(ui_.inspector_tabs, &QTabWidget::currentChanged, this, [](int index) {
        QSettings tab_settings;
        tab_settings.setValue("inspector/active_tab", index);
    });

    connect(ui_.text_color, &QPushButton::clicked,
            edit_controller, &ui::EditWorkspaceController::chooseTextColor);
    connect(ui_.apply_text, &QPushButton::clicked,
            edit_controller, &ui::EditWorkspaceController::applyTextStyle);
    connect(ui_.clip_volume, &QSlider::sliderPressed,
            edit_controller, &ui::EditWorkspaceController::beginAudioEdit);
    connect(ui_.clip_volume, &QSlider::valueChanged, this,
            [edit_controller](int) { edit_controller->applyClipAudioControls(); });
    connect(ui_.clip_volume, &QSlider::sliderReleased,
            edit_controller, &ui::EditWorkspaceController::finishAudioEdit);
    connect(ui_.track_volume, &QSlider::sliderPressed,
            edit_controller, &ui::EditWorkspaceController::beginAudioEdit);
    connect(ui_.track_volume, &QSlider::valueChanged, this,
            [edit_controller](int) { edit_controller->applyTrackAudioControls(); });
    connect(ui_.track_volume, &QSlider::sliderReleased,
            edit_controller, &ui::EditWorkspaceController::finishAudioEdit);
    connect(ui_.clip_mute, &QCheckBox::toggled, edit_controller, [edit_controller](bool) {
        edit_controller->beginAudioEdit();
        edit_controller->applyClipAudioControls();
        edit_controller->finishAudioEdit();
    });
    connect(ui_.track_mute, &QCheckBox::toggled, edit_controller, [edit_controller](bool) {
        edit_controller->beginAudioEdit();
        edit_controller->applyTrackAudioControls();
        edit_controller->finishAudioEdit();
    });
    layout->addStretch();
    return container;
}


void EditWorkspace::createTimelineControls(
    QWidget* container,
    QVBoxLayout* layout) {
    ui_.timeline_controls = new QWidget(container);
    ui_.timeline_controls->setObjectName("timelineControlsContainer");
    auto* controls = new QHBoxLayout(ui_.timeline_controls);
    controls->setSpacing(6);

    auto* playback_label = new QLabel("Playback", container);
    playback_label->setStyleSheet("color: #9aa4b2; font-weight: 600;");
    controls->addWidget(playback_label);

    ui_.previous_frame = new QPushButton(container);
    ui_.play_pause = new QPushButton(container);
    ui_.next_frame = new QPushButton(container);
    ui_.previous_frame->setIcon(
        QApplication::style()->standardIcon(QStyle::SP_MediaSeekBackward));
    ui_.play_pause->setIcon(
        QApplication::style()->standardIcon(QStyle::SP_MediaPlay));
    ui_.next_frame->setIcon(
        QApplication::style()->standardIcon(QStyle::SP_MediaSeekForward));
    ui_.previous_frame->setIconSize(QSize(16, 16));
    ui_.play_pause->setIconSize(QSize(16, 16));
    ui_.next_frame->setIconSize(QSize(16, 16));
    ui_.previous_frame->setFixedSize(32, 28);
    ui_.play_pause->setFixedSize(32, 28);
    ui_.next_frame->setFixedSize(32, 28);
    ui_.clear_timeline = new QPushButton("Clear Timeline", container);
    ui_.selection_tool = new QPushButton(container);
    ui_.razor_tool = new QPushButton(container);
    ui_.snap = new QPushButton(container);
    ui_.selection_tool->setIcon(timelineToolIcon(false));
    ui_.razor_tool->setIcon(timelineToolIcon(true));
    ui_.snap->setIcon(timelineSnapIcon());
    ui_.selection_tool->setIconSize(QSize(16, 16));
    ui_.razor_tool->setIconSize(QSize(16, 16));
    ui_.snap->setIconSize(QSize(16, 16));
    ui_.selection_tool->setFixedSize(32, 28);
    ui_.razor_tool->setFixedSize(32, 28);
    ui_.snap->setFixedSize(32, 28);
    ui_.selection_tool->setCheckable(true);
    ui_.razor_tool->setCheckable(true);
    ui_.snap->setCheckable(true);
    ui_.selection_tool->setAutoExclusive(true);
    ui_.razor_tool->setAutoExclusive(true);
    ui_.selection_tool->setChecked(true);
    controls->addWidget(ui_.previous_frame);
    controls->addWidget(ui_.play_pause);
    controls->addWidget(ui_.next_frame);
    controls->addSpacing(6);
    auto* monitor_volume_label = new QLabel("Volume", container);
    monitor_volume_label->setStyleSheet("color: #9aa4b2; font-weight: 600;");
    ui_.monitor_volume = new QSlider(Qt::Horizontal, container);
    ui_.monitor_volume_indicator = new QLabel(container);
    ui_.monitor_volume->setObjectName("monitorVolumeSlider");
    ui_.monitor_volume_indicator->setObjectName("monitorVolumeIndicator");
    ui_.monitor_volume->setRange(
        settings::kMinimumMonitorVolumePercent,
        settings::kMaximumMonitorVolumePercent);
    ui_.monitor_volume->setSingleStep(5);
    ui_.monitor_volume->setPageStep(10);
    ui_.monitor_volume->setFixedWidth(96);
    ui_.monitor_volume->setToolTip(
        "Editor monitoring volume (0% to 200%)");
    ui_.monitor_volume->setAccessibleName("Monitor Volume");
    ui_.monitor_volume_indicator->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    ui_.monitor_volume_indicator->setMinimumWidth(42);
    ui_.monitor_volume_indicator->setStyleSheet("color: #9aa4b2;");
    controls->addWidget(monitor_volume_label);
    controls->addWidget(ui_.monitor_volume);
    controls->addWidget(ui_.monitor_volume_indicator);
    controls->addWidget(ui_.clear_timeline);
    controls->addWidget(ui_.selection_tool);
    controls->addWidget(ui_.razor_tool);
    controls->addWidget(ui_.snap);
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
    ui_.zoom_out = zoom_out_button;
    ui_.zoom_slider = zoom_slider;
    ui_.zoom_indicator = zoom_indicator;
    ui_.zoom_in = zoom_in_button;
    controls->addStretch();
    layout->addWidget(ui_.timeline_controls);

    ui_.previous_frame->setToolTip("Step one frame backward");
    ui_.play_pause->setToolTip("Play or pause the active clip");
    ui_.next_frame->setToolTip("Step one frame forward");
    ui_.previous_frame->setAccessibleName("Previous Frame");
    ui_.play_pause->setAccessibleName("Play or Pause");
    ui_.next_frame->setAccessibleName("Next Frame");
    monitor_volume_label->setToolTip(
        "Volume heard during editor playback; does not modify the project.");
    ui_.clear_timeline->setToolTip("Remove all clips from every track");
    ui_.selection_tool->setToolTip("Select and move timeline clips");
    ui_.selection_tool->setAccessibleName("Selection Tool");
    ui_.razor_tool->setToolTip("Split a clip where you click");
    ui_.razor_tool->setAccessibleName("Blade Tool");
    ui_.snap->setToolTip(
        "Toggle magnetic snapping for clips and media drops");
    ui_.snap->setAccessibleName("Magnetic Snap");
    add_track_button->setToolTip("Create a new empty video track");
    rename_track_button->setToolTip("Rename the active track");
    move_track_up_button->setToolTip("Move the active track toward the top");
    move_track_down_button->setToolTip("Move the active track toward the bottom");
    remove_track_button->setToolTip("Remove the active track when it is empty");

    auto* dialog_parent = container->window();
    connect(add_track_button, &QPushButton::clicked, controller_,
            [this, dialog_parent]() {
                controller_->promptAddVideoTrack(dialog_parent);
            });
    connect(rename_track_button, &QPushButton::clicked, controller_,
            [this, dialog_parent]() {
                controller_->promptRenameActiveTrack(dialog_parent);
            });
    connect(move_track_up_button, &QPushButton::clicked, controller_,
            [this]() { controller_->moveActiveTrack(-1); });
    connect(move_track_down_button, &QPushButton::clicked, controller_,
            [this]() { controller_->moveActiveTrack(1); });
    connect(remove_track_button, &QPushButton::clicked, controller_,
            &EditWorkspaceController::removeActiveTrack);

}



void EditWorkspace::createTimelineViewport(QWidget* container, QVBoxLayout* layout) {
    ui_.timeline = new timeline::TimelineWidget(container);
    controller_->setTimelineWidget(ui_.timeline);
    ui_.timeline_scroll = new QScrollArea(container);
    ui_.timeline_scroll->setWidgetResizable(true);
    ui_.timeline_scroll->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
    ui_.timeline_scroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    ui_.timeline_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    ui_.timeline_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    ui_.timeline_scroll->setAcceptDrops(true);
    ui_.timeline_scroll->viewport()->setAcceptDrops(true);
    ui_.timeline_scroll->setFrameShape(QFrame::NoFrame);
    ui_.timeline_scroll->setStyleSheet(
        "QScrollArea { background: transparent; border: none; }"
        "QScrollArea > QWidget > QWidget { background: transparent; }");
    ui_.timeline_scroll->setWidget(ui_.timeline);
    ui_.timeline->setAcceptDrops(false);
    ui_.timeline_scroll->viewport()->installEventFilter(ui_.timeline);
    ui_.timeline->setTimelineViewportWidth(ui_.timeline_scroll->viewport()->width());
    ui_.track_header = new timeline::TimelineTrackHeaderOverlay(
        ui_.timeline,
        ui_.timeline_scroll->viewport());
    ui_.track_header->setVerticalScrollOffset(
        ui_.timeline_scroll->verticalScrollBar()->value());
    connect(
        ui_.timeline_scroll->verticalScrollBar(),
        &QScrollBar::valueChanged,
        ui_.track_header,
        &timeline::TimelineTrackHeaderOverlay::setVerticalScrollOffset);
    layout->addWidget(ui_.timeline_scroll, 1);
}

void EditWorkspace::createTimelineFooter(QWidget* container, QVBoxLayout* layout) {
    // Keep the playback status as a compact footer while giving the timeline
    // the expandable space in the dock.
    ui_.timeline_footer = new QWidget(container);
    ui_.timeline_footer->setObjectName("timelinePlaybackFooter");
    auto* playback_footer_layout = new QHBoxLayout(ui_.timeline_footer);
    playback_footer_layout->setContentsMargins(0, 0, 0, 0);
    playback_footer_layout->setSpacing(12);

    ui_.playback_status = new QLabel("No media selected.", ui_.timeline_footer);
    ui_.playback_status->setStyleSheet("color: #9aa4b2;");
    ui_.playback_status->setSizePolicy(
        QSizePolicy::Preferred,
        QSizePolicy::Fixed);
    playback_footer_layout->addWidget(ui_.playback_status);

    ui_.timeline_message = new QLabel(ui_.timeline_footer);
    ui_.timeline_message->setStyleSheet("color: #9aa4b2;");
    ui_.timeline_message->setSizePolicy(
        QSizePolicy::Preferred,
        QSizePolicy::Fixed);
    playback_footer_layout->addWidget(ui_.timeline_message);
    playback_footer_layout->addStretch(1);
    ui_.system_memory_indicator = new SystemMemoryIndicator(ui_.timeline_footer);
    playback_footer_layout->addWidget(ui_.system_memory_indicator);
    ui_.timeline_footer->setFixedHeight(ui_.timeline_footer->sizeHint().height());
    layout->addWidget(ui_.timeline_footer);

    connect(
        controller_,
        &EditWorkspaceController::statusMessageRequested,
        this,
        [this](const QString& message) {
            ui_.timeline_message->setText(message);
            ui_.timeline_message->setVisible(!message.isEmpty());
        });
}

QWidget* EditWorkspace::createTimeline(QWidget* parent) {
    auto* container = new QWidget(parent);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    createTimelineControls(container, layout);
    ui_.monitor_volume->setValue(settings::monitorVolumePercent());
    createTimelineViewport(container, layout);
    ui_.snap->setChecked(ui_.timeline->snapEnabled());
    createTimelineFooter(container, layout);

    return container;
}


}  // namespace ui
