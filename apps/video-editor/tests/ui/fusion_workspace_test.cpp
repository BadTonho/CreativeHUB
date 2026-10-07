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

#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
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
    add_type->setCurrentIndex(add_type->findData(
        static_cast<int>(fusion::nodes::NodeType::Transform)));
    add->click();
    require(selected_clip_id_was_preserved && edit_count == 1 &&
                edited.nodes.size() == 3,
            "Adding a node did not update the selected clip graph.");
    const auto transform_id = edited.nodes.back().id;

    auto* canvas = root.findChild<QGraphicsView*>("fusionNodeCanvas");
    require(canvas != nullptr, "The Fusion node canvas was not created.");
    root.resize(1000, 700);
    root.show();
    auto* canvas_panel = canvas->parentWidget()->parentWidget();
    canvas_panel->resize(950, 650);
    canvas_panel->move(0, 0);
    canvas_panel->show();
    canvas->parentWidget()->resize(950, 600);
    canvas->resize(950, 600);
    canvas->show();
    app.processEvents();
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
    const auto source_port = port_position(1, 2, 0);
    const auto transform_input_port = port_position(transform_id, 1, 0);
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(source_port),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas->viewport(), &press);
    QMouseEvent move(QEvent::MouseMove, QPointF(transform_input_port),
                     Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas->viewport(), &move);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(transform_input_port),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(canvas->viewport(), &release);
    require(edit_count == 2 && edited.connections.size() == 2,
            "Dragging an output port to an input port did not connect the nodes.");

    auto* from = root.findChild<QComboBox*>("fusionConnectionFrom");
    auto* to = root.findChild<QComboBox*>("fusionConnectionTo");
    auto* input = root.findChild<QComboBox*>("fusionConnectionInput");
    auto* connect = root.findChild<QPushButton*>("fusionConnectNodesButton");
    require(from && to && input && connect, "Connection controls were not created.");
    from->setCurrentIndex(from->findData(QVariant::fromValue<qulonglong>(transform_id)));
    to->setCurrentIndex(to->findData(QVariant::fromValue<qulonglong>(2)));
    connect->click();
    require(edit_count == 3 && static_cast<bool>(fusion::nodes::validate(edited)),
            "Compatible node connections were not applied.");

    from->setCurrentIndex(from->findData(QVariant::fromValue<qulonglong>(2)));
    to->setCurrentIndex(to->findData(QVariant::fromValue<qulonglong>(transform_id)));
    connect->click();
    auto* status = root.findChild<QLabel*>("fusionNodeStatus");
    require(edit_count == 3 && status && status->text().contains("rejected"),
            "The canvas did not explain a rejected cyclic connection.");

    auto* node_canvas = static_cast<fusion::nodes::NodeCanvas*>(canvas);
    node_canvas->setSelectedNode(transform_id);
    auto* scale = root.findChild<QDoubleSpinBox*>("fusionTransformScale");
    auto* apply = root.findChild<QPushButton*>("fusionApplyNodeSettingsButton");
    require(scale && apply, "Transform settings were not shown for the selected node.");
    scale->setValue(2.0);
    apply->click();
    const auto* transformed = fusion::nodes::findNode(edited, transform_id);
    require(edit_count == 4 && transformed && transformed->transform.scale == 2.0,
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
    require(edit_count == 5 && transformed && transformed->x > original_x,
            "Dragging a node body did not save its canvas position.");

    auto* remove = root.findChild<QPushButton*>("fusionRemoveNodeButton");
    require(remove != nullptr, "The Inspector did not provide node removal.");
    remove->click();
    require(edit_count == 6 &&
                fusion::nodes::findNode(edited, transform_id) == nullptr &&
                static_cast<bool>(fusion::nodes::validate(edited)),
            "Removing a node did not preserve a valid pass-through graph.");
    return 0;
}
