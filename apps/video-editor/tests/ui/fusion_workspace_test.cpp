#include "workspaces/fusion/ui/fusion_workspace.h"
#include "workspaces/fusion/nodes/ui/node_canvas.h"
#include "ui/media_browser/media_drag_mime.h"
#include "ui/effects/effects_list_widget.h"

#include <QApplication>
#include <QAbstractSpinBox>
#include <QByteArray>
#include <QComboBox>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDoubleSpinBox>
#include <QGraphicsItem>
#include <QGraphicsLineItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QLineF>
#include <QLabel>
#include <QMouseEvent>
#include <QMimeData>
#include <QCheckBox>
#include <QDropEvent>
#include <QImage>
#include <QIcon>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPushButton>
#include <QDialog>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSlider>
#include <QTemporaryDir>
#include <QWheelEvent>
#include <QWidget>

#include <cstdio>
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

void sendLeftDoubleClick(QWidget* widget) {
    require(widget != nullptr, "Cannot double-click a missing Fusion Inspector widget.");
    const auto local_position = QPointF(widget->rect().center());
    QMouseEvent event(QEvent::MouseButtonDblClick,
        local_position, QPointF(widget->mapToGlobal(local_position.toPoint())),
        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(widget, &event);
}

void verifyFusionInspectorScrolling(QScrollArea* scroll_area) {
    require(scroll_area != nullptr && scroll_area->widgetResizable() &&
                scroll_area->horizontalScrollBarPolicy() == Qt::ScrollBarAlwaysOff &&
                scroll_area->verticalScrollBarPolicy() == Qt::ScrollBarAsNeeded,
            "Fusion Inspector must use a vertical on-demand scroll area.");
    auto* content = scroll_area->widget();
    require(content != nullptr, "Fusion Inspector has no scrollable content.");
    const auto original_minimum_height = content->minimumHeight();
    content->setMinimumHeight(scroll_area->viewport()->height() + 300);
    QApplication::processEvents();

    auto* vertical_bar = scroll_area->verticalScrollBar();
    require(vertical_bar->maximum() > 0,
            "Fusion Inspector did not expose overflow through its scrollbar.");
    vertical_bar->setValue(0);
    const auto local_position = QPoint(8, 8);
    QWheelEvent wheel_event(
        local_position, scroll_area->viewport()->mapToGlobal(local_position),
        QPoint(), QPoint(0, -120), Qt::NoButton, Qt::NoModifier,
        Qt::NoScrollPhase, false);
    QApplication::sendEvent(scroll_area->viewport(), &wheel_event);
    QApplication::processEvents();
    require(vertical_bar->value() > 0,
            "Fusion Inspector did not scroll in response to the mouse wheel.");
    vertical_bar->setValue(0);
    content->setMinimumHeight(original_minimum_height);
    QApplication::processEvents();
}

bool imagesDiffer(const QImage& first, const QImage& second) {
    if (first.size() != second.size()) return true;
    for (int y = 0; y < first.height(); ++y) {
        for (int x = 0; x < first.width(); ++x) {
            if (first.pixel(x, y) != second.pixel(x, y)) return true;
        }
    }
    return false;
}

QImage renderGrid(fusion::nodes::BackgroundGridSettings settings) {
    fusion::nodes::NodeCanvas canvas;
    canvas.setGraph({});
    canvas.resize(420, 260);
    canvas.show();
    QApplication::processEvents();
    canvas.setBackgroundGridSettings(settings);
    QApplication::processEvents();
    return canvas.viewport()->grab().toImage().convertToFormat(QImage::Format_ARGB32);
}

void testGridPreferencesAndRendering() {
    using fusion::nodes::BackgroundGridSettings;
    using fusion::nodes::BackgroundGridStyle;

    QSettings settings;
    settings.clear();
    settings.sync();
    {
        fusion::nodes::NodeCanvas canvas;
        require(canvas.backgroundGridSettings() == BackgroundGridSettings{},
                "The Node Editor grid did not use its default settings.");
    }

    const BackgroundGridSettings lines{};
    auto hidden = lines;
    hidden.visible = false;
    auto dots = lines;
    dots.style = BackgroundGridStyle::Dots;
    auto wider = lines;
    wider.spacing = 48;
    auto brighter = lines;
    brighter.intensity_percent = 60;
    const auto lines_image = renderGrid(lines);
    require(imagesDiffer(lines_image, renderGrid(hidden)),
            "Hiding the grid did not change the canvas background.");
    require(imagesDiffer(lines_image, renderGrid(dots)),
            "Changing the grid style did not change its rendered pattern.");
    require(imagesDiffer(lines_image, renderGrid(wider)),
            "Changing grid spacing did not change its rendered pattern.");
    require(imagesDiffer(lines_image, renderGrid(brighter)),
            "Changing grid intensity did not change its rendered appearance.");

    auto custom = lines;
    custom.visible = false;
    custom.style = BackgroundGridStyle::Dots;
    custom.spacing = 37;
    custom.intensity_percent = 47;
    {
        fusion::nodes::NodeCanvas canvas;
        canvas.setBackgroundGridSettings(custom);
    }
    {
        fusion::nodes::NodeCanvas restored;
        require(restored.backgroundGridSettings() == custom,
                "The Node Editor grid preferences were not restored.");
    }

    settings.beginGroup(QStringLiteral("fusion/node_editor/grid"));
    settings.setValue(QStringLiteral("visible"), QStringLiteral("invalid"));
    settings.setValue(QStringLiteral("style"), 99);
    settings.setValue(QStringLiteral("spacing"), 3);
    settings.setValue(QStringLiteral("intensity"), 100);
    settings.endGroup();
    settings.sync();
    fusion::nodes::NodeCanvas invalid_values;
    auto normalized = invalid_values.backgroundGridSettings();
    require(normalized.visible && normalized.style == BackgroundGridStyle::Lines &&
                normalized.spacing == 12 && normalized.intensity_percent == 60,
            "Invalid grid preferences were not safely normalized.");
    fusion::nodes::NodeCanvas reset_defaults;
    reset_defaults.setBackgroundGridSettings(BackgroundGridSettings{});
}

void testLiveConnectionLines() {
    using namespace fusion::nodes;

    auto graph = makeCompositionGraph();
    Node alternate_source;
    alternate_source.id = graph.next_id++;
    alternate_source.type = NodeType::Background;
    alternate_source.x = 80.0;
    alternate_source.y = 360.0;
    graph.nodes.push_back(alternate_source);

    NodeCanvas canvas;
    canvas.resize(1000, 700);
    canvas.setGraph(graph);
    canvas.show();
    QApplication::processEvents();

    int position_edits = 0;
    int connection_edits = 0;
    bool rewire_was_accepted = false;
    canvas.setPositionChangedHandler([&](NodeId id, double x, double y) {
        const auto node = std::find_if(graph.nodes.begin(), graph.nodes.end(),
            [id](const Node& value) { return value.id == id; });
        if (node == graph.nodes.end()) return;
        node->x = x;
        node->y = y;
        ++position_edits;
        canvas.setGraph(graph);
    });
    canvas.setConnectionRequestedHandler(
        [&](NodeId from, NodeId to, std::uint8_t input) {
            auto candidate = connect(graph, from, to, input);
            if (!candidate) return;
            graph = std::move(*candidate);
            ++connection_edits;
            rewire_was_accepted = true;
            canvas.setGraph(graph);
        });

    const auto node_item = [&canvas](NodeId id) -> QGraphicsItem* {
        for (auto* item : canvas.scene()->items()) {
            if (item->data(0).toULongLong() == id && item->data(1).toInt() == 0)
                return item;
        }
        return nullptr;
    };
    const auto port_item = [&canvas](NodeId id, int kind,
                                     int port_index) -> QGraphicsItem* {
        for (auto* item : canvas.scene()->items()) {
            if (item->data(0).toULongLong() == id &&
                item->data(1).toInt() == kind &&
                item->data(2).toInt() == port_index)
                return item;
        }
        return nullptr;
    };
    const auto edge_lines = [&canvas](NodeId from, NodeId to,
                                      int input) {
        std::vector<QGraphicsLineItem*> result;
        for (auto* item : canvas.scene()->items()) {
            if (item->data(0).toULongLong() != to ||
                item->data(1).toInt() != 3 ||
                item->data(2).toInt() != input ||
                item->data(3).toULongLong() != from)
                continue;
            if (auto* line = qgraphicsitem_cast<QGraphicsLineItem*>(item))
                result.push_back(line);
        }
        return result;
    };
    const auto pending_line = [&canvas]() -> QGraphicsLineItem* {
        for (auto* item : canvas.scene()->items()) {
            auto* line = qgraphicsitem_cast<QGraphicsLineItem*>(item);
            if (line != nullptr && line->pen().style() == Qt::DashLine &&
                !line->data(1).isValid())
                return line;
        }
        return nullptr;
    };
    const auto require_edge_matches_ports = [&](NodeId from, NodeId to,
                                                int input) {
        const auto lines = edge_lines(from, to, input);
        auto* output = port_item(from, 2, 0);
        auto* destination = port_item(to, 1, input);
        require(lines.size() == 2 && output != nullptr && destination != nullptr,
                "A connected edge or its hit target is missing.");
        for (auto* line : lines) {
            require(QLineF(line->line().p1(), output->scenePos()).length() < 0.01 &&
                        QLineF(line->line().p2(), destination->scenePos()).length() < 0.01,
                    "A visible wire or its hit target does not meet its ports.");
        }
    };
    const auto drag_node_to = [&canvas](QGraphicsItem* node, QPoint delta) {
        require(node != nullptr, "Cannot drag a missing node.");
        const auto start = canvas.mapFromScene(node->scenePos() + QPointF(100, 65));
        const auto finish = start + delta;
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(start),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(canvas.viewport(), &press);
        QMouseEvent move(QEvent::MouseMove, QPointF(finish), Qt::NoButton,
                         Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(canvas.viewport(), &move);
        return std::pair{start, finish};
    };
    const auto release_left = [&canvas](QPoint position) {
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(position),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(canvas.viewport(), &release);
    };

    require_edge_matches_ports(1, 2, 0);
    auto* source_node = node_item(1);
    require(source_node != nullptr, "The connected source node is missing.");
    const auto [source_start, source_finish] = drag_node_to(source_node, QPoint(50, 30));
    static_cast<void>(source_start);
    require(position_edits == 0 && graph.nodes[0].x == 80.0 &&
                edge_lines(1, 2, 0).size() == 2,
            "Dragging a node must not commit an edit before release.");
    require_edge_matches_ports(1, 2, 0);
    release_left(source_finish);
    require(position_edits == 1 && graph.nodes[0].x != 80.0 &&
                graph.connections == std::vector<Connection>{{1, 2, 0}},
            "Moving a connected node must commit one position edit and preserve its cable.");
    require_edge_matches_ports(1, 2, 0);

    auto* output_node = node_item(2);
    require(output_node != nullptr, "The connected destination node is missing.");
    const auto [output_start, output_finish] = drag_node_to(output_node, QPoint(55, 35));
    static_cast<void>(output_start);
    require(position_edits == 1,
            "Moving the destination node committed before the drag finished.");
    require_edge_matches_ports(1, 2, 0);
    release_left(output_finish);
    require(position_edits == 2 && graph.connections.size() == 1,
            "Moving the destination node must commit once without changing the graph links.");
    require_edge_matches_ports(1, 2, 0);

    const auto original_connections = graph.connections;
    auto* alternate_output = port_item(3, 2, 0);
    require(alternate_output != nullptr, "The alternate source output is missing.");
    const auto invalid_drag_start = canvas.mapFromScene(alternate_output->scenePos());
    const QPoint empty_position(900, 640);
    QMouseEvent invalid_press(QEvent::MouseButtonPress, QPointF(invalid_drag_start),
        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas.viewport(), &invalid_press);
    QMouseEvent invalid_move(QEvent::MouseMove, QPointF(empty_position),
        Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas.viewport(), &invalid_move);
    auto* draft = pending_line();
    require(draft != nullptr && QLineF(draft->line().p2(),
                canvas.mapToScene(empty_position)).length() < 0.01,
            "The provisional output cable did not follow the cursor.");
    release_left(empty_position);
    require(pending_line() == nullptr && connection_edits == 0 &&
                graph.connections == original_connections,
            "Dropping a provisional cable on empty canvas must cancel without editing the graph.");

    auto* connected_input = port_item(2, 1, 0);
    alternate_output = port_item(3, 2, 0);
    require(connected_input != nullptr && alternate_output != nullptr,
            "The ports required for rewiring are missing.");
    const auto input_position = canvas.mapFromScene(connected_input->scenePos());
    const auto alternate_output_position = canvas.mapFromScene(alternate_output->scenePos());
    QMouseEvent rewire_press(QEvent::MouseButtonPress, QPointF(input_position),
        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas.viewport(), &rewire_press);
    QMouseEvent rewire_move(QEvent::MouseMove, QPointF(alternate_output_position),
        Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas.viewport(), &rewire_move);
    draft = pending_line();
    require(draft != nullptr && QLineF(draft->line().p2(),
                canvas.mapToScene(alternate_output_position)).length() < 0.01,
            "The provisional rewire cable did not follow the cursor.");
    release_left(alternate_output_position);
    require(pending_line() == nullptr && rewire_was_accepted &&
                connection_edits == 1 && graph.connections.size() == 1 &&
                graph.connections.front() == Connection{3, 2, 0},
            "Rewiring a connected input must replace its source with one graph edit.");
    require_edge_matches_ports(3, 2, 0);
}
}

int runFusionWorkspaceTest() {
    ui::FusionWorkspace workspace;
    QWidget root;
    root.resize(1100, 700);
    workspace.createPanels(&root);
    auto* inspector_panel = workspace.inspectorPanel();
    inspector_panel->setGeometry(800, 80, 280, 240);
    root.show();
    QApplication::processEvents();
    auto* inspector_scroll = root.findChild<QScrollArea*>(
        "fusionInspectorScrollArea");
    verifyFusionInspectorScrolling(inspector_scroll);
    inspector_panel->resize(280, 2000);
    QApplication::processEvents();
    require(inspector_scroll->verticalScrollBar()->maximum() == 0,
            "Fusion Inspector showed overflow when its content fit the viewport.");
    inspector_panel->resize(280, 240);
    QApplication::processEvents();

    timeline::TimelineClip selected;
    selected.clip_id = 42;
    selected.kind = timeline::ClipKind::Video;
    selected.timeline_start_frame = 100;
    selected.timeline_duration_frames = 100;
    workspace.setSelection(&selected, {});

    fusion::nodes::NodeGraph edited;
    int edit_count = 0;
    bool selected_clip_id_was_preserved = true;
    timeline::ClipId preview_clip_id = 0;
    fusion::nodes::NodeId preview_node_id = 0;
    int preview_request_count = 0;
    int preview_clear_count = 0;
    int pause_request_count = 0;
    int activation_request_count = 0;
    timeline::ClipId activation_clip_id = 0;
    std::int64_t activation_local_frame = -1;
    QObject::connect(&workspace, &ui::FusionWorkspace::graphEditRequested,
        [&edited, &edit_count, &selected_clip_id_was_preserved, &selected](timeline::ClipId clip_id,
            const fusion::nodes::NodeGraph& graph) {
            selected_clip_id_was_preserved = selected_clip_id_was_preserved &&
                clip_id == 42;
            edited = graph;
            selected.node_graph = graph;
            ++edit_count;
        });
    QObject::connect(&workspace, &ui::FusionWorkspace::previewTargetRequested,
        [&preview_clip_id, &preview_node_id, &preview_request_count](
            timeline::ClipId clip_id, fusion::nodes::NodeId node_id) {
            preview_clip_id = clip_id;
            preview_node_id = node_id;
            ++preview_request_count;
        });
    QObject::connect(&workspace, &ui::FusionWorkspace::previewTargetCleared,
        [&preview_clear_count] { ++preview_clear_count; });
    QObject::connect(&workspace, &ui::FusionWorkspace::playbackPauseRequested,
        [&pause_request_count] { ++pause_request_count; });
    QObject::connect(&workspace,
        &ui::FusionWorkspace::playbackClipActivationRequested,
        [&activation_request_count, &activation_clip_id, &activation_local_frame](
            timeline::ClipId clip_id, std::int64_t local_frame) {
            ++activation_request_count;
            activation_clip_id = clip_id;
            activation_local_frame = local_frame;
        });
    workspace.setActive(true);
    require(workspace.isActive() && pause_request_count == 1 &&
                activation_request_count == 1 && activation_clip_id == 42 &&
                activation_local_frame == 0 && preview_request_count == 1 &&
                preview_clip_id == 42 && preview_node_id == 2 &&
                preview_clear_count == 0,
            "Entering Fusion must pause playback, activate the clip at its start, and target Output.");
    auto* add_type = root.findChild<QComboBox*>("fusionAddNodeType");
    auto* add = root.findChild<QPushButton*>("fusionAddNodeButton");
    require(!add_type && !add, "The obsolete Add Node controls must be removed.");

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
    auto* grid_button = root.findChild<QPushButton*>("fusionGridSettingsButton");
    auto* grid_dialog = root.findChild<QDialog*>("fusionGridSettingsDialog");
    auto* grid_visible = root.findChild<QCheckBox*>("fusionGridVisible");
    auto* grid_style = root.findChild<QComboBox*>("fusionGridStyle");
    auto* grid_spacing = root.findChild<QSlider*>("fusionGridSpacing");
    auto* grid_intensity = root.findChild<QSlider*>("fusionGridIntensity");
    require(grid_button && grid_dialog && grid_visible && grid_style &&
                grid_spacing && grid_intensity,
            "The Node Editor did not expose its grid settings controls.");
    require(grid_spacing->minimum() == 12 && grid_spacing->maximum() == 64 &&
                grid_intensity->minimum() == 10 &&
                grid_intensity->maximum() == 60,
            "Grid adjustment controls did not enforce their supported ranges.");
    require(!grid_dialog->isVisible() && grid_button->accessibleName() ==
                QStringLiteral("Grid Settings"),
            "Grid settings should open on demand and be accessible by name.");
    grid_button->click();
    QApplication::processEvents();
    require(grid_dialog->isVisible(), "The Grid button did not open Grid Settings.");
    auto* node_canvas = root.findChild<fusion::nodes::NodeCanvas*>("fusionNodeCanvas");
    auto grid_settings = node_canvas->backgroundGridSettings();
    require(grid_settings == fusion::nodes::BackgroundGridSettings{},
            "Grid Settings did not reflect the canvas defaults.");
    grid_style->setCurrentIndex(grid_style->findData(static_cast<int>(
        fusion::nodes::BackgroundGridStyle::Dots)));
    grid_spacing->setValue(40);
    grid_intensity->setValue(50);
    grid_visible->setChecked(false);
    grid_settings = node_canvas->backgroundGridSettings();
    require(grid_settings == fusion::nodes::BackgroundGridSettings{
                false, fusion::nodes::BackgroundGridStyle::Dots, 40, 50} &&
                edit_count == 0,
            "Grid controls did not update immediately without editing the node graph.");
    fusion::nodes::NodeCanvas restored_grid_canvas;
    require(restored_grid_canvas.backgroundGridSettings() == grid_settings,
            "Grid control changes were not restored from local preferences.");
    grid_visible->setChecked(true);
    grid_dialog->hide();
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

    require(workspace.addCatalogNode("fusion.transform"), "Catalog node creation failed.");
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
    require(preview_request_count == 2 && preview_clip_id == 42 &&
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

    require(workspace.addCatalogNode("fusion.color"), "Catalog node creation failed.");
    const auto color_id = edited.nodes.back().id;
    require(edit_count == 8,
            "Adding a Color node did not update the selected graph.");
    drag_between_ports(port_position(transform_id, 1, 0), port_position(color_id, 2, 0));
    require(edit_count == 9 && edited.connections.size() == 2,
            "Dragging a connected input to another output did not rewire it.");
    drag_between_ports(port_position(transform_id, 2, 0), port_position(color_id, 1, 0));
    require(edit_count == 9 && status && status->text().contains("rejected"),
            "Dragging a wire that would create a cycle did not show an explanation.");

    node_canvas->setSelectedNode(transform_id);
    verifyFusionInspectorScrolling(inspector_scroll);
    auto* scale = root.findChild<QDoubleSpinBox*>("fusionTransformScale");
    auto* apply = root.findChild<QPushButton*>("fusionApplyNodeSettingsButton");
    require(scale && apply &&
                scale->buttonSymbols() == QAbstractSpinBox::NoButtons &&
                !scale->isReadOnly() && scale->decimals() == 3,
            "Transform numeric fields must hide step buttons and remain editable.");
    auto* scale_text = scale->findChild<QLineEdit*>();
    require(scale_text != nullptr,
            "The Transform numeric field no longer exposes its editable text input.");
    scale_text->setText(scale->locale().toString(2.0, 'f', 3));
    QKeyEvent commit_typed_value(
        QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(scale_text, &commit_typed_value);
    require(scale->value() == 2.0,
            "Typing and committing a value must still work without step buttons.");
    scale->setFocus();
    const auto scale_step = scale->singleStep();
    const auto send_scale_key = [scale_text](int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        QKeyEvent event(QEvent::KeyPress, key, modifiers);
        QApplication::sendEvent(scale_text, &event);
    };
    send_scale_key(Qt::Key_Up);
    require(std::abs(scale->value() - (2.0 + scale_step)) < 1e-9,
            "Up must increase the focused Fusion Inspector field by its configured step.");
    send_scale_key(Qt::Key_Down);
    require(std::abs(scale->value() - 2.0) < 1e-9,
            "Down must reverse one Fusion Inspector step.");
    send_scale_key(Qt::Key_Up, Qt::ShiftModifier);
    require(std::abs(scale->value() - 2.001) < 1e-9,
            "Shift+Up must increase a Fusion Inspector field by 0.001.");
    send_scale_key(Qt::Key_Down, Qt::ShiftModifier);
    require(std::abs(scale->value() - 2.0) < 1e-9,
            "Shift+Down must reverse the fine Fusion Inspector step.");
    send_scale_key(Qt::Key_Up, Qt::AltModifier);
    require(std::abs(scale->value() - 2.100) < 1e-9,
            "Alt+Up must increase a Fusion Inspector field by 0.100.");
    send_scale_key(Qt::Key_Down, Qt::AltModifier);
    require(std::abs(scale->value() - 2.0) < 1e-9,
            "Alt+Down must reverse the coarse Fusion Inspector step.");
    send_scale_key(Qt::Key_Up, Qt::ShiftModifier | Qt::AltModifier);
    require(std::abs(scale->value() - (2.0 + scale_step)) < 1e-9,
            "Shift+Alt+Up must retain the normal Fusion Inspector step.");
    send_scale_key(Qt::Key_Down, Qt::ShiftModifier | Qt::AltModifier);
    require(std::abs(scale->value() - 2.0) < 1e-9,
            "Shift+Alt+Down must reverse the normal Fusion Inspector step.");
    send_scale_key(Qt::Key_Up, Qt::ControlModifier);
    require(std::abs(scale->value() - 3.0) < 1e-9,
            "Ctrl+Up must increase a Fusion Inspector field by 1.000.");
    send_scale_key(Qt::Key_Down, Qt::ControlModifier);
    require(std::abs(scale->value() - 2.0) < 1e-9,
            "Ctrl+Down must reverse the coarse Fusion Inspector step.");
    for (const auto modifiers : {
             Qt::KeyboardModifiers(Qt::ControlModifier | Qt::ShiftModifier),
             Qt::KeyboardModifiers(Qt::ControlModifier | Qt::AltModifier)}) {
        send_scale_key(Qt::Key_Up, modifiers);
        require(std::abs(scale->value() - (2.0 + scale_step)) < 1e-9,
                "Ctrl combined with Shift or Alt must retain the normal Fusion Inspector step.");
        send_scale_key(Qt::Key_Down, modifiers);
        require(std::abs(scale->value() - 2.0) < 1e-9,
                "Combined Fusion Inspector modifiers must reverse the normal step.");
    }
    scale->setValue(scale->maximum());
    QKeyEvent increase_at_limit(
        QEvent::KeyPress, Qt::Key_Up, Qt::NoModifier);
    QApplication::sendEvent(scale_text, &increase_at_limit);
    require(scale->value() == scale->maximum(),
            "Up must respect the Fusion Inspector field's maximum.");
    scale->setValue(scale->minimum());
    send_scale_key(Qt::Key_Down);
    require(scale->value() == scale->minimum(),
            "Down must respect the Fusion Inspector field's minimum.");
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
        std::unique_ptr<QMimeData> mime(
            ui::createEffectIdMimeData(QString::fromUtf8(effect_id)));
        QDragEnterEvent enter(position, Qt::CopyAction, mime.get(),
                              Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(canvas->viewport(), &enter);
        QDragMoveEvent move(position, Qt::CopyAction, mime.get(),
                            Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(canvas->viewport(), &move);
        QDropEvent drop(QPointF(position), Qt::CopyAction, mime.get(),
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
    for (const auto* rejected_id : {"audio.gain", "video.fusion_composition", "video.adjustment_clip",
                                    "transitions.cross_dissolve",
                                    "transitions.fade_to_black"}) {
        require(!drop_effect(blank_canvas_position, QByteArray(rejected_id)) &&
                    edit_count == 14 &&
                    fusion::nodes::findNode(edited, detached_effect_id) != nullptr,
                "An Edit-only Effects item was accepted by the Fusion canvas.");
    }

    node_canvas->setSelectedNode(connected_effect_id);
    auto* effect_enabled = root.findChild<QCheckBox*>("fusionEffectEnabled");
    auto* effect_amount = root.findChild<QDoubleSpinBox*>(
        "fusionEffectParameter_amount");
    apply = root.findChild<QPushButton*>("fusionApplyNodeSettingsButton");
    require(effect_enabled && effect_amount && apply,
            "The effect Inspector did not show the effect's enabled state and parameters.");
    require(effect_amount->buttonSymbols() == QAbstractSpinBox::NoButtons &&
                !effect_amount->isReadOnly(),
            "Fusion effect numeric fields must hide step buttons and remain editable.");
    effect_enabled->setChecked(false);
    effect_amount->setValue(45.0);
    apply->click();
    const auto* effect_node = fusion::nodes::findNode(edited, connected_effect_id);
    require(edit_count == 15 && effect_node != nullptr &&
                !effect_node->effect.enabled &&
                creative_suite::effects::parameterValue(effect_node->effect, "amount") == 45.0,
            "Effect Inspector changes were not committed to the selected graph node.");

    require(workspace.addCatalogNode("fusion.transform"), "Catalog node creation failed.");
    const auto animated_transform_id = edited.nodes.back().id;
    node_canvas->setSelectedNode(animated_transform_id);
    auto* animated_scale = root.findChild<QDoubleSpinBox*>("fusionTransformScale");
    auto* scale_key = root.findChild<QPushButton*>("fusionTransformScaleKeyframe");
    apply = root.findChild<QPushButton*>("fusionApplyNodeSettingsButton");
    require(animated_scale && scale_key && apply,
            "Transform animation controls were not shown in the Inspector.");
    require(animated_scale->buttonSymbols() == QAbstractSpinBox::NoButtons &&
                scale_key->isCheckable() && scale_key->text().isEmpty() &&
                !scale_key->icon().isNull(),
            "Hiding Fusion numeric step buttons must preserve keyframe controls.");
    const auto scale_key_off = scale_key->icon().pixmap(
        QSize(24, 24), QIcon::Normal, QIcon::Off).toImage();
    const auto scale_key_on = scale_key->icon().pixmap(
        QSize(24, 24), QIcon::Normal, QIcon::On).toImage();
    require(scale_key_off.pixelColor(12, 12).alpha() == 0 &&
                scale_key_on.pixelColor(12, 12).alpha() > 0,
            "Fusion keyframe toggles must render outlined and filled diamond icons.");
    scale_key->click();
    auto* animated_transform = fusion::nodes::findNode(edited, animated_transform_id);
    scale_key = root.findChild<QPushButton*>("fusionTransformScaleKeyframe");
    require(animated_transform && edit_count == 17 &&
                scale_key && scale_key->isChecked() &&
                scale_key->icon().pixmap(QSize(24, 24), QIcon::Normal,
                    scale_key->isChecked() ? QIcon::On : QIcon::Off)
                        .toImage().pixelColor(12, 12).alpha() > 0 &&
                animated_transform->transform_keyframes.scale.size() == 1 &&
                animated_transform->transform_keyframes.scale.front() ==
                    creative_suite::animation::Keyframe{0, 1.0} && pause_request_count > 0,
            "Adding a Transform keyframe did not use the evaluated local-frame value or pause playback.");
    animated_scale = root.findChild<QDoubleSpinBox*>("fusionTransformScale");
    apply = root.findChild<QPushButton*>("fusionApplyNodeSettingsButton");
    workspace.setTimelinePlayheadFrame(110);
    QApplication::processEvents();
    animated_scale = root.findChild<QDoubleSpinBox*>("fusionTransformScale");
    scale_key = root.findChild<QPushButton*>("fusionTransformScaleKeyframe");
    require(animated_scale->value() == 1.0 && !scale_key->isChecked() &&
                scale_key->text().isEmpty(),
            "The Transform Inspector did not follow the clip-local playhead frame.");
    animated_scale->setValue(2.0);
    apply->click();
    animated_transform = fusion::nodes::findNode(edited, animated_transform_id);
    animated_scale = root.findChild<QDoubleSpinBox*>("fusionTransformScale");
    require(animated_transform && edit_count == 18 &&
                animated_transform->transform_keyframes.scale.size() == 2 &&
                animated_transform->transform_keyframes.scale.back() ==
                    creative_suite::animation::Keyframe{10, 2.0},
            "Editing an animated Transform parameter did not update the key at the current frame.");
    workspace.setTimelinePlayheadFrame(100);
    QApplication::processEvents();
    animated_scale = root.findChild<QDoubleSpinBox*>("fusionTransformScale");
    scale_key = root.findChild<QPushButton*>("fusionTransformScaleKeyframe");
    require(animated_scale->value() == 1.0 && scale_key->isChecked() &&
                workspace.selectedNodeId() == animated_transform_id,
            "Transform animation changed Inspector selection or failed to restore the keyed value.");

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
                drop_effect(blank_canvas_position, QByteArray("video.brightness")),
            "A Brightness node could not be created for Inspector animation coverage.");
    const auto animated_brightness_id = edited.nodes.back().id;
    node_canvas->setSelectedNode(animated_brightness_id);
    auto* brightness_amount = root.findChild<QDoubleSpinBox*>(
        "fusionEffectParameter_amount");
    auto* brightness_key = root.findChild<QPushButton*>(
        "fusionEffectParameter_amountKeyframe");
    apply = root.findChild<QPushButton*>("fusionApplyNodeSettingsButton");
    require(brightness_amount && brightness_key && apply,
            "Brightness animation controls were not shown in the Inspector.");
    workspace.setTimelinePlayheadFrame(110);
    brightness_key->click();
    auto* animated_brightness = fusion::nodes::findNode(edited, animated_brightness_id);
    require(animated_brightness && edit_count == 20 &&
                animated_brightness->effect_parameter_keyframes.size() == 1 &&
                animated_brightness->effect_parameter_keyframes.front().keyframes.front() ==
                    creative_suite::animation::Keyframe{10, 0.0},
            "Adding a Brightness keyframe did not use its evaluated value at the local frame.");
    brightness_amount = root.findChild<QDoubleSpinBox*>("fusionEffectParameter_amount");
    brightness_key = root.findChild<QPushButton*>(
        "fusionEffectParameter_amountKeyframe");
    apply = root.findChild<QPushButton*>("fusionApplyNodeSettingsButton");
    brightness_amount->setValue(25.0);
    apply->click();
    animated_brightness = fusion::nodes::findNode(edited, animated_brightness_id);
    brightness_key = root.findChild<QPushButton*>(
        "fusionEffectParameter_amountKeyframe");
    require(animated_brightness && edit_count == 21 &&
                animated_brightness->effect_parameter_keyframes.front().keyframes.front() ==
                    creative_suite::animation::Keyframe{10, 25.0},
            "Editing animated Brightness did not update its key at the current frame.");
    brightness_key->click();
    animated_brightness = fusion::nodes::findNode(edited, animated_brightness_id);
    require(animated_brightness && edit_count == 22 &&
                animated_brightness->effect_parameter_keyframes.empty(),
            "The Brightness diamond did not remove the keyframe at the current frame.");

    brightness_amount = root.findChild<QDoubleSpinBox*>(
        "fusionEffectParameter_amount");
    apply = root.findChild<QPushButton*>("fusionApplyNodeSettingsButton");
    brightness_amount->setValue(25.0);
    apply->click();
    animated_brightness = fusion::nodes::findNode(edited, animated_brightness_id);
    require(animated_brightness && edit_count == 23 &&
                creative_suite::effects::parameterValue(
                    animated_brightness->effect, "amount") == 25.0,
            "The Brightness reset test could not set a non-default base value.");
    brightness_amount = root.findChild<QDoubleSpinBox*>(
        "fusionEffectParameter_amount");
    auto* brightness_enabled = root.findChild<QCheckBox*>("fusionEffectEnabled");
    auto* brightness_label = root.findChild<QLabel*>(
        "fusionEffectParameter_amountLabel");
    require(brightness_enabled && brightness_label,
            "The Brightness Inspector did not expose its resettable property name.");
    const auto refresh_selection_connection = QObject::connect(
        &workspace, &ui::FusionWorkspace::graphEditRequested, &workspace,
        [&workspace, &selected](timeline::ClipId,
                                const fusion::nodes::NodeGraph& graph) {
            selected.node_graph = graph;
            workspace.setSelection(&selected, {});
        });
    brightness_enabled->setChecked(false);
    sendLeftDoubleClick(brightness_amount);
    require(edit_count == 23 &&
                creative_suite::effects::parameterValue(
                    animated_brightness->effect, "amount") == 25.0,
            "Double-clicking the Fusion numeric field must not reset or commit its value.");
    sendLeftDoubleClick(brightness_label);
    animated_brightness = fusion::nodes::findNode(edited, animated_brightness_id);
    const auto* brightness_definition =
        creative_suite::effects::findDefinition("video.brightness");
    require(brightness_definition != nullptr &&
                !brightness_definition->parameters.empty() &&
                animated_brightness && edit_count == 24 &&
                creative_suite::effects::parameterValue(
                    animated_brightness->effect, "amount") ==
                    brightness_definition->parameters.front().default_value &&
                animated_brightness->effect.enabled &&
                brightness_enabled->isChecked() == false,
            "Double-clicking a Fusion effect property name must apply only its default, preserving other pending controls.");

    node_canvas->setSelectedNode(animated_transform_id);
    workspace.setTimelinePlayheadFrame(110);
    auto* reset_scale = root.findChild<QDoubleSpinBox*>("fusionTransformScale");
    auto* reset_scale_label = root.findChild<QLabel*>("fusionTransformScaleLabel");
    auto* pending_y = root.findChild<QDoubleSpinBox*>("fusionTransformY");
    animated_transform = fusion::nodes::findNode(edited, animated_transform_id);
    require(reset_scale && reset_scale_label && pending_y && animated_transform,
            "The animated Transform Inspector reset controls were not available.");
    require(animated_transform->transform_keyframes.scale.size() == 2 &&
                animated_transform->transform_keyframes.scale[1] ==
                    creative_suite::animation::Keyframe{10, 2.0},
            "The animated Transform reset test did not find its later keyframe.");
    pending_y->setValue(2.25);
    sendLeftDoubleClick(reset_scale);
    require(edit_count == 24 && reset_scale->value() == 2.0,
            "Double-clicking the animated Transform numeric field must not reset it.");
    sendLeftDoubleClick(reset_scale_label);
    animated_transform = fusion::nodes::findNode(edited, animated_transform_id);
    require(animated_transform && edit_count == 25 &&
                reset_scale->value() == 1.0 &&
                animated_transform->transform_keyframes.scale.size() == 2 &&
                animated_transform->transform_keyframes.scale[0] ==
                    creative_suite::animation::Keyframe{0, 1.0} &&
                animated_transform->transform_keyframes.scale[1] ==
                    creative_suite::animation::Keyframe{10, 1.0} &&
                pending_y->value() == 2.25 &&
                animated_transform->transform.position_y == 0.5,
            "Resetting an animated property name must change only the current keyframe and preserve other pending fields.");
    QObject::disconnect(refresh_selection_connection);

    const auto preview_requests_before_reentry = preview_request_count;
    workspace.setActive(false);
    require(!workspace.isActive() && preview_clear_count == 1,
            "Leaving Fusion must clear its temporary Viewer target.");
    workspace.setActive(true);
    require(workspace.isActive() && activation_request_count == 2 &&
                activation_local_frame == 0 &&
                preview_request_count == preview_requests_before_reentry + 1 &&
                preview_node_id == 1,
            "Reentering Fusion must restore its current node target at the clip start.");
    workspace.setSelection(nullptr, {});
    require(workspace.selectedClipId() == 0 && preview_clear_count == 2,
            "Losing the selected clip while Fusion is active must clear the Viewer target.");
    workspace.setSelection(&selected, {});
    require(workspace.selectedClipId() == 42 && workspace.previewNodeId() == 2 &&
                preview_request_count == preview_requests_before_reentry + 2 &&
                preview_clip_id == 42 &&
                preview_node_id == 2,
            "A replacement clip must fall back to its Output node while Fusion is active.");

    node_canvas->setSelectedNode(detached_effect_id);
    QGraphicsItem* detached_view_button = nullptr;
    for (auto* item : canvas->scene()->items()) {
        if (item->data(0).toULongLong() == detached_effect_id &&
            item->data(1).toInt() == 4) {
            detached_view_button = item;
            break;
        }
    }
    require(detached_view_button != nullptr,
            "The detached effect node does not expose a Viewer indicator.");
    const auto detached_view_position = canvas->mapFromScene(
        detached_view_button->sceneBoundingRect().center());
    QMouseEvent detached_view_press(QEvent::MouseButtonPress,
        QPointF(detached_view_position), Qt::LeftButton, Qt::LeftButton,
        Qt::NoModifier);
    QApplication::sendEvent(canvas->viewport(), &detached_view_press);
    QMouseEvent detached_view_release(QEvent::MouseButtonRelease,
        QPointF(detached_view_position), Qt::LeftButton, Qt::NoButton,
        Qt::NoModifier);
    QApplication::sendEvent(canvas->viewport(), &detached_view_release);
    require(preview_request_count == preview_requests_before_reentry + 3 &&
                preview_node_id == detached_effect_id,
            "Selecting a node for preview did not update the active Viewer target.");
    auto* remove_preview_node = root.findChild<QPushButton*>(
        "fusionRemoveNodeButton");
    require(remove_preview_node != nullptr,
            "The selected preview node did not expose its Remove Node action.");
    remove_preview_node->click();
    workspace.setSelection(&selected, {});
    require(fusion::nodes::findNode(edited, detached_effect_id) == nullptr &&
                workspace.previewNodeId() == 2 &&
                preview_request_count == preview_requests_before_reentry + 4 &&
                preview_node_id == 2,
            "Removing the preview node must restore the Output target.");
    workspace.setActive(false);
    require(preview_clear_count == 3,
            "Leaving Fusion after a preview-node fallback must clear the target.");
    return 0;
}

void verifyCatalogDrops() {
    QWidget owner;
    ui::FusionWorkspace workspace;
    workspace.createPanels(&owner);
    timeline::TimelineClip clip;
    clip.kind = timeline::ClipKind::FusionComposition;
    clip.clip_id = 71;
    clip.timeline_duration_frames = 150;
    clip.node_graph = fusion::nodes::makeCompositionGraph();
    workspace.setSelection(&clip, {});
    require(!workspace.addCatalogNode("fusion.input"), "Inactive Fusion must refuse creation.");
    workspace.setActive(true);
    auto graph = *clip.node_graph;
    int edits = 0;
    QObject::connect(&workspace, &ui::FusionWorkspace::graphEditRequested,
        [&](auto, const auto& value) { graph = value; ++edits; });
    EffectsListWidget catalog;
    catalog.setWorkspace(effects::Workspace::Fusion);
    auto* canvas = workspace.nodeEditorPanel()->findChild<fusion::nodes::NodeCanvas*>();
    const auto initial_connections = graph.connections;
    const QStringList ids{"fusion.input", "fusion.transform", "fusion.color", "fusion.merge",
        "video.background", "text.text", "fusion.rectangle_mask", "fusion.ellipse_mask", "video.gaussian_blur"};
    for (const auto& id : ids) {
        QListWidgetItem* entry = nullptr;
        for (int i = 0; i < catalog.count(); ++i)
            if (catalog.item(i)->data(Qt::UserRole).toString() == id) entry = catalog.item(i);
        require(entry != nullptr, "Fusion catalog entry is missing.");
        std::unique_ptr<QMimeData> mime(catalog.mimeData({entry}));
        require(mime != nullptr, "Fusion catalog entry cannot be dragged.");
        const QPoint position(80 + edits * 15, 220);
        const auto scene_position = canvas->mapToScene(position);
        QDragEnterEvent enter(position, Qt::CopyAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(canvas->viewport(), &enter);
        QDropEvent drop(QPointF(position), Qt::CopyAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(canvas->viewport(), &drop);
        require(enter.isAccepted() && drop.isAccepted() && graph.nodes.size() == 2 + edits &&
                graph.nodes.back().x == scene_position.x() && graph.nodes.back().y == scene_position.y() &&
                workspace.selectedNodeId() == graph.nodes.back().id && graph.connections == initial_connections,
                "Catalog drop must create and select a disconnected node at the drop position.");
        if (id == "video.background") require(graph.nodes.back().background_color[3] == 0,
            "Fusion Background must retain transparent defaults.");
        if (id == "text.text") require(graph.nodes.back().text.content == "Text",
            "Fusion Text must retain its generator defaults.");
    }
    require(edits == 9, "Each catalog drop must produce one graph edit.");
    require(workspace.addCatalogNode("fusion.merge"), "Double-click creation route failed.");
    const auto& added = graph.nodes.back();
    for (std::size_t i = 0; i + 1 < graph.nodes.size(); ++i) {
        const auto& other = graph.nodes[i];
        require(!QRectF(added.x, added.y, 210, 140).intersects(QRectF(other.x, other.y, 210, 140)),
                "Automatic catalog placement must avoid existing nodes.");
    }
    const auto before = edits;
    require(!workspace.addCatalogNode("video.adjustment_clip") &&
            !workspace.addCatalogNode("video.fusion_composition") &&
            !workspace.addCatalogNode("unknown") && edits == before,
            "Unknown and Edit-only actions must not edit a graph.");
    // Blur inserts on an image cable, rather than becoming an unconnected node.
    clip.node_graph=fusion::nodes::makeCompositionGraph();workspace.setSelection(&clip,{});
    QPoint image_cable_position;
    for(auto* item:canvas->scene()->items())if(item->data(1).toInt()==3 && item->data(2).toInt()==0)
        if(auto* line=qgraphicsitem_cast<QGraphicsLineItem*>(item)){image_cable_position=canvas->mapFromScene(line->line().pointAt(.5));break;}
    QMimeData blur_mime;blur_mime.setData(ui::kEffectIdMimeType,"video.gaussian_blur");
    QDragEnterEvent blur_enter(image_cable_position,Qt::CopyAction,&blur_mime,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(canvas->viewport(),&blur_enter);
    QDropEvent blur_drop(QPointF(image_cable_position),Qt::CopyAction,&blur_mime,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(canvas->viewport(),&blur_drop);
    require(blur_drop.isAccepted() && graph.nodes.back().type==fusion::nodes::NodeType::GaussianBlur && graph.connections.size()==2,"Blur must split image cables.");
    auto masked_graph = fusion::nodes::makeCompositionGraph();
    fusion::nodes::Node mask;
    mask.id = masked_graph.next_id++;
    mask.type = fusion::nodes::NodeType::RectangleMask;
    mask.x = 700; mask.y = 300;
    masked_graph.nodes.push_back(mask);
    masked_graph.connections.push_back({mask.id, 2, 2});
    clip.node_graph = masked_graph;
    workspace.setSelection(&clip, {});
    QPoint mask_cable_position;
    bool found_mask_cable = false;
    for (auto* item : canvas->scene()->items()) {
        if (item->data(1).toInt() == 3 && item->data(2).toInt() == 2) {
            auto* line = qgraphicsitem_cast<QGraphicsLineItem*>(item);
            require(line != nullptr, "Mask cable geometry is unavailable.");
            mask_cable_position = canvas->mapFromScene(line->line().pointAt(0.5));
            found_mask_cable = true;
            break;
        }
    }
    require(found_mask_cable, "Mask cable must be drawn.");
    QMimeData filter_mime;
    filter_mime.setData(ui::kEffectIdMimeType, "video.gaussian_blur");
    QDragEnterEvent enter(mask_cable_position, Qt::CopyAction, &filter_mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas->viewport(), &enter);
    QDropEvent drop(QPointF(mask_cable_position), Qt::CopyAction, &filter_mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas->viewport(), &drop);
    require(!drop.isAccepted() && edits == before+1, "Visual effects must not split mask cables.");
    workspace.setSelection(nullptr, {});
    require(!workspace.addCatalogNode("fusion.input") && edits == before+1,
            "Missing selection must refuse node creation.");
    timeline::TimelineClip audio = clip;
    audio.kind = timeline::ClipKind::Audio;
    workspace.setSelection(&audio, {});
    require(!workspace.addCatalogNode("text.text") && edits == before+1,
            "Audio clips must refuse graph creation.");
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    try {
        QTemporaryDir settings_directory;
        require(settings_directory.isValid(),
                "Could not create isolated settings storage for the Fusion test.");
        app.setOrganizationName("CreativeSuiteTests");
        app.setApplicationName("FusionWorkspaceTest");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           settings_directory.path());
        testGridPreferencesAndRendering();
        testLiveConnectionLines();
        verifyCatalogDrops();
        return runFusionWorkspaceTest();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
