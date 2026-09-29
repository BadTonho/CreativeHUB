#pragma once

#include "motion_video_export.h"

#include <QDialog>

#include <creative_suite/media/video_encoder.h>

#include <vector>

class QComboBox;
class QDoubleSpinBox;
class QDialogButtonBox;
class QLineEdit;
class QLabel;
class QSpinBox;

namespace motion::ui {

class MotionVideoExportDialog final : public QDialog {
public:
    MotionVideoExportDialog(
        model::CanvasSize canvas_size,
        model::FrameRate composition_rate,
        QWidget* parent = nullptr);

    [[nodiscard]] std::optional<MotionExportSettings> exportSettings() const;

private:
    void browseOutput();
    void populateContainers();
    void updateEncoders();
    void updateResolutionFields();
    void updateQualitySuggestion();
    void updateAcceptState();
    void updateDefaultExtension();

    model::CanvasSize canvas_size_{};
    model::FrameRate composition_rate_{};
    std::vector<creative_suite::media::VideoContainerOption> containers_;
    QLineEdit* output_path_ = nullptr;
    QComboBox* container_combo_ = nullptr;
    QComboBox* encoder_combo_ = nullptr;
    QComboBox* resolution_combo_ = nullptr;
    QLabel* custom_width_label_ = nullptr;
    QLabel* custom_height_label_ = nullptr;
    QSpinBox* custom_width_ = nullptr;
    QSpinBox* custom_height_ = nullptr;
    QDoubleSpinBox* frame_rate_ = nullptr;
    QComboBox* quality_combo_ = nullptr;
    QDoubleSpinBox* bitrate_ = nullptr;
    QDialogButtonBox* buttons_ = nullptr;
    bool frame_rate_overridden_ = false;
    bool applying_quality_suggestion_ = false;
};

} // namespace motion::ui
