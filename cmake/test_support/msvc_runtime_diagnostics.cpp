#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cwchar>

#include <crtdbg.h>
#include <rtcapi.h>

namespace {

constexpr UINT kRtcFailureExitCode = 0xE001;
constexpr UINT kCrtFailureExitCode = 0xE002;

void writeUtf8Line(const wchar_t* line) noexcept {
    const HANDLE output = GetStdHandle(STD_ERROR_HANDLE);
    if (output == nullptr || output == INVALID_HANDLE_VALUE || line == nullptr) {
        return;
    }

    const int wide_length = static_cast<int>(wcslen(line));
    const int utf8_length = WideCharToMultiByte(
        CP_UTF8, 0, line, wide_length, nullptr, 0, nullptr, nullptr);
    if (utf8_length <= 0) {
        return;
    }

    char utf8[8192]{};
    if (utf8_length >= static_cast<int>(sizeof(utf8))) {
        return;
    }
    if (WideCharToMultiByte(CP_UTF8, 0, line, wide_length, utf8,
                            utf8_length, nullptr, nullptr) != utf8_length) {
        return;
    }

    DWORD written = 0;
    (void)WriteFile(output, utf8, static_cast<DWORD>(utf8_length), &written, nullptr);
    (void)WriteFile(output, "\r\n", 2, &written, nullptr);
}

[[noreturn]] void terminateTest(UINT exit_code) noexcept {
    const HANDLE output = GetStdHandle(STD_ERROR_HANDLE);
    if (output != nullptr && output != INVALID_HANDLE_VALUE) {
        (void)FlushFileBuffers(output);
    }
    (void)TerminateProcess(GetCurrentProcess(), exit_code);
    _exit(static_cast<int>(exit_code));
}

int __cdecl reportRuntimeCheck(int error_type, const wchar_t* file, int line,
                                const wchar_t* module, const wchar_t* format, ...) {
    wchar_t detail[2048]{};
    if (format != nullptr) {
        va_list arguments;
        va_start(arguments, format);
        (void)_vsnwprintf_s(detail, _countof(detail), _TRUNCATE, format, arguments);
        va_end(arguments);
    }

    wchar_t report[4096]{};
    (void)_snwprintf_s(report, _countof(report), _TRUNCATE,
        L"CREATIVE_SUITE_RUNTIME_DIAGNOSTIC kind=RTC error_type=%d file=%s line=%d module=%s message=%s",
        error_type,
        file != nullptr ? file : L"<unknown>",
        line,
        module != nullptr ? module : L"<unknown>",
        detail[0] != L'\0' ? detail : L"<empty>");
    writeUtf8Line(report);
    terminateTest(kRtcFailureExitCode);
}

int __cdecl reportCrtDiagnostic(int report_type, wchar_t* message, int* return_value) {
    const wchar_t* type_name = report_type == _CRT_ASSERT ? L"ASSERT" :
        report_type == _CRT_ERROR ? L"ERROR" : L"WARN";

    wchar_t module[MAX_PATH]{};
    const DWORD module_length = GetModuleFileNameW(
        nullptr, module, static_cast<DWORD>(_countof(module)));
    if (module_length == 0 || module_length >= _countof(module)) {
        wcscpy_s(module, L"<unknown>");
    }

    wchar_t report[8192]{};
    (void)_snwprintf_s(report, _countof(report), _TRUNCATE,
        L"CREATIVE_SUITE_RUNTIME_DIAGNOSTIC kind=CRT report_type=%s module=%s message=%s",
        type_name, module, message != nullptr ? message : L"<empty>");
    writeUtf8Line(report);

    if (report_type == _CRT_ASSERT || report_type == _CRT_ERROR) {
        if (return_value != nullptr) {
            *return_value = 0;
        }
        terminateTest(kCrtFailureExitCode);
    }

    return FALSE;
}

struct RuntimeDiagnosticsInitializer {
    RuntimeDiagnosticsInitializer() noexcept {
        (void)_RTC_SetErrorFuncW(&reportRuntimeCheck);
        (void)_CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
        (void)_CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
        (void)_CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
        (void)_CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
        (void)_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
        (void)_CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
        (void)_CrtSetReportHookW2(_CRT_RPTHOOK_INSTALL, &reportCrtDiagnostic);
    }
};

RuntimeDiagnosticsInitializer runtime_diagnostics_initializer;

}  // namespace
