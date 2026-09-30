#include "layer_effect_worker_pool.h"

#include <QSemaphore>
#include <QThreadPool>
#include <QRunnable>

#include <algorithm>
#include <atomic>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

namespace motion::ui::detail {
namespace {

constexpr std::size_t maximum_effect_threads = 8;

std::size_t recommendedThreadCount() noexcept
{
    const auto logical_cores = std::thread::hardware_concurrency();
    if (logical_cores == 0) return 1;
    const auto reserved_core_count = logical_cores > 1 ? logical_cores - 1U : 1U;
    return std::min(maximum_effect_threads,
                    static_cast<std::size_t>(reserved_core_count));
}

struct TaskGroup {
    std::atomic<bool> cancelled{false};
    QSemaphore completed;
    std::mutex exception_mutex;
    std::exception_ptr exception;
};

void saveException(TaskGroup& group, std::exception_ptr error) noexcept
{
    {
        std::lock_guard lock(group.exception_mutex);
        if (group.exception == nullptr) group.exception = std::move(error);
    }
    group.cancelled.store(true, std::memory_order_release);
}

} // namespace

LayerEffectWorkerPool::LayerEffectWorkerPool(std::size_t maximum_threads)
    : maximum_threads_(std::clamp<std::size_t>(maximum_threads, 1, maximum_effect_threads))
    , pool_(std::make_unique<QThreadPool>())
{
    pool_->setMaxThreadCount(static_cast<int>(maximum_threads_));
}

LayerEffectWorkerPool::~LayerEffectWorkerPool()
{
    if (pool_ != nullptr) pool_->waitForDone();
}

std::size_t LayerEffectWorkerPool::maximumThreadCount() const noexcept
{
    return maximum_threads_;
}

bool LayerEffectWorkerPool::parallelFor(
    std::size_t count,
    const CancellationPredicate& should_cancel,
    const RangeOperation& operation)
{
    if (count == 0) return true;
    if (should_cancel && should_cancel()) return false;
    if (maximum_threads_ <= 1 || count <= 1) {
        operation(0, count, should_cancel);
        return !(should_cancel && should_cancel());
    }

    const auto task_count = std::min(maximum_threads_, count);
    const auto shared_operation = std::make_shared<const RangeOperation>(operation);
    const auto group = std::make_shared<TaskGroup>();
    std::vector<std::unique_ptr<QRunnable>> jobs;
    jobs.reserve(task_count);

    for (std::size_t task_index = 0; task_index < task_count; ++task_index) {
        const auto quotient = count / task_count;
        const auto remainder = count % task_count;
        const auto begin = quotient * task_index + std::min(task_index, remainder);
        const auto end = begin + quotient + (task_index < remainder ? 1U : 0U);
        auto* runnable = QRunnable::create(
            [group, shared_operation, begin, end] {
                try {
                    const CancellationPredicate is_cancelled = [group] {
                        return group->cancelled.load(std::memory_order_acquire);
                    };
                    if (!is_cancelled()) (*shared_operation)(begin, end, is_cancelled);
                } catch (...) {
                    saveException(*group, std::current_exception());
                }
                group->completed.release();
            });
        jobs.emplace_back(runnable);
    }

    for (auto& job : jobs) {
        auto* runnable = job.release();
        pool_->start(runnable);
    }

    std::size_t completed = 0;
    while (completed < task_count) {
        if (group->completed.tryAcquire()) {
            ++completed;
            continue;
        }
        if (should_cancel && !group->cancelled.load(std::memory_order_acquire)) {
            try {
                if (should_cancel())
                    group->cancelled.store(true, std::memory_order_release);
            } catch (...) {
                saveException(*group, std::current_exception());
            }
        }
        if (group->completed.tryAcquire(1, 1)) ++completed;
    }

    if (should_cancel && !group->cancelled.load(std::memory_order_acquire)) {
        try {
            if (should_cancel()) group->cancelled.store(true, std::memory_order_release);
        } catch (...) {
            saveException(*group, std::current_exception());
        }
    }

    std::exception_ptr worker_exception;
    {
        std::lock_guard lock(group->exception_mutex);
        worker_exception = group->exception;
    }
    if (worker_exception != nullptr) std::rethrow_exception(worker_exception);
    return !group->cancelled.load(std::memory_order_acquire);
}

LayerEffectWorkerPool& sharedLayerEffectWorkerPool()
{
    static LayerEffectWorkerPool pool(recommendedThreadCount());
    return pool;
}

} // namespace motion::ui::detail
