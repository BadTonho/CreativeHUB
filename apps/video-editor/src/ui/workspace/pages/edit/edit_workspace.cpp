#include "ui/workspace/pages/edit/edit_workspace.h"

namespace ui {

EditWorkspace::EditWorkspace(
    application::EditorSession& session,
    application::TimelineCommandService& command_service,
    QWidget* preview_widget,
    QWidget* inspector_panel,
    QWidget* timeline_panel,
    QObject* parent)
    : QObject(parent),
      preview_widget_(preview_widget),
      inspector_panel_(inspector_panel),
      timeline_panel_(timeline_panel),
      controller_(new EditWorkspaceController(session, command_service, this)) {}

void EditWorkspace::setSharedPanels(
    QWidget* preview_widget,
    QWidget* inspector_panel,
    QWidget* timeline_panel) noexcept {
    preview_widget_ = preview_widget;
    inspector_panel_ = inspector_panel;
    timeline_panel_ = timeline_panel;
}

}  // namespace ui
