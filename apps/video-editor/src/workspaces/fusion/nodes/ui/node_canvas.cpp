#include "node_canvas.h"

#include "ui/media_browser/media_drag_mime.h"
#include "ui/effects/effects_catalog.h"

#include <creative_suite/effects/effects.h>

#include <QApplication>
#include <QAction>
#include <QActionGroup>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QGraphicsEllipseItem>
#include <QGraphicsItem>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QContextMenuEvent>
#include <QLineF>
#include <QMenu>
#include <QMetaType>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QBrush>
#include <QColor>
#include <QFont>
#include <QVector>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <queue>
#include <array>
#include <vector>
#include <QCoreApplication>

namespace fusion::nodes {
namespace {
QString title(NodeType type) {
    switch (type) {
    case NodeType::Input: return ::effects::translateCatalogLabel(QStringLiteral("Input"));
    case NodeType::Background: return ::effects::translateCatalogLabel(QStringLiteral("Background"));
    case NodeType::Text: return ::effects::translateCatalogLabel(QStringLiteral("Text"));
    case NodeType::RectangleMask: return ::effects::translateCatalogLabel(QStringLiteral("Rectangle Mask"));
    case NodeType::EllipseMask: return ::effects::translateCatalogLabel(QStringLiteral("Ellipse Mask"));
    case NodeType::Transform: return ::effects::translateCatalogLabel(QStringLiteral("Transform"));
    case NodeType::Color: return ::effects::translateCatalogLabel(QStringLiteral("Color"));
    case NodeType::GaussianBlur: return ::effects::translateCatalogLabel(QStringLiteral("Gaussian Blur"));
    case NodeType::Merge: return ::effects::translateCatalogLabel(QStringLiteral("Merge"));
    case NodeType::Output: return QCoreApplication::translate("NodeCanvas", "Output");
    case NodeType::Effect: return ::effects::translateCatalogLabel(QStringLiteral("Effect"));
    }
    return ::effects::translateCatalogLabel(QStringLiteral("Node"));
}

QString title(const Node& node) {
    if (node.type == NodeType::Effect) {
        if (const auto* definition = creative_suite::effects::findDefinition(node.effect.id))
            return ::effects::translateCatalogLabel(QString::fromUtf8(
                definition->name.data(), static_cast<qsizetype>(definition->name.size())));
    }
    return title(node.type);
}

QString effectIdFromMime(const QMimeData* mime) {
    if (mime == nullptr || !mime->hasFormat(ui::kEffectIdMimeType)) return {};
    const auto value = mime->data(ui::kEffectIdMimeType);
    const auto id = QString::fromUtf8(value.constData(), value.size());
    return effects::isDraggable(id, effects::Workspace::Fusion) ? id : QString{};
}

double inputPortY(NodeType type, std::uint8_t input) {
    if (input == 2) return 0.0;
    if (type == NodeType::Merge) return input == 0 ? 42.0 : 80.0;
    return 47.0;
}

double outputPortY(NodeType type) {
    return type == NodeType::Merge ? 56.0 : 47.0;
}

double nodeHeight(NodeType type) {
    return type == NodeType::Merge ? 132.0 : 112.0;
}

constexpr auto kGridSettingsGroup = "fusion/node_editor/grid";
constexpr int kGridMinimumSpacing = 12;
constexpr int kGridMaximumSpacing = 64;
constexpr int kGridMinimumIntensity = 10;
constexpr int kGridMaximumIntensity = 60;
constexpr auto kNodeEditorSettingsGroup = "fusion/node_editor";

struct NodeCanvasBehaviorSettings {
    ConnectionLineStyle line_style = ConnectionLineStyle::Straight;
    bool snap_to_grid = false;
};

NodeCanvasBehaviorSettings readNodeCanvasBehaviorSettings(QSettings& settings) {
    NodeCanvasBehaviorSettings result;
    settings.beginGroup(QString::fromLatin1(kNodeEditorSettingsGroup));
    bool valid = false;
    const auto style = settings.value(QStringLiteral("connection_line_style"), 0)
                           .toInt(&valid);
    if (valid && style == static_cast<int>(ConnectionLineStyle::Curved))
        result.line_style = ConnectionLineStyle::Curved;
    const auto snap = settings.value(QStringLiteral("snap_to_grid"), false);
    const auto normalized = snap.toString().trimmed().toLower();
    if (snap.metaType().id() == QMetaType::Bool || normalized == "true" ||
        normalized == "false" || normalized == "1" || normalized == "0")
        result.snap_to_grid = snap.toBool();
    settings.endGroup();
    return result;
}

struct GridPoint {
    int x = 0;
    int y = 0;
    friend bool operator==(const GridPoint&, const GridPoint&) = default;
};

struct RouteEntry {
    int x = 0;
    int y = 0;
    int direction = 4;
    int cost = 0;
    int estimate = 0;
};

struct RouteEntryLater {
    bool operator()(const RouteEntry& left, const RouteEntry& right) const noexcept {
        if (left.estimate != right.estimate) return left.estimate > right.estimate;
        if (left.cost != right.cost) return left.cost > right.cost;
        if (left.y != right.y) return left.y > right.y;
        if (left.x != right.x) return left.x > right.x;
        return left.direction > right.direction;
    }
};

QPointF gridPosition(GridPoint point, int spacing) {
    return QPointF(point.x * spacing, point.y * spacing);
}

int gridCoordinate(double value, int spacing) {
    return static_cast<int>(std::lround(value / static_cast<double>(spacing)));
}

QPainterPath smoothOrthogonalPath(const std::vector<QPointF>& points,
                                  double corner_radius) {
    QPainterPath path;
    if (points.empty()) return path;
    path.moveTo(points.front());
    if (points.size() == 1) return path;
    for (std::size_t i = 1; i + 1 < points.size(); ++i) {
        const auto before = points[i - 1];
        const auto corner = points[i];
        const auto after = points[i + 1];
        const auto before_length = QLineF(before, corner).length();
        const auto after_length = QLineF(corner, after).length();
        const auto radius = std::min({corner_radius, before_length / 2.0,
                                      after_length / 2.0});
        if (radius <= 0.01) {
            path.lineTo(corner);
            continue;
        }
        const auto approach = corner + (before - corner) * (radius / before_length);
        const auto depart = corner + (after - corner) * (radius / after_length);
        path.lineTo(approach);
        path.quadTo(corner, depart);
    }
    path.lineTo(points.back());
    return path;
}

QPainterPath connectionPath(
    const QPointF& start, const QPointF& end,
    std::optional<NodeId> source_id, std::optional<NodeId> target_id,
    std::uint8_t input, const NodeGraph& graph,
    const std::unordered_map<NodeId, QPointF>& node_positions,
    ConnectionLineStyle style, bool snap_to_grid, int spacing) {
    QPainterPath direct;
    direct.moveTo(start);
    if (!snap_to_grid) {
        if (style == ConnectionLineStyle::Straight) {
            direct.lineTo(end);
        } else {
            const auto dx = end.x() - start.x();
            const auto handle = std::max(40.0, std::abs(dx) * 0.45) *
                                (dx >= 0.0 ? 1.0 : -1.0);
            direct.cubicTo(start + QPointF(handle, 0.0),
                           end - QPointF(handle, 0.0), end);
        }
        return direct;
    }

    const auto start_direction = source_id ? 1 :
        (target_id && input == 2 ? 0 : 3); // right, up, or left
    const auto end_direction = target_id ?
        (input == 2 ? 0 : 3) : 4; // up for mask, left for image, or free
    const auto start_grid_x = start_direction == 1
        ? static_cast<int>(std::floor(start.x() / spacing)) + 1
        : start_direction == 3
            ? static_cast<int>(std::ceil(start.x() / spacing)) - 1
            : gridCoordinate(start.x(), spacing);
    const auto start_grid_y = start_direction == 2
        ? static_cast<int>(std::floor(start.y() / spacing)) + 1
        : start_direction == 0
            ? static_cast<int>(std::ceil(start.y() / spacing)) - 1
            : gridCoordinate(start.y(), spacing);
    const auto end_grid_x = end_direction == 3
        ? static_cast<int>(std::ceil(end.x() / spacing)) - 1
        : gridCoordinate(end.x(), spacing);
    const auto end_grid_y = end_direction == 0
        ? static_cast<int>(std::ceil(end.y() / spacing)) - 1
        : gridCoordinate(end.y(), spacing);
    const GridPoint start_grid{start_grid_x, start_grid_y};
    const GridPoint end_grid{end_grid_x, end_grid_y};

    std::vector<QRectF> obstacles;
    const auto margin = std::max(12.0, static_cast<double>(spacing) * 0.75);
    for (const auto& node : graph.nodes) {
        const auto position = node_positions.find(node.id);
        if (position == node_positions.end()) continue;
        const bool endpoint = (source_id && node.id == *source_id) ||
                              (target_id && node.id == *target_id);
        const auto node_margin = endpoint ? 0.0 : margin;
        obstacles.push_back(QRectF(position->second,
            QSizeF(210.0, nodeHeight(node.type))).adjusted(
                -node_margin, -node_margin, node_margin, node_margin));
    }

    const auto blocked_segment = [&obstacles](QPointF first, QPointF second) {
        constexpr auto epsilon = 0.01;
        for (const auto& obstacle : obstacles) {
            if (std::abs(first.y() - second.y()) < epsilon) {
                if (first.y() > obstacle.top() + epsilon &&
                    first.y() < obstacle.bottom() - epsilon &&
                    std::max(first.x(), second.x()) > obstacle.left() + epsilon &&
                    std::min(first.x(), second.x()) < obstacle.right() - epsilon)
                    return true;
            } else if (std::abs(first.x() - second.x()) < epsilon) {
                if (first.x() > obstacle.left() + epsilon &&
                    first.x() < obstacle.right() - epsilon &&
                    std::max(first.y(), second.y()) > obstacle.top() + epsilon &&
                    std::min(first.y(), second.y()) < obstacle.bottom() - epsilon)
                    return true;
            }
        }
        return false;
    };

    int min_x = std::min(start_grid.x, end_grid.x);
    int max_x = std::max(start_grid.x, end_grid.x);
    int min_y = std::min(start_grid.y, end_grid.y);
    int max_y = std::max(start_grid.y, end_grid.y);
    for (const auto& obstacle : obstacles) {
        min_x = std::min(min_x, static_cast<int>(std::floor(obstacle.left() / spacing)));
        max_x = std::max(max_x, static_cast<int>(std::ceil(obstacle.right() / spacing)));
        min_y = std::min(min_y, static_cast<int>(std::floor(obstacle.top() / spacing)));
        max_y = std::max(max_y, static_cast<int>(std::ceil(obstacle.bottom() / spacing)));
    }
    min_x -= 8; max_x += 8; min_y -= 8; max_y += 8;

    const auto width = max_x - min_x + 1;
    const auto state_key = [min_x, min_y, width](int x, int y, int direction) {
        return (((y - min_y) * width + (x - min_x)) * 5) + direction;
    };
    const auto manhattan = [&end_grid](int x, int y) {
        return (std::abs(x - end_grid.x) + std::abs(y - end_grid.y)) * 10;
    };
    std::priority_queue<RouteEntry, std::vector<RouteEntry>, RouteEntryLater> queue;
    std::unordered_map<int, int> distances;
    std::unordered_map<int, int> previous;
    const auto start_key = state_key(start_grid.x, start_grid.y, 4);
    distances.emplace(start_key, 0);
    queue.push({start_grid.x, start_grid.y, 4, 0,
                manhattan(start_grid.x, start_grid.y)});
    constexpr std::array<int, 4> delta_x{0, 1, 0, -1};
    constexpr std::array<int, 4> delta_y{-1, 0, 1, 0};
    int finish_key = -1;
    while (!queue.empty()) {
        const auto current = queue.top();
        queue.pop();
        const auto current_key = state_key(current.x, current.y, current.direction);
        const auto known = distances.find(current_key);
        if (known == distances.end() || known->second != current.cost) continue;
        if (current.x == end_grid.x && current.y == end_grid.y) {
            finish_key = current_key;
            break;
        }
        const auto current_position = gridPosition({current.x, current.y}, spacing);
        for (int direction = 0; direction < 4; ++direction) {
            const auto next_x = current.x + delta_x[direction];
            const auto next_y = current.y + delta_y[direction];
            if (next_x < min_x || next_x > max_x || next_y < min_y || next_y > max_y)
                continue;
            const auto next_position = gridPosition({next_x, next_y}, spacing);
            if (blocked_segment(current_position, next_position)) continue;
            const auto next_cost = current.cost + 10 +
                (current.direction != 4 && current.direction != direction ? 4 : 0);
            const auto next_key = state_key(next_x, next_y, direction);
            const auto old = distances.find(next_key);
            if (old != distances.end() && old->second <= next_cost) continue;
            distances[next_key] = next_cost;
            previous[next_key] = current_key;
            queue.push({next_x, next_y, direction, next_cost,
                        next_cost + manhattan(next_x, next_y)});
        }
    }

    if (finish_key < 0) {
        direct.lineTo(end);
        return direct;
    }
    std::vector<QPointF> route;
    for (auto key = finish_key; key != start_key;) {
        const auto cell = key / 5;
        const auto x = cell % width + min_x;
        const auto y = cell / width + min_y;
        route.push_back(gridPosition({x, y}, spacing));
        const auto parent = previous.find(key);
        if (parent == previous.end()) break;
        key = parent->second;
    }
    route.push_back(gridPosition(start_grid, spacing));
    std::reverse(route.begin(), route.end());
    std::vector<QPointF> simplified;
    simplified.push_back(start);
    auto start_stub = gridPosition(start_grid, spacing);
    if (start_direction == 1 || start_direction == 3) start_stub.setY(start.y());
    else start_stub.setX(start.x());
    if (QLineF(simplified.back(), start_stub).length() > 0.01)
        simplified.push_back(start_stub);
    if (QLineF(simplified.back(), route.front()).length() > 0.01)
        simplified.push_back(route.front());
    for (std::size_t i = 1; i < route.size(); ++i) {
        const auto previous_point = simplified.back();
        const auto point = route[i];
        if (simplified.size() >= 2) {
            const auto before = simplified[simplified.size() - 2];
            if ((std::abs(before.x() - previous_point.x()) < 0.01 &&
                 std::abs(previous_point.x() - point.x()) < 0.01) ||
                (std::abs(before.y() - previous_point.y()) < 0.01 &&
                 std::abs(previous_point.y() - point.y()) < 0.01)) {
                simplified.back() = point;
                continue;
            }
        }
        simplified.push_back(point);
    }
    auto end_stub = gridPosition(end_grid, spacing);
    if (end_direction == 3) end_stub.setY(end.y());
    else if (end_direction == 0) end_stub.setX(end.x());
    if (QLineF(simplified.back(), end_stub).length() > 0.01)
        simplified.push_back(end_stub);
    if (QLineF(simplified.back(), end).length() > 0.01)
        simplified.push_back(end);
    if (style == ConnectionLineStyle::Curved)
        return smoothOrthogonalPath(simplified, static_cast<double>(spacing) * 0.35);
    QPainterPath path;
    path.moveTo(simplified.front());
    for (std::size_t i = 1; i < simplified.size(); ++i)
        path.lineTo(simplified[i]);
    return path;
}

BackgroundGridSettings readGridSettings(QSettings& settings) {
    BackgroundGridSettings result;
    settings.beginGroup(QString::fromLatin1(kGridSettingsGroup));

    if (settings.contains(QStringLiteral("visible"))) {
        const auto value = settings.value(QStringLiteral("visible"));
        const auto normalized = value.toString().trimmed().toLower();
        if (value.metaType().id() == QMetaType::Bool || normalized == "true" ||
            normalized == "false" || normalized == "1" || normalized == "0") {
            result.visible = value.toBool();
        }
    }

    bool valid = false;
    const auto style = settings.value(QStringLiteral("style"),
        static_cast<int>(BackgroundGridStyle::Lines)).toInt(&valid);
    if (valid && style == static_cast<int>(BackgroundGridStyle::Dots))
        result.style = BackgroundGridStyle::Dots;
    else
        result.style = BackgroundGridStyle::Lines;

    valid = false;
    const auto spacing = settings.value(QStringLiteral("spacing"), result.spacing)
                             .toInt(&valid);
    if (valid) result.spacing = std::clamp(
        spacing, kGridMinimumSpacing, kGridMaximumSpacing);

    valid = false;
    const auto intensity = settings.value(QStringLiteral("intensity"),
        result.intensity_percent).toInt(&valid);
    if (valid) result.intensity_percent = std::clamp(
        intensity, kGridMinimumIntensity, kGridMaximumIntensity);

    settings.endGroup();
    return result;
}
}

NodeCanvas::NodeCanvas(QWidget* parent) : QGraphicsView(parent) {
    grid_settings_ = readGridSettings(settings_);
    const auto behavior = readNodeCanvasBehaviorSettings(settings_);
    connection_line_style_ = behavior.line_style;
    snap_to_grid_ = behavior.snap_to_grid;
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

void NodeCanvas::setBackgroundGridSettings(BackgroundGridSettings settings) {
    settings.spacing = std::clamp(
        settings.spacing, kGridMinimumSpacing, kGridMaximumSpacing);
    settings.intensity_percent = std::clamp(
        settings.intensity_percent, kGridMinimumIntensity, kGridMaximumIntensity);
    if (settings.style != BackgroundGridStyle::Dots)
        settings.style = BackgroundGridStyle::Lines;
    if (settings == grid_settings_) return;

    grid_settings_ = settings;
    settings_.beginGroup(QString::fromLatin1(kGridSettingsGroup));
    settings_.setValue(QStringLiteral("visible"), grid_settings_.visible);
    settings_.setValue(QStringLiteral("style"),
                       static_cast<int>(grid_settings_.style));
    settings_.setValue(QStringLiteral("spacing"), grid_settings_.spacing);
    settings_.setValue(QStringLiteral("intensity"),
                       grid_settings_.intensity_percent);
    settings_.endGroup();
    updateConnectionLines();
    if (pending_connection_line_ != nullptr)
        updatePendingConnectionLine(pending_cursor_scene_);
    viewport()->update();
}

void NodeCanvas::setConnectionLineStyle(ConnectionLineStyle style) {
    if (style != ConnectionLineStyle::Curved)
        style = ConnectionLineStyle::Straight;
    if (style == connection_line_style_) return;
    connection_line_style_ = style;
    settings_.beginGroup(QString::fromLatin1(kNodeEditorSettingsGroup));
    settings_.setValue(QStringLiteral("connection_line_style"),
                       static_cast<int>(connection_line_style_));
    settings_.endGroup();
    updateConnectionLines();
    if (pending_connection_line_ != nullptr)
        updatePendingConnectionLine(pending_cursor_scene_);
}

void NodeCanvas::setSnapToGrid(bool enabled) {
    if (enabled == snap_to_grid_) return;
    snap_to_grid_ = enabled;
    settings_.beginGroup(QString::fromLatin1(kNodeEditorSettingsGroup));
    settings_.setValue(QStringLiteral("snap_to_grid"), snap_to_grid_);
    settings_.endGroup();
    updateConnectionLines();
    if (pending_connection_line_ != nullptr)
        updatePendingConnectionLine(pending_cursor_scene_);
}

QPointF NodeCanvas::snapPositionToGrid(QPointF position) const noexcept {
    if (!snap_to_grid_) return position;
    const auto spacing = static_cast<double>(grid_settings_.spacing);
    position.setX(std::round(position.x() / spacing) * spacing);
    position.setY(std::round(position.y() / spacing) * spacing);
    return position;
}

void NodeCanvas::drawBackground(QPainter* painter, const QRectF& rect) {
    painter->save();
    painter->fillRect(rect, backgroundBrush());
    if (!grid_settings_.visible || rect.isEmpty()) {
        painter->restore();
        return;
    }

    const auto spacing = static_cast<double>(grid_settings_.spacing);
    const auto first_x = static_cast<qint64>(std::floor(rect.left() / spacing));
    const auto last_x = static_cast<qint64>(std::ceil(rect.right() / spacing));
    const auto first_y = static_cast<qint64>(std::floor(rect.top() / spacing));
    const auto last_y = static_cast<qint64>(std::ceil(rect.bottom() / spacing));
    const auto intensity = static_cast<double>(grid_settings_.intensity_percent) / 100.0;
    QColor minor_color(QStringLiteral("#8999ae"));
    minor_color.setAlphaF(intensity * 0.4);
    QColor major_color(QStringLiteral("#a4b7d0"));
    major_color.setAlphaF(intensity * 0.9);

    painter->setRenderHint(QPainter::Antialiasing, false);
    if (grid_settings_.style == BackgroundGridStyle::Lines) {
        QVector<QLineF> minor_lines;
        QVector<QLineF> major_lines;
        for (auto index = first_x; index <= last_x; ++index) {
            const auto x = static_cast<double>(index) * spacing;
            auto& lines = index % 5 == 0 ? major_lines : minor_lines;
            lines.push_back(QLineF(x, rect.top(), x, rect.bottom()));
        }
        for (auto index = first_y; index <= last_y; ++index) {
            const auto y = static_cast<double>(index) * spacing;
            auto& lines = index % 5 == 0 ? major_lines : minor_lines;
            lines.push_back(QLineF(rect.left(), y, rect.right(), y));
        }
        painter->setPen(QPen(minor_color, 1.0));
        painter->drawLines(minor_lines.constData(),
                           static_cast<int>(minor_lines.size()));
        painter->setPen(QPen(major_color, 1.0));
        painter->drawLines(major_lines.constData(),
                           static_cast<int>(major_lines.size()));
    } else {
        QVector<QPointF> minor_dots;
        QVector<QPointF> major_dots;
        for (auto x_index = first_x; x_index <= last_x; ++x_index) {
            for (auto y_index = first_y; y_index <= last_y; ++y_index) {
                const QPointF point(
                    static_cast<double>(x_index) * spacing,
                    static_cast<double>(y_index) * spacing);
                if (x_index % 5 == 0 && y_index % 5 == 0)
                    major_dots.push_back(point);
                else
                    minor_dots.push_back(point);
            }
        }
        painter->setPen(QPen(minor_color, 1.0));
        painter->drawPoints(minor_dots.constData(),
                            static_cast<int>(minor_dots.size()));
        painter->setPen(QPen(major_color, 2.0));
        painter->drawPoints(major_dots.constData(),
                            static_cast<int>(major_dots.size()));
    }
    painter->restore();
}

void NodeCanvas::setGraph(const NodeGraph& graph) {
    graph_ = graph;
    dragging_from_output_ = 0;
    dragging_from_input_ = 0;
    dragging_input_index_ = 0;
    dragging_node_ = 0;
    pending_connection_line_ = nullptr;
    pending_fixed_node_ = 0;
    pending_input_index_ = 0;
    connections_ = graph.connections;
    scene_->clear();
    std::unordered_map<NodeId, QGraphicsRectItem*> items;
    for (const auto& node : graph.nodes) {
        const auto height = nodeHeight(node.type);
        auto* item = scene_->addRect(QRectF(0, 0, 210, height),
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
        auto* viewer_button = scene_->addRect(
            QRectF(12, height - 24.0, 48, 16),
            QPen(node.id == viewer_node_id_ ? QColor("#66b5ff") : QColor("#5b6574"), 1.0),
            QBrush(node.id == viewer_node_id_ ? QColor("#285578") : QColor("#303742")));
        viewer_button->setParentItem(item);
        viewer_button->setData(0, QVariant::fromValue<qulonglong>(node.id));
        viewer_button->setData(1, 4);
        viewer_button->setData(3, node.id == viewer_node_id_);
        viewer_button->setToolTip(QCoreApplication::translate("NodeCanvas", "Show this node in the Fusion Viewer."));
        viewer_button->setFlag(QGraphicsItem::ItemIsSelectable, false);
        viewer_button->setZValue(2);
        auto* viewer_label = scene_->addText(QStringLiteral("VIEW"));
        viewer_label->setDefaultTextColor(QColor("#e4e9f0"));
        viewer_label->setFont(QFont(QStringLiteral("Segoe UI"), 7, QFont::DemiBold));
        viewer_label->setParentItem(item);
        viewer_label->setPos(21, height - 25.0);
        viewer_label->setData(0, QVariant::fromValue<qulonglong>(node.id));
        viewer_label->setData(1, 4);
        viewer_label->setData(3, node.id == viewer_node_id_);
        viewer_label->setToolTip(QCoreApplication::translate("NodeCanvas", "Show this node in the Fusion Viewer."));
        viewer_label->setZValue(3);
        const auto add_port = [this, item, &node](double x, double y,
                                                  int kind, std::uint8_t input) {
            auto* port = scene_->addEllipse(QRectF(-5, -5, 10, 10),
                QPen(QColor("#c7d2e0"), 1.0), QBrush(QColor((kind==1 && input==2) || (kind==2 && isMaskNode(node.type)) ? "#d1aa52" : "#438fca")));
            port->setParentItem(item);
            port->setPos(x, y);
            port->setData(0, QVariant::fromValue<qulonglong>(node.id));
            port->setData(1, kind);
            port->setData(2, static_cast<int>(input));
            port->setToolTip((kind==1 && input==2) || (kind==2 && isMaskNode(node.type)) ? QCoreApplication::translate("NodeCanvas", "Mask coverage (Mask connections only)") : kind == 2
                ? QCoreApplication::translate("NodeCanvas", "Drag from this output to a node input.")
                : node.type == NodeType::Merge && input == 0
                    ? QCoreApplication::translate("NodeCanvas", "Merge background input.")
                    : node.type == NodeType::Merge
                        ? QCoreApplication::translate("NodeCanvas", "Merge foreground input.")
                        : QCoreApplication::translate("NodeCanvas", "Node input."));
            port->setZValue(2);
        };
        constexpr int input_port = 1;
        constexpr int output_port = 2;
        for (const auto& port : inputPorts(node.type)) {
            const auto y=inputPortY(node.type,port.index);
            add_port(port.index==2 ? 105 : 0,y,input_port,port.index);
            auto* port_label=scene_->addText(QString::fromLatin1(port.label));
            port_label->setDefaultTextColor(QColor(port.kind==PortKind::Mask ? "#d1aa52" : "#9aa4b2"));
            port_label->setFont(QFont(QStringLiteral("Segoe UI"),7));
            port_label->setParentItem(item);
            port_label->setPos(port.index==2 ? 112 : 10,port.index==2 ? -20 : y-10);
        }
        if (outputPort(node.type)) {
            add_port(210,outputPortY(node.type),output_port,0);
            auto* port_label=scene_->addText(isMaskNode(node.type) ? "Mask" : "Image");
            port_label->setDefaultTextColor(QColor(isMaskNode(node.type) ? "#d1aa52" : "#9aa4b2"));
            port_label->setFont(QFont(QStringLiteral("Segoe UI"),7)); port_label->setParentItem(item);
            port_label->setPos(165,outputPortY(node.type)-10);
        }
        items.emplace(node.id, item);
    }
    std::unordered_map<NodeId, QPointF> node_positions;
    node_positions.reserve(items.size());
    for (const auto& [id, node_item] : items)
        node_positions.emplace(id, node_item->pos());
    for (const auto& edge : graph.connections) {
        const auto from = items.find(edge.from), to = items.find(edge.to);
        if (from == items.end() || to == items.end()) continue;
        const auto* from_node = findNode(graph, edge.from);
        const auto* to_node = findNode(graph, edge.to);
        if (from_node == nullptr || to_node == nullptr) continue;
        const auto start = from->second->pos() + QPointF(210, outputPortY(from_node->type));
        const auto end = to->second->pos() + QPointF(edge.input==2 ? 105 : 0, inputPortY(to_node->type, edge.input));
        const auto path = connectionPath(start, end, edge.from, edge.to, edge.input,
            graph_, node_positions, connection_line_style_, snap_to_grid_, grid_settings_.spacing);
        auto* line = scene_->addPath(path, QPen(QColor(edge.input==2 ? "#d1aa52" : "#66b5ff"), 2.0));
        line->setData(0, QVariant::fromValue<qulonglong>(edge.to));
        line->setData(1, 3);
        line->setData(2, static_cast<int>(edge.input));
        line->setData(3, QVariant::fromValue<qulonglong>(edge.from));
        line->setZValue(-1);
        auto* hit_target = scene_->addPath(path, QPen(QColor(0, 0, 0, 0), 12.0));
        hit_target->setData(0, QVariant::fromValue<qulonglong>(edge.to));
        hit_target->setData(1, 3);
        hit_target->setData(2, static_cast<int>(edge.input));
        hit_target->setData(3, QVariant::fromValue<qulonglong>(edge.from));
        hit_target->setZValue(-0.5);
    }
}

void NodeCanvas::updateConnectionLines(NodeId moved_node) {
    std::unordered_map<NodeId, QPointF> node_positions;
    std::vector<QGraphicsPathItem*> connection_lines;
    for (auto* item : scene_->items()) {
        if (!item->data(0).isValid()) continue;
        const auto item_kind = item->data(1).toInt();
        if (item_kind == 0) {
            node_positions.emplace(
                static_cast<NodeId>(item->data(0).toULongLong()), item->scenePos());
        } else if (item_kind == 3) {
            if (auto* line = qgraphicsitem_cast<QGraphicsPathItem*>(item)) {
                const auto from_id = static_cast<NodeId>(line->data(3).toULongLong());
                const auto to_id = static_cast<NodeId>(line->data(0).toULongLong());
                if (moved_node == 0 || from_id == moved_node || to_id == moved_node)
                    connection_lines.push_back(line);
            }
        }
    }

    for (auto* line : connection_lines) {
        const auto from_id = static_cast<NodeId>(line->data(3).toULongLong());
        const auto to_id = static_cast<NodeId>(line->data(0).toULongLong());
        const auto input = static_cast<std::uint8_t>(line->data(2).toInt());
        const auto from_position = node_positions.find(from_id);
        const auto to_position = node_positions.find(to_id);
        const auto* from_node = findNode(graph_, from_id);
        const auto* to_node = findNode(graph_, to_id);
        if (from_position == node_positions.end() || to_position == node_positions.end() ||
            from_node == nullptr || to_node == nullptr)
            continue;

        const auto start = from_position->second +
            QPointF(210.0, outputPortY(from_node->type));
        const auto end = to_position->second + QPointF(
            input == 2 ? 105.0 : 0.0, inputPortY(to_node->type, input));
        line->setPath(connectionPath(start, end, from_id, to_id, input, graph_,
            node_positions, connection_line_style_, snap_to_grid_, grid_settings_.spacing));
    }
}

QPointF NodeCanvas::freeNodePosition(NodeType type) const {
    const auto center = mapToScene(viewport()->rect().center());
    const auto origin = center - QPointF(105.0, nodeHeight(type) / 2.0);
    // Search outward with a fixed gap, bounded by the existing node count.
    const auto free = [&](const QPointF& position) {
        const auto snapped = snapPositionToGrid(position);
        const QRectF bounds(snapped, QSizeF(230.0, nodeHeight(type) + 20.0));
        return std::none_of(graph_.nodes.begin(), graph_.nodes.end(),
            [&](const Node& node) {
                return bounds.intersects(QRectF(node.x - 10.0, node.y - 10.0,
                    230.0, nodeHeight(node.type) + 20.0));
            });
    };
    for (std::size_t ring = 0; ring <= graph_.nodes.size() + 1; ++ring) {
        const auto radius = static_cast<int>(ring);
        for (int y = -radius; y <= radius; ++y) {
            for (int x = -radius; x <= radius; ++x) {
                if (std::max(std::abs(x), std::abs(y)) != radius) continue;
                const auto position = snapPositionToGrid(
                    origin + QPointF(x * 250.0, y * 170.0));
                if (free(position)) return position;
            }
        }
    }
    return snapPositionToGrid(origin);
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

void NodeCanvas::setViewerNode(NodeId id) {
    if (viewer_node_id_ == id) return;
    viewer_node_id_ = id;
    setGraph(graph_);
}

void NodeCanvas::setSelectionChangedHandler(std::function<void(NodeId)> handler) {
    selection_changed_ = std::move(handler);
}
void NodeCanvas::setViewerNodeRequestedHandler(
    std::function<void(NodeId)> handler) {
    viewer_node_requested_ = std::move(handler);
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
    std::function<bool(const QString&, const QPointF&,
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
    const auto scene_position = snapPositionToGrid(mapToScene(viewport_position));
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
    if (!effect_drop_requested_(effect_id, scene_position, cable)) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
}

void NodeCanvas::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        auto* item = itemAt(event->pos());
        if (item != nullptr && item->data(1).toInt() == 4) {
            const auto node_id = static_cast<NodeId>(item->data(0).toULongLong());
            if (viewer_node_requested_) viewer_node_requested_(node_id);
            event->accept();
            return;
        }
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
                pending_start_scene_ = item->scenePos();
                pending_fixed_node_ = node_id;
                pending_fixed_is_source_ = false;
                pending_input_index_ = input;
                QPainterPath preview;
                preview.moveTo(pending_start_scene_);
                pending_connection_line_ = scene_->addPath(
                    preview, QPen(QColor("#9ad3ff"), 2.0, Qt::DashLine));
                pending_connection_line_->setZValue(-2);
                updatePendingConnectionLine(pending_start_scene_);
            }
            event->accept();
            return;
        }
        if (item != nullptr && item->data(1).toInt() == 2) {
            dragging_from_output_ = static_cast<NodeId>(item->data(0).toULongLong());
            if (item->parentItem() != nullptr) item->parentItem()->setSelected(true);
            pending_start_scene_ = item->scenePos();
            pending_fixed_node_ = dragging_from_output_;
            pending_fixed_is_source_ = true;
            pending_input_index_ = 0;
            QPainterPath preview;
            preview.moveTo(pending_start_scene_);
            pending_connection_line_ = scene_->addPath(
                preview, QPen(QColor("#9ad3ff"), 2.0, Qt::DashLine));
            pending_connection_line_->setZValue(-2);
            updatePendingConnectionLine(pending_start_scene_);
            event->accept();
            return;
        }
        if (item != nullptr && item->data(1).toInt() == 3) {
            dragging_from_input_ = static_cast<NodeId>(item->data(0).toULongLong());
            dragging_input_index_ = static_cast<std::uint8_t>(item->data(2).toInt());
            dragging_input_start_ = event->pos();
            pending_start_scene_ = mapToScene(event->pos());
            pending_fixed_node_ = dragging_from_input_;
            pending_fixed_is_source_ = false;
            pending_input_index_ = dragging_input_index_;
            QPainterPath preview;
            preview.moveTo(pending_start_scene_);
            pending_connection_line_ = scene_->addPath(
                preview, QPen(QColor("#9ad3ff"), 2.0, Qt::DashLine));
            pending_connection_line_->setZValue(-2);
            updatePendingConnectionLine(pending_start_scene_);
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
        updatePendingConnectionLine(mapToScene(event->pos()));
        event->accept();
        return;
    }
    if (dragging_node_ != 0) {
        for (auto* item : scene_->items()) {
            if (item->data(0).isValid() && item->data(1).toInt() == 0 &&
                static_cast<NodeId>(item->data(0).toULongLong()) == dragging_node_) {
                const auto scene_position = mapToScene(event->pos());
                item->setPos(snapPositionToGrid(QPointF(
                    scene_position.x() - drag_offset_x_,
                    scene_position.y() - drag_offset_y_)));
                updateConnectionLines(snap_to_grid_ ? 0 : dragging_node_);
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
            if (dynamic_cast<QGraphicsPathItem*>(candidate) != nullptr) continue;
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
            if (dynamic_cast<QGraphicsPathItem*>(candidate) != nullptr) continue;
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

void NodeCanvas::updatePendingConnectionLine(const QPointF& cursor_position) {
    if (pending_connection_line_ == nullptr) return;
    pending_cursor_scene_ = snapPositionToGrid(cursor_position);
    auto start = pending_start_scene_;
    const auto end = pending_cursor_scene_;
    std::optional<NodeId> source_id;
    std::optional<NodeId> target_id;
    if (pending_fixed_is_source_) {
        source_id = pending_fixed_node_;
    } else if (pending_fixed_node_ != 0) {
        const auto* node = findNode(graph_, pending_fixed_node_);
        if (node != nullptr) {
            target_id = pending_fixed_node_;
            start = QPointF(node->x, node->y) + QPointF(
                pending_input_index_ == 2 ? 105.0 : 0.0,
                inputPortY(node->type, pending_input_index_));
        }
    }
    std::unordered_map<NodeId, QPointF> positions;
    for (auto* item : scene_->items()) {
        if (item->data(0).isValid() && item->data(1).toInt() == 0)
            positions.emplace(static_cast<NodeId>(item->data(0).toULongLong()),
                              item->scenePos());
    }
    pending_connection_line_->setPath(connectionPath(start, end, source_id,
        target_id, pending_input_index_, graph_, positions,
        connection_line_style_, snap_to_grid_, grid_settings_.spacing));
}

void NodeCanvas::contextMenuEvent(QContextMenuEvent* event) {
    if (itemAt(event->pos()) != nullptr) {
        event->ignore();
        return;
    }
    auto* menu = new QMenu(this);
    menu->setObjectName(QStringLiteral("fusionNodeCanvasContextMenu"));
    menu->setAttribute(Qt::WA_DeleteOnClose);
    auto* style_menu = menu->addMenu(
        QCoreApplication::translate("NodeCanvas", "Wire style"));
    auto* style_group = new QActionGroup(style_menu);
    style_group->setExclusive(true);
    auto* straight = style_menu->addAction(
        QCoreApplication::translate("NodeCanvas", "Straight"));
    straight->setObjectName(QStringLiteral("fusionWireStraightAction"));
    straight->setCheckable(true);
    straight->setChecked(connection_line_style_ == ConnectionLineStyle::Straight);
    style_group->addAction(straight);
    auto* curved = style_menu->addAction(
        QCoreApplication::translate("NodeCanvas", "Curved"));
    curved->setObjectName(QStringLiteral("fusionWireCurvedAction"));
    curved->setCheckable(true);
    curved->setChecked(connection_line_style_ == ConnectionLineStyle::Curved);
    style_group->addAction(curved);
    connect(straight, &QAction::triggered, this, [this] {
        setConnectionLineStyle(ConnectionLineStyle::Straight);
    });
    connect(curved, &QAction::triggered, this, [this] {
        setConnectionLineStyle(ConnectionLineStyle::Curved);
    });
    menu->addSeparator();
    auto* snap = menu->addAction(
        QCoreApplication::translate("NodeCanvas", "Snap to grid"));
    snap->setObjectName(QStringLiteral("fusionSnapToGridAction"));
    snap->setCheckable(true);
    snap->setChecked(snap_to_grid_);
    connect(snap, &QAction::toggled, this, &NodeCanvas::setSnapToGrid);
    menu->popup(mapToGlobal(event->pos()));
    event->accept();
}
} // namespace fusion::nodes
