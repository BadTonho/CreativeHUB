#include "ui/workspace/pages/render/render_output_capabilities.h"
#include "ui/workspace/pages/render/render_queue_model.h"

#include <QCoreApplication>

#include <cstdio>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

ui::RenderJob makeJob(
    const QString& name,
    const QString& output_path,
    const std::string& container,
    const std::string& encoder) {
    ui::RenderJob job;
    job.display_name = name;
    job.settings.output_path = output_path;
    job.settings.container_name = QString::fromStdString(container);
    job.settings.video_encoder_name = QString::fromStdString(encoder);
    job.settings.width = 1920;
    job.settings.height = 1080;
    job.settings.frame_rate = 30.0;
    job.project_snapshot.canvas_width = 1920;
    job.project_snapshot.canvas_height = 1080;
    return job;
}

}  // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    try {
        const auto containers = ui::RenderOutputCapabilities::availableContainers();
        require(!containers.empty(),
                "The current FFmpeg runtime did not expose a video output container.");
        for (const auto& container : containers) {
            require(!container.video_encoders.empty(),
                    "A listed container must have at least one compatible video encoder.");
            for (const auto& encoder : container.video_encoders) {
                require(ui::RenderOutputCapabilities::supportsVideoEncoder(
                            container, encoder.name),
                        "A listed video encoder must be valid for its container.");
            }
            for (const auto& encoder : container.audio_encoders) {
                require(ui::RenderOutputCapabilities::supportsAudioEncoder(
                            container, encoder.name),
                        "A listed audio encoder must be valid for its container.");
            }
            require(!ui::RenderOutputCapabilities::supportsVideoEncoder(
                        container, "render-test-unsupported-encoder"),
                    "An encoder absent from FFmpeg must not be reported as compatible.");
        }

        const auto& compatible_container = containers.front();
        const auto& compatible_encoder = compatible_container.video_encoders.front();
        ui::RenderQueueModel queue;
        auto first_job = makeJob(
            QStringLiteral("First"),
            QStringLiteral("first.mp4"),
            compatible_container.name,
            compatible_encoder.name);
        project::ProjectTrack track;
        track.track_id = 1;
        track.name = "Original track";
        first_job.project_snapshot.timeline_tracks.push_back(track);

        const auto first_id = queue.addJob(first_job);
        first_job.project_snapshot.timeline_tracks.front().name = "Changed source";
        require(first_id != 0 && queue.jobCount() == 1 &&
                    queue.rowCount() == 1 && queue.jobAt(0) != nullptr &&
                    queue.jobAt(0)->project_snapshot.timeline_tracks.front().name ==
                        "Original track",
                "Queue entries must retain a copy of the project state at insertion.");
        require(queue.data(queue.index(0, 0), Qt::UserRole).toULongLong() == first_id &&
                    queue.data(queue.index(0, 0), Qt::UserRole + 1).toString() ==
                        QStringLiteral("Prepared"),
                "Queue rows must expose their stable ID and prepared state.");
        require(queue.setJobStatus(first_id, ui::RenderJobStatus::Rendering, 42) &&
                    queue.data(queue.index(0, 0), Qt::UserRole + 1).toString() ==
                        QStringLiteral("Rendering") &&
                    queue.data(queue.index(0, 0), Qt::UserRole + 2).toInt() == 42,
                "Queue rows must expose live render status and progress.");
        require(queue.setJobStatus(first_id, ui::RenderJobStatus::Failed, 0,
                                   QStringLiteral("Synthetic failure")) &&
                    queue.data(queue.index(0, 0), Qt::ToolTipRole).toString().contains(
                        QStringLiteral("Synthetic failure")),
                "Failed queue rows must retain their diagnostic message.");
        require(queue.setJobStatus(first_id, ui::RenderJobStatus::Prepared, 0) &&
                    queue.data(queue.index(0, 0), Qt::UserRole + 1).toString() ==
                        QStringLiteral("Prepared") &&
                    queue.data(queue.index(0, 0), Qt::UserRole + 3).toString().isEmpty(),
                "Retrying a failed job must clear its error and return it to Prepared.");

        const auto second_id = queue.addJob(makeJob(
            QStringLiteral("Second"), QStringLiteral("second.mp4"),
            compatible_container.name, compatible_encoder.name));
        const auto third_id = queue.addJob(makeJob(
            QStringLiteral("Third"), QStringLiteral("third.mp4"),
            compatible_container.name, compatible_encoder.name));
        require(second_id != first_id && third_id != second_id && queue.jobCount() == 3,
                "Adding jobs must assign unique IDs and append them to the queue.");
        queue.setLocked(true);
        require(queue.addJob(makeJob(
                    QStringLiteral("Locked"), QStringLiteral("locked.mp4"),
                    compatible_container.name, compatible_encoder.name)) == 0 &&
                    !queue.moveJob(0, 1) && !queue.removeJobAt(0),
                "Queue structure must not change while a render run is active.");
        queue.setLocked(false);
        require(queue.moveJob(2, 0) && queue.jobAt(0)->id == third_id &&
                    queue.moveJob(0, 2) && queue.jobAt(2)->id == third_id,
                "Queue jobs must move both up and down without losing identity.");
        require(!queue.moveJob(-1, 0) && !queue.moveJob(0, 0) &&
                    !queue.removeJobAt(-1) && queue.removeJobAt(1) &&
                    queue.jobCount() == 2,
                "Invalid queue operations must be rejected and valid removal must work.");

        ui::RenderQueueModel new_session_queue;
        require(new_session_queue.jobCount() == 0,
                "A new queue model must start empty for a new application session.");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
