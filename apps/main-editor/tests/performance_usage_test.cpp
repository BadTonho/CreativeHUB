#include "system/performance_usage.h"

#include <cassert>
#include <cmath>

int main() {
    const auto fifty_percent = system_monitor::calculateProcessCpuPercent(
        500,
        1000);
    assert(fifty_percent.has_value());
    assert(std::abs(*fifty_percent - 50.0) < 1e-12);

    const auto over_one_core = system_monitor::calculateProcessCpuPercent(
        2500,
        1000);
    assert(over_one_core.has_value());
    assert(std::abs(*over_one_core - 250.0) < 1e-12);

    assert(!system_monitor::calculateProcessCpuPercent(1, 0).has_value());

    system_monitor::PerformanceSampler sampler;
    const auto first = sampler.sample();
    assert(!first.process_cpu_percent.has_value());
    const auto second = sampler.sample();
    if (second.process_cpu_percent.has_value()) {
        assert(std::isfinite(*second.process_cpu_percent));
        assert(*second.process_cpu_percent >= 0.0);
    }
    sampler.reset();
    assert(!sampler.sample().process_cpu_percent.has_value());
    return 0;
}
