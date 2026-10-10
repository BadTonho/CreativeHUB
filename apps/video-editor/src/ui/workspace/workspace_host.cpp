#include "ui/workspace/workspace_host.h"
#include "workspaces/edit/ui/edit_workspace.h"
#include "workspaces/fusion/ui/fusion_workspace.h"
#include "workspaces/render/ui/render_workspace.h"

#include <QLayout>
#include <QDockWidget>
#include <QMainWindow>
#include <QStackedWidget>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

namespace ui {

WorkspaceHost::WorkspaceHost(
    EditWorkspace* edit_workspace,
    FusionWorkspace* fusion_workspace,
    RenderWorkspace* render_workspace,
    QWidget* preview_dock_contents,
    QWidget* parent)
    : WorkspaceHost(
          edit_workspace != nullptr ? edit_workspace->previewWidget() : nullptr,
          preview_dock_contents,
          edit_workspace != nullptr ? edit_workspace->inspectorPanel() : nullptr,
          edit_workspace != nullptr ? edit_workspace->timelinePanel() : nullptr,
          fusion_workspace,
          render_workspace,
          parent) {}

WorkspaceHost::WorkspaceHost(
    QWidget* preview_widget,
    QWidget* preview_dock_contents,
    QWidget* edit_inspector,
    QWidget* timeline_panel,
    FusionWorkspace* fusion_workspace,
    RenderWorkspace* render_workspace,
    QWidget* parent)
    : QWidget(parent),
      preview_widget_(preview_widget),
      preview_dock_contents_(preview_dock_contents),
      fusion_workspace_(fusion_workspace),
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
                        : nullptr),
      render_page_(render_workspace != nullptr
                       ? render_workspace->centralPage()
                       : nullptr) {
    setObjectName("workspaceHost");
    setMinimumSize(0, 0);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto* viewer_layout = new QVBoxLayout(this);
    viewer_layout->setContentsMargins(0, 0, 0, 0);
    viewer_layout->setSpacing(0);

    central_workspace_pages_ = new QStackedWidget(this);
    central_workspace_pages_->setObjectName("centralWorkspacePages");
    central_workspace_pages_->setMinimumSize(0, 0);
    central_workspace_pages_->setSizePolicy(
        QSizePolicy::Expanding, QSizePolicy::Expanding);
    empty_central_page_ = new QWidget(central_workspace_pages_);
    empty_central_page_->setObjectName("emptyWorkspaceCenterPage");
    empty_central_page_->setMinimumSize(0, 0);
    empty_central_page_->setSizePolicy(
        QSizePolicy::Ignored, QSizePolicy::Ignored);
    central_workspace_pages_->addWidget(empty_central_page_);
    if (render_page_ != nullptr) {
        // Keep Render's page owned and hidden by the stack until Render is
        // selected. It is initially created with MainWindow as its parent.
        central_workspace_pages_->addWidget(render_page_);
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
    if (current_page_ == page) {
        if (viewer_title_ != nullptr) {
            viewer_title_->setVisible(page == WorkspacePageId::Fusion);
        }
        refreshCentralWorkspaceVisibility();
        return;
    }

    const bool entering_render =
        page == WorkspacePageId::Render &&
        current_page_ != WorkspacePageId::Render;
    const bool leaving_render =
        page != WorkspacePageId::Render &&
        current_page_ == WorkspacePageId::Render;

    if (fusion_workspace_ != nullptr) {
        fusion_workspace_->setActive(page == WorkspacePageId::Fusion);
    }
    if (render_workspace_ != nullptr) {
        render_workspace_->setActive(page == WorkspacePageId::Render);
    }
    if (entering_render) {
        if (preview_widget_ != nullptr && preview_dock_contents_ != nullptr &&
            preview_widget_->parentWidget() == preview_dock_contents_) {
            if (auto* layout = preview_dock_contents_->layout()) {
                layout->removeWidget(preview_widget_);
            }
            preview_widget_->setParent(nullptr);
        }
        if (render_workspace_ != nullptr && preview_widget_ != nullptr &&
            render_workspace_->previewWidget() != preview_widget_) {
            render_workspace_->setPreviewWidget(preview_widget_);
        }
        if (render_page_ != nullptr) {
            central_workspace_pages_->setCurrentWidget(render_page_);
        }
        current_page_ = page;
        if (viewer_title_ != nullptr) viewer_title_->hide();
        if (timeline_panel_ != nullptr) {
            lower_workspace_panel_->setCurrentWidget(timeline_panel_);
        }
        refreshCentralWorkspaceVisibility();
        return;
    }

    if (leaving_render && render_workspace_ != nullptr &&
        render_workspace_->previewWidget() == preview_widget_) {
        auto* preview_widget = render_workspace_->takePreviewWidget();
        if (preview_widget != nullptr && preview_dock_contents_ != nullptr &&
            preview_dock_contents_->layout() != nullptr) {
            preview_dock_contents_->layout()->addWidget(preview_widget);
            preview_widget->show();
        }
    }

    central_workspace_pages_->setCurrentWidget(empty_central_page_);

    if (viewer_title_ != nullptr) {
        viewer_title_->setVisible(page == WorkspacePageId::Fusion);
    }
    if (preview_widget_ != nullptr) {
        preview_widget_->setVisible(true);
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
    current_page_ = page;
    refreshCentralWorkspaceVisibility();
}

void WorkspaceHost::refreshCentralWorkspaceVisibility() {
    auto* main_window = qobject_cast<QMainWindow*>(window());
    if (main_window == nullptr) return;
    if (current_page_ == WorkspacePageId::Render) {
        setMaximumWidth(QWIDGETSIZE_MAX);
        setCentralWorkspaceVisible(true);
        return;
    }

    const auto* timeline_dock = qobject_cast<QDockWidget*>(
        lower_workspace_panel_ != nullptr
            ? lower_workspace_panel_->parentWidget() : nullptr);
    const auto docks = main_window->findChildren<QDockWidget*>();
    const auto has_side_docked_workspace = std::any_of(
        docks.cbegin(), docks.cend(),
        [main_window, timeline_dock](QDockWidget* dock) {
            if (dock == nullptr || dock == timeline_dock ||
                dock->isHidden() || dock->isFloating()) {
                return false;
            }
            const auto area = main_window->dockWidgetArea(dock);
            return area == Qt::LeftDockWidgetArea ||
                area == Qt::RightDockWidgetArea;
        });
    // Qt needs a central layout item for the bottom dock's outer separator.
    // Collapse its width while side docks occupy the upper workspace.
    // Retain a nonempty central rectangle for Qt's dock-layout calculations.
    setMaximumWidth(has_side_docked_workspace ? 1 : QWIDGETSIZE_MAX);
    setCentralWorkspaceVisible(true);
}

void WorkspaceHost::setCentralWorkspaceVisible(bool visible) {
    auto* main_window = qobject_cast<QMainWindow*>(window());
    if (main_window == nullptr) return;

    QWidget* desired = visible ? this : nullptr;
    if (main_window->centralWidget() == desired) return;

    if (auto* current = main_window->takeCentralWidget()) {
        current->setParent(main_window);
    }
    if (desired != nullptr) {
        main_window->setCentralWidget(desired);
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
