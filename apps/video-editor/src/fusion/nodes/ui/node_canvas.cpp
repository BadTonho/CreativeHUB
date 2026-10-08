#include "node_canvas.h"

#include "ui/media_browser/media_drag_mime.h"

#include <creative_suite/effects/effects.h>

#include <QApplication>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QGraphicsEllipseItem>
#include <QGraphicsItem>
#include <QGraphicsLineItem>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QBrush>
#include <QColor>
#include <QFont>
#include <algorithm>
#include <cstdint>
#include <unordered_map>

namespace fusion::nodes {
namespace {
QString title(NodeType type) {
    switch (type) {
    case NodeType::Input: return QStringLiteral("Input");
    case NodeType::Transform: return QStringLiteral("Transform");
    case NodeType::Color: return QStringLiteral("Color");
    case NodeType::Merge: return QStringLiteral("Merge");
    case NodeType::Output: return QStringLiteral("Output");
    case NodeType::Effect: return QStringLiteral("Effect");
    }
    return QStringLiteral("Node");
}

QString title(const Node& node) {
    if (node.type == NodeType::Effect) {
        if (const auto* definition = creative_suite::effects::findDefinition(node.effect.id))
            return QString::fromUtf8(definition->name.data(),
                                     static_cast<qsizetype>(definition->name.size()));
    }
    return title(node.type);
}

QString effectIdFromMime(const QMimeData* mime) {
    if (mime == nullptr || !mime->hasFormat(ui::kEffectIdMimeType)) return {};
    const auto value = mime->data(ui::kEffectIdMimeType);
    const auto id = value.toStdString();
    return creative_suite::effects::findDefinition(id) != nullptr
        ? QString::fromUtf8(value.constData(), value.size()) : QString{};
}

double inputPortY(NodeType type, std::uint8_t input) {
    if (type == NodeType::Merge) return input == 0 ? 42.0 : 80.0;
    return 47.0;
}

double outputPortY(NodeType type) {
    return type == NodeType::Merge ? 56.0 : 47.0;
}
}

NodeCanvas::NodeCanvas(QWidget* parent) : QGraphicsView(parent) {
    scene_ = new QGraphicsScene(this);
    scene_->setSceneRect(0, 0, 1600, 900);
    setScene(scene_);
    setRenderHint(QPainter::Antialiasing);
    setDragMode(QGraphicsView::NoDrag);
    setBackgroundBrush(QColor("#171a20"));
    setObjectName("fusionNodeCanvasView");
    setAcceptDrops(true);
    viewport()->setAcceptDrops(true);
    connect(scene_, &QGraphicsScene::selectionChanged, this, [this] {
        if (!selection_changed_) return;
        for (auto* item : scene_->selectedItems()) {
            while (item != nullptr && !item->data(0).isValid()) item = item->parentItem();
            if (item != nullptr) {
                selection_changed_(static_cast<NodeId>(item->data(0).toULongLong()));
                return;
            }
        }
        selection_changed_(0);
    });
}

void NodeCanvas::setGraph(const NodeGraph& graph) {
    graph_ = graph;
    dragging_from_output_ = 0;
    dragging_from_input_ = 0;
    dragging_input_index_ = 0;
    dragging_node_ = 0;
    pending_connection_line_ = nullptr;
    connections_ = graph.connections;
    scene_->clear();
    std::unordered_map<NodeId, QGraphicsRectItem*> items;
    for (const auto& node : graph.nodes) {
        auto* item = scene_->addRect(QRectF(0, 0, 210, node.type == NodeType::Merge ? 112 : 94),
            QPen(QColor("#5b6574"), 1.5), QBrush(QColor("#252b35")));
        item->setData(0, QVariant::fromValue<qulonglong>(node.id));
        item->setData(1, 0);
        item->setFlag(QGraphicsItem::ItemIsMovable, true);
        item->setFlag(QGraphicsItem::ItemIsSelectable, true);
        item->setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);
        item->setPos(node.x, node.y);
        auto* label = scene_->addText(title(node));
        label->setDefaultTextColor(QColor("#e4e9f0"));
        label->setFont(QFont(QStringLiteral("Segoe UI"), 10, QFont::DemiBold));
        label->setParentItem(item);
        label->setPos(12, 9);
        auto* id_label = scene_->addText(QStringLiteral("#%1").arg(node.id));
        id_label->setDefaultTextColor(QColor("#9aa4b2"));
        id_label->setFont(QFont(QStringLiteral("Segoe UI"), 8));
        id_label->setParentItem(item);
        id_label->setPos(12, node.type == NodeType::Merge ? 94 : 35);
        const auto add_port = [this, item, &node](double x, double y,
                                                  int kind, std::uint8_t input) {
            auto* port = scene_->addEllipse(QRectF(-5, -5, 10, 10),
                QPen(QColor("#c7d2e0"), 1.0), QBrush(QColor("#438fca")));
            port->setParentItem(item);
            port->setPos(x, y);
            port->setData(0, QVariant::fromValue<qulonglong>(node.id));
            port->setData(1, kind);
            port->setData(2, static_cast<int>(input));
            port->setToolTip(kind == 2
                ? QStringLiteral("Drag from this output to a node input.")
                : node.type == NodeType::Merge && input == 0
                    ? QStringLiteral("Merge background input.")
                    : node.type == NodeType::Merge
                        ? QStringLiteral("Merge foreground input.")
                        : QStringLiteral("Node input."));
            port->setZValue(2);
        };
        constexpr int input_port = 1;
        constexpr int output_port = 2;
        if (node.type != NodeType::Input) {
            const auto count = node.type == NodeType::Merge ? 2 : 1;
            for (int input = 0; input < count; ++input) {
                const auto y = inputPortY(node.type, static_cast<std::uint8_t>(input));
                add_port(0, y, input_port, static_cast<std::uint8_t>(input));
                if (node.type == NodeType::Merge) {
                    auto* input_label = scene_->addText(input == 0
                        ? QStringLiteral("BG") : QStringLiteral("FG"));
                    input_label->setDefaultTextColor(QColor("#9aa4b2"));
                    input_label->setFont(QFont(QStringLiteral("Segoe UI"), 7));
                    input_label->setParentItem(item);
                    input_label->setPos(10, y - 10);
                }
            }
        }
        if (node.type != NodeType::Output)
            add_port(210, outputPortY(node.type), output_port, 0);
        items.emplace(node.id, item);
    }
    for (const auto& edge : graph.connections) {
        const auto from = items.find(edge.from), to = items.find(edge.to);
        if (from == items.end() || to == items.end()) continue;
        const auto* from_node = findNode(graph, edge.from);
        const auto* to_node = findNode(graph, edge.to);
        if (from_node == nullptr || to_node == nullptr) continue;
        const auto start = from->second->pos() + QPointF(210, outputPortY(from_node->type));
        const auto end = to->second->pos() + QPointF(0, inputPortY(to_node->type, edge.input));
        auto* line = scene_->addLine(QLineF(start, end), QPen(QColor("#66b5ff"), 2.0));
        line->setData(0, QVariant::fromValue<qulonglong>(edge.to));
        line->setData(1, 3);
        line->setData(2, static_cast<int>(edge.input));
        line->setZValue(-1);
        auto* hit_target = scene_->addLine(
            QLineF(start, end), QPen(QColor(0, 0, 0, 0), 12.0));
        hit_target->setData(0, QVariant::fromValue<qulonglong>(edge.to));
        hit_target->setData(1, 3);
        hit_target->setData(2, static_cast<int>(edge.input));
        hit_target->setZValue(-0.5);
    }
}

