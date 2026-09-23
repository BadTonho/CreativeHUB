#include "main_window.h"

#include "logging/logger.h"

#include <QApplication>

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>

namespace {

std::string pathToUtf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return std::string(reinterpret_cast<const char*>(value.data()), value.size());
}

[[noreturn]] void handleTerminate() noexcept {
    auto& logger = logging::Logger::instance();
    const auto exception = std::current_exception();
    if (exception != nullptr) {
        try {
            std::rethrow_exception(exception);
        } catch (const std::exception& error) {
            logger.log(logging::Level::Fatal, "application", "terminate", error.what());
        } catch (...) {
            logger.log(
                logging::Level::Fatal,
                "application",
                "terminate",
                "Application terminated because of an unknown exception.");
        }
    } else {
        logger.log(
            logging::Level::Fatal,
            "application",
            "terminate",
            "Application terminated without an active exception.");
    }
    std::abort();
}

} // namespace

int main(int argc, char* argv[]) {
    auto& logger = logging::Logger::instance();
    logger.initialize_default();
    std::set_terminate(handleTerminate);
    logger.log(
        logging::Level::Info,
        "application",
        "startup",
        "Main Editor started.",
        {{"version", "Beta 0.1.0"}, {"log_path", pathToUtf8(logger.log_path())}});

    try {
        QApplication application(argc, argv);
        QApplication::setApplicationName("Main Editor");
        QApplication::setApplicationVersion("Beta 0.1.0");

        MainWindow window;
        window.show();

        const int exit_code = application.exec();
        logger.log(
            logging::Level::Info,
            "application",
            "shutdown",
            "Main Editor stopped.",
            {{"exit_code", std::to_string(exit_code)}});
        return exit_code;
    } catch (const std::exception& error) {
        logger.log(logging::Level::Fatal, "application", "main", error.what());
        return 1;
    } catch (...) {
        logger.log(
            logging::Level::Fatal,
            "application",
            "main",
            "Main Editor failed because of an unknown exception.");
        return 1;
    }
}
