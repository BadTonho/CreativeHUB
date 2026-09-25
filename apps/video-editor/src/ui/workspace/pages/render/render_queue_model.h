#pragma once

#include "ui/workspace/pages/render/render_job.h"

#include <QAbstractListModel>

#include <cstdint>
#include <vector>

namespace ui {

class RenderQueueModel final : public QAbstractListModel {
public:
    explicit RenderQueueModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(
        const QModelIndex& index,
        int role = Qt::DisplayRole) const override;

    [[nodiscard]] std::uint64_t addJob(RenderJob job);
    [[nodiscard]] bool removeJobAt(int row);
    [[nodiscard]] bool moveJob(int source_row, int destination_row);
    [[nodiscard]] const RenderJob* jobAt(int row) const noexcept;
    [[nodiscard]] int jobCount() const noexcept;

private:
    std::vector<RenderJob> jobs_;
    std::uint64_t next_id_ = 1;
};

}  // namespace ui
