#pragma once

#include <cstdlib>
#include <iostream>

namespace creative_suite::test_support {

// Test-only checks always evaluate their condition, regardless of NDEBUG.
// A failed check reports its source location and ends the test with exit code 1.
inline void check(bool passed, const char* expression, const char* file, int line) {
    if (!passed) {
        std::cerr << file << ':' << line << ": check failed: " << expression << std::endl;
        std::exit(EXIT_FAILURE);
    }
}

} // namespace creative_suite::test_support

#define CS_TEST_CHECK(condition) \
    ::creative_suite::test_support::check(static_cast<bool>(condition), #condition, __FILE__, __LINE__)
