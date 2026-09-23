#include "ui/workspace_page_view.h"

#include <QFrame>
#include <QLabel>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace ui {

WorkspacePageView::WorkspacePageView(
    QWidget* preview_widget,
    QWidget* edit_inspector,
    QWidget* timeline_panel,
    QWidget* parent)
    : QWidget(parent),
      preview_widget_(preview_widget),
      timeline_panel_(timeline_panel),
      edit_inspector_(edit_inspector) {
    setObjectName("workspacePageView");

    auto* viewer_layout = new QVBoxLayout(this);
    viewer_layout->setContentsMargins(0, 0, 0, 0);
    viewer_layout->setSpacing(0);
    viewer_title_ = new QLabel("Viewer", this);
    viewer_title_->setObjectName("workspaceViewerTitle");
    viewer_title_->setContentsMargins(8, 5, 8, 5);
    viewer_title_->setStyleSheet(
        "font-weight: 600; color: #d6dce6; background: #20242b;");
    viewer_title_->hide();
    viewer_layout->addWidget(viewer_title_);
    if (preview_widget_ != nullptr) {
        viewer_layout->addWidget(preview_widget_, 1);
    }

    lower_workspace_panel_ = new QStackedWidget(this);
    lower_workspace_panel_->setObjectName("lowerWorkspacePages");
    if (timeline_panel_ != nullptr) {
        lower_workspace_panel_->addWidget(timeline_panel_);
    }

    node_editor_panel_ = new QWidget(this);
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
    lower_workspace_panel_->addWidget(node_editor_panel_);

    inspector_panel_ = new QStackedWidget(this);
    inspector_panel_->setObjectName("workspaceInspectorPages");
    if (edit_inspector_ != nullptr) {
        inspector_panel_->addWidget(edit_inspector_);
    }

    fusion_inspector_ = new QWidget(inspector_panel_);
    fusion_inspector_->setObjectName("fusionInspector");
    auto* fusion_inspector_layout = new QVBoxLayout(fusion_inspector_);
    fusion_inspector_layout->setContentsMargins(12, 12, 12, 12);
    fusion_inspector_layout->setSpacing(8);
    auto* fusion_title = new QLabel("Fusion Inspector", fusion_inspector_);
    fusion_title->setObjectName("fusionInspectorTitle");
    fusion_title->setStyleSheet("font-weight: 600; font-size: 14px;");
    fusion_inspector_layout->addWidget(fusion_title);
    auto* fusion_placeholder = new QLabel(
        "Fusion controls are not available yet.", fusion_inspector_);
    fusion_placeholder->setObjectName("fusionInspectorPlaceholder");
    fusion_placeholder->setWordWrap(true);
    fusion_placeholder->setStyleSheet("color: #9aa4b2;");
    fusion_inspector_layout->addWidget(fusion_placeholder);
    fusion_inspector_layout->addStretch(1);
    inspector_panel_->addWidget(fusion_inspector_);
    inspector_panel_->setCurrentWidget(edit_inspector_);
}

void WorkspacePageView::setFusionPageActive(bool active) {
    viewer_title_->setVisible(active);
    lower_workspace_panel_->setCurrentWidget(
        active ? node_editor_panel_ : timeline_panel_);
    inspector_panel_->setCurrentWidget(
        active ? fusion_inspector_ : edit_inspector_);
}

QWidget* WorkspacePageView::previewWidget() const noexcept {
    return preview_widget_;
}

QWidget* WorkspacePageView::timelinePanel() const noexcept {
    return timeline_panel_;
}

QWidget* WorkspacePageView::nodeEditorPanel() const noexcept {
    return node_editor_panel_;
}

QWidget* WorkspacePageView::editInspectorPage() const noexcept {
    return edit_inspector_;
}

QWidget* WorkspacePageView::fusionInspectorPage() const noexcept {
    return fusion_inspector_;
}

QStackedWidget* WorkspacePageView::inspectorPanel() const noexcept {
    return inspector_panel_;
}

QStackedWidget* WorkspacePageView::lowerWorkspacePanel() const noexcept {
    return lower_workspace_panel_;
}

}  // namespace ui
