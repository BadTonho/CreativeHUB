#pragma once

#include <cstddef>
#include <functional>
#include <memory>

class QThreadPool;

namespace motion::ui::detail {

class LayerEffectWorkerPool final {
public:
    using CancellationPredicate = std::function<bool()>;
    using RangeOperation = std::function<void(
        std::size_t begin,
        std::size_t end,
        const CancellationPredicate& is_cancelled)>;

    explicit LayerEffectWorkerPool(std::size_t maximum_threads);
    ~LayerEffectWorkerPool();

    LayerEffectWorkerPool(const LayerEffectWorkerPool&) = delete;
    LayerEffectWorkerPool& operator=(const LayerEffectWorkerPool&) = delete;

    [[nodiscard]] std::size_t maximumThreadCount() const noexcept;
    [[nodiscard]] bool parallelFor(
        std::size_t count,
        const CancellationPredicate& should_cancel,
        const RangeOperation& operation);

private:
    std::size_t maximum_threads_ = 1;
    std::unique_ptr<QThreadPool> pool_;
};

[[nodiscard]] LayerEffectWorkerPool& sharedLayerEffectWorkerPool();

} // namespace motion::ui::detail
