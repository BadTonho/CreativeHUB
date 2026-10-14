#include "edit_workspace_controller.h"
#include "logging/logger.h"

#include <QColorDialog>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QPalette>
#include <QSignalBlocker>
#include <algorithm>
#include <cmath>

namespace ui {

void EditWorkspaceController::addBackgroundGeneratorAt(timeline::TrackId track_id, qint64 frame) {
    if (frame < 0) return;
    const auto duration = std::max<std::int64_t>(1,
        static_cast<std::int64_t>(std::ceil(timeline_model_.frameRate().asDouble() * 5.0)));
    try {
        const auto result = execute(application::AddBackgroundGeneratorCommand{track_id, frame, duration});
        if (result.changed()) {
            publishCommittedEdit(result, false, true, QStringLiteral("Background added."));
            if (playback_available_) emit renderCompositionFrameRequested(timelinePlayheadFrame(), playback_frame_index_);
        } else {
            emit statusMessageRequested(result.reason == application::EditReason::Overlap
                ? QStringLiteral("A clip already occupies that range.")
                : QStringLiteral("The Background could not be added."));
        }
    } catch (const std::exception& error) {
        logging::Logger::instance().log(logging::Level::Error, "timeline", "add-background", error.what(),
            {{"track_id", std::to_string(track_id)}, {"timeline_frame", std::to_string(frame)}});
        emit warningMessageRequested(QStringLiteral("Timeline error"), QStringLiteral("The Background could not be added."));
    }
}

void EditWorkspaceController::refreshBackgroundInspector(const timeline::TimelineClip* clip) {
    if (!ui_.background_controls || !ui_.background_nodes || !ui_.background_color || !ui_.background_sample) return;
    const auto clip_id = clip ? clip->clip_id : 0;
    if (background_clip_id_ != clip_id) background_node_id_ = 0;
    background_clip_id_ = clip_id;
    auto* nodes = ui_.background_nodes;
    const QSignalBlocker blocker(nodes);
    nodes->clear();
    const fusion::nodes::Node* selected = nullptr;
    if (clip && timeline::supportsNodeGraph(clip->kind) && clip->node_graph) {
        for (const auto& node : clip->node_graph->nodes) {
            if (node.type != fusion::nodes::NodeType::Background) continue;
            nodes->addItem(QStringLiteral("Background #%1").arg(node.id), QVariant::fromValue<qulonglong>(node.id));
            if (node.id == background_node_id_) selected = &node;
        }
        if (!selected && nodes->count() > 0)
            selected = fusion::nodes::findNode(*clip->node_graph, nodes->itemData(0).toULongLong());
    }
    ui_.background_controls->setVisible(selected != nullptr);
    ui_.background_controls->setEnabled(selected != nullptr);
    nodes->setVisible(nodes->count() > 1);
    if (!selected) { background_node_id_ = 0; return; }
    background_node_id_ = selected->id;
    nodes->setCurrentIndex(nodes->findData(QVariant::fromValue<qulonglong>(selected->id)));
    const auto& c = selected->background_color;
    ui_.background_sample->setText(QStringLiteral("RGBA  %1, %2, %3, %4").arg(c[0]).arg(c[1]).arg(c[2]).arg(c[3]));
    const auto base = ui_.background_controls->palette().color(QPalette::Window);
    const int luminance = (qGray(c[0],c[1],c[2])*c[3] + qGray(base.rgb())*(255-c[3]))/255;
    const auto ink = luminance > 128 ? QStringLiteral("#111111") : QStringLiteral("#ffffff");
    ui_.background_sample->setStyleSheet(QStringLiteral("border: 1px solid #78818f; padding: 6px; background-color: rgba(%1,%2,%3,%4); color: %5;")
        .arg(c[0]).arg(c[1]).arg(c[2]).arg(c[3]).arg(ink));
}

void EditWorkspaceController::selectBackgroundNode(int index) {
    if (!ui_.background_nodes || index < 0) return;
    background_node_id_ = ui_.background_nodes->itemData(index).toULongLong();
    updateInspector(); // Only presentation state changes when choosing a node.
}

void EditWorkspaceController::chooseBackgroundColor() {
    const auto clip_id = background_clip_id_;
    const auto node_id = background_node_id_;
    const auto location = timeline_model_.locateClip(clip_id);
    if (!location) return;
    const auto& clip = timeline_model_.tracks()[location->track_index].clips[location->clip_index];
    const auto* node = clip.node_graph ? fusion::nodes::findNode(*clip.node_graph, node_id) : nullptr;
    if (!node || node->type != fusion::nodes::NodeType::Background) return;
    const auto c = node->background_color;
    try {
        QColorDialog dialog(QColor(c[0],c[1],c[2],c[3]), ui_.inspector_panel);
        dialog.setObjectName(QStringLiteral("editBackgroundColorDialog"));
        dialog.setWindowTitle(QStringLiteral("Background Color"));
        dialog.setOptions(QColorDialog::ShowAlphaChannel | QColorDialog::DontUseNativeDialog);
        if (dialog.exec() != QDialog::Accepted || !dialog.currentColor().isValid()) return;
        // Re-read the graph after the modal dialog; never restore an obsolete graph.
        const auto current = timeline_model_.locateClip(clip_id);
        if (!current) return;
        const auto& latest = timeline_model_.tracks()[current->track_index].clips[current->clip_index];
        if (!latest.node_graph) return;
        auto graph = *latest.node_graph;
        auto found = std::find_if(graph.nodes.begin(), graph.nodes.end(), [node_id](const auto& value) {
            return value.id == node_id && value.type == fusion::nodes::NodeType::Background;
        });
        if (found == graph.nodes.end()) return;
        const auto color = dialog.currentColor();
        found->background_color = {static_cast<std::uint8_t>(color.red()), static_cast<std::uint8_t>(color.green()),
            static_cast<std::uint8_t>(color.blue()), static_cast<std::uint8_t>(color.alpha())};
        applyFusionNodeGraph(clip_id, graph);
        updateInspector();
    } catch (const std::exception& error) {
        logging::Logger::instance().log(logging::Level::Error, "fusion", "background-color", error.what(),
            {{"clip_id", std::to_string(clip_id)}, {"node_id", std::to_string(node_id)}});
        emit warningMessageRequested(QStringLiteral("Background"), QStringLiteral("The Background color could not be changed."));
    }
}
} // namespace ui
