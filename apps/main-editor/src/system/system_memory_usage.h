#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace system_monitor {

struct ProcessMemoryUsage {
    std::uint64_t working_set_bytes = 0;
};

std::optional<ProcessMemoryUsage> queryProcessMemory() noexcept;

double bytesToMegabytes(std::uint64_t bytes) noexcept;

std::string formatProcessMemoryUsage(
    const std::optional<ProcessMemoryUsage>& usage);

}  // namespace system_monitor
