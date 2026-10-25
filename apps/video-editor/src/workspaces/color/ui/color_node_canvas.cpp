#include "color_node_canvas.h"

#include "ui/effects/effects_catalog.h"
#include "ui/media_browser/media_drag_mime.h"

#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QContextMenuEvent>
#include <QLineF>
#include <QMenu>
#include <QMouseEvent>
#include <QMimeData>
#include <QPainter>
#include <QScrollBar>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <utility>

namespace ui {
namespace {

constexpr qreal node_width = 154.0;
constexpr qreal node_height = 78.0;
constexpr qreal port_radius = 5.0;

QString colorEffectIdFromMime(const QMimeData* mime) {
    if (mime == nullptr || !mime->hasFormat(ui::kEffectIdMimeType)) return {};
    const auto value = mime->data(ui::kEffectIdMimeType);
    const auto id = QString::fromUtf8(value.constData(), value.size());
    return effects::actionFor(id, effects::Workspace::Color) ==
            effects::Action::VisualEffect
        ? id : QString{};
}

QString nodeTitle(const color::Node& node) {
    switch (node.type) {
    case color::NodeType::Input: return QObject::tr("Input");
    case color::NodeType::Curves: return QObject::tr("Curves");
    case color::NodeType::ParallelMixer: return QObject::tr("Parallel Mixer");
    case color::NodeType::Output: return QObject::tr("Output");
    case color::NodeType::Effect:
        if (!node.effect) return QObject::tr("Effect");
        if (node.effect->id == "video.primary_grade") return QObject::tr("Primary Grade");
        if (node.effect->id == "video.brightness") return QObject::tr("Brightness");
        if (node.effect->id == "video.contrast") return QObject::tr("Contrast");
        if (node.effect->id == "video.saturation") return QObject::tr("Saturation");
        if (node.effect->id == "video.grayscale") return QObject::tr("Grayscale");
        return QString::fromStdString(node.effect->id);
    }
    return QObject::tr("Node");
}

class GraphNodeItem final : public QGraphicsItem {
public:
    GraphNodeItem(color::Node node, std::uint32_t input_count, bool selected)
        : node_(std::move(node)), input_count_(input_count), selected_(selected) {
        setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
        setPos(node_.x, node_.y);
        setZValue(1.0);
    }

    QRectF boundingRect() const override {
        const auto rows = std::max<std::uint32_t>(1, input_count_);
        const auto height = std::max<qreal>(node_height, 30.0 + rows * 18.0);
        return {-port_radius - 2.0, -2.0,
                node_width + 2.0 * (port_radius + 2.0), height + 4.0};
    }

    void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override {
        painter->setRenderHint(QPainter::Antialiasing);
        const auto body = QRectF(0.0, 0.0, node_width, boundingRect().height() - 4.0);
        QColor fill = QColor(43, 51, 63);
        if (node_.type == color::NodeType::Input) fill = QColor(37, 68, 80);
        else if (node_.type == color::NodeType::Output) fill = QColor(51, 67, 48);
        else if (node_.type == color::NodeType::ParallelMixer) fill = QColor(69, 55, 82);
        else if (node_.type == color::NodeType::Curves) fill = QColor(68, 65, 43);
        painter->setBrush(fill);
        painter->setPen(QPen(selected_ ? QColor(245, 130, 50) : QColor(100, 116, 139),
                             selected_ ? 2.0 : 1.0));
        painter->drawRoundedRect(body, 7.0, 7.0);
        painter->setPen(QColor(231, 237, 245));
        QFont title_font = painter->font();
        title_font.setBold(true);
        painter->setFont(title_font);
        painter->drawText(QRectF(13.0, 12.0, node_width - 26.0, 24.0),
                          Qt::AlignLeft | Qt::AlignVCenter, nodeTitle(node_));
        painter->setFont(QFont{});
        if (node_.type == color::NodeType::Effect && node_.effect && !node_.effect->enabled) {
            painter->setPen(QColor(170, 180, 194));
            painter->drawText(QRectF(13.0, 39.0, node_width - 26.0, 18.0),
                              Qt::AlignLeft | Qt::AlignVCenter, QObject::tr("Off"));
        } else if (node_.type == color::NodeType::Curves && node_.curves &&
                   !node_.curves->enabled) {
            painter->setPen(QColor(170, 180, 194));
            painter->drawText(QRectF(13.0, 39.0, node_width - 26.0, 18.0),
                              Qt::AlignLeft | Qt::AlignVCenter, QObject::tr("Off"));
        } else if (node_.type == color::NodeType::ParallelMixer) {
            painter->setPen(QColor(170, 180, 194));
            painter->drawText(QRectF(13.0, 40.0, node_width - 26.0, 18.0),
                              Qt::AlignLeft | Qt::AlignVCenter, QObject::tr("Weighted mix"));
        }
        painter->setPen(Qt::NoPen);
        if (node_.type != color::NodeType::Input) {
            for (std::uint32_t index = 0; index < input_count_; ++index) {
                painter->setBrush(QColor(74, 193, 220));
                painter->drawEllipse(inputPort(index), port_radius, port_radius);
            }
        }
        if (node_.type != color::NodeType::Output) {
            painter->setBrush(QColor(168, 218, 92));
            painter->drawEllipse(outputPort(), port_radius, port_radius);
        }
    }

