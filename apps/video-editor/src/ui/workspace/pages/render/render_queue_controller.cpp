#include "ui/workspace/pages/render/render_queue_controller.h"

#include "rendering/offline_export_renderer.h"

#include <QMetaObject>

#include <algorithm>
#include <exception>
#include <utility>

namespace ui {

RenderQueueController::RenderQueueController(QObject* parent)
    : QObject(parent) {}

RenderQueueController::~RenderQueueController() {
    cancel();
    if (worker_thread_.joinable()) worker_thread_.join();
}

bool RenderQueueController::start(std::vector<RenderJob> jobs) {
    std::erase_if(jobs, [](const auto& job) {
        return job.status == RenderJobStatus::Completed;
    });
    if (jobs.empty() || running_.exchange(true, std::memory_order_acq_rel)) return false;
    joinFinishedThread();
    cancel_requested_.store(false, std::memory_order_release);
    worker_thread_ = std::thread([this, jobs = std::move(jobs)]() mutable {
        bool canceled = false;
        for (const auto& job : jobs) {
            if (cancel_requested_.load(std::memory_order_acquire)) {
                canceled = true;
                break;
            }
            emit jobStarted(static_cast<qulonglong>(job.id));
            try {
                rendering::OfflineExportRenderer::render(
                    job, cancel_requested_, [this, id = static_cast<qulonglong>(job.id)](int value) {
                        emit jobProgress(id, value);
                    });
                emit jobCompleted(static_cast<qulonglong>(job.id));
            } catch (const rendering::ExportCanceled&) {
                emit jobCanceled(static_cast<qulonglong>(job.id));
                canceled = true;
                break;
            } catch (const rendering::ExportError& error) {
                emit jobFailed(static_cast<qulonglong>(job.id),
                               QString::fromUtf8(error.what()),
                               QString::number(error.errorCode()));
            } catch (const std::exception& error) {
                emit jobFailed(static_cast<qulonglong>(job.id),
                               QString::fromUtf8(error.what()), {});
            } catch (...) {
                emit jobFailed(static_cast<qulonglong>(job.id),
                               QStringLiteral("An unknown export error occurred."), {});
            }
        }
        QMetaObject::invokeMethod(this, [this, canceled] {
            running_.store(false, std::memory_order_release);
            emit queueFinished(canceled);
        }, Qt::QueuedConnection);
    });
    return true;
}

void RenderQueueController::cancel() noexcept {
    cancel_requested_.store(true, std::memory_order_release);
}

bool RenderQueueController::isRunning() const noexcept {
    return running_.load(std::memory_order_acquire);
}

void RenderQueueController::joinFinishedThread() {
    if (worker_thread_.joinable()) worker_thread_.join();
}

}  // namespace ui
