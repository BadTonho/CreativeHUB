#pragma once

#include "fusion/nodes/model/node_graph.h"
#include "timeline/timeline_model.h"

#include <QObject>
#include <cstdint>
#include <filesystem>
#include <vector>

class QWidget;

namespace ui {

class FusionWorkspace final : public QObject {
    Q_OBJECT
public:
    explicit FusionWorkspace(QObject* parent = nullptr);

    void createPanels(QWidget* parent);
    struct MediaChoice {
        QString label;
        std::filesystem::path path;
        double frame_rate = 30.0;
        std::int64_t frame_count = 0;
        bool still = false;
    };
    void setSelection(const timeline::TimelineClip* clip,
                      std::vector<MediaChoice> media_choices);
    void setMediaChoices(std::vector<MediaChoice> media_choices);
    void setTimelinePlayheadFrame(std::int64_t timeline_frame);
    void setActive(bool active);
    [[nodiscard]] bool isActive() const noexcept { return active_; }
    [[nodiscard]] timeline::ClipId selectedClipId() const noexcept {
        return clip_id_;
    }
    [[nodiscard]] fusion::nodes::NodeId previewNodeId() const noexcept {
        return preview_node_id_;
    }
    [[nodiscard]] fusion::nodes::NodeId selectedNodeId() const noexcept {
        return selected_node_id_;
    }

    [[nodiscard]] QWidget* viewerTitle() const noexcept { return viewer_title_; }
    [[nodiscard]] QWidget* nodeEditorPanel() const noexcept {
        return node_editor_panel_;
    }
    [[nodiscard]] QWidget* inspectorPanel() const noexcept {
        return inspector_panel_;
    }

signals:
    void playbackPauseRequested();
    void playbackClipActivationRequested(timeline::ClipId clip_id,
                                         std::int64_t local_frame);
    void graphEditRequested(timeline::ClipId clip_id,
                            const fusion::nodes::NodeGraph& graph);
    void previewTargetRequested(timeline::ClipId clip_id,
                                fusion::nodes::NodeId node_id);
    void previewTargetCleared();

private:
    QWidget* viewer_title_ = nullptr;
    QWidget* node_editor_panel_ = nullptr;
    QWidget* inspector_panel_ = nullptr;
    QWidget* canvas_host_ = nullptr;
    QWidget* node_properties_ = nullptr;
    std::vector<MediaChoice> media_choices_;
    fusion::nodes::NodeGraph graph_;
    timeline::ClipId clip_id_ = 0;
    fusion::nodes::NodeId selected_node_id_ = 0;
    fusion::nodes::NodeId preview_node_id_ = 0;
    std::int64_t clip_timeline_start_frame_ = 0;
    std::int64_t clip_duration_frames_ = 0;
    std::int64_t local_frame_ = 0;
    bool active_ = false;
    bool refreshing_ = false;
    void publishPreviewTarget();
    void refreshCanvas();
    void refreshInspector();
    void refreshAnimatedControls();
    void toggleTransformKeyframe(
        fusion::nodes::NodeId node_id,
        timeline::TransformProperty property);
    void toggleEffectParameterKeyframe(
        fusion::nodes::NodeId node_id,
        const std::string& parameter_id);
    void commitGraph(fusion::nodes::NodeGraph graph, const QString& status);
};

}  // namespace ui
