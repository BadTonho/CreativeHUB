#pragma once

#include <array>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFontComboBox;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QSlider;
class QSpinBox;
class QTabWidget;
class QWidget;
class SystemMemoryIndicator;

namespace timeline {
class TimelineTrackHeaderOverlay;
class TimelineWidget;
}

namespace ui {

// Non-owning handles to widgets built by EditWorkspace and shared with its
// controller and the application shell.
struct EditWorkspaceUi final {
    QWidget* inspector_panel = nullptr;
    QWidget* timeline_panel = nullptr;
    QWidget* timeline_controls = nullptr;
    QWidget* workspace_footer = nullptr;
    QWidget* workspace_navigation_slot = nullptr;
    timeline::TimelineWidget* timeline = nullptr;
    timeline::TimelineTrackHeaderOverlay* track_header = nullptr;
    QScrollArea* timeline_scroll = nullptr;
    QPushButton* previous_frame = nullptr;
    QPushButton* play_pause = nullptr;
    QPushButton* next_frame = nullptr;
    QPushButton* clear_timeline = nullptr;
    QPushButton* selection_tool = nullptr;
    QPushButton* razor_tool = nullptr;
    QPushButton* volume_tool = nullptr;
    QPushButton* snap = nullptr;
    QPushButton* zoom_out = nullptr;
    QSlider* zoom_slider = nullptr;
    QLabel* zoom_indicator = nullptr;
    QPushButton* zoom_in = nullptr;
    QSlider* monitor_volume = nullptr;
    QLabel* monitor_volume_indicator = nullptr;
    QLabel* playback_status = nullptr;
    QLabel* timeline_message = nullptr;
    SystemMemoryIndicator* system_memory_indicator = nullptr;
    QSlider* clip_volume = nullptr;
    QSlider* track_volume = nullptr;
    QCheckBox* clip_mute = nullptr;
    QCheckBox* track_mute = nullptr;
    std::array<QDoubleSpinBox*, 5> transform_spins{};
    std::array<QSlider*, 5> transform_sliders{};
    std::array<QPushButton*, 5> transform_keys{};
    QWidget* text_controls = nullptr;
    QLabel* effect_selection_hint = nullptr;
    QWidget* clip_effects_controls = nullptr;
    QListWidget* clip_effects_list = nullptr;
    QPushButton* clip_effect_up = nullptr;
    QPushButton* clip_effect_down = nullptr;
    QPushButton* clip_effect_remove = nullptr;
    QLabel* clip_effect_parameter_label = nullptr;
    QDoubleSpinBox* clip_effect_parameter_value = nullptr;
    QSlider* clip_effect_parameter_slider = nullptr;
    QPlainTextEdit* text_content = nullptr;
    QFontComboBox* text_font = nullptr;
    QSpinBox* text_font_size = nullptr;
    QComboBox* text_alignment = nullptr;
    QPushButton* text_color = nullptr;
    QPushButton* apply_text = nullptr;
    QWidget* transition_controls = nullptr;
    QComboBox* transition_type = nullptr;
    QSpinBox* transition_duration = nullptr;
    QPushButton* apply_transition = nullptr;
    QPushButton* remove_transition = nullptr;
    QTabWidget* inspector_tabs = nullptr;
};

}  // namespace ui