void NodeCanvas::setSelectedNode(NodeId id) {
    scene_->clearSelection();
    for (auto* item : scene_->items()) {
        if (item->data(0).isValid() && item->data(1).toInt() == 0 &&
            static_cast<NodeId>(item->data(0).toULongLong()) == id) {
            item->setSelected(true);
            break;
        }
    }
}

void NodeCanvas::setSelectionChangedHandler(std::function<void(NodeId)> handler) {
    selection_changed_ = std::move(handler);
}
void NodeCanvas::setPositionChangedHandler(
    std::function<void(NodeId, double, double)> handler) {
    position_changed_ = std::move(handler);
}

void NodeCanvas::setConnectionRequestedHandler(
    std::function<void(NodeId, NodeId, std::uint8_t)> handler) {
    connection_requested_ = std::move(handler);
}

void NodeCanvas::setDisconnectionRequestedHandler(
    std::function<void(NodeId, std::uint8_t)> handler) {
    disconnection_requested_ = std::move(handler);
}

void NodeCanvas::setEffectDropRequestedHandler(
    std::function<void(const QString&, const QPointF&,
                       std::optional<Connection>)> handler) {
    effect_drop_requested_ = std::move(handler);
}

void NodeCanvas::dragEnterEvent(QDragEnterEvent* event) {
    if (!effectIdFromMime(event->mimeData()).isEmpty()) {
        event->acceptProposedAction();
        return;
    }
    event->ignore();
}

