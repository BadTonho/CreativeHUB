#include "system/performance_usage.h"

#include "../../../../cmake/test_support/test_check.h"
#include <cmath>

int main() {
    const auto fifty_percent = system_monitor::calculateProcessCpuPercent(
        500,
        1000);
    CS_TEST_CHECK(fifty_percent.has_value());
    CS_TEST_CHECK(std::abs(*fifty_percent - 50.0) < 1e-12);

    const auto over_one_core = system_monitor::calculateProcessCpuPercent(
        2500,
        1000);
    CS_TEST_CHECK(over_one_core.has_value());
    CS_TEST_CHECK(std::abs(*over_one_core - 250.0) < 1e-12);

    CS_TEST_CHECK(!system_monitor::calculateProcessCpuPercent(1, 0).has_value());

    system_monitor::PerformanceSampler sampler;
    const auto first = sampler.sample();
    CS_TEST_CHECK(!first.process_cpu_percent.has_value());
    const auto second = sampler.sample();
    if (second.process_cpu_percent.has_value()) {
        CS_TEST_CHECK(std::isfinite(*second.process_cpu_percent));
        CS_TEST_CHECK(*second.process_cpu_percent >= 0.0);
    }
    sampler.reset();
    const auto after_reset = sampler.sample();
    CS_TEST_CHECK(!after_reset.process_cpu_percent.has_value());
    return 0;
}
