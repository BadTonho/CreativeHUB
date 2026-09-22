#pragma once

#include <cstdint>
#include <optional>

namespace system_monitor {

struct PerformanceSnapshot {
    std::optional<double> process_cpu_percent;
    std::optional<std::uint64_t> process_working_set_bytes;
    std::optional<std::uint64_t> process_private_usage_bytes;
    std::optional<std::uint64_t> system_total_bytes;
    std::optional<std::uint64_t> system_available_bytes;
    std::optional<double> gpu_utilization_percent;
    std::optional<std::uint64_t> gpu_memory_used_bytes;
};

[[nodiscard]] std::optional<double> calculateProcessCpuPercent(
    std::uint64_t process_time_delta_nanoseconds,
    std::uint64_t wall_time_delta_nanoseconds) noexcept;

class PerformanceSampler final {
public:
    [[nodiscard]] PerformanceSnapshot sample() noexcept;
    void reset() noexcept;

private:
    std::uint64_t previous_process_time_nanoseconds_ = 0;
    std::uint64_t previous_wall_time_nanoseconds_ = 0;
};

}  // namespace system_monitor