void NodeCanvas::dragMoveEvent(QDragMoveEvent* event) {
    if (!effectIdFromMime(event->mimeData()).isEmpty()) {
        event->acceptProposedAction();
        return;
    }
    event->ignore();
}

void NodeCanvas::dropEvent(QDropEvent* event) {
    const auto effect_id = effectIdFromMime(event->mimeData());
    if (effect_id.isEmpty() || !effect_drop_requested_) {
        event->ignore();
        return;
    }
    const auto viewport_position = event->position().toPoint();
    const auto scene_position = mapToScene(viewport_position);
    std::optional<Connection> cable;
    for (auto* item : items(viewport_position)) {
        if (item->data(1).toInt() != 3) continue;
        const auto to = static_cast<NodeId>(item->data(0).toULongLong());
        const auto input = static_cast<std::uint8_t>(item->data(2).toInt());
        const auto found = std::find_if(graph_.connections.begin(), graph_.connections.end(),
            [to, input](const Connection& edge) {
                return edge.to == to && edge.input == input;
            });
        if (found != graph_.connections.end()) {
            cable = *found;
            break;
        }
    }
    effect_drop_requested_(effect_id, scene_position, cable);
    event->acceptProposedAction();
}

void NodeCanvas::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        auto* item = itemAt(event->pos());
        if (item != nullptr && item->data(1).toInt() == 1) {
            auto* node = item->parentItem();
            if (node != nullptr) {
                scene_->clearSelection();
                node->setSelected(true);
            }
            const auto node_id = static_cast<NodeId>(item->data(0).toULongLong());
            const auto input = static_cast<std::uint8_t>(item->data(2).toInt());
            const bool is_connected = std::any_of(
                connections_.begin(), connections_.end(),
                [node_id, input](const Connection& connection) {
                    return connection.to == node_id && connection.input == input;
                });
            if (is_connected) {
                dragging_from_input_ = node_id;
                dragging_input_index_ = input;
                dragging_input_start_ = event->pos();
                const auto position = mapToScene(event->pos());
                pending_connection_line_ = scene_->addLine(
                    QLineF(position, position),
                    QPen(QColor("#9ad3ff"), 2.0, Qt::DashLine));
                pending_connection_line_->setZValue(-2);
            }
            event->accept();
            return;
        }
        if (item != nullptr && item->data(1).toInt() == 2) {
            dragging_from_output_ = static_cast<NodeId>(item->data(0).toULongLong());
            if (item->parentItem() != nullptr) item->parentItem()->setSelected(true);
            const auto position = mapToScene(event->pos());
            pending_connection_line_ = scene_->addLine(
                QLineF(position, position), QPen(QColor("#9ad3ff"), 2.0, Qt::DashLine));
            pending_connection_line_->setZValue(-2);
            event->accept();
            return;
        }
        if (item != nullptr && item->data(1).toInt() == 3) {
            dragging_from_input_ = static_cast<NodeId>(item->data(0).toULongLong());
            dragging_input_index_ = static_cast<std::uint8_t>(item->data(2).toInt());
            dragging_input_start_ = event->pos();
            const auto position = mapToScene(event->pos());
            pending_connection_line_ = scene_->addLine(
                QLineF(position, position),
                QPen(QColor("#9ad3ff"), 2.0, Qt::DashLine));
            pending_connection_line_->setZValue(-2);
            event->accept();
            return;
        }
        while (item != nullptr &&
               (!item->data(0).isValid() || item->data(1).toInt() != 0)) {
            item = item->parentItem();
        }
        if (item != nullptr && item->flags().testFlag(QGraphicsItem::ItemIsMovable)) {
            dragging_node_ = static_cast<NodeId>(item->data(0).toULongLong());
            const auto scene_position = mapToScene(event->pos());
            drag_offset_x_ = scene_position.x() - item->pos().x();
            drag_offset_y_ = scene_position.y() - item->pos().y();
            drag_start_x_ = item->pos().x();
            drag_start_y_ = item->pos().y();
            scene_->clearSelection();
            item->setSelected(true);
            event->accept();
            return;
        }
    }
    QGraphicsView::mousePressEvent(event);
}

