#include "logging/logger.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream content;
    content << input.rdbuf();
    return content.str();
}

std::filesystem::path uniqueTestDirectory() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
        ("creative-suite-logger-test-" + std::to_string(stamp));
}

} // namespace

int main() {
    const auto directory = uniqueTestDirectory();

    try {
        logging::Logger logger;
        logging::Options options;
        options.max_file_size_bytes = 256;
        options.max_file_count = 3;
        options.minimum_level = logging::Level::Debug;

        require(logger.initialize(directory, options), "Logger initialization failed.");

        logger.log(
            logging::Level::Error,
            "media",
            "probe",
            "A \"quoted\" message\nwith a new line and a \\ slash.",
            {{"path", "C:\\media\\clip.mkv"}, {"error_code", "-22"}});

        const auto initial_content = readFile(logger.log_path());
        require(initial_content.find("level=Error") != std::string::npos,
                "Log level was not written.");
        require(initial_content.find("subsystem=\"media\"") != std::string::npos,
                "Log subsystem was not written.");
        require(initial_content.find("operation=\"probe\"") != std::string::npos,
                "Log operation was not written.");
        require(initial_content.find("message=\"A \\\"quoted\\\" message\\nwith a new line and a \\\\ slash.\"")
                    != std::string::npos,
                "Log message escaping is incorrect.");
        require(initial_content.find("error_code=\"-22\"") != std::string::npos,
                "Log context was not written.");

        for (int index = 0; index < 40; ++index) {
            logger.log(
                logging::Level::Info,
                "test",
                "rotation",
                "This entry forces the bounded log rotation behavior.",
                {{"index", std::to_string(index)}});
        }

        require(std::filesystem::exists(logger.log_path()), "Current log file is missing.");
        require(std::filesystem::exists(logger.log_path().string() + ".1"),
                "First rotated log file is missing.");
        require(std::filesystem::exists(logger.log_path().string() + ".2"),
                "Second rotated log file is missing.");
        require(!std::filesystem::exists(logger.log_path().string() + ".3"),
                "Log rotation exceeded the configured file count.");
    } catch (const std::exception& error) {
        std::error_code cleanup_error;
        std::filesystem::remove_all(directory, cleanup_error);
        std::cerr << error.what() << '\n';
        return 1;
    }

    std::error_code cleanup_error;
    std::filesystem::remove_all(directory, cleanup_error);
    return cleanup_error ? 1 : 0;
}
