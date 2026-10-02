#include "ui/workspace/pages/render/render_queue_controller.h"

#include "rendering/offline_export_renderer.h"
#include "logging/logger.h"

#include <QMetaObject>
#include <QOffscreenSurface>

#include <algorithm>
#include <exception>
#include <utility>
#include <system_error>

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
    gpu_surface_.reset();
    if (std::any_of(jobs.begin(), jobs.end(), [](const auto& job) { return job.settings.gpu_composition_enabled; })) {
        try { gpu_surface_ = creative_suite::composition::OpenGlFrameCompositor::createSurface(); }
        catch (const std::exception& error) {
            logging::Logger::instance().log(logging::Level::Error, "render", "create-export-surface", error.what());
        }
    }
    cancel_requested_.store(false, std::memory_order_release);
    try {
        worker_thread_ = std::thread([this, jobs = std::move(jobs)]() mutable {
            bool canceled = false;
            for (const auto& job : jobs) {
                if (cancel_requested_.load(std::memory_order_acquire)) {
                    canceled = true;
                    break;
                }
                emit jobStarted(static_cast<qulonglong>(job.id));
                try {
                    rendering::OfflineExportOptions options;
                    options.gpu_surface = gpu_surface_.get();
                    options.warning_callback = [this, id = static_cast<qulonglong>(job.id)](const auto& message, auto code) {
                        emit jobWarning(id, QString::fromStdString(message), QString::number(code));
                    };
                    rendering::OfflineExportRenderer::render(
                        job, cancel_requested_, [this, id = static_cast<qulonglong>(job.id)](int value) {
                            emit jobProgress(id, value);
                        }, options);
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
                joinFinishedThread();
                gpu_surface_.reset();
                running_.store(false, std::memory_order_release);
                emit queueFinished(canceled);
            }, Qt::QueuedConnection);
        });
    } catch (const std::system_error& error) {
        logging::Logger::instance().log(logging::Level::Error, "render", "start-export-worker", error.what(),
            {{"error_code", std::to_string(error.code().value())}});
        running_.store(false, std::memory_order_release);
        gpu_surface_.reset();
        return false;
    }
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
