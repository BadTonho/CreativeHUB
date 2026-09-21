#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace system_monitor {

struct MemoryUsage {
    std::uint64_t total_bytes = 0;
    std::uint64_t available_bytes = 0;
};

std::optional<MemoryUsage> querySystemMemory() noexcept;

double usedPercentage(const MemoryUsage& usage) noexcept;

double bytesToGigabytes(std::uint64_t bytes) noexcept;

std::string formatMemoryUsage(const std::optional<MemoryUsage>& usage);

}  // namespace system_monitor
