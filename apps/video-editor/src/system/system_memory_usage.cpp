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
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <sys/sysctl.h>
#elif defined(__linux__)
#include <fstream>
#include <string>
#endif

namespace system_monitor {

#if defined(__linux__)
namespace {

std::optional<std::uint64_t> parseProcBytesLine(
    const std::string& line,
    const char* expected_name) {
    std::istringstream stream(line);
    std::string name;
    std::uint64_t value = 0;
    std::string unit;
    if (!(stream >> name >> value)) return std::nullopt;
    if (name != expected_name) return std::nullopt;
    stream >> unit;
    return unit == "kB" ? value * 1024ULL : value;
}

}  // namespace
#endif

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
#elif defined(__APPLE__)
    std::uint64_t total_memory = 0;
    std::size_t total_size = sizeof(total_memory);
    if (sysctlbyname(
            "hw.memsize",
            &total_memory,
            &total_size,
            nullptr,
            0) == 0) {
        snapshot.system_total_bytes = total_memory;
    }

    mach_port_t host = mach_host_self();
    vm_size_t page_size = 0;
    if (host_page_size(host, &page_size) == KERN_SUCCESS) {
        vm_statistics64_data_t vm_stats{};
        mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
        if (host_statistics64(
                host,
                HOST_VM_INFO64,
                reinterpret_cast<host_info64_t>(&vm_stats),
                &count) == KERN_SUCCESS) {
            const auto available_pages = static_cast<std::uint64_t>(
                vm_stats.free_count + vm_stats.inactive_count +
                vm_stats.speculative_count);
            snapshot.system_available_bytes = available_pages * page_size;
        }
    }

    mach_task_basic_info_data_t task_info_data{};
    mach_msg_type_number_t task_count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(
            mach_task_self(),
            MACH_TASK_BASIC_INFO,
            reinterpret_cast<task_info_t>(&task_info_data),
            &task_count) == KERN_SUCCESS) {
        snapshot.process_working_set_bytes =
            static_cast<std::uint64_t>(task_info_data.resident_size);
    }

    task_vm_info_data_t vm_info{};
    mach_msg_type_number_t vm_count = TASK_VM_INFO_COUNT;
    if (task_info(
            mach_task_self(),
            TASK_VM_INFO,
            reinterpret_cast<task_info_t>(&vm_info),
            &vm_count) == KERN_SUCCESS) {
        snapshot.process_private_usage_bytes =
            static_cast<std::uint64_t>(vm_info.phys_footprint);
    }
    mach_port_deallocate(mach_task_self(), host);
#elif defined(__linux__)
    try {
        std::ifstream memory_info("/proc/meminfo");
        std::string line;
        while (std::getline(memory_info, line)) {
            if (const auto bytes = parseProcBytesLine(line, "MemTotal:");
                bytes.has_value()) {
                snapshot.system_total_bytes = *bytes;
            }
            if (const auto bytes = parseProcBytesLine(line, "MemAvailable:");
                bytes.has_value()) {
                snapshot.system_available_bytes = *bytes;
            }
        }

        std::ifstream process_status("/proc/self/status");
        while (std::getline(process_status, line)) {
            if (const auto bytes = parseProcBytesLine(line, "VmRSS:");
                bytes.has_value()) {
                snapshot.process_working_set_bytes = *bytes;
            }
        }

        std::ifstream private_memory("/proc/self/smaps_rollup");
        std::uint64_t private_bytes = 0;
        while (std::getline(private_memory, line)) {
            if (const auto bytes = parseProcBytesLine(line, "Private_Clean:");
                bytes.has_value()) {
                private_bytes += *bytes;
            }
            if (const auto bytes = parseProcBytesLine(line, "Private_Dirty:");
                bytes.has_value()) {
                private_bytes += *bytes;
            }
        }
        if (private_bytes > 0) snapshot.process_private_usage_bytes = private_bytes;
    } catch (...) {
        // Memory metrics are best-effort. Preserve any values already read and
        // let the UI render unavailable fields instead of terminating on OOM.
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
