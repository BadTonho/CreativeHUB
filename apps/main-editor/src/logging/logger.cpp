#include "logger.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <system_error>

#include <cstdlib>

namespace logging {
namespace {

int levelRank(Level level) noexcept {
    switch (level) {
    case Level::Debug: return 0;
    case Level::Info: return 1;
    case Level::Warning: return 2;
    case Level::Error: return 3;
    case Level::Fatal: return 4;
    }
    return 4;
}

std::string escapeValue(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size() + 8);

    for (const unsigned char character : value) {
        switch (character) {
        case '\\': escaped += "\\\\"; break;
        case '"': escaped += "\\\""; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default: escaped += static_cast<char>(character); break;
        }
    }

    return escaped;
}

std::string quoteValue(std::string_view value) {
    return '"' + escapeValue(value) + '"';
}

std::string timestampUtc() {
    const auto now = std::chrono::system_clock::now();
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch());
    const auto millisecond_part = milliseconds.count() % 1000;
    const std::time_t time = std::chrono::system_clock::to_time_t(now);

    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &time);
#else
    gmtime_r(&time, &utc);
#endif

    std::ostringstream result;
    result << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S")
           << '.' << std::setfill('0') << std::setw(3) << millisecond_part << 'Z';
    return result.str();
}

std::filesystem::path environmentPath(const char* name) {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || value == nullptr || length == 0) {
        return {};
    }

    const std::filesystem::path result(value);
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') return {};
    return std::filesystem::path(value);
#endif
}

std::filesystem::path fallbackDirectory() {
    std::error_code error;
    const auto temporary = std::filesystem::temp_directory_path(error);
    if (error) return {};
    return temporary / "creative-suite" / "main-editor" / "logs";
}

std::filesystem::path defaultDirectory() {
#ifdef _WIN32
    auto root = environmentPath("LOCALAPPDATA");
    if (root.empty()) root = environmentPath("APPDATA");
    if (!root.empty()) return root / "CreativeSuite" / "MainEditor" / "logs";
#elif defined(__APPLE__)
    const auto home = environmentPath("HOME");
    if (!home.empty()) return home / "Library" / "Logs" / "CreativeSuite" / "MainEditor";
#else
    auto root = environmentPath("XDG_STATE_HOME");
    if (root.empty()) {
        const auto home = environmentPath("HOME");
        if (!home.empty()) root = home / ".local" / "state";
    }
    if (!root.empty()) return root / "creative-suite" / "main-editor" / "logs";
#endif

    return fallbackDirectory();
}

bool ensureDirectory(const std::filesystem::path& directory) {
    if (directory.empty()) return false;

    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) return false;
    return std::filesystem::is_directory(directory, error) && !error;
}

std::filesystem::path indexedPath(const std::filesystem::path& directory,
                                  std::size_t index) {
    const auto current = directory / "main-editor.log";
    if (index == 0) return current;
    return std::filesystem::path(current.string() + "." + std::to_string(index));
}

std::string formatLine(Level level,
                       std::string_view subsystem,
                       std::string_view operation,
                       std::string_view message,
                       const Context& context) {
    std::ostringstream line;
    line << timestampUtc()
         << " | level=" << level_name(level)
         << " | subsystem=" << quoteValue(subsystem)
         << " | operation=" << quoteValue(operation)
         << " | message=" << quoteValue(message);

    for (const auto& [key, value] : context) {
        line << " | " << key << '=' << quoteValue(value);
    }
    line << '\n';
    return line.str();
}

void writeFallback(std::string_view line) noexcept {
    try {
        std::cerr << "[logger fallback] " << line;
        std::cerr.flush();
    } catch (...) {
        // Logging must never terminate the application while reporting a failure.
    }
}

} // namespace

std::string_view level_name(Level level) noexcept {
    switch (level) {
    case Level::Debug: return "Debug";
    case Level::Info: return "Info";
    case Level::Warning: return "Warning";
    case Level::Error: return "Error";
    case Level::Fatal: return "Fatal";
    }
    return "Unknown";
}

bool Logger::initialize(const std::filesystem::path& directory, Options options) noexcept {
    try {
        std::lock_guard lock(mutex_);
        options.max_file_size_bytes = options.max_file_size_bytes == 0
            ? 5U * 1024U * 1024U
            : options.max_file_size_bytes;
        options.max_file_count = options.max_file_count == 0 ? 1U : options.max_file_count;

        auto selected_directory = directory;
        if (!ensureDirectory(selected_directory)) {
            writeFallback("Could not create the configured log directory; using a temporary directory.\n");
            selected_directory = fallbackDirectory();
        }
        if (!ensureDirectory(selected_directory)) {
            directory_.clear();
            options_ = options;
            initialized_ = false;
            return false;
        }

        directory_ = std::move(selected_directory);
        options_ = options;
        initialized_ = true;
        return true;
    } catch (...) {
        initialized_ = false;
        return false;
    }
}

bool Logger::initialize_default(Options options) noexcept {
    return initialize(defaultDirectory(), options);
}

void Logger::log(Level level,
                 std::string_view subsystem,
                 std::string_view operation,
                 std::string_view message,
                 const Context& context) noexcept {
    try {
        const auto line = formatLine(level, subsystem, operation, message, context);
        std::lock_guard lock(mutex_);
        if (levelRank(level) < levelRank(options_.minimum_level)) return;
        if (!initialized_) {
            writeFallback(line);
            return;
        }

        const auto current = indexedPath(directory_, 0);
        std::error_code error;
        const auto current_size = std::filesystem::exists(current, error)
            ? std::filesystem::file_size(current, error)
            : 0U;
        if (!error && current_size > 0 &&
            current_size + line.size() > options_.max_file_size_bytes) {
            for (std::size_t index = options_.max_file_count; index > 1; --index) {
                const auto source = indexedPath(directory_, index - 2);
                const auto destination = indexedPath(directory_, index - 1);
                std::filesystem::remove(destination, error);
                error.clear();
                if (std::filesystem::exists(source, error) && !error) {
                    std::filesystem::rename(source, destination, error);
                    if (error) break;
                }
                error.clear();
            }
        }

        std::ofstream output(current, std::ios::binary | std::ios::app);
        if (!output) {
            writeFallback(line);
            return;
        }
        output << line;
        if (!output) writeFallback(line);
    } catch (...) {
        writeFallback("Logger could not write an entry.\n");
    }
}

bool Logger::is_initialized() const noexcept {
    std::lock_guard lock(mutex_);
    return initialized_;
}

std::filesystem::path Logger::log_directory() const {
    std::lock_guard lock(mutex_);
    return directory_;
}

std::filesystem::path Logger::log_path() const {
    std::lock_guard lock(mutex_);
    if (!initialized_) return {};
    return indexedPath(directory_, 0);
}

Logger& Logger::instance() noexcept {
    static Logger logger;
    return logger;
}

} // namespace logging
