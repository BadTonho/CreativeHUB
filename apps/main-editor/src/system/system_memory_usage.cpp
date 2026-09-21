#include "system_memory_usage.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#endif

namespace system_monitor {

MemorySnapshot queryMemorySnapshot() noexcept {
    MemorySnapshot snapshot;

#if defined(_WIN32)
    MEMORYSTATUSEX system_status{};
    system_status.dwLength = sizeof(system_status);
    if (GlobalMemoryStatusEx(&system_status) != 0) {
        snapshot.system_total_bytes =
            static_cast<std::uint64_t>(system_status.ullTotalPhys);
        snapshot.system_available_bytes =
            static_cast<std::uint64_t>(system_status.ullAvailPhys);
    }

    PROCESS_MEMORY_COUNTERS_EX process_counters{};
    if (GetProcessMemoryInfo(
            GetCurrentProcess(),
            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&process_counters),
            sizeof(process_counters)) != 0) {
        snapshot.process_working_set_bytes =
            static_cast<std::uint64_t>(process_counters.WorkingSetSize);
        snapshot.process_private_usage_bytes =
            static_cast<std::uint64_t>(process_counters.PrivateUsage);
    }
#endif

    return snapshot;
}

std::optional<std::uint64_t> usedSystemBytes(
    const MemorySnapshot& snapshot) noexcept {
    if (!snapshot.system_total_bytes.has_value() ||
        !snapshot.system_available_bytes.has_value() ||
        *snapshot.system_total_bytes == 0) {
        return std::nullopt;
    }

    const auto available_bytes = std::min(
        *snapshot.system_available_bytes,
        *snapshot.system_total_bytes);
    return *snapshot.system_total_bytes - available_bytes;
}

double bytesToMegabytes(std::uint64_t bytes) noexcept {
    constexpr double bytes_per_megabyte = 1024.0 * 1024.0;
    return static_cast<double>(bytes) / bytes_per_megabyte;
}

double bytesToGigabytes(std::uint64_t bytes) noexcept {
    constexpr double bytes_per_gigabyte = 1024.0 * 1024.0 * 1024.0;
    return static_cast<double>(bytes) / bytes_per_gigabyte;
}

namespace {

std::string formatFixed(double value, int precision, bool trim_zero) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(precision) << value;
    auto result = stream.str();
    if (trim_zero && result.size() >= 2 && result.ends_with(".0")) {
        result.erase(result.size() - 2);
    }
    return result;
}

}  // namespace

std::string formatMegabytes(
    const std::optional<std::uint64_t>& bytes) {
    if (!bytes.has_value()) {
        return "N/A";
    }

    return formatFixed(bytesToMegabytes(*bytes), 1, true) + " MB";
}

std::string formatGigabytesWithMegabytes(
    const std::optional<std::uint64_t>& bytes) {
    if (!bytes.has_value()) {
        return "N/A";
    }

    return formatFixed(bytesToGigabytes(*bytes), 1, false) + " GB (" +
        formatMegabytes(bytes) + ")";
}

std::string formatProcessMemoryUsage(const MemorySnapshot& snapshot) {
    if (!snapshot.process_working_set_bytes.has_value() ||
        *snapshot.process_working_set_bytes == 0) {
        return "RAM: N/A";
    }

    return "RAM: " +
        formatMegabytes(snapshot.process_working_set_bytes);
}

}  // namespace system_monitor
