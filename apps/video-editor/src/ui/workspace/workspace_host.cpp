#include "ui/workspace/workspace_host.h"
#include "ui/workspace/pages/edit/edit_workspace.h"
#include "ui/workspace/pages/fusion/fusion_workspace.h"
#include "ui/workspace/pages/render/render_workspace.h"

#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidget>

namespace ui {

WorkspaceHost::WorkspaceHost(
    EditWorkspace* edit_workspace,
    FusionWorkspace* fusion_workspace,
    RenderWorkspace* render_workspace,
    QWidget* parent)
    : WorkspaceHost(
          edit_workspace != nullptr ? edit_workspace->previewWidget() : nullptr,
          edit_workspace != nullptr ? edit_workspace->inspectorPanel() : nullptr,
          edit_workspace != nullptr ? edit_workspace->timelinePanel() : nullptr,
          fusion_workspace,
          render_workspace,
          parent) {}

WorkspaceHost::WorkspaceHost(
    QWidget* preview_widget,
    QWidget* edit_inspector,
    QWidget* timeline_panel,
    FusionWorkspace* fusion_workspace,
    RenderWorkspace* render_workspace,
    QWidget* parent)
    : QWidget(parent),
      preview_widget_(preview_widget),
      render_workspace_(render_workspace),
      timeline_panel_(timeline_panel),
      node_editor_panel_(fusion_workspace != nullptr
                             ? fusion_workspace->nodeEditorPanel()
                             : nullptr),
      edit_inspector_(edit_inspector),
      fusion_inspector_(fusion_workspace != nullptr
                            ? fusion_workspace->inspectorPanel()
                            : nullptr),
      viewer_title_(fusion_workspace != nullptr
                        ? fusion_workspace->viewerTitle()
                        : nullptr) {
    setObjectName("workspaceHost");

    auto* viewer_layout = new QVBoxLayout(this);
    viewer_layout->setContentsMargins(0, 0, 0, 0);
    viewer_layout->setSpacing(0);
    if (viewer_title_ != nullptr) {
        viewer_layout->addWidget(viewer_title_);
    }

    central_workspace_pages_ = new QStackedWidget(this);
    central_workspace_pages_->setObjectName("centralWorkspacePages");
    if (preview_widget_ != nullptr) {
        central_workspace_pages_->addWidget(preview_widget_);
    }
    if (render_workspace_ != nullptr &&
        render_workspace_->centralPage() != nullptr) {
        central_workspace_pages_->addWidget(render_workspace_->centralPage());
    }
    viewer_layout->addWidget(central_workspace_pages_, 1);

    lower_workspace_panel_ = new QStackedWidget(this);
    lower_workspace_panel_->setObjectName("lowerWorkspacePages");
    if (timeline_panel_ != nullptr) {
        lower_workspace_panel_->addWidget(timeline_panel_);
    }

    if (node_editor_panel_ != nullptr) {
        lower_workspace_panel_->addWidget(node_editor_panel_);
    }

    inspector_panel_ = new QStackedWidget(this);
    inspector_panel_->setObjectName("workspaceInspectorPages");
    if (edit_inspector_ != nullptr) {
        inspector_panel_->addWidget(edit_inspector_);
    }

    if (fusion_inspector_ != nullptr) {
        inspector_panel_->addWidget(fusion_inspector_);
    }
    inspector_panel_->setCurrentWidget(edit_inspector_);
}

void WorkspaceHost::setPage(WorkspacePageId page) {
    if (render_workspace_ != nullptr) {
        render_workspace_->setActive(page == WorkspacePageId::Render);
    }
    current_page_ = page;
    if (page == WorkspacePageId::Render) {
        if (viewer_title_ != nullptr) viewer_title_->hide();
        if (render_workspace_ != nullptr &&
            render_workspace_->centralPage() != nullptr) {
            central_workspace_pages_->setCurrentWidget(
                render_workspace_->centralPage());
        }
        if (timeline_panel_ != nullptr) {
            lower_workspace_panel_->setCurrentWidget(timeline_panel_);
        }
        return;
    }

    if (viewer_title_ != nullptr) {
        viewer_title_->setVisible(page == WorkspacePageId::Fusion);
    }
    if (preview_widget_ != nullptr) {
        central_workspace_pages_->setCurrentWidget(preview_widget_);
    }
    if (page == WorkspacePageId::Fusion) {
        if (node_editor_panel_ != nullptr) {
            lower_workspace_panel_->setCurrentWidget(node_editor_panel_);
        }
        if (fusion_inspector_ != nullptr) {
            inspector_panel_->setCurrentWidget(fusion_inspector_);
        }
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
    return render_workspace_ != nullptr
               ? render_workspace_->centralPage()
               : nullptr;
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
