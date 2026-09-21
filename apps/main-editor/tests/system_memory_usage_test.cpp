#include "system/system_memory_usage.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <optional>

int main() {
    constexpr std::uint64_t gib = 1024ULL * 1024ULL * 1024ULL;

    assert(std::abs(system_monitor::bytesToGigabytes(0)) < 1e-12);
    assert(std::abs(system_monitor::bytesToGigabytes(gib) - 1.0) < 1e-12);

    const system_monitor::MemoryUsage usage{
        16 * gib,
        6 * gib + gib / 10,
    };
    assert(std::abs(system_monitor::usedPercentage(usage) - 61.875) < 1e-12);
    assert(
        system_monitor::formatMemoryUsage(usage) ==
        "RAM: 62% (9.9/16 GB)");

    const system_monitor::MemoryUsage rounded_usage{10 * gib, gib};
    assert(
        system_monitor::formatMemoryUsage(rounded_usage) ==
        "RAM: 90% (9/10 GB)");

    const system_monitor::MemoryUsage clamped_usage{gib, 2 * gib};
    assert(
        system_monitor::formatMemoryUsage(clamped_usage) ==
        "RAM: 0% (0/1 GB)");

    assert(system_monitor::formatMemoryUsage(std::nullopt) == "RAM: N/A");
    assert(
        system_monitor::formatMemoryUsage(system_monitor::MemoryUsage{}) ==
        "RAM: N/A");

    return 0;
}
