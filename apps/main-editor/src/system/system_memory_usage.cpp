#include "system_memory_usage.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace system_monitor {

std::optional<MemoryUsage> querySystemMemory() noexcept {
#if defined(_WIN32)
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status) == 0) {
        return std::nullopt;
    }

    return MemoryUsage{
        static_cast<std::uint64_t>(status.ullTotalPhys),
        static_cast<std::uint64_t>(status.ullAvailPhys),
    };
#else
    return std::nullopt;
#endif
}

double usedPercentage(const MemoryUsage& usage) noexcept {
    if (usage.total_bytes == 0) {
        return 0.0;
    }

    const auto available_bytes =
        std::min(usage.available_bytes, usage.total_bytes);
    const auto used_bytes = usage.total_bytes - available_bytes;
    return std::clamp(
        100.0 * static_cast<double>(used_bytes) /
            static_cast<double>(usage.total_bytes),
        0.0,
        100.0);
}

double bytesToGigabytes(std::uint64_t bytes) noexcept {
    constexpr double bytes_per_gigabyte = 1024.0 * 1024.0 * 1024.0;
    return static_cast<double>(bytes) / bytes_per_gigabyte;
}

namespace {

std::string formatGigabytes(double value) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(1) << value;
    auto result = stream.str();
    if (result.size() >= 2 && result.ends_with(".0")) {
        result.erase(result.size() - 2);
    }
    return result;
}

}  // namespace

std::string formatMemoryUsage(const std::optional<MemoryUsage>& usage) {
    if (!usage.has_value() || usage->total_bytes == 0) {
        return "RAM: N/A";
    }

    const auto available_bytes =
        std::min(usage->available_bytes, usage->total_bytes);
    const auto used_bytes = usage->total_bytes - available_bytes;
    const auto percentage = static_cast<long>(std::lround(usedPercentage(*usage)));

    return "RAM: " + std::to_string(percentage) + "% (" +
        formatGigabytes(bytesToGigabytes(used_bytes)) + "/" +
        formatGigabytes(bytesToGigabytes(usage->total_bytes)) + " GB)";
}

}  // namespace system_monitor
