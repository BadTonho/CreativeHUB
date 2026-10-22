#pragma once

#include "../model/node_graph.h"

#include <QGraphicsView>
#include <QPoint>
#include <QSettings>
#include <QString>
#include <functional>
#include <optional>
#include <vector>

class QGraphicsPathItem;
class QPainter;
class QContextMenuEvent;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QMimeData;

namespace fusion::nodes {

enum class BackgroundGridStyle : int {
    Lines = 0,
    Dots = 1,
};

struct BackgroundGridSettings {
    bool visible = true;
    BackgroundGridStyle style = BackgroundGridStyle::Lines;
    int spacing = 24;
    int intensity_percent = 30;

    friend bool operator==(const BackgroundGridSettings&,
                           const BackgroundGridSettings&) = default;
};

enum class ConnectionLineStyle : int {
    Straight = 0,
    Curved = 1,
};

class NodeCanvas final : public QGraphicsView {
    Q_OBJECT
public:
    explicit NodeCanvas(QWidget* parent = nullptr);
    ~NodeCanvas() override;
    [[nodiscard]] BackgroundGridSettings backgroundGridSettings() const noexcept {
        return grid_settings_;
    }
    void setBackgroundGridSettings(BackgroundGridSettings settings);
    [[nodiscard]] ConnectionLineStyle connectionLineStyle() const noexcept {
        return connection_line_style_;
    }
    void setConnectionLineStyle(ConnectionLineStyle style);
    [[nodiscard]] bool snapToGrid() const noexcept { return snap_to_grid_; }
    void setSnapToGrid(bool enabled);
    // Returns the nearest configured grid intersection when snapping is enabled.
    [[nodiscard]] QPointF snapPositionToGrid(QPointF position) const noexcept;
    void setGraph(const NodeGraph& graph);
    void setSelectedNode(NodeId id);
    void setViewerNode(NodeId id);
    // Finds an unoccupied node-sized area near the current viewport center.
    [[nodiscard]] QPointF freeNodePosition(NodeType type) const;
    void setSelectionChangedHandler(std::function<void(NodeId)> handler);
    void setViewerNodeRequestedHandler(std::function<void(NodeId)> handler);
    void setPositionChangedHandler(std::function<void(NodeId, double, double)> handler);
    void setConnectionRequestedHandler(
        std::function<void(NodeId, NodeId, std::uint8_t)> handler);
    void setDisconnectionRequestedHandler(
        std::function<void(NodeId, std::uint8_t)> handler);
    void setEffectDropRequestedHandler(
        std::function<bool(const QString&, const QPointF&,
                           std::optional<Connection>)> handler);

protected:
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void updateConnectionLines(NodeId moved_node = 0);
    void updatePendingConnectionLine(const QPointF& cursor_position);

    QGraphicsScene* scene_ = nullptr;
    QSettings settings_;
    BackgroundGridSettings grid_settings_;
    ConnectionLineStyle connection_line_style_ = ConnectionLineStyle::Straight;
    bool snap_to_grid_ = false;
    std::function<void(NodeId)> selection_changed_;
    std::function<void(NodeId)> viewer_node_requested_;
    std::function<void(NodeId, double, double)> position_changed_;
    std::function<void(NodeId, NodeId, std::uint8_t)> connection_requested_;
    std::function<void(NodeId, std::uint8_t)> disconnection_requested_;
    std::function<bool(const QString&, const QPointF&,
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
    QGraphicsPathItem* pending_connection_line_ = nullptr;
    QPointF pending_start_scene_;
    QPointF pending_cursor_scene_;
    NodeId pending_fixed_node_ = 0;
    bool pending_fixed_is_source_ = false;
    std::uint8_t pending_input_index_ = 0;
};

} // namespace fusion::nodes
