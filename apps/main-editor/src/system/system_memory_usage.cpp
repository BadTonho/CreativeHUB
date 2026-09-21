#include "system_memory_usage.h"

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

std::optional<ProcessMemoryUsage> queryProcessMemory() noexcept {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX counters{};
    if (GetProcessMemoryInfo(
            GetCurrentProcess(),
            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
            sizeof(counters)) == 0) {
        return std::nullopt;
    }

    return ProcessMemoryUsage{
        static_cast<std::uint64_t>(counters.WorkingSetSize),
    };
#else
    return std::nullopt;
#endif
}

double bytesToMegabytes(std::uint64_t bytes) noexcept {
    constexpr double bytes_per_megabyte = 1024.0 * 1024.0;
    return static_cast<double>(bytes) / bytes_per_megabyte;
}

namespace {

std::string formatMegabytes(double value) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(1) << value;
    auto result = stream.str();
    if (result.size() >= 2 && result.ends_with(".0")) {
        result.erase(result.size() - 2);
    }
    return result;
}

}  // namespace

std::string formatProcessMemoryUsage(
    const std::optional<ProcessMemoryUsage>& usage) {
    if (!usage.has_value() || usage->working_set_bytes == 0) {
        return "RAM: N/A";
    }

    return "RAM: " +
        formatMegabytes(bytesToMegabytes(usage->working_set_bytes)) + " MB";
}

}  // namespace system_monitor