void NodeCanvas::mouseMoveEvent(QMouseEvent* event) {
    if ((dragging_from_output_ != 0 || dragging_from_input_ != 0) &&
        pending_connection_line_ != nullptr) {
        const auto line = pending_connection_line_->line();
        pending_connection_line_->setLine(line.x1(), line.y1(),
                                           mapToScene(event->pos()).x(),
                                           mapToScene(event->pos()).y());
        event->accept();
        return;
    }
    if (dragging_node_ != 0) {
        for (auto* item : scene_->items()) {
            if (item->data(0).isValid() && item->data(1).toInt() == 0 &&
                static_cast<NodeId>(item->data(0).toULongLong()) == dragging_node_) {
                const auto scene_position = mapToScene(event->pos());
                item->setPos(scene_position.x() - drag_offset_x_,
                             scene_position.y() - drag_offset_y_);
                break;
            }
        }
        event->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}

void NodeCanvas::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && dragging_from_input_ != 0) {
        const auto target_node = dragging_from_input_;
        const auto target_input = dragging_input_index_;
        QGraphicsItem* target = nullptr;
        for (auto* candidate : items(event->pos())) {
            if (dynamic_cast<QGraphicsLineItem*>(candidate) != nullptr) continue;
            target = candidate;
            break;
        }
        const bool dropped_on_output =
            target != nullptr && target->data(1).toInt() == 2;
        const bool dropped_on_empty_canvas = target == nullptr;
        const auto source_node = dropped_on_output
            ? static_cast<NodeId>(target->data(0).toULongLong()) : NodeId{0};
        if (pending_connection_line_ != nullptr) {
            scene_->removeItem(pending_connection_line_);
            delete pending_connection_line_;
            pending_connection_line_ = nullptr;
        }
        const bool was_dragged =
            (event->pos() - dragging_input_start_).manhattanLength() >=
            QApplication::startDragDistance();
        dragging_from_input_ = 0;
        dragging_input_index_ = 0;
        if (dropped_on_output && connection_requested_) {
            connection_requested_(source_node, target_node, target_input);
        } else if (was_dragged && dropped_on_empty_canvas &&
                   disconnection_requested_) {
            disconnection_requested_(target_node, target_input);
        }
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && dragging_from_output_ != 0) {
        const auto source = dragging_from_output_;
        QGraphicsItem* target = nullptr;
        for (auto* candidate : items(event->pos())) {
            if (dynamic_cast<QGraphicsLineItem*>(candidate) != nullptr) continue;
            target = candidate;
            break;
        }
        NodeId target_id = 0;
        std::uint8_t target_input = 0;
        if (target != nullptr && target->data(1).toInt() == 1) {
            target_id = static_cast<NodeId>(target->data(0).toULongLong());
            target_input = static_cast<std::uint8_t>(target->data(2).toInt());
        }
        if (pending_connection_line_ != nullptr) {
            scene_->removeItem(pending_connection_line_);
            delete pending_connection_line_;
            pending_connection_line_ = nullptr;
        }
        dragging_from_output_ = 0;
        if (target_id != 0 && connection_requested_)
            connection_requested_(source, target_id, target_input);
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && dragging_node_ != 0) {
        const auto node_id = dragging_node_;
        dragging_node_ = 0;
        if (position_changed_) for (auto* item : scene_->items()) {
            if (item->data(0).isValid() && item->data(1).toInt() == 0 &&
                static_cast<NodeId>(item->data(0).toULongLong()) == node_id) {
                const auto position = item->pos();
                if (position.x() != drag_start_x_ || position.y() != drag_start_y_)
                    position_changed_(node_id, position.x(), position.y());
                break;
            }
        }
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}
} // namespace fusion::nodes
