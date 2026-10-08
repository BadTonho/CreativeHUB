#pragma once

#include "ui/workspace/pages/edit/edit_workspace_controller.h"
#include "ui/workspace/pages/edit/edit_workspace_ui.h"

#include <QObject>
#include <QString>

#include <functional>
#include <vector>

class QAction;
class QWidget;
class QVBoxLayout;

namespace settings {
class ShortcutManager;
}

namespace ui {

class EditWorkspaceActions;

// EditWorkspace builds the Inspector and Timeline panels and exposes the
// existing Preview and shared panel widgets to WorkspaceHost.
class EditWorkspace final : public QObject {
    Q_OBJECT

public:
    EditWorkspace(
        application::EditorSession& session,
        application::TimelineCommandService& command_service,
        QWidget* preview_widget,
        settings::ShortcutManager& shortcut_manager,
        QWidget* command_dialog_parent,
        std::function<void(const QString&)> status_message,
        QObject* parent = nullptr);

    void createPanels(QWidget* parent);

    [[nodiscard]] QWidget* previewWidget() const noexcept {
        return preview_widget_;
    }
    [[nodiscard]] QWidget* inspectorPanel() const noexcept {
        return inspector_panel_;
    }
    [[nodiscard]] QWidget* timelinePanel() const noexcept {
        return timeline_panel_;
    }
    [[nodiscard]] const EditWorkspaceUi& ui() const noexcept { return ui_; }
    [[nodiscard]] EditWorkspaceController* controller() const noexcept {
        return controller_;
    }
    [[nodiscard]] const std::vector<QAction*>& menuActions() const noexcept;
    [[nodiscard]] const std::vector<QAction*>& shortcutOnlyActions() const noexcept;
    void setProjectLoading(bool loading);
    void setPlaybackActivationLoading(bool loading);
    void refreshCommandAvailability();

private:
    [[nodiscard]] QWidget* createInspector(QWidget* parent);
    [[nodiscard]] QWidget* createTimeline(QWidget* parent);
    void createTimelineControls(QWidget* container, QVBoxLayout* layout);
    void createTimelineViewport(QWidget* container, QVBoxLayout* layout);
    void createTimelineFooter(QWidget* container, QVBoxLayout* layout);

    QWidget* preview_widget_ = nullptr;
    QWidget* inspector_panel_ = nullptr;
    QWidget* timeline_panel_ = nullptr;
    EditWorkspaceController* controller_ = nullptr;
    settings::ShortcutManager& shortcut_manager_;
    QWidget* command_dialog_parent_ = nullptr;
    std::function<void(const QString&)> status_message_;
    EditWorkspaceActions* actions_ = nullptr;
    EditWorkspaceUi ui_;
};

}  // namespace ui
