#pragma once

#include "ui/workspace/pages/render/render_job.h"

#include <QObject>

#include <atomic>
#include <thread>
#include <vector>

namespace ui {

class RenderQueueController final : public QObject {
    Q_OBJECT

public:
    explicit RenderQueueController(QObject* parent = nullptr);
    ~RenderQueueController() override;

    [[nodiscard]] bool start(std::vector<RenderJob> jobs);
    void cancel() noexcept;
    [[nodiscard]] bool isRunning() const noexcept;

signals:
    void jobStarted(qulonglong job_id);
    void jobProgress(qulonglong job_id, int progress_percent);
    void jobCompleted(qulonglong job_id);
    void jobFailed(qulonglong job_id, QString message, QString error_code);
    void jobCanceled(qulonglong job_id);
    void queueFinished(bool canceled);

private:
    void joinFinishedThread();

    std::atomic_bool cancel_requested_{false};
    std::atomic_bool running_{false};
    std::thread worker_thread_;
};

}  // namespace ui
