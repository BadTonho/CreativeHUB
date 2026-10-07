#pragma once

#include "../model/node_graph.h"

#include <QGraphicsView>
#include <functional>

class QGraphicsLineItem;

namespace fusion::nodes {

class NodeCanvas final : public QGraphicsView {
public:
    explicit NodeCanvas(QWidget* parent = nullptr);
    void setGraph(const NodeGraph& graph);
    void setSelectedNode(NodeId id);
    void setSelectionChangedHandler(std::function<void(NodeId)> handler);
    void setPositionChangedHandler(std::function<void(NodeId, double, double)> handler);
    void setConnectionRequestedHandler(
        std::function<void(NodeId, NodeId, std::uint8_t)> handler);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    QGraphicsScene* scene_ = nullptr;
    std::function<void(NodeId)> selection_changed_;
    std::function<void(NodeId, double, double)> position_changed_;
    std::function<void(NodeId, NodeId, std::uint8_t)> connection_requested_;
    NodeId dragging_from_output_ = 0;
    QGraphicsLineItem* pending_connection_line_ = nullptr;
};

} // namespace fusion::nodes
