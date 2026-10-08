#include "ui/workspace/pages/edit/edit_workspace_actions.h"

#include "media/media_library.h"
#include "settings/shortcut_manager.h"
#include "timeline/timeline_model.h"
#include "timeline/timeline_widget.h"
#include "ui/workspace/pages/edit/edit_workspace_controller.h"
#include "ui/workspace/pages/edit/edit_workspace_ui.h"

#include <QAction>
#include <QApplication>
#include <QKeySequence>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSettings>
#include <QSignalBlocker>
#include <QTextEdit>
#include <QWidget>

#include <algorithm>
#include <utility>

namespace ui {

EditWorkspaceActions::EditWorkspaceActions(
    EditWorkspaceController& controller,
    EditWorkspaceUi& ui,
    settings::ShortcutManager& shortcut_manager,
    QWidget* dialog_parent,
    std::function<void(const QString&)> status_message,
    QObject* parent)
    : QObject(parent),
      controller_(controller),
      ui_(ui),
      shortcut_manager_(shortcut_manager),
      dialog_parent_(dialog_parent),
      status_message_(std::move(status_message)) {
    delete_clip_action_ = createMenuAction(
        QStringLiteral("Delete Selected Clip"),
        QStringLiteral("edit.delete_clip"),
        QStringLiteral("edit.delete_clip"),
        QKeySequence(Qt::Key_Delete));
    delete_clip_action_->setEnabled(false);
    connect(delete_clip_action_, &QAction::triggered, this, [this] {
        auto* focus = QApplication::focusWidget();
        if (auto* line_edit = qobject_cast<QLineEdit*>(focus)) {
            line_edit->del();
            return;
        }
        if (auto* text_edit = qobject_cast<QTextEdit*>(focus)) {
            if (text_edit->isReadOnly()) return;
            auto cursor = text_edit->textCursor();
            cursor.deleteChar();
            text_edit->setTextCursor(cursor);
            return;
        }
        if (auto* plain_text_edit = qobject_cast<QPlainTextEdit*>(focus)) {
            if (plain_text_edit->isReadOnly()) return;
            auto cursor = plain_text_edit->textCursor();
            cursor.deleteChar();
            plain_text_edit->setTextCursor(cursor);
            return;
        }
        controller_.deleteActiveTimelineClip();
    });

    ripple_delete_clip_action_ = createMenuAction(
        QStringLiteral("Ripple Delete Selected Clip"),
        QStringLiteral("edit.ripple_delete_clip"),
        QStringLiteral("edit.ripple_delete_clip"),
        QKeySequence(QStringLiteral("Shift+Delete")));
    ripple_delete_clip_action_->setEnabled(false);
    connect(ripple_delete_clip_action_, &QAction::triggered, this, [this] {
        auto* focus = QApplication::focusWidget();
        if (auto* line_edit = qobject_cast<QLineEdit*>(focus)) {
            line_edit->cut();
            return;
        }
        if (auto* text_edit = qobject_cast<QTextEdit*>(focus)) {
            text_edit->cut();
            return;
        }
        if (auto* plain_text_edit = qobject_cast<QPlainTextEdit*>(focus)) {
            plain_text_edit->cut();
            return;
        }
        controller_.rippleDeleteActiveTimelineClip();
    });

    auto* split_clip_action = createMenuAction(
        QStringLiteral("Split Clip at Playhead"),
        QStringLiteral("edit.split_clip"),
        QStringLiteral("edit.split_clip"),
        QKeySequence(QStringLiteral("Ctrl+K")));
    connect(split_clip_action, &QAction::triggered,
            &controller_, &EditWorkspaceController::splitActiveClipAtPlayhead);

    copy_attributes_action_ = createMenuAction(
        QStringLiteral("Copy Attributes"),
        QStringLiteral("edit.copy_attributes"),
        QStringLiteral("edit.copy_attributes"),
        QKeySequence(QStringLiteral("Ctrl+C")));
    connect(copy_attributes_action_, &QAction::triggered, this, [this] {
        auto* focus = QApplication::focusWidget();
        if (auto* line_edit = qobject_cast<QLineEdit*>(focus)) {
            line_edit->copy();
            return;
        }
        if (auto* text_edit = qobject_cast<QTextEdit*>(focus)) {
            text_edit->copy();
            return;
        }
        if (auto* plain_text_edit = qobject_cast<QPlainTextEdit*>(focus)) {
            plain_text_edit->copy();
            return;
        }
        controller_.copySelectedClipAttributes();
        refreshAvailability();
    });

    paste_attributes_action_ = createMenuAction(
        QStringLiteral("Paste Attributes"),
        QStringLiteral("edit.paste_attributes"),
        QStringLiteral("edit.paste_attributes"),
        QKeySequence(QStringLiteral("Ctrl+Shift+V")));
    connect(paste_attributes_action_, &QAction::triggered, this, [this] {
        controller_.showPasteCopiedClipAttributesDialog(dialog_parent_);
        refreshAvailability();
    });

    menu_actions_.push_back(nullptr);
    auto* add_video_track = createMenuAction(
        QStringLiteral("Add Video Track"), QStringLiteral("edit.add_video_track"));
    connect(add_video_track, &QAction::triggered, this, [this] {
        controller_.promptAddVideoTrack(dialog_parent_);
    });
    auto* rename_track = createMenuAction(
        QStringLiteral("Rename Track"), QStringLiteral("edit.rename_track"));
    connect(rename_track, &QAction::triggered, this, [this] {
        controller_.promptRenameActiveTrack(dialog_parent_);
    });
    auto* move_track_up = createMenuAction(
        QStringLiteral("Move Track Up"), QStringLiteral("edit.move_track_up"));
    connect(move_track_up, &QAction::triggered, this,
            [this] { controller_.moveActiveTrack(-1); });
    auto* move_track_down = createMenuAction(
        QStringLiteral("Move Track Down"), QStringLiteral("edit.move_track_down"));
    connect(move_track_down, &QAction::triggered, this,
            [this] { controller_.moveActiveTrack(1); });
    auto* remove_track = createMenuAction(
        QStringLiteral("Remove Track"), QStringLiteral("edit.remove_track"));
    connect(remove_track, &QAction::triggered, this,
            [this] { controller_.removeActiveTrack(); });

    auto* blade_tool = createMenuAction(
        QStringLiteral("Blade Tool"), QStringLiteral("edit.blade_tool"),
        {}, {}, false);
    blade_tool->setCheckable(true);
    connect(blade_tool, &QAction::toggled,
            &controller_, &EditWorkspaceController::setRazorMode);
    connect(&controller_, &EditWorkspaceController::razorToolStateChanged,
            this, [blade_tool](bool enabled) {
                if (blade_tool->isChecked() == enabled) return;
                const QSignalBlocker blocker(blade_tool);
                blade_tool->setChecked(enabled);
            });

    menu_actions_.push_back(nullptr);
    QSettings preferences;
    auto* require_alt_to_move = createMenuAction(
        QStringLiteral("Require Alt to Move Clips"),
        QStringLiteral("edit.require_alt_to_move"), {}, {}, false);
    require_alt_to_move->setCheckable(true);
    const bool require_alt = preferences.value(
        QStringLiteral("timeline/require_alt_to_move"), false).toBool();
    require_alt_to_move->setChecked(require_alt);
    if (ui_.timeline != nullptr) ui_.timeline->setMoveRequiresAlt(require_alt);
    connect(require_alt_to_move, &QAction::toggled, this, [this](bool enabled) {
        QSettings settings;
        settings.setValue(QStringLiteral("timeline/require_alt_to_move"), enabled);
        if (ui_.timeline != nullptr) ui_.timeline->setMoveRequiresAlt(enabled);
        if (status_message_) {
            status_message_(enabled
                ? QStringLiteral("Alt is required to move timeline clips.")
                : QStringLiteral("Timeline clips can be moved by dragging."));
        }
    });

    auto* move_playhead_on_selection = createMenuAction(
        QStringLiteral("Move Playhead to Selected Clip Start"),
        QStringLiteral("edit.move_playhead_on_clip_selection"), {}, {}, false);
    move_playhead_on_selection->setCheckable(true);
    const bool move_playhead = preferences.value(
        QStringLiteral("timeline/move_playhead_on_clip_selection"), false).toBool();
    move_playhead_on_selection->setChecked(move_playhead);
    controller_.setMovePlayheadOnClipSelection(move_playhead);
    connect(move_playhead_on_selection, &QAction::toggled, this,
            [this](bool enabled) {
                QSettings settings;
                settings.setValue(
                    QStringLiteral("timeline/move_playhead_on_clip_selection"),
                    enabled);
                controller_.setMovePlayheadOnClipSelection(enabled);
            });

    auto* nudge_left = createShortcutAction(
        QStringLiteral("timeline.nudge_left"),
        QStringLiteral("timeline.nudge_left"),
        QStringLiteral("Nudge Clip Left"),
        QKeySequence(Qt::CTRL | Qt::Key_Left));
    connect(nudge_left, &QAction::triggered, this,
            [this] { controller_.moveActiveTimelineClip(-1); });
    auto* nudge_right = createShortcutAction(
        QStringLiteral("timeline.nudge_right"),
        QStringLiteral("timeline.nudge_right"),
        QStringLiteral("Nudge Clip Right"),
        QKeySequence(Qt::CTRL | Qt::Key_Right));
    connect(nudge_right, &QAction::triggered, this,
            [this] { controller_.moveActiveTimelineClip(1); });

    connect(&controller_, &EditWorkspaceController::historyStateChanged,
            this, [this](bool, bool) { refreshAvailability(); });
    connect(&controller_, &EditWorkspaceController::timelineSelectionPresentationChanged,
            this, &EditWorkspaceActions::refreshAvailability);
    connect(&controller_, &EditWorkspaceController::effectTargetAvailabilityChanged,
            this, &EditWorkspaceActions::refreshAvailability);
    refreshAvailability();
}

QAction* EditWorkspaceActions::createMenuAction(
    const QString& text,
    const QString& object_name,
    const QString& shortcut_id,
    const QKeySequence& default_shortcut,
    bool disabled_during_project_load) {
    auto* action = new QAction(text, this);
    action->setObjectName(object_name);
    if (disabled_during_project_load) {
        action->setProperty("disabledDuringProjectLoad", true);
    }
    action->setShortcutContext(Qt::WindowShortcut);
    action->setShortcut(default_shortcut);
    if (!shortcut_id.isEmpty()) {
        registerShortcut(shortcut_id, text, action);
    }
    menu_actions_.push_back(action);
    return action;
}

QAction* EditWorkspaceActions::createShortcutAction(
    const QString& object_name,
    const QString& shortcut_id,
    const QString& label,
    const QKeySequence& default_shortcut) {
    auto* action = new QAction(this);
    action->setObjectName(object_name);
    action->setProperty("disabledDuringProjectLoad", true);
    action->setShortcut(default_shortcut);
    action->setShortcutContext(Qt::WindowShortcut);
    registerShortcut(shortcut_id, label, action);
    shortcut_only_actions_.push_back(action);
    return action;
}

void EditWorkspaceActions::registerShortcut(
    const QString& id,
    const QString& label,
    QAction* action) {
    shortcut_manager_.registerAction(
        id, label, action, settings::ShortcutScope::Edit,
        QStringLiteral("Edit workspace"));
}

void EditWorkspaceActions::setProjectLoading(bool loading) {
    project_loading_ = loading;
    refreshAvailability();
}

void EditWorkspaceActions::setPlaybackActivationLoading(bool loading) {
    playback_activation_loading_ = loading;
    refreshAvailability();
}

void EditWorkspaceActions::refreshAvailability() {
    const auto& session = controller_.session();
    const bool attributes_available = !project_loading_;
    copy_attributes_action_->setEnabled(
        attributes_available && controller_.canCopySelectedClipAttributes());
    paste_attributes_action_->setEnabled(
        attributes_available && controller_.canPasteCopiedClipAttributes());

    const auto clip_id = session.selection().active_clip_id;
    const bool no_transition = !session.selection().active_transition.has_value();
    const auto location = clip_id.has_value()
        ? session.timeline().locateClip(*clip_id)
        : std::nullopt;
    const bool can_edit_timeline = !project_loading_ &&
        !playback_activation_loading_ && no_transition && location.has_value();
    bool can_delete = false;
    if (can_edit_timeline) {
        const auto& clip = session.timeline().tracks()[location->track_index]
            .clips[location->clip_index];
        can_delete = clip.kind == timeline::ClipKind::Text ||
            std::any_of(
                session.mediaItems().cbegin(), session.mediaItems().cend(),
                [&clip](const application::ImportedMedia& item) {
                    return !item.offline &&
                        media::MediaLibrary::canonicalPath(item.metadata.source_path) ==
                            media::MediaLibrary::canonicalPath(clip.source_path);
                });
    }
    delete_clip_action_->setEnabled(can_delete);
    ripple_delete_clip_action_->setEnabled(can_edit_timeline);
}

}  // namespace ui
