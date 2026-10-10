#include "test_check.h"

#include <string_view>

#ifndef NDEBUG
#error This probe must be built with NDEBUG to verify Release-style checks.
#endif

int main(int argc, char* argv[]) {
    int evaluations = 0;
    CS_TEST_CHECK(++evaluations == 1);
    if (evaluations != 1) {
        return 2;
    }

    if (argc == 2 && std::string_view(argv[1]) == "fail") {
        CS_TEST_CHECK(false);
        return 3;
    }
    return 0;
}
