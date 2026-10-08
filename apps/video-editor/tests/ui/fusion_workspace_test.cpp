#include "ui/workspace/pages/fusion/fusion_workspace.h"
#include "fusion/nodes/ui/node_canvas.h"

#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QWidget>

#include <cstdio>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
}

int runFusionWorkspaceTest() {
    ui::FusionWorkspace workspace;
    QWidget root;
    workspace.createPanels(&root);
    timeline::TimelineClip selected;
    selected.clip_id = 42;
    selected.kind = timeline::ClipKind::Video;
    workspace.setSelection(&selected, {});

    fusion::nodes::NodeGraph edited;
    int edit_count = 0;
    bool selected_clip_id_was_preserved = true;
    QObject::connect(&workspace, &ui::FusionWorkspace::graphEditRequested,
        [&edited, &edit_count, &selected_clip_id_was_preserved, &selected](timeline::ClipId clip_id,
            const fusion::nodes::NodeGraph& graph) {
            selected_clip_id_was_preserved = selected_clip_id_was_preserved &&
                clip_id == 42;
            edited = graph;
            selected.node_graph = graph;
            ++edit_count;
        });
    auto* add_type = root.findChild<QComboBox*>("fusionAddNodeType");
    auto* add = root.findChild<QPushButton*>("fusionAddNodeButton");
    require(add_type && add, "Node controls were not created.");

    auto* canvas = root.findChild<QGraphicsView*>("fusionNodeCanvas");
    require(canvas != nullptr, "The Fusion node canvas was not created.");
    root.resize(1250, 700);
    root.show();
    auto* canvas_panel = canvas->parentWidget()->parentWidget();
    canvas_panel->resize(1200, 650);
    canvas_panel->move(0, 0);
    canvas_panel->show();
    canvas->parentWidget()->resize(1200, 600);
    canvas->resize(1200, 600);
    canvas->show();
    QApplication::processEvents();
    const auto port_position = [canvas](fusion::nodes::NodeId node_id,
                                         int port_kind, int port_index) {
        for (auto* item : canvas->scene()->items()) {
            if (item->data(0).toULongLong() == node_id &&
                item->data(1).toInt() == port_kind &&
                item->data(2).toInt() == port_index)
                return canvas->mapFromScene(item->scenePos());
        }
        throw std::runtime_error("A requested node port was not drawn.");
    };
    const auto drag_between_ports = [canvas](const QPoint& from, const QPoint& to) {
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(from),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(canvas->viewport(), &press);
        QMouseEvent move(QEvent::MouseMove, QPointF(to),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(canvas->viewport(), &move);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(to),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(canvas->viewport(), &release);
    };
    const auto drag_to_empty = [canvas](const QPoint& start, const QPoint& empty) {
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(start),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(canvas->viewport(), &press);
        QMouseEvent move(QEvent::MouseMove, QPointF(empty),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(canvas->viewport(), &move);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(empty),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(canvas->viewport(), &release);
    };
    auto* status = root.findChild<QLabel*>("fusionNodeStatus");
    QPoint blank_canvas_position;
    bool found_blank_canvas_position = false;
    const auto viewport_size = canvas->viewport()->size();
    for (int y = 8; y < viewport_size.height() && !found_blank_canvas_position; y += 16) {
        for (int x = 8; x < viewport_size.width(); x += 16) {
            if (canvas->itemAt(QPoint(x, y)) == nullptr) {
                blank_canvas_position = QPoint(x, y);
                found_blank_canvas_position = true;
                break;
            }
        }
    }
    require(found_blank_canvas_position,
            "Fusion node canvas has no empty viewport area for a wire drop.");
    const auto default_output = port_position(1, 2, 0);
    const auto default_input = port_position(2, 1, 0);
    const auto default_wire = (default_output + default_input) / 2;
    auto* default_wire_hit = canvas->itemAt(default_wire);
    require(default_wire_hit != nullptr && default_wire_hit->data(1).toInt() == 3,
            "The default Input-to-Output cable does not have a draggable hit area.");
    drag_to_empty(default_wire, blank_canvas_position);
    require(edit_count == 1 && edited.connections.empty() &&
                status && status->text() == QStringLiteral("Connection removed."),
            "Dragging the default Input-to-Output cable to empty canvas did not disconnect it.");
    drag_between_ports(default_output, default_input);
    require(edit_count == 2 && edited.connections.size() == 1,
            "Dragging node ports did not restore the default connection.");

    add_type->setCurrentIndex(add_type->findData(
        static_cast<int>(fusion::nodes::NodeType::Transform)));
    add->click();
    require(selected_clip_id_was_preserved && edit_count == 3 &&
                edited.nodes.size() == 3,
            "Adding a node did not update the selected clip graph.");
    const auto transform_id = edited.nodes.back().id;

    drag_between_ports(port_position(1, 2, 0), port_position(transform_id, 1, 0));
    require(edit_count == 4 && edited.connections.size() == 2,
            "Dragging an output port to an input port did not connect the nodes.");

    drag_between_ports(port_position(transform_id, 2, 0), port_position(2, 1, 0));
    require(edit_count == 5 && static_cast<bool>(fusion::nodes::validate(edited)),
            "Dragging a node output to the Output input did not connect the graph.");

    require(root.findChild<QComboBox*>("fusionConnectionFrom") == nullptr &&
                root.findChild<QComboBox*>("fusionConnectionTo") == nullptr &&
                root.findChild<QComboBox*>("fusionConnectionInput") == nullptr &&
                root.findChild<QPushButton*>("fusionConnectNodesButton") == nullptr &&
                root.findChild<QPushButton*>("fusionDisconnectNodeButton") == nullptr,
            "Node connection dropdowns and buttons should not appear in the Inspector.");

    const auto transform_input_position = port_position(transform_id, 1, 0);
    QMouseEvent input_click_press(QEvent::MouseButtonPress,
                                  QPointF(transform_input_position),
                                  Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas->viewport(), &input_click_press);
    QMouseEvent input_click_release(QEvent::MouseButtonRelease,
                                    QPointF(transform_input_position),
                                    Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(canvas->viewport(), &input_click_release);
    require(edit_count == 5 && edited.connections.size() == 2,
            "Clicking a connected input port disconnected it without a drag.");

    found_blank_canvas_position = false;
    for (int y = 8; y < viewport_size.height() && !found_blank_canvas_position; y += 16) {
        for (int x = 8; x < viewport_size.width(); x += 16) {
            if (canvas->itemAt(QPoint(x, y)) == nullptr) {
                blank_canvas_position = QPoint(x, y);
                found_blank_canvas_position = true;
                break;
            }
        }
    }
    require(found_blank_canvas_position,
            "Fusion node canvas has no empty viewport area after adding nodes.");
    drag_to_empty(transform_input_position, blank_canvas_position);
    require(edit_count == 6 && edited.connections.size() == 1 &&
                status && status->text() == QStringLiteral("Connection removed."),
            "Dragging a connected input port to empty canvas did not disconnect it.");
    drag_between_ports(port_position(1, 2, 0), port_position(transform_id, 1, 0));
    require(edit_count == 7 && edited.connections.size() == 2,
            "Dragging an output back to a disconnected input did not reconnect it.");

    add_type->setCurrentIndex(add_type->findData(
        static_cast<int>(fusion::nodes::NodeType::Color)));
    add->click();
    const auto color_id = edited.nodes.back().id;
    require(edit_count == 8,
            "Adding a Color node did not update the selected graph.");
    drag_between_ports(port_position(transform_id, 1, 0), port_position(color_id, 2, 0));
    require(edit_count == 9 && edited.connections.size() == 2,
            "Dragging a connected input to another output did not rewire it.");
    drag_between_ports(port_position(transform_id, 2, 0), port_position(color_id, 1, 0));
    require(edit_count == 9 && status && status->text().contains("rejected"),
            "Dragging a wire that would create a cycle did not show an explanation.");

    auto* node_canvas = static_cast<fusion::nodes::NodeCanvas*>(canvas);
    node_canvas->setSelectedNode(transform_id);
    auto* scale = root.findChild<QDoubleSpinBox*>("fusionTransformScale");
    auto* apply = root.findChild<QPushButton*>("fusionApplyNodeSettingsButton");
    require(scale && apply, "Transform settings were not shown for the selected node.");
    scale->setValue(2.0);
    apply->click();
    const auto* transformed = fusion::nodes::findNode(edited, transform_id);
    require(edit_count == 10 && transformed && transformed->transform.scale == 2.0,
            "Inspector edits were not committed to the graph.");
    workspace.setSelection(&selected, {});
    auto* preserved_scale = root.findChild<QDoubleSpinBox*>("fusionTransformScale");
    require(preserved_scale && preserved_scale->value() == 2.0,
            "Refreshing the selected clip reset the Inspector's current node.");

    const auto original_x = transformed->x;
    QGraphicsItem* transform_item = nullptr;
    for (auto* item : canvas->scene()->items()) {
        if (item->data(0).toULongLong() == transform_id &&
            item->data(1).toInt() == 0) {
            transform_item = item;
            break;
        }
    }
    require(transform_item != nullptr, "The Transform node body was not drawn.");
    const auto node_body = canvas->mapFromScene(
        transform_item->scenePos() + QPointF(100, 70));
    QMouseEvent click_press(QEvent::MouseButtonPress, QPointF(node_body),
                            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas->viewport(), &click_press);
    QMouseEvent click_release(QEvent::MouseButtonRelease, QPointF(node_body),
                              Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(canvas->viewport(), &click_release);
    require(edit_count == 10,
            "Selecting a node without moving it created a graph edit.");
    const auto moved_body = node_body + QPoint(40, 20);
    QMouseEvent move_press(QEvent::MouseButtonPress, QPointF(node_body),
                           Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas->viewport(), &move_press);
    QMouseEvent node_move(QEvent::MouseMove, QPointF(moved_body),
                          Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas->viewport(), &node_move);
    QMouseEvent move_release(QEvent::MouseButtonRelease, QPointF(moved_body),
                             Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(canvas->viewport(), &move_release);
    transformed = fusion::nodes::findNode(edited, transform_id);
    if (transformed == nullptr || edit_count != 11 || transformed->x <= original_x) {
        std::fprintf(stderr, "node drag: edits=%d x-before=%.3f x-after=%.3f\n",
            edit_count, original_x, transformed ? transformed->x : -1.0);
    }
    require(edit_count == 11 && transformed && transformed->x > original_x,
            "Dragging a node body did not save its canvas position.");

    auto* remove = root.findChild<QPushButton*>("fusionRemoveNodeButton");
    require(remove != nullptr, "The Inspector did not provide node removal.");
    remove->click();
    require(edit_count == 12 &&
                fusion::nodes::findNode(edited, transform_id) == nullptr &&
                static_cast<bool>(fusion::nodes::validate(edited)),
            "Removing a node did not preserve a valid pass-through graph.");
    return 0;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    try {
        return runFusionWorkspaceTest();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
