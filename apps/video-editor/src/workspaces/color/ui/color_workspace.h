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

namespace ui {

class ColorWorkspace final : public QWidget {
public:
    struct ClipItem {
        timeline::ClipId id = 0;
        QString name;
        QImage thumbnail;
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
    QWidget* takePreviewWidget();
    void setActive(bool active);
    void setClips(std::vector<ClipItem> clips, timeline::ClipId selected,
                  color::Graph graph);
    void setClipSelectedHandler(std::function<void(timeline::ClipId)> handler);
    void setEffectsChangedHandler(std::function<void(timeline::ClipId,
        color::Graph)> handler);
    void setGradeEditBatchHandlers(std::function<void()> started,
        std::function<void()> finished);
    void setPlaybackHandler(std::function<void(playback::PlaybackCommand)> handler);
    void setScopeFrameRequestHandler(std::function<void(quint64, qint64, quint64,
        quint64)> handler);
    void acceptScopeFrame(quint64 request_id, playback::VideoFramePtr frame,
        qint64 timeline_frame, quint64 composition_revision,
        quint64 playback_generation);
    void setGalleryStills(std::vector<StillItem> stills);
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
    void renameSelectedStill();

    QWidget* preview_container_ = nullptr;
    QWidget* preview_ = nullptr;
    QListWidget* clips_ = nullptr;
    QListWidget* stills_ = nullptr;
    QPushButton* capture_still_button_ = nullptr;
    ColorNodeCanvas* nodes_ = nullptr;
    QComboBox* node_kind_ = nullptr;
    QPushButton* add_branch_button_ = nullptr;
    QWidget* controls_ = nullptr;
    QFormLayout* controls_layout_ = nullptr;
    QLabel* waveform_ = nullptr;
    QComboBox* scope_mode_ = nullptr;
    QTimer* waveform_timer_ = nullptr;
    timeline::ClipId selected_clip_ = 0;
    color::Graph graph_ = color::makePassthroughGraph();
    std::optional<color::NodeId> selected_node_;
    std::function<void(timeline::ClipId)> clip_selected_handler_;
    std::function<void(timeline::ClipId,
        color::Graph)> effects_changed_handler_;
    std::function<void()> grade_edit_started_handler_;
    std::function<void()> grade_edit_finished_handler_;
    std::function<void(playback::PlaybackCommand)> playback_handler_;
    std::function<void(quint64, qint64, quint64, quint64)> scope_frame_request_handler_;
    std::function<void()> capture_still_handler_;
    std::function<void(const QString&, const QString&)> rename_still_handler_;
    std::function<void(const QString&)> remove_still_handler_;
    bool active_ = false;
    bool waveform_pending_ = false;
    unsigned int waveform_generation_ = 0;
    quint64 next_scope_request_id_ = 1;
    quint64 active_scope_request_id_ = 0;
    rendering::PreviewFramePayload active_scope_identity_;
};

}  // namespace ui
