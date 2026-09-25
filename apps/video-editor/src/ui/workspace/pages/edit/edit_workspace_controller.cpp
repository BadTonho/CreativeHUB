#include "ui/workspace/pages/edit/edit_workspace_controller.h"

#include "logging/logger.h"
#include "timeline/timeline_widget.h"

#include <QInputDialog>
#include <QLineEdit>

#include <exception>
#include <string>
#include <utility>

namespace ui {

EditWorkspaceController::EditWorkspaceController(
    application::EditorSession& session,
    application::TimelineCommandService& command_service,
    QObject* parent) noexcept
    : QObject(parent), session_(session), command_service_(command_service) {}

application::TimelineEditResult EditWorkspaceController::undo() {
    try {
        const auto result = command_service_.undo();
        publishResult(result);
        if (result.changed()) {
            publishCommittedEdit(
                result, true, true, QStringLiteral("Timeline edit undone."));
        }
        return result;
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error, "timeline", "undo", error.what(), {});
        publishHistoryState();
        emit statusMessageRequested(
            QStringLiteral("Could not undo the timeline edit."));
        return {};
    }
}

application::TimelineEditResult EditWorkspaceController::redo() {
    try {
        const auto result = command_service_.redo();
        publishResult(result);
        if (result.changed()) {
            publishCommittedEdit(
                result, true, true, QStringLiteral("Timeline edit redone."));
        }
        return result;
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error, "timeline", "redo", error.what(), {});
        publishHistoryState();
        emit statusMessageRequested(
            QStringLiteral("Could not redo the timeline edit."));
        return {};
    }
}

application::TimelineEditResult EditWorkspaceController::deleteSelectedClip() {
    const auto clip_id = session_.selection().active_clip_id;
    if (!clip_id.has_value()) {
        application::TimelineEditResult result;
        result.status = application::EditStatus::NoChange;
        publishResult(result);
        return result;
    }
    return execute(application::DeleteClipCommand{*clip_id});
}

application::TimelineEditResult EditWorkspaceController::addTrack(std::string name) {
    auto result = execute(application::AddTrackCommand{std::move(name)});
    if (result.changed()) {
        publishCommittedEdit(result, true, false, QStringLiteral("Video track added."));
    }
    return result;
}

application::TimelineEditResult EditWorkspaceController::renameTrack(
    timeline::TrackId track_id,
    std::string name) {
    auto result = execute(application::RenameTrackCommand{track_id, std::move(name)});
    if (result.changed()) {
        publishCommittedEdit(result, true, false, QStringLiteral("Track renamed."));
    }
    return result;
}

application::TimelineEditResult EditWorkspaceController::moveTrack(
    timeline::TrackId track_id,
    std::size_t target_index) {
    auto result = execute(application::MoveTrackCommand{track_id, target_index});
    if (result.changed()) {
        publishCommittedEdit(result, true, true, QStringLiteral("Track order updated."));
    }
    return result;
}

application::TimelineEditResult EditWorkspaceController::removeTrack(
    timeline::TrackId track_id) {
    auto result = execute(application::RemoveTrackCommand{track_id});
    if (result.changed()) {
        publishCommittedEdit(result, true, true, QStringLiteral("Video track removed."));
    }
    return result;
}

void EditWorkspaceController::promptAddVideoTrack(QWidget* dialog_parent) {
    bool accepted = false;
    const auto name = QInputDialog::getText(
        dialog_parent,
        QStringLiteral("Add Video Track"),
        QStringLiteral("Track name:"),
        QLineEdit::Normal,
        QStringLiteral("Video %1").arg(session_.timeline().trackCount() + 1),
        &accepted);
    if (!accepted) return;

    try {
        const auto result = addTrack(name.toUtf8().toStdString());
        if (!result.changed()) {
            emit statusMessageRequested(QStringLiteral("The track name is invalid."));
        }
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "timeline",
            "add_track",
            error.what(),
            {});
        emit statusMessageRequested(QStringLiteral("Could not add the video track."));
    }
}

void EditWorkspaceController::promptRenameActiveTrack(QWidget* dialog_parent) {
    const auto track_id = session_.selection().active_track_id;
    if (!track_id.has_value()) return;
    const auto track_index = session_.timeline().locateTrack(*track_id);
    if (!track_index.has_value()) return;

    bool accepted = false;
    const auto current = QString::fromUtf8(
        session_.timeline().tracks()[*track_index].name.data(),
        static_cast<qsizetype>(session_.timeline().tracks()[*track_index].name.size()));
    const auto name = QInputDialog::getText(
        dialog_parent,
        QStringLiteral("Rename Track"),
        QStringLiteral("Track name:"),
        QLineEdit::Normal,
        current,
        &accepted);
    if (!accepted) return;

    try {
        const auto result = renameTrack(*track_id, name.toUtf8().toStdString());
        if (result.status == application::EditStatus::Rejected &&
            result.reason == application::EditReason::InvalidName) {
            emit statusMessageRequested(QStringLiteral("The track name is invalid."));
        }
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "timeline",
            "rename_track",
            error.what(),
            {{"track_index", std::to_string(*track_index)}});
        emit statusMessageRequested(QStringLiteral("Could not rename the track."));
    }
}

