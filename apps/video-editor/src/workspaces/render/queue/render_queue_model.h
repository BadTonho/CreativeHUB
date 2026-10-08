#pragma once

#include "rendering/render_job.h"

#include <QAbstractListModel>
#include <QString>

#include <cstdint>
#include <vector>

namespace ui {

[[nodiscard]] QString renderJobStatusName(rendering::RenderJobStatus status);

class RenderQueueModel final : public QAbstractListModel {
public:
    explicit RenderQueueModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(
        const QModelIndex& index,
        int role = Qt::DisplayRole) const override;

    [[nodiscard]] std::uint64_t addJob(rendering::RenderJob job);
    [[nodiscard]] bool removeJobAt(int row);
    [[nodiscard]] bool moveJob(int source_row, int destination_row);
    [[nodiscard]] bool setJobStatus(
        std::uint64_t id,
        rendering::RenderJobStatus status,
        int progress_percent = 0,
        QString error_message = {});
    void setLocked(bool locked);
    [[nodiscard]] bool isLocked() const noexcept { return locked_; }
    [[nodiscard]] const rendering::RenderJob* jobAt(int row) const noexcept;
    [[nodiscard]] int jobCount() const noexcept;
private:
    std::vector<rendering::RenderJob> jobs_;
    std::uint64_t next_id_ = 1;
    bool locked_ = false;
};

}  // namespace ui
