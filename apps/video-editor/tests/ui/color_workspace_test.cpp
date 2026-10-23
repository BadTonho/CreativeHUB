#include "workspaces/color/ui/color_workspace.h"
#include "workspaces/color/ui/color_node_canvas.h"
#include "ui/preview/preview_widget.h"

#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMetaObject>
#include <QPushButton>
#include <QThread>
#include <QTimer>

#include <creative_suite/effects/effects.h>

#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    try {
        ui::ColorWorkspace workspace;
        auto graph = color::makePassthroughGraph();
        int edits = 0;
        workspace.setEffectsChangedHandler([&](timeline::ClipId id, color::Graph updated) {
            require(id == 7, "Color must edit the selected clip identity.");
            graph = std::move(updated);
            ++edits;
            workspace.setClips({{7, QStringLiteral("Shot A")}}, 7, graph);
        });
        workspace.setClips({{7, QStringLiteral("Shot A")}}, 7, graph);
        auto* add = workspace.findChild<QPushButton*>("colorAddNodeButton");
        auto* add_branch = workspace.findChild<QPushButton*>("colorAddParallelBranchButton");
        auto* toggle = workspace.findChild<QPushButton*>("colorToggleNodeButton");
        auto* remove = workspace.findChild<QPushButton*>("colorRemoveNodeButton");
        auto* nodes = workspace.findChild<ui::ColorNodeCanvas*>("colorNodeCanvas");
        require(add && add_branch && toggle && remove && nodes,
                "Color graph controls must exist.");
        add->click();
        const auto selected_after_add = nodes->selectedNode();
        const auto* added_node = selected_after_add
            ? color::findNode(graph, *selected_after_add) : nullptr;
        require(color::validate(graph) && graph.nodes.size() == 3 && added_node &&
                added_node->effect && added_node->effect->id == "video.primary_grade" && edits == 1,
                "Adding a correction must create a Color effect node.");
        auto* red = workspace.findChild<QDoubleSpinBox*>("colorParameter_red_gain");
        require(red != nullptr, "Primary Grade must expose red gain.");
        red->setValue(145);
        require(QMetaObject::invokeMethod(red, "editingFinished"),
                "Red gain edit must be committed.");
        const auto selected_after_parameter = nodes->selectedNode();
        const auto* edited_node = selected_after_parameter
            ? color::findNode(graph, *selected_after_parameter) : nullptr;
        require(edited_node && edited_node->effect &&
                    creative_suite::effects::parameterValue(*edited_node->effect, "red_gain") == 145,
                "Primary Grade parameters must update the selected graph node.");
        require(red->isHidden(),
                "Rebuilt controls must hide the old spin box before deferred deletion.");
        add_branch->click();
        require(color::validate(graph) &&
                    std::count_if(graph.nodes.begin(), graph.nodes.end(), [](const auto& node) {
                        return node.type == color::NodeType::ParallelMixer;
                    }) == 1 && graph.nodes.size() == 5,
                "Adding a parallel correction must create and connect a mixer graph.");
        add->click();
        require(graph.nodes.size() == 6,
                "A correction inserted into a selected branch must add a graph node.");
        toggle->click();
        const auto selected_after_insert = nodes->selectedNode();
        const auto* toggled_node = selected_after_insert
            ? color::findNode(graph, *selected_after_insert) : nullptr;
        require(toggled_node && toggled_node->effect && !toggled_node->effect->enabled,
                "Node enable state must be editable.");
        remove->click();
        require(color::validate(graph) && graph.nodes.size() == 5,
                "Removing a correction must preserve a valid connected graph.");
        auto* new_type = workspace.findChild<QComboBox*>("colorNewNodeType");
        require(new_type != nullptr, "Color must offer correction node types.");
        new_type->setCurrentIndex(new_type->findData(QStringLiteral("video.saturation")));
        add->click();
        require(std::any_of(graph.nodes.begin(), graph.nodes.end(), [](const auto& node) {
                    return node.effect && node.effect->id == "video.saturation";
                }), "Color must add the chosen effect type to its graph.");
        QImage thumbnail(4, 3, QImage::Format_RGBA8888);
        thumbnail.fill(Qt::red);
        workspace.setClips({{7, QStringLiteral("Shot A"), thumbnail}}, 7, graph);
        auto* clip_strip = workspace.findChild<QListWidget*>("colorClipStrip");
        require(clip_strip && !clip_strip->item(0)->icon().isNull(),
                "Color clip strip must display available media thumbnails.");
        workspace.setGalleryStills({{"still-a", QStringLiteral("Still 1"),
            QStringLiteral("Shot A"), 42, thumbnail, true}});
        int capture_requests = 0;
        QString renamed_id;
        QString renamed_value;
        QString removed_id;
        workspace.setCaptureStillHandler([&] { ++capture_requests; });
        workspace.setRenameStillHandler([&](const QString& id, const QString& name) {
            renamed_id = id;
            renamed_value = name;
        });
        workspace.setRemoveStillHandler([&](const QString& id) { removed_id = id; });
        auto* capture_button = workspace.findChild<QPushButton*>("colorCaptureStillButton");
        auto* rename_button = workspace.findChild<QPushButton*>("colorRenameStillButton");
        auto* remove_button = workspace.findChild<QPushButton*>("colorRemoveStillButton");
        auto* gallery_list = workspace.findChild<QListWidget*>("colorGalleryStills");
        require(capture_button && rename_button && remove_button && gallery_list &&
                    gallery_list->count() == 1 && !gallery_list->item(0)->icon().isNull(),
                "Color Gallery must show captured stills and its actions.");
        capture_button->click();
        require(capture_requests == 1, "Color Gallery did not request a current-frame capture.");
        gallery_list->setCurrentRow(0);
        QTimer::singleShot(0, [] {
            for (auto* widget : QApplication::topLevelWidgets()) {
                if (auto* dialog = qobject_cast<QInputDialog*>(widget)) {
                    dialog->setTextValue(QStringLiteral("Warm Look"));
                    dialog->accept();
                }
            }
        });
        rename_button->click();
        require(renamed_id == QStringLiteral("still-a") &&
                    renamed_value == QStringLiteral("Warm Look"),
                "Color Gallery rename did not preserve the selected still identity.");
        remove_button->click();
        require(removed_id == QStringLiteral("still-a"),
                "Color Gallery removal did not target the selected still.");
        auto* viewer = new PreviewWidget(&workspace);
        auto frame = std::make_shared<media::VideoFrame>();
        frame->width = 2; frame->height = 2; frame->stride = 8;
        frame->rgba_pixels = {255, 0, 0, 255, 0, 255, 0, 255,
                              0, 0, 255, 255, 255, 255, 255, 255};
        viewer->setFrame(frame);
        workspace.setPreviewWidget(viewer);
        workspace.setActive(true);
        auto* waveform = workspace.findChild<QLabel*>("colorWaveform");
        require(waveform != nullptr, "Color waveform display must exist.");
        QElapsedTimer timeout;
        timeout.start();
        while (waveform->pixmap().isNull() && timeout.elapsed() < 2000) {
            application.processEvents();
            QThread::msleep(10);
        }
        require(!waveform->pixmap().isNull() && waveform->pixmap().size() == QSize(320, 256),
                "Waveform must process the displayed frame asynchronously at bounded resolution.");
        workspace.setActive(false);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
