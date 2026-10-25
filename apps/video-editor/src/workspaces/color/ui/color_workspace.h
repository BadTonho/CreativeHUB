#pragma once

#include "timeline/timeline_model.h"
#include "rendering/preview_frame_payload.h"
#include "playback/playback_controller.h"
#include "workspaces/color/ui/color_node_canvas.h"

#include <QWidget>
#include <QImage>

#include <functional>
#include <vector>

class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QListWidget;
class QComboBox;
class QPushButton;
class QTimer;
class QCheckBox;
class QSlider;
class QSplitter;
class QVBoxLayout;
class PreviewWidget;

namespace ui {

class ColorWorkspace final : public QWidget {
public:
    struct ClipItem {
        timeline::ClipId id = 0;
        QString name;
        QImage thumbnail;
        std::vector<timeline::ColorGradeVersion> grade_versions;
    };
    struct StillItem {
        QString id;
        QString name;
        QString source;
        qint64 frame = 0;
        QImage thumbnail;
        bool available = true;
    };

    explicit ColorWorkspace(QWidget* parent = nullptr);
    void setPreviewWidget(QWidget* preview);
    void setEffectsCatalogWidget(QWidget* catalog);
    void setEffectsPanelVisible(bool visible);
    [[nodiscard]] bool effectsPanelVisible() const noexcept;
    QWidget* takePreviewWidget();
    void setActive(bool active);
    void setClips(std::vector<ClipItem> clips, timeline::ClipId selected,
                  color::Graph graph);
    void setClipSelectedHandler(std::function<void(timeline::ClipId)> handler);
    void setEffectsChangedHandler(std::function<void(timeline::ClipId,
        color::Graph)> handler);
    void setCopyGradeHandler(std::function<void(timeline::ClipId)> handler);
    void setApplyGradeHandler(std::function<void(timeline::ClipId)> handler);
    void setGradeVersionsChangedHandler(std::function<void(timeline::ClipId,
        std::vector<timeline::ColorGradeVersion>)> handler);
    void setCanApplyCopiedGrade(bool available);
    void setGradeEditBatchHandlers(std::function<void()> started,
        std::function<void()> finished);
    void setPlaybackHandler(std::function<void(playback::PlaybackCommand)> handler);
    void setScopeFrameRequestHandler(std::function<void(quint64, qint64, quint64,
        quint64)> handler);
    void setColorInputFrameRequestHandler(std::function<void(quint64,
        timeline::ClipId, color::NodeId, qint64, quint64, quint64, quint64,
        color::Graph)> handler);
    void setGradeComparisonRequestHandler(
        std::function<void(std::optional<playback::PlaybackColorGradeComparisonRequest>)> handler);
    void acceptScopeFrame(quint64 request_id, playback::VideoFramePtr frame,
        qint64 timeline_frame, quint64 composition_revision,
        quint64 playback_generation);
    void acceptColorInputFrame(quint64 request_id, timeline::ClipId clip_id,
        color::NodeId node_id, playback::VideoFramePtr frame, qint64 timeline_frame,
        quint64 delivery_epoch, quint64 composition_revision,
        quint64 playback_generation);
    void acceptGradeComparisonFrame(quint64 request_id, quint64 project_generation,
        timeline::ClipId clip_id, const std::string& version_id,
        rendering::PreviewFramePayload frame);
    void setGalleryStills(std::vector<StillItem> stills, quint64 project_generation = 0);
    void setGalleryReferenceRequestHandler(
        std::function<void(quint64, const QString&)> handler);
    void acceptGalleryReferenceImage(quint64 request_id, const QString& still_id,
        QImage image);
    void setCapturePending(bool pending);
    void setCaptureStillHandler(std::function<void()> handler);
    void setRenameStillHandler(std::function<void(const QString&, const QString&)> handler);
    void setRemoveStillHandler(std::function<void(const QString&)> handler);

private:
    void refreshNodes(std::optional<color::NodeId> selected = {});
    void refreshControls();
    void submit(color::Graph graph, std::optional<color::NodeId> selected = {},
        bool rebuild_controls = true);
    void scheduleWaveform();
    void renderScopeFrame(rendering::PreviewFramePayload identity,
        playback::VideoFramePtr frame, quint64 request_id);
    void requestCurvesInputFrame(const rendering::PreviewFramePayload& identity);
    void refreshMattePreview();
    void renameSelectedStill();
    void handleGallerySelectionChanged();
    void clearGalleryReference(bool reset_mode);
    void applyPreviewComparison();
    void compareSelectedGradeVersion();
    void clearGradeComparisonReference(bool reset_mode);
    void saveGradeVersion();
    void applySelectedGradeVersion();
    void renameSelectedGradeVersion();
    void removeSelectedGradeVersion();

