#include "ui/workspace/pages/fusion/fusion_workspace.h"
#include "fusion/nodes/ui/node_canvas.h"
#include "ui/media_browser/media_drag_mime.h"

#include <QApplication>
#include <QByteArray>
#include <QComboBox>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDoubleSpinBox>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QLabel>
#include <QMouseEvent>
#include <QMimeData>
#include <QCheckBox>
#include <QDropEvent>
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
    timeline::ClipId preview_clip_id = 0;
    fusion::nodes::NodeId preview_node_id = 0;
    int preview_request_count = 0;
    QObject::connect(&workspace, &ui::FusionWorkspace::graphEditRequested,
        [&edited, &edit_count, &selected_clip_id_was_preserved, &selected](timeline::ClipId clip_id,
            const fusion::nodes::NodeGraph& graph) {
            selected_clip_id_was_preserved = selected_clip_id_was_preserved &&
                clip_id == 42;
            edited = graph;
            selected.node_graph = graph;
            ++edit_count;
        });
    QObject::connect(&workspace, &ui::FusionWorkspace::nodePreviewRequested,
        [&preview_clip_id, &preview_node_id, &preview_request_count](
            timeline::ClipId clip_id, fusion::nodes::NodeId node_id) {
            preview_clip_id = clip_id;
            preview_node_id = node_id;
            ++preview_request_count;
        });
    auto* add_type = root.findChild<QComboBox*>("fusionAddNodeType");
    auto* add = root.findChild<QPushButton*>("fusionAddNodeButton");
    require(add_type && add, "Node controls were not created.");

    auto* canvas = root.findChild<QGraphicsView*>("fusionNodeCanvas");
    require(canvas != nullptr, "The Fusion node canvas was not created.");
    require(workspace.previewNodeId() == 2,
            "The Output node should be the default Fusion Viewer target.");
    bool output_view_button_active = false;
    for (auto* item : canvas->scene()->items()) {
        output_view_button_active = output_view_button_active ||
            (item->data(0).toULongLong() == 2 && item->data(1).toInt() == 4 &&
             item->data(3).toBool());
    }
    require(output_view_button_active,
            "The Output node's Fusion Viewer indicator was not shown as active.");
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

    QGraphicsItem* input_view_button = nullptr;
    for (auto* item : canvas->scene()->items()) {
        if (item->data(0).toULongLong() == 1 && item->data(1).toInt() == 4) {
            input_view_button = item;
            break;
        }
    }
    require(input_view_button != nullptr,
            "The Input node does not expose its Fusion Viewer indicator.");
    const auto input_view_position = canvas->mapFromScene(
        input_view_button->sceneBoundingRect().center());
    QMouseEvent viewer_press(QEvent::MouseButtonPress,
        QPointF(input_view_position), Qt::LeftButton, Qt::LeftButton,
        Qt::NoModifier);
    QApplication::sendEvent(canvas->viewport(), &viewer_press);
    QMouseEvent viewer_release(QEvent::MouseButtonRelease,
        QPointF(input_view_position), Qt::LeftButton, Qt::NoButton,
        Qt::NoModifier);
    QApplication::sendEvent(canvas->viewport(), &viewer_release);
    require(preview_request_count == 1 && preview_clip_id == 42 &&
                preview_node_id == 1 && workspace.previewNodeId() == 1 &&
                workspace.selectedNodeId() == transform_id && edit_count == 3,
            "The node Viewer indicator changed the graph or failed to select its preview target.");

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

    const auto drop_effect = [canvas](const QPoint& position,
                                      const QByteArray& effect_id) {
        QMimeData mime;
        mime.setData(ui::kEffectIdMimeType, effect_id);
        QDragEnterEvent enter(position, Qt::CopyAction, &mime,
                              Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(canvas->viewport(), &enter);
        QDragMoveEvent move(position, Qt::CopyAction, &mime,
                            Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(canvas->viewport(), &move);
        QDropEvent drop(QPointF(position), Qt::CopyAction, &mime,
                        Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(canvas->viewport(), &drop);
        return enter.isAccepted() && move.isAccepted() && drop.isAccepted();
    };
    const auto connected_edge = edited.connections.back();
    const auto cable_start = port_position(connected_edge.from, 2, 0);
    const auto cable_end = port_position(connected_edge.to, 1, connected_edge.input);
    const auto cable_position = (cable_start + cable_end) / 2;
    require(drop_effect(cable_position, QByteArray("video.grayscale")) &&
                edit_count == 13 && edited.nodes.back().type ==
                    fusion::nodes::NodeType::Effect &&
                edited.connections.size() == 2 &&
                edited.connections[edited.connections.size() - 2].to ==
                    edited.nodes.back().id &&
                edited.connections.back().from == edited.nodes.back().id,
            "Dropping a video effect on a cable did not split and preserve the connection.");
    const auto connected_effect_id = edited.nodes.back().id;

    found_blank_canvas_position = false;
    for (int y = 8; y < canvas->viewport()->height() && !found_blank_canvas_position; y += 16) {
        for (int x = 8; x < canvas->viewport()->width(); x += 16) {
            if (canvas->itemAt(QPoint(x, y)) == nullptr) {
                blank_canvas_position = QPoint(x, y);
                found_blank_canvas_position = true;
                break;
            }
        }
    }
    require(found_blank_canvas_position &&
                drop_effect(blank_canvas_position, QByteArray("video.saturation")) &&
                edit_count == 14 && edited.nodes.back().type ==
                    fusion::nodes::NodeType::Effect && edited.connections.size() == 2,
            "Dropping a video effect on empty canvas did not create a disconnected node.");
    const auto detached_effect_id = edited.nodes.back().id;
    for (const auto* rejected_id : {"audio.gain", "text.text",
                                    "transitions.cross_dissolve",
                                    "transitions.fade_to_black"}) {
        require(!drop_effect(blank_canvas_position, QByteArray(rejected_id)) &&
                    edit_count == 14 &&
                    fusion::nodes::findNode(edited, detached_effect_id) != nullptr,
                "A non-video Effects item was accepted by the Fusion canvas.");
    }

    node_canvas->setSelectedNode(connected_effect_id);
    auto* effect_enabled = root.findChild<QCheckBox*>("fusionEffectEnabled");
    auto* effect_amount = root.findChild<QDoubleSpinBox*>(
        "fusionEffectParameter_amount");
    apply = root.findChild<QPushButton*>("fusionApplyNodeSettingsButton");
    require(effect_enabled && effect_amount && apply,
            "The effect Inspector did not show the effect's enabled state and parameters.");
    effect_enabled->setChecked(false);
    effect_amount->setValue(45.0);
    apply->click();
    const auto* effect_node = fusion::nodes::findNode(edited, connected_effect_id);
    require(edit_count == 15 && effect_node != nullptr &&
                !effect_node->effect.enabled &&
                creative_suite::effects::parameterValue(effect_node->effect, "amount") == 45.0,
            "Effect Inspector changes were not committed to the selected graph node.");
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
