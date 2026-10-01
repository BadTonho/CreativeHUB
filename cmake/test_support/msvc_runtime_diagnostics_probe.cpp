#include <rtcapi.h>

#include <crtdbg.h>

#include <cstring>

int main(int argc, char* argv[]) {
    if (argc != 2) {
        return 2;
    }

    if (std::strcmp(argv[1], "rtc") == 0) {
        _RTC_UninitUse("controlled_runtime_diagnostic_probe");
        return 0;
    }

    if (std::strcmp(argv[1], "crt-assert") == 0) {
        _ASSERTE(false && "controlled_crt_assert_probe");
        return 0;
    }

    return 2;
}
