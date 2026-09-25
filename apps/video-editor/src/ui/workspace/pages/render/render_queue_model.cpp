#include "ui/workspace/pages/render/render_queue_model.h"

#include <QStringList>

#include <utility>

namespace ui {

RenderQueueModel::RenderQueueModel(QObject* parent)
    : QAbstractListModel(parent) {}

int RenderQueueModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(jobs_.size());
}

QVariant RenderQueueModel::data(const QModelIndex& index, int role) const {
    const auto* job = jobAt(index.row());
    if (job == nullptr) return {};

    if (role == Qt::DisplayRole) {
        return QStringLiteral("%1  |  %2  |  %3 × %4 @ %5 fps")
            .arg(job->display_name, job->settings.container_name)
            .arg(job->settings.width)
            .arg(job->settings.height)
            .arg(job->settings.frame_rate, 0, 'g', 6);
    }
    if (role == Qt::ToolTipRole) {
        return QStringLiteral(
                   "Prepared\nOutput: %1\nVideo: %2\nAudio: %3")
            .arg(job->settings.output_path,
                 job->settings.video_encoder_name,
                 job->settings.export_audio
                     ? job->settings.audio_encoder_name
                     : QStringLiteral("Disabled"));
    }
    if (role == Qt::UserRole) {
        return QVariant::fromValue<qulonglong>(job->id);
    }
    if (role == Qt::UserRole + 1) {
        return QStringLiteral("Prepared");
    }
    return {};
}

std::uint64_t RenderQueueModel::addJob(RenderJob job) {
    const int row = static_cast<int>(jobs_.size());
    beginInsertRows({}, row, row);
    job.id = next_id_++;
    jobs_.push_back(std::move(job));
    endInsertRows();
    return jobs_.back().id;
}

bool RenderQueueModel::removeJobAt(int row) {
    if (row < 0 || row >= static_cast<int>(jobs_.size())) return false;
    beginRemoveRows({}, row, row);
    jobs_.erase(jobs_.begin() + row);
    endRemoveRows();
    return true;
}

bool RenderQueueModel::moveJob(int source_row, int destination_row) {
    if (source_row < 0 || source_row >= static_cast<int>(jobs_.size()) ||
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

const RenderJob* RenderQueueModel::jobAt(int row) const noexcept {
    if (row < 0 || row >= static_cast<int>(jobs_.size())) return nullptr;
    return &jobs_[static_cast<std::size_t>(row)];
}

int RenderQueueModel::jobCount() const noexcept {
    return static_cast<int>(jobs_.size());
}

}  // namespace ui
