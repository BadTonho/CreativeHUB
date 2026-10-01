#pragma once

#include <QWidget>

class QLabel;
class QComboBox;
class QPushButton;
class QSlider;

namespace motion::ui {

class TimelineControls final : public QWidget {
public:
    explicit TimelineControls(QWidget* parent = nullptr);

    [[nodiscard]] QPushButton* previousFrameButton() const noexcept;
    [[nodiscard]] QPushButton* playPauseButton() const noexcept;
    [[nodiscard]] QPushButton* loopButton() const noexcept;
    [[nodiscard]] QComboBox* displayModeCombo() const noexcept;
    [[nodiscard]] QLabel* frameLabel() const noexcept;
    [[nodiscard]] QPushButton* zoomOutButton() const noexcept;
    [[nodiscard]] QSlider* zoomSlider() const noexcept;
    [[nodiscard]] QLabel* zoomLevelLabel() const noexcept;
    [[nodiscard]] QPushButton* zoomInButton() const noexcept;
    [[nodiscard]] QLabel* frameRateLabel() const noexcept;
    [[nodiscard]] QPushButton* nextFrameButton() const noexcept;
    [[nodiscard]] QPushButton* graphEditorButton() const noexcept;

private:
    QPushButton* previous_frame_button_ = nullptr;
    QPushButton* play_pause_button_ = nullptr;
    QPushButton* loop_button_ = nullptr;
    QComboBox* display_mode_combo_ = nullptr;
    QLabel* frame_label_ = nullptr;
    QPushButton* zoom_out_button_ = nullptr;
    QSlider* zoom_slider_ = nullptr;
    QLabel* zoom_level_label_ = nullptr;
    QPushButton* zoom_in_button_ = nullptr;
    QLabel* frame_rate_label_ = nullptr;
    QPushButton* next_frame_button_ = nullptr;
    QPushButton* graph_editor_button_ = nullptr;
};

} // namespace motion::ui