void EditWorkspaceController::moveActiveTrack(int direction) {
    if (direction == 0 || session_.timeline().trackCount() < 2) return;
    const auto track_id = session_.selection().active_track_id;
    if (!track_id.has_value()) return;
    const auto source_index = session_.timeline().locateTrack(*track_id);
    if (!source_index.has_value()) return;
    if ((direction < 0 && *source_index == 0) ||
        (direction > 0 && *source_index + 1 >= session_.timeline().trackCount())) {
        return;
    }
    const auto target_index = direction < 0 ? *source_index - 1 : *source_index + 1;
    try {
        static_cast<void>(moveTrack(*track_id, target_index));
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "timeline",
            "move_track",
            error.what(),
            {{"from_index", std::to_string(*source_index)},
             {"to_index", std::to_string(target_index)}});
        emit statusMessageRequested(QStringLiteral("Could not move the track."));
    }
}

void EditWorkspaceController::removeActiveTrack() {
    const auto track_id = session_.selection().active_track_id;
    if (!track_id.has_value()) return;
    const auto track_index = session_.timeline().locateTrack(*track_id);
    if (!track_index.has_value()) return;
    try {
        const auto result = removeTrack(*track_id);
        if (result.reason == application::EditReason::TrackNotEmpty) {
            emit statusMessageRequested(QStringLiteral("Only empty tracks can be removed."));
        }
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "timeline",
            "remove_track",
            error.what(),
            {{"track_index", std::to_string(*track_index)}});
        emit statusMessageRequested(QStringLiteral("Could not remove the video track."));
    }
}

void EditWorkspaceController::setTimelineWidget(
    timeline::TimelineWidget* timeline_widget) {
    if (timeline_widget_ == timeline_widget) return;
    timeline_widget_ = timeline_widget;
    if (timeline_widget_ == nullptr) return;
    connect(
        timeline_widget_,
        &timeline::TimelineWidget::clipMoveRequested,
        this,
        &EditWorkspaceController::moveClip);
}

void EditWorkspaceController::moveClip(
    timeline::ClipId clip_id,
    timeline::TrackId target_track_id,
    std::int64_t timeline_start_frame) {
    if (timeline_start_frame < 0) return;
    try {
        const auto result = execute(application::MoveClipCommand{
            clip_id, target_track_id, timeline_start_frame});
        if (!result.changed()) {
            if (result.status != application::EditStatus::NoChange) {
                emit statusMessageRequested(
                    QStringLiteral("The clip cannot be moved to that position."));
            }
            return;
        }
        publishCommittedEdit(
            result, true, true, QStringLiteral("Timeline clip moved."));
    } catch (const std::exception& error) {
        logging::Logger::instance().log(
            logging::Level::Error,
            "timeline",
            "move_clip",
            error.what(),
            {{"clip_id", std::to_string(clip_id)},
             {"target_track_id", std::to_string(target_track_id)},
             {"timeline_frame", std::to_string(timeline_start_frame)}});
        emit statusMessageRequested(
            QStringLiteral("Could not move the timeline clip."));
    }
}

application::TimelineCommandService::EditBatchId
EditWorkspaceController::beginEditBatch() {
    return command_service_.beginEditBatch();
}

application::TimelineEditResult EditWorkspaceController::finishEditBatch(
    application::TimelineCommandService::EditBatchId batch_id) {
    const auto result = command_service_.finishEditBatch(batch_id);
    publishResult(result);
    return result;
}

void EditWorkspaceController::recordLegacyEdit(timeline::EditState state) {
    command_service_.recordLegacyEdit(std::move(state));
    publishHistoryState();
}

void EditWorkspaceController::clearHistory() noexcept {
    command_service_.clearHistory();
    publishHistoryState();
}

bool EditWorkspaceController::canUndo() const noexcept {
    return command_service_.canUndo();
}

bool EditWorkspaceController::canRedo() const noexcept {
    return command_service_.canRedo();
}

application::EditorSession& EditWorkspaceController::session() const noexcept {
    return session_;
}

void EditWorkspaceController::publishResult(
    const application::TimelineEditResult& result) {
    emit commandResultProduced(result);
    publishHistoryState();
}

void EditWorkspaceController::publishHistoryState() {
    emit historyStateChanged(canUndo(), canRedo());
}

void EditWorkspaceController::publishCommittedEdit(
    const application::TimelineEditResult& result,
    bool stop_playback,
    bool refresh_composition,
    const QString& status_message) {
    emit timelineEditCommitted(
        result, stop_playback, refresh_composition, status_message);
}

}  // namespace ui
