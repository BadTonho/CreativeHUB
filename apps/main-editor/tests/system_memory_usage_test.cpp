#include "system/system_memory_usage.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <optional>

int main() {
    constexpr std::uint64_t mib = 1024ULL * 1024ULL;

    assert(std::abs(system_monitor::bytesToMegabytes(0)) < 1e-12);
    assert(std::abs(system_monitor::bytesToMegabytes(mib) - 1.0) < 1e-12);

    const system_monitor::ProcessMemoryUsage usage{
        320 * mib,
    };
    assert(
        system_monitor::formatProcessMemoryUsage(usage) ==
        "RAM: 320 MB");

    const system_monitor::ProcessMemoryUsage rounded_usage{
        320 * mib + mib / 2,
    };
    assert(
        system_monitor::formatProcessMemoryUsage(rounded_usage) ==
        "RAM: 320.5 MB");

    const system_monitor::ProcessMemoryUsage zero_usage{};
    assert(
        system_monitor::formatProcessMemoryUsage(zero_usage) ==
        "RAM: N/A");

    assert(
        system_monitor::formatProcessMemoryUsage(std::nullopt) ==
        "RAM: N/A");

    return 0;
}
