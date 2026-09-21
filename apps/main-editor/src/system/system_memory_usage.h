#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace system_monitor {

struct MemorySnapshot {
    std::optional<std::uint64_t> system_total_bytes;
    std::optional<std::uint64_t> system_available_bytes;
    std::optional<std::uint64_t> process_working_set_bytes;
    std::optional<std::uint64_t> process_private_usage_bytes;
};

MemorySnapshot queryMemorySnapshot() noexcept;

std::optional<std::uint64_t> usedSystemBytes(
    const MemorySnapshot& snapshot) noexcept;

double bytesToMegabytes(std::uint64_t bytes) noexcept;

double bytesToGigabytes(std::uint64_t bytes) noexcept;

std::string formatMegabytes(
    const std::optional<std::uint64_t>& bytes);

std::string formatGigabytesWithMegabytes(
    const std::optional<std::uint64_t>& bytes);

std::string formatProcessMemoryUsage(const MemorySnapshot& snapshot);

}  // namespace system_monitor
