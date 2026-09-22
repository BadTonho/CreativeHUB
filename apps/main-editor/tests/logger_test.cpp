#include "logging/logger.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

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

std::string contextValue(const std::string& line, const std::string& key) {
    const auto prefix = key + "=\"";
    const auto start = line.find(prefix);
    if (start == std::string::npos) return {};
    const auto value_start = start + prefix.size();
    const auto end = line.find('"', value_start);
    if (end == std::string::npos) return {};
    return line.substr(value_start, end - value_start);
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
            {{"path", "C:\\media\\clip.mkv"},
             {"error_code", "-22"},
             {"process_id", "spoofed"},
             {"thread_id", "spoofed"},
             {"process_instance_id", "spoofed"}});

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
        require(initial_content.find("process_id=\"") != std::string::npos,
                "Process identifier was not written.");
        require(initial_content.find("thread_id=\"") != std::string::npos,
                "Thread identifier was not written.");
        require(initial_content.find("process_instance_id=\"") != std::string::npos,
                "Process instance identifier was not written.");
        require(initial_content.find("process_id=\"spoofed\"") == std::string::npos,
                "Reserved process identifier was overridden by caller context.");
        require(initial_content.find("thread_id=\"spoofed\"") == std::string::npos,
                "Reserved thread identifier was overridden by caller context.");
        require(initial_content.find("process_instance_id=\"spoofed\"") == std::string::npos,
                "Reserved process instance identifier was overridden by caller context.");

        const auto main_thread_id = contextValue(initial_content, "thread_id");
        const auto process_instance_id = contextValue(
            initial_content,
            "process_instance_id");
        require(!main_thread_id.empty() && !process_instance_id.empty(),
                "Runtime identifiers were empty.");

        std::thread worker([&logger]() {
            logger.log(
                logging::Level::Info,
                "test",
                "thread_identity",
                "Thread identity sample.");
        });
        worker.join();

        const auto worker_content = readFile(logger.log_path());
        const auto worker_thread_id = contextValue(worker_content, "thread_id");
        require(!worker_thread_id.empty() && worker_thread_id != main_thread_id,
                "Worker log did not expose a distinct thread identifier.");
        require(contextValue(worker_content, "process_instance_id") == process_instance_id,
                "Process instance identifier changed between threads.");

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
