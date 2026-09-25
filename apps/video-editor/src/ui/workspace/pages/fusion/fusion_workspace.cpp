#include "ui/workspace/pages/fusion/fusion_workspace.h"

#include <QFrame>
#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>

namespace ui {

FusionWorkspace::FusionWorkspace(QObject* parent) : QObject(parent) {}

void FusionWorkspace::createPanels(QWidget* parent) {
    if (viewer_title_ != nullptr || parent == nullptr) return;

    auto* viewer_title = new QLabel("Viewer", parent);
    viewer_title->setObjectName("workspaceViewerTitle");
    viewer_title->setContentsMargins(8, 5, 8, 5);
    viewer_title->setStyleSheet(
        "font-weight: 600; color: #d6dce6; background: #20242b;");
    viewer_title->hide();
    viewer_title_ = viewer_title;

    node_editor_panel_ = new QWidget(parent);
    node_editor_panel_->setObjectName("fusionNodeEditor");
    auto* node_layout = new QVBoxLayout(node_editor_panel_);
    node_layout->setContentsMargins(0, 0, 0, 0);
    node_layout->setSpacing(0);

    auto* node_canvas = new QFrame(node_editor_panel_);
    node_canvas->setObjectName("fusionNodeCanvas");
    node_canvas->setFrameShape(QFrame::NoFrame);
    node_canvas->setStyleSheet("background: #171a20;");
    auto* canvas_layout = new QVBoxLayout(node_canvas);
    auto* node_placeholder = new QLabel(
        "Node-based composition is planned for a future phase.", node_canvas);
    node_placeholder->setObjectName("fusionNodeEditorPlaceholder");
    node_placeholder->setAlignment(Qt::AlignCenter);
    node_placeholder->setStyleSheet("color: #8b95a4;");
    canvas_layout->addWidget(node_placeholder);
    node_layout->addWidget(node_canvas, 1);

    inspector_panel_ = new QWidget(parent);
    inspector_panel_->setObjectName("fusionInspector");
    auto* inspector_layout = new QVBoxLayout(inspector_panel_);
    inspector_layout->setContentsMargins(12, 12, 12, 12);
    inspector_layout->setSpacing(8);
    auto* inspector_title = new QLabel("Fusion Inspector", inspector_panel_);
    inspector_title->setObjectName("fusionInspectorTitle");
    inspector_title->setStyleSheet("font-weight: 600; font-size: 14px;");
    inspector_layout->addWidget(inspector_title);
    auto* inspector_placeholder = new QLabel(
        "Fusion controls are not available yet.", inspector_panel_);
    inspector_placeholder->setObjectName("fusionInspectorPlaceholder");
    inspector_placeholder->setWordWrap(true);
    inspector_placeholder->setStyleSheet("color: #9aa4b2;");
    inspector_layout->addWidget(inspector_placeholder);
    inspector_layout->addStretch(1);
}

}  // namespace ui
