#include "system/system_memory_usage.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <optional>

int main() {
    constexpr std::uint64_t mib = 1024ULL * 1024ULL;
    constexpr std::uint64_t gib = 1024ULL * 1024ULL * 1024ULL;

    assert(std::abs(system_monitor::bytesToMegabytes(0)) < 1e-12);
    assert(std::abs(system_monitor::bytesToMegabytes(mib) - 1.0) < 1e-12);
    assert(std::abs(system_monitor::bytesToGigabytes(gib) - 1.0) < 1e-12);

    const system_monitor::MemorySnapshot snapshot{
        16 * gib,
        6 * gib,
        320 * mib,
        500 * mib,
    };
    assert(
        system_monitor::usedSystemBytes(snapshot) ==
        std::optional<std::uint64_t>(10 * gib));
    assert(
        system_monitor::formatProcessMemoryUsage(snapshot) ==
        "RAM: 320 MB");
    assert(
        system_monitor::formatGigabytesWithMegabytes(snapshot.system_total_bytes) ==
        "16.0 GB (16384 MB)");
    assert(
        system_monitor::formatGigabytesWithMegabytes(
            system_monitor::usedSystemBytes(snapshot)) ==
        "10.0 GB (10240 MB)");
    assert(
        system_monitor::formatMegabytes(snapshot.process_private_usage_bytes) ==
        "500 MB");

    const system_monitor::MemorySnapshot rounded_snapshot{
        0,
        0,
        320 * mib + mib / 2,
        std::nullopt,
    };
    assert(
        system_monitor::formatProcessMemoryUsage(rounded_snapshot) ==
        "RAM: 320.5 MB");

    const system_monitor::MemorySnapshot clamped_snapshot{
        gib,
        2 * gib,
        std::nullopt,
        std::nullopt,
    };
    assert(
        system_monitor::usedSystemBytes(clamped_snapshot) ==
        std::optional<std::uint64_t>(0));

    const system_monitor::MemorySnapshot invalid_snapshot{};
    assert(
        !system_monitor::usedSystemBytes(invalid_snapshot).has_value());
    assert(
        system_monitor::formatProcessMemoryUsage(invalid_snapshot) ==
        "RAM: N/A");
    assert(
        system_monitor::formatMegabytes(std::nullopt) == "N/A");
    assert(
        system_monitor::formatGigabytesWithMegabytes(std::nullopt) == "N/A");

    return 0;
}
