#pragma once

#include <cstddef>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace logging {

enum class Level {
    Debug,
    Info,
    Warning,
    Error,
    Fatal,
};

using Context = std::vector<std::pair<std::string, std::string>>;

struct Options {
    std::size_t max_file_size_bytes = 5U * 1024U * 1024U;
    std::size_t max_file_count = 3U;
#ifdef NDEBUG
    Level minimum_level = Level::Info;
#else
    Level minimum_level = Level::Debug;
#endif
};

class Logger final {
public:
    Logger() = default;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    bool initialize(const std::filesystem::path& directory,
                    Options options = {}) noexcept;
    bool initialize_default(Options options = {}) noexcept;

    void log(Level level,
             std::string_view subsystem,
             std::string_view operation,
             std::string_view message,
             const Context& context = {}) noexcept;

    [[nodiscard]] bool is_initialized() const noexcept;
    [[nodiscard]] std::filesystem::path log_directory() const;
    [[nodiscard]] std::filesystem::path log_path() const;

    static Logger& instance() noexcept;

private:
    mutable std::mutex mutex_;
    std::filesystem::path directory_;
    Options options_;
    bool initialized_ = false;
};

[[nodiscard]] std::string_view level_name(Level level) noexcept;

} // namespace logging
