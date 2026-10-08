#pragma once

#include <QObject>
#include <QKeySequence>
#include <QString>

#include <functional>
#include <vector>

class QAction;
class QWidget;

namespace settings {
class ShortcutManager;
}

namespace ui {

class EditWorkspaceController;
struct EditWorkspaceUi;

// Owns Edit-only commands, their shortcut registrations, and Timeline-specific
// availability and preference behavior.
class EditWorkspaceActions final : public QObject {
public:
    EditWorkspaceActions(
        EditWorkspaceController& controller,
        EditWorkspaceUi& ui,
        settings::ShortcutManager& shortcut_manager,
        QWidget* dialog_parent,
        std::function<void(const QString&)> status_message,
        QObject* parent = nullptr);

    [[nodiscard]] const std::vector<QAction*>& menuActions() const noexcept {
        return menu_actions_;
    }
    [[nodiscard]] const std::vector<QAction*>& shortcutOnlyActions() const noexcept {
        return shortcut_only_actions_;
    }

    void setProjectLoading(bool loading);
    void setPlaybackActivationLoading(bool loading);
    void refreshAvailability();

private:
    [[nodiscard]] QAction* createMenuAction(
        const QString& text,
        const QString& object_name,
        const QString& shortcut_id = {},
        const QKeySequence& default_shortcut = {},
        bool disabled_during_project_load = true);
    [[nodiscard]] QAction* createShortcutAction(
        const QString& object_name,
        const QString& shortcut_id,
        const QString& label,
        const QKeySequence& default_shortcut);
    void registerShortcut(
        const QString& id,
        const QString& label,
        QAction* action);

    EditWorkspaceController& controller_;
    EditWorkspaceUi& ui_;
    settings::ShortcutManager& shortcut_manager_;
    QWidget* dialog_parent_ = nullptr;
    std::function<void(const QString&)> status_message_;
    std::vector<QAction*> menu_actions_;
    std::vector<QAction*> shortcut_only_actions_;
    QAction* delete_clip_action_ = nullptr;
    QAction* ripple_delete_clip_action_ = nullptr;
    QAction* copy_attributes_action_ = nullptr;
    QAction* paste_attributes_action_ = nullptr;
    bool project_loading_ = false;
    bool playback_activation_loading_ = false;
};

}  // namespace ui
