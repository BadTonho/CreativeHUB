#include "performance_usage.h"

#include "system_memory_usage.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <sys/resource.h>
#endif

namespace system_monitor {
namespace {

using Nanoseconds = std::uint64_t;

Nanoseconds wallTimeNanoseconds() noexcept {
    const auto count = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    return count <= 0 ? 0U : static_cast<Nanoseconds>(count);
}

#if defined(_WIN32)
Nanoseconds fileTimeToNanoseconds(const FILETIME& value) noexcept {
    ULARGE_INTEGER ticks{};
    ticks.LowPart = value.dwLowDateTime;
    ticks.HighPart = value.dwHighDateTime;
    return static_cast<Nanoseconds>(ticks.QuadPart) * 100U;
}

std::optional<Nanoseconds> processTimeNanoseconds() noexcept {
    FILETIME creation{};
    FILETIME exit{};
    FILETIME kernel{};
    FILETIME user{};
    if (GetProcessTimes(
            GetCurrentProcess(),
            &creation,
            &exit,
            &kernel,
            &user) == 0) {
        return std::nullopt;
    }
    return fileTimeToNanoseconds(kernel) + fileTimeToNanoseconds(user);
}
#elif defined(__unix__) || defined(__APPLE__)
std::optional<Nanoseconds> processTimeNanoseconds() noexcept {
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) return std::nullopt;
    const auto user = static_cast<Nanoseconds>(usage.ru_utime.tv_sec) * 1'000'000'000ULL +
        static_cast<Nanoseconds>(usage.ru_utime.tv_usec) * 1'000ULL;
    const auto system = static_cast<Nanoseconds>(usage.ru_stime.tv_sec) * 1'000'000'000ULL +
        static_cast<Nanoseconds>(usage.ru_stime.tv_usec) * 1'000ULL;
    return user + system;
}
#else
std::optional<Nanoseconds> processTimeNanoseconds() noexcept {
    return std::nullopt;
}
#endif

}  // namespace

std::optional<double> calculateProcessCpuPercent(
    std::uint64_t process_time_delta_nanoseconds,
    std::uint64_t wall_time_delta_nanoseconds) noexcept {
    if (wall_time_delta_nanoseconds == 0) return std::nullopt;
    const auto percent = static_cast<double>(process_time_delta_nanoseconds) *
        100.0 / static_cast<double>(wall_time_delta_nanoseconds);
    if (!std::isfinite(percent)) return std::nullopt;
    return std::max(0.0, percent);
}

PerformanceSnapshot PerformanceSampler::sample() noexcept {
    PerformanceSnapshot result;
    const auto memory = queryMemorySnapshot();
    result.process_working_set_bytes = memory.process_working_set_bytes;
    result.process_private_usage_bytes = memory.process_private_usage_bytes;
    result.system_total_bytes = memory.system_total_bytes;
    result.system_available_bytes = memory.system_available_bytes;

    const auto wall_time = wallTimeNanoseconds();
    const auto process_time = processTimeNanoseconds();
    if (process_time.has_value() &&
        previous_process_time_nanoseconds_ != 0 &&
        previous_wall_time_nanoseconds_ != 0 &&
        *process_time >= previous_process_time_nanoseconds_ &&
        wall_time > previous_wall_time_nanoseconds_) {
        const auto process_delta = *process_time - previous_process_time_nanoseconds_;
        const auto wall_delta = wall_time - previous_wall_time_nanoseconds_;
        result.process_cpu_percent = calculateProcessCpuPercent(
            process_delta,
            wall_delta);
    }

    previous_process_time_nanoseconds_ = process_time.value_or(0);
    previous_wall_time_nanoseconds_ = wall_time;
    return result;
}

void PerformanceSampler::reset() noexcept {
    previous_process_time_nanoseconds_ = 0;
    previous_wall_time_nanoseconds_ = 0;
}

}  // namespace system_monitor