    [[nodiscard]] const color::Node& node() const noexcept { return node_; }
    [[nodiscard]] QPointF outputPort() const noexcept {
        return {node_width, (boundingRect().height() - 4.0) * 0.5};
    }
    [[nodiscard]] QPointF inputPort(std::uint32_t index) const noexcept {
        const auto count = std::max<std::uint32_t>(1, input_count_);
        const auto spacing = (boundingRect().height() - 20.0) / (count + 1.0);
        return {0.0, 10.0 + spacing * (index + 1.0)};
    }
    [[nodiscard]] std::uint32_t inputCount() const noexcept { return input_count_; }

private:
    color::Node node_;
    std::uint32_t input_count_ = 1;
    bool selected_ = false;
};

QPointF portScenePosition(const GraphNodeItem* item, bool output, std::uint32_t input = 0) {
    return item->mapToScene(output ? item->outputPort() : item->inputPort(input));
}

}  // namespace

ColorNodeCanvas::ColorNodeCanvas(QWidget* parent)
    : QGraphicsView(parent), scene_(new QGraphicsScene(this)),
      graph_(color::makePassthroughGraph()) {
    setObjectName(QStringLiteral("colorNodeCanvas"));
    setScene(scene_);
    setRenderHint(QPainter::Antialiasing);
    setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);
    setDragMode(QGraphicsView::NoDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setMinimumSize(300, 210);
    setBackgroundBrush(QColor(29, 34, 42));
    setAcceptDrops(true);
    viewport()->setAcceptDrops(true);
    refreshScene();
}

void ColorNodeCanvas::setGraph(color::Graph graph, std::optional<color::NodeId> selected) {
    if (!color::validate(graph)) graph = color::makePassthroughGraph();
    graph_ = std::move(graph);
    selected_node_ = selected && color::findNode(graph_, *selected) ? selected : std::nullopt;
    refreshScene();
}

std::optional<color::NodeId> ColorNodeCanvas::selectedNode() const noexcept {
    return selected_node_;
}

void ColorNodeCanvas::setGraphChangedHandler(std::function<void(color::Graph)> handler) {
    graph_changed_handler_ = std::move(handler);
}

void ColorNodeCanvas::setSelectionChangedHandler(
    std::function<void(color::NodeId)> handler) {
    selection_changed_handler_ = std::move(handler);
}

void ColorNodeCanvas::setEffectDropRequestedHandler(
    std::function<bool(const QString&, const QPointF&)> handler) {
    effect_drop_requested_handler_ = std::move(handler);
}

void ColorNodeCanvas::drawBackground(QPainter* painter, const QRectF& rect) {
    painter->fillRect(rect, QColor(29, 34, 42));
    constexpr qreal grid = 24.0;
    const auto left = static_cast<int>(std::floor(rect.left() / grid));
    const auto right = static_cast<int>(std::ceil(rect.right() / grid));
    const auto top = static_cast<int>(std::floor(rect.top() / grid));
    const auto bottom = static_cast<int>(std::ceil(rect.bottom() / grid));
    painter->setPen(QPen(QColor(42, 49, 59), 0.0));
    for (int x = left; x <= right; ++x)
        painter->drawLine(QPointF(x * grid, rect.top()), QPointF(x * grid, rect.bottom()));
    for (int y = top; y <= bottom; ++y)
        painter->drawLine(QPointF(rect.left(), y * grid), QPointF(rect.right(), y * grid));
}

std::optional<ColorNodeCanvas::PortHit> ColorNodeCanvas::inputAt(
    const QPointF& scene_position) const {
    for (auto* item : scene_->items()) {
        auto* node_item = dynamic_cast<GraphNodeItem*>(item);
        if (!node_item || node_item->node().type == color::NodeType::Input) continue;
        for (std::uint32_t index = 0; index < node_item->inputCount(); ++index) {
            if (QLineF(scene_position, portScenePosition(node_item, false, index)).length() <= 12.0)
                return PortHit{node_item->node().id, index};
        }
    }
    return std::nullopt;
}

std::optional<color::NodeId> ColorNodeCanvas::outputAt(
    const QPointF& scene_position) const {
    for (auto* item : scene_->items()) {
        auto* node_item = dynamic_cast<GraphNodeItem*>(item);
        if (!node_item || node_item->node().type == color::NodeType::Output) continue;
        if (QLineF(scene_position, portScenePosition(node_item, true)).length() <= 12.0)
            return node_item->node().id;
    }
    return std::nullopt;
}

void ColorNodeCanvas::refreshScene() {
    rebuilding_ = true;
    scene_->clear();
    temporary_connection_ = nullptr;
    std::unordered_map<color::NodeId, GraphNodeItem*> items;
    for (const auto& node : graph_.nodes) {
        std::uint32_t input_count = 1;
        if (node.type == color::NodeType::Input) input_count = 0;
        else if (node.type == color::NodeType::ParallelMixer) {
            input_count = 1;
            for (const auto& edge : graph_.connections)
                if (edge.to == node.id) input_count = std::max(input_count, edge.input + 2);
        }
        auto* item = new GraphNodeItem(node, input_count, selected_node_ == node.id);
        scene_->addItem(item);
        items.emplace(node.id, item);
    }
    for (const auto& edge : graph_.connections) {
        const auto from = items.find(edge.from);
        const auto to = items.find(edge.to);
        if (from == items.end() || to == items.end()) continue;
        const auto start = portScenePosition(from->second, true);
        const auto end = portScenePosition(to->second, false, edge.input);
        const auto bend = std::max(35.0, std::abs(end.x() - start.x()) * 0.45);
        QPainterPath path(start);
        path.cubicTo(start + QPointF(bend, 0.0), end - QPointF(bend, 0.0), end);
        auto* line = scene_->addPath(path, QPen(QColor(133, 148, 164), 2.0));
        line->setZValue(-1.0);
    }
    scene_->setSceneRect(scene_->itemsBoundingRect().adjusted(-80.0, -70.0, 80.0, 70.0));
    rebuilding_ = false;
}

void ColorNodeCanvas::commitGraph() {
    if (rebuilding_ || !color::validate(graph_)) return;
    if (graph_changed_handler_) graph_changed_handler_(graph_);
}

void ColorNodeCanvas::mousePressEvent(QMouseEvent* event) {
    const auto position = mapToScene(event->position().toPoint());
    if (event->button() == Qt::MiddleButton) {
        panning_ = true;
        last_pan_position_ = event->position().toPoint();
        viewport()->setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton) {
        if (const auto source = outputAt(position)) {
            connecting_from_ = source;
            if (selection_changed_handler_) selection_changed_handler_(*source);
            auto* line = scene_->addPath(QPainterPath(), QPen(QColor(245, 174, 75), 2.0,
                Qt::DashLine));
            line->setZValue(2.0);
            temporary_connection_ = line;
            event->accept();
            return;
        }
        const auto items = scene_->items(position);
        for (auto* item : items) {
            if (auto* node_item = dynamic_cast<GraphNodeItem*>(item)) {
                selected_node_ = node_item->node().id;
                refreshScene();
                if (selection_changed_handler_) selection_changed_handler_(*selected_node_);
                break;
            }
        }
    }
    QGraphicsView::mousePressEvent(event);
}

void ColorNodeCanvas::mouseMoveEvent(QMouseEvent* event) {
    if (panning_) {
        const auto current = event->position().toPoint();
        const auto delta = current - last_pan_position_;
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        last_pan_position_ = current;
        event->accept();
        return;
    }
    if (connecting_from_ && temporary_connection_) {
        GraphNodeItem* source_item = nullptr;
        for (auto* item : scene_->items()) {
            auto* node_item = dynamic_cast<GraphNodeItem*>(item);
            if (node_item && node_item->node().id == *connecting_from_) {
                source_item = node_item;
                break;
            }
        }
        if (source_item) {
            QPainterPath path(portScenePosition(source_item, true));
            const auto end = mapToScene(event->position().toPoint());
            const auto bend = std::max(35.0, std::abs(end.x() - path.currentPosition().x()) * 0.45);
            path.cubicTo(path.currentPosition() + QPointF(bend, 0.0),
                         end - QPointF(bend, 0.0), end);
            temporary_connection_->setPath(path);
        }
        event->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}

void ColorNodeCanvas::mouseReleaseEvent(QMouseEvent* event) {
    if (panning_ && event->button() == Qt::MiddleButton) {
        panning_ = false;
        viewport()->unsetCursor();
        event->accept();
        return;
    }
    if (connecting_from_ && event->button() == Qt::LeftButton) {
        const auto target = inputAt(mapToScene(event->position().toPoint()));
        if (target && color::connect(graph_, *connecting_from_, target->node_id, target->input)) {
            selected_node_ = target->node_id;
            refreshScene();
            commitGraph();
            if (selection_changed_handler_) selection_changed_handler_(*selected_node_);
        } else if (temporary_connection_) {
            scene_->removeItem(temporary_connection_);
            delete temporary_connection_;
            temporary_connection_ = nullptr;
        }
        connecting_from_.reset();
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
    bool moved = false;
    for (auto* item : scene_->items()) {
        auto* node_item = dynamic_cast<GraphNodeItem*>(item);
        if (!node_item) continue;
        if (auto* node = color::findNode(graph_, node_item->node().id)) {
            const auto position = node_item->pos();
            if (node->x != position.x() || node->y != position.y()) {
                node->x = position.x();
                node->y = position.y();
                moved = true;
            }
        }
    }
    if (moved) commitGraph();
}

void ColorNodeCanvas::wheelEvent(QWheelEvent* event) {
    const auto steps = event->angleDelta().y() / 120.0;
    const auto factor = std::pow(1.12, steps);
    const auto current = transform().m11();
    if ((factor > 1.0 && current < 2.5) || (factor < 1.0 && current > 0.45))
        scale(factor, factor);
    event->accept();
}

void ColorNodeCanvas::contextMenuEvent(QContextMenuEvent* event) {
    const auto scene_position = mapToScene(event->pos());
    GraphNodeItem* hit = nullptr;
    for (auto* item : scene_->items(scene_position)) {
        hit = dynamic_cast<GraphNodeItem*>(item);
        if (hit) break;
    }
    if (hit) {
        selected_node_ = hit->node().id;
        refreshScene();
        if (selection_changed_handler_) selection_changed_handler_(*selected_node_);
    }
    const auto* selected = selected_node_ ? color::findNode(graph_, *selected_node_) : nullptr;
    if (!selected) return;

    QMenu menu(this);
    menu.setObjectName(QStringLiteral("colorNodeCanvasContextMenu"));
    auto* add_curves = menu.addAction(tr("Add Curves"));
    add_curves->setEnabled(selected->type != color::NodeType::Output);
    QAction* toggle = nullptr;
    QAction* remove = nullptr;
    if (selected->type == color::NodeType::Curves && selected->curves) {
        toggle = menu.addAction(selected->curves->enabled ? tr("Disable Curves")
                                                         : tr("Enable Curves"));
        remove = menu.addAction(tr("Remove Curves"));
    }
    const auto* chosen = menu.exec(event->globalPos());
    if (chosen == add_curves) {
        auto graph = graph_;
        const auto anchor = selected->id;
        if (color::insertCurvesAfter(graph, anchor)) {
            const auto inserted = graph.nodes.back().id;
            graph_ = std::move(graph);
            selected_node_ = inserted;
            refreshScene();
            commitGraph();
            if (selection_changed_handler_) selection_changed_handler_(inserted);
        }
    } else if (chosen == toggle) {
        auto graph = graph_;
        if (color::setCurvesEnabled(graph, selected->id, !selected->curves->enabled)) {
            graph_ = std::move(graph);
            refreshScene();
            commitGraph();
        }
    } else if (chosen == remove) {
        auto graph = graph_;
        const auto removed_id = selected->id;
        if (color::removeEffect(graph, removed_id)) {
            selected_node_.reset();
            const auto input = std::find_if(graph.nodes.begin(), graph.nodes.end(),
                [](const color::Node& node) { return node.type == color::NodeType::Input; });
            if (input != graph.nodes.end()) selected_node_ = input->id;
            graph_ = std::move(graph);
            refreshScene();
            commitGraph();
            if (selected_node_ && selection_changed_handler_)
                selection_changed_handler_(*selected_node_);
        }
    }
    event->accept();
}

void ColorNodeCanvas::dragEnterEvent(QDragEnterEvent* event) {
    if (!colorEffectIdFromMime(event->mimeData()).isEmpty()) {
        event->acceptProposedAction();
        return;
    }
    event->ignore();
}

void ColorNodeCanvas::dragMoveEvent(QDragMoveEvent* event) {
    if (!colorEffectIdFromMime(event->mimeData()).isEmpty()) {
        event->acceptProposedAction();
        return;
    }
    event->ignore();
}

void ColorNodeCanvas::dropEvent(QDropEvent* event) {
    const auto effect_id = colorEffectIdFromMime(event->mimeData());
    if (effect_id.isEmpty() || !effect_drop_requested_handler_) {
        event->ignore();
        return;
    }

    const auto position = mapToScene(event->position().toPoint());
    if (!effect_drop_requested_handler_(effect_id, position)) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
}

}  // namespace ui
