#include "../src/diagnostics/hub_logger.h"
#include "../../../cmake/test_support/test_check.h"
#include <iostream>
#include <QTemporaryDir>
#include <QFileInfo>

using namespace creative_suite::hub;

int main() {
    auto& logger = HubLogger::instance();
    logger.clear();

    QTemporaryDir tempLog;
    CS_TEST_CHECK(tempLog.isValid());
    const QString logPath = tempLog.filePath(QStringLiteral("hub.log"));

    logger.setLogFilePath(logPath);

    logger.logInfo(QStringLiteral("SubsystemA"), QStringLiteral("Op1"), QStringLiteral("Message 1"));
    logger.logError(QStringLiteral("SubsystemB"), QStringLiteral("Op2"), QStringLiteral("Error Message"), QStringLiteral("context_data"));

    auto entries = logger.entries();
    CS_TEST_CHECK(entries.size() == 2);
    CS_TEST_CHECK(entries[0].level == LogLevel::Info);
    CS_TEST_CHECK(entries[0].subsystem == QStringLiteral("SubsystemA"));
    CS_TEST_CHECK(entries[1].level == LogLevel::Error);
    CS_TEST_CHECK(entries[1].context == QStringLiteral("context_data"));

    for (int i = 0; i < 70; ++i) {
        logger.logInfo(QStringLiteral("SubsystemA"), QStringLiteral("large"),
                       QString(32 * 1024, QLatin1Char('x')));
    }
    logger.logInfo(QStringLiteral("SubsystemA"), QStringLiteral("rotate"), QStringLiteral("after limit"));
    CS_TEST_CHECK(QFileInfo::exists(logPath + QStringLiteral(".1")));
    CS_TEST_CHECK(QFileInfo(logPath + QStringLiteral(".1")).size() <= 2 * 1024 * 1024 + 64 * 1024);

    std::cout << "All HubLogger tests passed successfully.\n";
    return 0;
}
