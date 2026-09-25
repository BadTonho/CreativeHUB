#include "ui/workspace/pages/render/render_queue_model.h"

#include <QStringList>

#include <algorithm>
#include <iterator>
#include <utility>

namespace ui {

QString renderJobStatusName(RenderJobStatus status) {
    switch (status) {
    case RenderJobStatus::Prepared: return QStringLiteral("Prepared");
    case RenderJobStatus::Rendering: return QStringLiteral("Rendering");
    case RenderJobStatus::Completed: return QStringLiteral("Completed");
    case RenderJobStatus::Failed: return QStringLiteral("Failed");
    case RenderJobStatus::Canceled: return QStringLiteral("Canceled");
    }
    return QStringLiteral("Unknown");
}

RenderQueueModel::RenderQueueModel(QObject* parent)
    : QAbstractListModel(parent) {}

int RenderQueueModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(jobs_.size());
}

QVariant RenderQueueModel::data(const QModelIndex& index, int role) const {
    const auto* job = jobAt(index.row());
    if (job == nullptr) return {};

    if (role == Qt::DisplayRole) {
        auto label = QStringLiteral("%1  |  %2  |  %3 × %4 @ %5 fps  |  %6")
            .arg(job->display_name, job->settings.container_name)
            .arg(job->settings.width)
            .arg(job->settings.height)
            .arg(job->settings.frame_rate, 0, 'g', 6)
            .arg(renderJobStatusName(job->status));
        if (job->status == RenderJobStatus::Rendering) {
            label += QStringLiteral(" (%1%)").arg(job->progress_percent);
        }
        return label;
    }
    if (role == Qt::ToolTipRole) {
        return QStringLiteral(
                   "%1\nOutput: %2\nVideo: %3\nAudio: %4%5")
            .arg(renderJobStatusName(job->status))
            .arg(job->settings.output_path,
                 job->settings.video_encoder_name,
                 job->settings.export_audio
                     ? job->settings.audio_encoder_name
                     : QStringLiteral("Disabled"),
                 job->error_message.isEmpty()
                     ? QString{}
                     : QStringLiteral("\nError: ") + job->error_message);
    }
    if (role == Qt::UserRole) {
        return QVariant::fromValue<qulonglong>(job->id);
    }
    if (role == Qt::UserRole + 1) {
        return renderJobStatusName(job->status);
    }
    if (role == Qt::UserRole + 2) {
        return job->progress_percent;
    }
    if (role == Qt::UserRole + 3) {
        return job->error_message;
    }
    return {};
}

std::uint64_t RenderQueueModel::addJob(RenderJob job) {
    if (locked_) return 0;
    const int row = static_cast<int>(jobs_.size());
    beginInsertRows({}, row, row);
    job.id = next_id_++;
    jobs_.push_back(std::move(job));
    endInsertRows();
    return jobs_.back().id;
}

bool RenderQueueModel::removeJobAt(int row) {
    if (locked_ || row < 0 || row >= static_cast<int>(jobs_.size())) return false;
    beginRemoveRows({}, row, row);
    jobs_.erase(jobs_.begin() + row);
    endRemoveRows();
    return true;
}

bool RenderQueueModel::moveJob(int source_row, int destination_row) {
    if (locked_ || source_row < 0 || source_row >= static_cast<int>(jobs_.size()) ||
        destination_row < 0 || destination_row >= static_cast<int>(jobs_.size()) ||
        source_row == destination_row) {
        return false;
    }

    const int destination_child = destination_row > source_row
        ? destination_row + 1
        : destination_row;
    if (!beginMoveRows({}, source_row, source_row, {}, destination_child)) {
        return false;
    }
    auto job = std::move(jobs_[static_cast<std::size_t>(source_row)]);
    jobs_.erase(jobs_.begin() + source_row);
    jobs_.insert(jobs_.begin() + destination_row, std::move(job));
    endMoveRows();
    return true;
}

bool RenderQueueModel::setJobStatus(
    std::uint64_t id,
    RenderJobStatus status,
    int progress_percent,
    QString error_message) {
    const auto found = std::find_if(jobs_.begin(), jobs_.end(), [id](const auto& job) {
        return job.id == id;
    });
    if (found == jobs_.end()) return false;
    found->status = status;
    found->progress_percent = std::clamp(progress_percent, 0, 100);
    found->error_message = std::move(error_message);
    const auto row = static_cast<int>(std::distance(jobs_.begin(), found));
    const auto model_index = index(row, 0);
    emit dataChanged(model_index, model_index,
                     {Qt::DisplayRole, Qt::ToolTipRole,
                      Qt::UserRole + 1, Qt::UserRole + 2, Qt::UserRole + 3});
    return true;
}

void RenderQueueModel::setLocked(bool locked) {
    locked_ = locked;
}

const RenderJob* RenderQueueModel::jobAt(int row) const noexcept {
    if (row < 0 || row >= static_cast<int>(jobs_.size())) return nullptr;
    return &jobs_[static_cast<std::size_t>(row)];
}

int RenderQueueModel::jobCount() const noexcept {
    return static_cast<int>(jobs_.size());
}

}  // namespace ui
