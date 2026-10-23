#pragma once

#include "../model/color_graph.h"

#include <QGraphicsView>
#include <QPoint>

#include <functional>
#include <optional>

class QGraphicsPathItem;
class QGraphicsScene;

namespace ui {

class ColorNodeCanvas final : public QGraphicsView {
public:
    explicit ColorNodeCanvas(QWidget* parent = nullptr);
    void setGraph(color::Graph graph, std::optional<color::NodeId> selected = {});
    [[nodiscard]] std::optional<color::NodeId> selectedNode() const noexcept;
    void setGraphChangedHandler(std::function<void(color::Graph)> handler);
    void setSelectionChangedHandler(std::function<void(color::NodeId)> handler);

protected:
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    struct PortHit {
        color::NodeId node_id = 0;
        std::uint32_t input = 0;
    };

    [[nodiscard]] std::optional<PortHit> inputAt(const QPointF& scene_position) const;
    [[nodiscard]] std::optional<color::NodeId> outputAt(const QPointF& scene_position) const;
    void refreshScene();
    void commitGraph();

    QGraphicsScene* scene_ = nullptr;
    color::Graph graph_;
    std::optional<color::NodeId> selected_node_;
    std::optional<color::NodeId> connecting_from_;
    QGraphicsPathItem* temporary_connection_ = nullptr;
    QPoint last_pan_position_;
    std::function<void(color::Graph)> graph_changed_handler_;
    std::function<void(color::NodeId)> selection_changed_handler_;
    bool rebuilding_ = false;
    bool panning_ = false;
};

}  // namespace ui
