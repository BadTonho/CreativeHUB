#pragma once

#include "ui/workspace/workspace_page_id.h"

#include <QWidget>

class QStackedWidget;

namespace ui {

class EditWorkspace;
class FusionWorkspace;
class RenderWorkspace;

class WorkspaceHost final : public QWidget {
public:
    explicit WorkspaceHost(
        EditWorkspace* edit_workspace,
        FusionWorkspace* fusion_workspace,
        RenderWorkspace* render_workspace,
        QWidget* parent = nullptr);
    explicit WorkspaceHost(
        QWidget* preview_widget,
        QWidget* edit_inspector,
        QWidget* timeline_panel,
        FusionWorkspace* fusion_workspace,
        RenderWorkspace* render_workspace,
        QWidget* parent = nullptr);

    void setPage(WorkspacePageId page);
    [[nodiscard]] WorkspacePageId currentPage() const noexcept;

    [[nodiscard]] QWidget* previewWidget() const noexcept;
    [[nodiscard]] QWidget* renderPage() const noexcept;
    [[nodiscard]] QWidget* timelinePanel() const noexcept;
    [[nodiscard]] QWidget* nodeEditorPanel() const noexcept;
    [[nodiscard]] QWidget* editInspectorPage() const noexcept;
    [[nodiscard]] QWidget* fusionInspectorPage() const noexcept;
    [[nodiscard]] QStackedWidget* inspectorPanel() const noexcept;
    [[nodiscard]] QStackedWidget* lowerWorkspacePanel() const noexcept;

private:
    QWidget* preview_widget_ = nullptr;
    RenderWorkspace* render_workspace_ = nullptr;
    QWidget* timeline_panel_ = nullptr;
    QWidget* node_editor_panel_ = nullptr;
    QWidget* edit_inspector_ = nullptr;
    QWidget* fusion_inspector_ = nullptr;
    QWidget* viewer_title_ = nullptr;
    QStackedWidget* central_workspace_pages_ = nullptr;
    QStackedWidget* lower_workspace_panel_ = nullptr;
    QStackedWidget* inspector_panel_ = nullptr;
    WorkspacePageId current_page_ = WorkspacePageId::Edit;
};

}  // namespace ui
