#pragma once

#include "../model/node_graph.h"

#include <QGraphicsView>
#include <QPoint>
#include <QString>
#include <functional>
#include <optional>
#include <vector>

class QGraphicsLineItem;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QMimeData;

namespace fusion::nodes {

class NodeCanvas final : public QGraphicsView {
    Q_OBJECT
public:
    explicit NodeCanvas(QWidget* parent = nullptr);
    void setGraph(const NodeGraph& graph);
    void setSelectedNode(NodeId id);
    void setViewerNode(NodeId id);
    void setSelectionChangedHandler(std::function<void(NodeId)> handler);
    void setViewerNodeRequestedHandler(std::function<void(NodeId)> handler);
    void setPositionChangedHandler(std::function<void(NodeId, double, double)> handler);
    void setConnectionRequestedHandler(
        std::function<void(NodeId, NodeId, std::uint8_t)> handler);
    void setDisconnectionRequestedHandler(
        std::function<void(NodeId, std::uint8_t)> handler);
    void setEffectDropRequestedHandler(
        std::function<void(const QString&, const QPointF&,
                           std::optional<Connection>)> handler);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    QGraphicsScene* scene_ = nullptr;
    std::function<void(NodeId)> selection_changed_;
    std::function<void(NodeId)> viewer_node_requested_;
    std::function<void(NodeId, double, double)> position_changed_;
    std::function<void(NodeId, NodeId, std::uint8_t)> connection_requested_;
    std::function<void(NodeId, std::uint8_t)> disconnection_requested_;
    std::function<void(const QString&, const QPointF&,
                       std::optional<Connection>)> effect_drop_requested_;
    NodeGraph graph_;
    std::vector<Connection> connections_;
    NodeId viewer_node_id_ = 0;
    NodeId dragging_from_output_ = 0;
    NodeId dragging_from_input_ = 0;
    std::uint8_t dragging_input_index_ = 0;
    QPoint dragging_input_start_;
    NodeId dragging_node_ = 0;
    double drag_offset_x_ = 0.0;
    double drag_offset_y_ = 0.0;
    double drag_start_x_ = 0.0;
    double drag_start_y_ = 0.0;
    QGraphicsLineItem* pending_connection_line_ = nullptr;
};

} // namespace fusion::nodes
