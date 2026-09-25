#include "main_window/main_window.h"

const ui::EditWorkspaceUi& MainWindow::editUi() const noexcept {
    static const ui::EditWorkspaceUi empty_ui{};
    return edit_workspace_ != nullptr
        ? edit_workspace_->ui()
        : empty_ui;
}
