#include "ui/workspace/workspace_host.h"

#include <QFrame>
#include <QLabel>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace ui {

WorkspaceHost::WorkspaceHost(
    QWidget* preview_widget,
    QWidget* edit_inspector,
    QWidget* timeline_panel,
    QWidget* parent)
    : QWidget(parent),
      preview_widget_(preview_widget),
      timeline_panel_(timeline_panel),
      edit_inspector_(edit_inspector) {
    setObjectName("workspaceHost");

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

    central_workspace_pages_ = new QStackedWidget(this);
    central_workspace_pages_->setObjectName("centralWorkspacePages");
    if (preview_widget_ != nullptr) {
        central_workspace_pages_->addWidget(preview_widget_);
    }
    render_page_ = new QWidget(central_workspace_pages_);
    render_page_->setObjectName("renderWorkspacePage");
    central_workspace_pages_->addWidget(render_page_);
    viewer_layout->addWidget(central_workspace_pages_, 1);

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

void WorkspaceHost::setPage(WorkspacePageId page) {
    current_page_ = page;
    if (page == WorkspacePageId::Render) {
        viewer_title_->hide();
        central_workspace_pages_->setCurrentWidget(render_page_);
        if (timeline_panel_ != nullptr) {
            lower_workspace_panel_->setCurrentWidget(timeline_panel_);
        }
        return;
    }

    viewer_title_->setVisible(page == WorkspacePageId::Fusion);
    if (preview_widget_ != nullptr) {
        central_workspace_pages_->setCurrentWidget(preview_widget_);
    }
    if (page == WorkspacePageId::Fusion) {
        lower_workspace_panel_->setCurrentWidget(node_editor_panel_);
        inspector_panel_->setCurrentWidget(fusion_inspector_);
    } else {
        lower_workspace_panel_->setCurrentWidget(timeline_panel_);
        inspector_panel_->setCurrentWidget(edit_inspector_);
    }
}

WorkspacePageId WorkspaceHost::currentPage() const noexcept {
    return current_page_;
}

QWidget* WorkspaceHost::previewWidget() const noexcept {
    return preview_widget_;
}

QWidget* WorkspaceHost::renderPage() const noexcept {
    return render_page_;
}

QWidget* WorkspaceHost::timelinePanel() const noexcept {
    return timeline_panel_;
}

QWidget* WorkspaceHost::nodeEditorPanel() const noexcept {
    return node_editor_panel_;
}

QWidget* WorkspaceHost::editInspectorPage() const noexcept {
    return edit_inspector_;
}

QWidget* WorkspaceHost::fusionInspectorPage() const noexcept {
    return fusion_inspector_;
}

QStackedWidget* WorkspaceHost::inspectorPanel() const noexcept {
    return inspector_panel_;
}

QStackedWidget* WorkspaceHost::lowerWorkspacePanel() const noexcept {
    return lower_workspace_panel_;
}

}  // namespace ui
