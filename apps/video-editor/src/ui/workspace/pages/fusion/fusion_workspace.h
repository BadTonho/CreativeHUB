#pragma once

#include "fusion/nodes/model/node_graph.h"
#include "timeline/timeline_model.h"

#include <QObject>
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

signals:
    void graphEditRequested(timeline::ClipId clip_id,
                            const fusion::nodes::NodeGraph& graph);

    [[nodiscard]] QWidget* viewerTitle() const noexcept { return viewer_title_; }
    [[nodiscard]] QWidget* nodeEditorPanel() const noexcept {
        return node_editor_panel_;
    }
    [[nodiscard]] QWidget* inspectorPanel() const noexcept {
        return inspector_panel_;
    }

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
    bool refreshing_ = false;
    void refreshCanvas();
    void refreshInspector();
    void commitGraph(fusion::nodes::NodeGraph graph, const QString& status);
};

}  // namespace ui