    QWidget* preview_container_ = nullptr;
    QSplitter* upper_splitter_ = nullptr;
    QWidget* effects_panel_ = nullptr;
    QVBoxLayout* effects_catalog_layout_ = nullptr;
    QWidget* effects_catalog_widget_ = nullptr;
    bool effects_panel_size_initialized_ = false;
    QWidget* preview_ = nullptr;
    ::PreviewWidget* preview_widget_ = nullptr;
    QLabel* comparison_reference_label_ = nullptr;
    QComboBox* comparison_mode_ = nullptr;
    QSlider* comparison_wipe_slider_ = nullptr;
    QListWidget* clips_ = nullptr;
    QListWidget* stills_ = nullptr;
    QPushButton* capture_still_button_ = nullptr;
    QWidget* grade_versions_panel_ = nullptr;
    QListWidget* grade_versions_ = nullptr;
    QPushButton* save_grade_version_button_ = nullptr;
    QPushButton* apply_grade_version_button_ = nullptr;
    QPushButton* compare_grade_version_button_ = nullptr;
    QPushButton* rename_grade_version_button_ = nullptr;
    QPushButton* remove_grade_version_button_ = nullptr;
    ColorNodeCanvas* nodes_ = nullptr;
    QComboBox* node_kind_ = nullptr;
    QPushButton* add_branch_button_ = nullptr;
    QWidget* controls_ = nullptr;
    QFormLayout* controls_layout_ = nullptr;
    QComboBox* primary_grade_mode_ = nullptr;
    QLabel* waveform_ = nullptr;
    QLabel* matte_preview_ = nullptr;
    QComboBox* matte_preview_mode_ = nullptr;
    QComboBox* scope_mode_ = nullptr;
    QTimer* waveform_timer_ = nullptr;
    QImage latest_scope_source_;
    QImage latest_curves_input_;
    timeline::ClipId selected_clip_ = 0;
    color::Graph graph_ = color::makePassthroughGraph();
    std::vector<timeline::ColorGradeVersion> selected_grade_versions_;
    std::optional<color::NodeId> selected_node_;
    std::function<void(timeline::ClipId)> clip_selected_handler_;
    std::function<void(timeline::ClipId,
        color::Graph)> effects_changed_handler_;
    std::function<void(timeline::ClipId)> copy_grade_handler_;
    std::function<void(timeline::ClipId)> apply_grade_handler_;
    std::function<void(timeline::ClipId,
        std::vector<timeline::ColorGradeVersion>)> grade_versions_changed_handler_;
    std::function<void()> grade_edit_started_handler_;
    std::function<void()> grade_edit_finished_handler_;
    std::function<void(playback::PlaybackCommand)> playback_handler_;
    std::function<void(quint64, qint64, quint64, quint64)> scope_frame_request_handler_;
    std::function<void(quint64, timeline::ClipId, color::NodeId, qint64,
        quint64, quint64, quint64, color::Graph)> color_input_frame_request_handler_;
    std::function<void(std::optional<playback::PlaybackColorGradeComparisonRequest>)>
        grade_comparison_request_handler_;
    std::function<void()> capture_still_handler_;
    std::function<void(const QString&, const QString&)> rename_still_handler_;
    std::function<void(const QString&)> remove_still_handler_;
    std::function<void(quint64, const QString&)> gallery_reference_request_handler_;
    QString reference_still_id_;
    QString reference_still_name_;
    QImage reference_image_;
    quint64 gallery_project_generation_ = 0;
    quint64 next_gallery_reference_request_id_ = 1;
    quint64 active_gallery_reference_request_id_ = 0;
    enum class ReferenceSource { None, Gallery, GradeVersion };
    ReferenceSource reference_source_ = ReferenceSource::None;
    std::optional<playback::PlaybackColorGradeComparisonRequest> grade_comparison_reference_;
    rendering::PreviewFramePayload latest_grade_comparison_frame_;
    quint64 next_grade_comparison_request_id_ = 1;
    bool grade_comparison_request_active_ = false;
    bool active_ = false;
    bool can_apply_copied_grade_ = false;
    bool waveform_pending_ = false;
    unsigned int waveform_generation_ = 0;
    quint64 next_scope_request_id_ = 1;
    quint64 active_scope_request_id_ = 0;
    rendering::PreviewFramePayload active_scope_identity_;
    quint64 active_color_input_request_id_ = 0;
    rendering::PreviewFramePayload active_color_input_identity_;
};

}  // namespace ui
