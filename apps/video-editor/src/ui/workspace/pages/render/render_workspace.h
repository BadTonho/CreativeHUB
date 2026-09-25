#pragma once

#include "project/project_document.h"
#include "ui/workspace/pages/render/render_output_capabilities.h"

#include <QObject>

#include <functional>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListView;
class QPushButton;
class QSpinBox;
class QSplitter;
class QWidget;

namespace ui {

class RenderQueueModel;

class RenderWorkspace final : public QObject {
public:
    using TimelineReadOnlyHandler = std::function<void(bool)>;
    using ProjectSnapshotProvider = std::function<project::ProjectDocument()>;
    using FrameRateProvider = std::function<double()>;

    explicit RenderWorkspace(
        TimelineReadOnlyHandler timeline_read_only_handler,
        ProjectSnapshotProvider project_snapshot_provider = {},
        FrameRateProvider frame_rate_provider = {},
        QObject* parent = nullptr);

    void createPanels(QWidget* parent);
    void setActive(bool active);
    void setPreviewWidget(QWidget* preview_widget);
    [[nodiscard]] QWidget* takePreviewWidget();

    [[nodiscard]] QWidget* centralPage() const noexcept { return central_page_; }
    [[nodiscard]] QWidget* settingsPanel() const noexcept { return settings_panel_; }
    [[nodiscard]] QWidget* previewPanel() const noexcept { return preview_panel_; }
    [[nodiscard]] QWidget* previewWidget() const noexcept { return preview_widget_; }
    [[nodiscard]] QWidget* queuePanel() const noexcept { return queue_panel_; }
    [[nodiscard]] QSplitter* splitter() const noexcept { return splitter_; }
    [[nodiscard]] RenderQueueModel* queueModel() const noexcept {
        return queue_model_;
    }
    [[nodiscard]] bool isActive() const noexcept { return active_; }

private:
    void createSettingsPanel();
    void createPreviewPanel();
    void createQueuePanel();
    void populateContainerOptions();
    void updateEncoderOptions();
    void updateResolutionFields();
    void updateQualitySuggestions();
    void updateQueueActions();
    void updateAddAction();
    void updateDefaultFrameRate();
    void updateOutputPathExtension();
    void browseOutputPath();
    void addCurrentJob();
    [[nodiscard]] int customWidth() const noexcept;
    [[nodiscard]] int customHeight() const noexcept;

    TimelineReadOnlyHandler timeline_read_only_handler_;
    ProjectSnapshotProvider project_snapshot_provider_;
    FrameRateProvider frame_rate_provider_;
    QWidget* central_page_ = nullptr;
    QWidget* settings_panel_ = nullptr;
    QWidget* preview_panel_ = nullptr;
    QWidget* preview_widget_ = nullptr;
    QWidget* queue_panel_ = nullptr;
    QSplitter* splitter_ = nullptr;
    QLineEdit* output_path_ = nullptr;
    QLabel* capability_warning_ = nullptr;
    QComboBox* container_combo_ = nullptr;
    QComboBox* video_encoder_combo_ = nullptr;
    QCheckBox* export_audio_check_ = nullptr;
    QComboBox* audio_encoder_combo_ = nullptr;
    QComboBox* resolution_combo_ = nullptr;
    QSpinBox* custom_width_ = nullptr;
    QSpinBox* custom_height_ = nullptr;
    QDoubleSpinBox* frame_rate_spin_ = nullptr;
    QComboBox* quality_preset_combo_ = nullptr;
    QDoubleSpinBox* video_bitrate_spin_ = nullptr;
    QSpinBox* audio_bitrate_spin_ = nullptr;
    QLabel* empty_queue_label_ = nullptr;
    QListView* queue_view_ = nullptr;
    QPushButton* add_job_button_ = nullptr;
    QPushButton* remove_job_button_ = nullptr;
    QPushButton* move_job_up_button_ = nullptr;
    QPushButton* move_job_down_button_ = nullptr;
    RenderQueueModel* queue_model_ = nullptr;
    std::vector<RenderContainerOption> containers_;
    int project_width_ = 1920;
    int project_height_ = 1080;
    bool active_ = false;
    bool frame_rate_user_modified_ = false;
    bool applying_quality_suggestion_ = false;
};

}  // namespace ui
