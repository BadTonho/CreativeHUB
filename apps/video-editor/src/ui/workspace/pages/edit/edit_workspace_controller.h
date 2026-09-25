#pragma once

#include "application/editor_session.h"
#include "application/timeline_command_service.h"

#include <QObject>
#include <QString>

#include <string>

class QWidget;

namespace timeline {
class TimelineWidget;
}

namespace ui {

// Coordinates Edit commands against the session shared by the workspace
// pages. The command service remains the authority for edits and history.
class EditWorkspaceController final : public QObject {
    Q_OBJECT

public:
    EditWorkspaceController(
        application::EditorSession& session,
        application::TimelineCommandService& command_service,
        QObject* parent = nullptr) noexcept;

    template <typename Command>
    [[nodiscard]] application::TimelineEditResult execute(const Command& command) {
        const auto result = command_service_.execute(command);
        publishResult(result);
        return result;
    }

    [[nodiscard]] application::TimelineEditResult undo();
    [[nodiscard]] application::TimelineEditResult redo();
    [[nodiscard]] application::TimelineEditResult deleteSelectedClip();
    [[nodiscard]] application::TimelineEditResult addTrack(std::string name);
    [[nodiscard]] application::TimelineEditResult renameTrack(
        timeline::TrackId track_id,
        std::string name);
    [[nodiscard]] application::TimelineEditResult moveTrack(
        timeline::TrackId track_id,
        std::size_t target_index);
    [[nodiscard]] application::TimelineEditResult removeTrack(
        timeline::TrackId track_id);
    void promptAddVideoTrack(QWidget* dialog_parent);
    void promptRenameActiveTrack(QWidget* dialog_parent);
    void moveActiveTrack(int direction);
    void removeActiveTrack();
    void setTimelineWidget(timeline::TimelineWidget* timeline_widget);
    void moveClip(
        timeline::ClipId clip_id,
        timeline::TrackId target_track_id,
        std::int64_t timeline_start_frame);
    [[nodiscard]] application::TimelineCommandService::EditBatchId beginEditBatch();
    [[nodiscard]] application::TimelineEditResult finishEditBatch(
        application::TimelineCommandService::EditBatchId batch_id);
    void recordLegacyEdit(timeline::EditState state);
    void clearHistory() noexcept;

    [[nodiscard]] bool canUndo() const noexcept;
    [[nodiscard]] bool canRedo() const noexcept;
    [[nodiscard]] application::EditorSession& session() const noexcept;

signals:
    void commandResultProduced(const application::TimelineEditResult& result);
    void historyStateChanged(bool can_undo, bool can_redo);
    void statusMessageRequested(const QString& message);
    void timelineEditCommitted(
        const application::TimelineEditResult& result,
        bool stop_playback,
        bool refresh_composition,
        const QString& status_message);

private:
    void publishResult(const application::TimelineEditResult& result);
    void publishHistoryState();
    void publishCommittedEdit(
        const application::TimelineEditResult& result,
        bool stop_playback,
        bool refresh_composition,
        const QString& status_message);

    application::EditorSession& session_;
    application::TimelineCommandService& command_service_;
    timeline::TimelineWidget* timeline_widget_ = nullptr;
};

}  // namespace ui
