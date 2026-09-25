#pragma once

#include "ui/workspace/pages/edit/edit_workspace_controller.h"

#include <QObject>

class QWidget;

namespace ui {

// The Edit page exposes the existing shared surfaces to WorkspaceHost. It
// keeps references to the Preview, Inspector, and Timeline rather than
// creating parallel copies of those project views.
class EditWorkspace final : public QObject {
    Q_OBJECT

public:
    EditWorkspace(
        application::EditorSession& session,
        application::TimelineCommandService& command_service,
        QWidget* preview_widget,
        QWidget* inspector_panel,
        QWidget* timeline_panel,
        QObject* parent = nullptr);

    void setSharedPanels(
        QWidget* preview_widget,
        QWidget* inspector_panel,
        QWidget* timeline_panel) noexcept;

    [[nodiscard]] QWidget* previewWidget() const noexcept {
        return preview_widget_;
    }
    [[nodiscard]] QWidget* inspectorPanel() const noexcept {
        return inspector_panel_;
    }
    [[nodiscard]] QWidget* timelinePanel() const noexcept {
        return timeline_panel_;
    }
    [[nodiscard]] EditWorkspaceController* controller() const noexcept {
        return controller_;
    }

private:
    QWidget* preview_widget_ = nullptr;
    QWidget* inspector_panel_ = nullptr;
    QWidget* timeline_panel_ = nullptr;
    EditWorkspaceController* controller_ = nullptr;
};

}  // namespace ui
