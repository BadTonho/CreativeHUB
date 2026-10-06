#include "main_window/main_window.h"

#include "logging/logger.h"
#ifdef Q_OS_WIN
#include <creative_suite/updater/update_service.h>
#endif

#include <QApplication>
#include <QIcon>
#include <QSurfaceFormat>

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>

namespace {

#ifndef CREATIVE_SUITE_APP_VERSION
#define CREATIVE_SUITE_APP_VERSION "0.1.6"
#endif

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
        "Video Editor started.",
        {{"version", CREATIVE_SUITE_APP_VERSION}, {"log_path", pathToUtf8(logger.log_path())}});

    try {
        QSurfaceFormat format;
        format.setRenderableType(QSurfaceFormat::OpenGL);
        format.setVersion(3, 2);
        format.setProfile(QSurfaceFormat::CoreProfile);
        QSurfaceFormat::setDefaultFormat(format);
        QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
        QApplication application(argc, argv);
        application.setWindowIcon(QIcon(QStringLiteral(":/app-icon/icon.png")));
        QApplication::setApplicationName("Video Editor");
        QApplication::setApplicationVersion(CREATIVE_SUITE_APP_VERSION);

        MainWindow window;
#ifdef Q_OS_WIN
        creative_suite::updater::UpdateCenter updater(
            &window,
            creative_suite::updater::defaultConfig(
                QStringLiteral("video-editor"), QStringLiteral("Video Editor"),
                QStringLiteral("creative-suite-video-editor.exe"),
                QStringLiteral(CREATIVE_SUITE_APP_VERSION)));
#endif
        window.show();
#ifdef Q_OS_WIN
        creative_suite::updater::markApplicationStartupHealthy(QStringLiteral("video-editor"));
#endif

        const int exit_code = application.exec();
        logger.log(
            logging::Level::Info,
            "application",
            "shutdown",
            "Video Editor stopped.",
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
            "Video Editor failed because of an unknown exception.");
        return 1;
    }
}
