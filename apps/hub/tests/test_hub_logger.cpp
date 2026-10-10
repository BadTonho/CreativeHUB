#include "../src/diagnostics/hub_logger.h"
#include "../../../cmake/test_support/test_check.h"
#include <iostream>
#include <QTemporaryFile>

using namespace creative_suite::hub;

int main() {
    auto& logger = HubLogger::instance();
    logger.clear();

    QTemporaryFile tempLog;
    const bool opened = tempLog.open();
    CS_TEST_CHECK(opened);
    const QString logPath = tempLog.fileName();
    tempLog.close();

    logger.setLogFilePath(logPath);

    logger.logInfo(QStringLiteral("SubsystemA"), QStringLiteral("Op1"), QStringLiteral("Message 1"));
    logger.logError(QStringLiteral("SubsystemB"), QStringLiteral("Op2"), QStringLiteral("Error Message"), QStringLiteral("context_data"));

    auto entries = logger.entries();
    CS_TEST_CHECK(entries.size() == 2);
    CS_TEST_CHECK(entries[0].level == LogLevel::Info);
    CS_TEST_CHECK(entries[0].subsystem == QStringLiteral("SubsystemA"));
    CS_TEST_CHECK(entries[1].level == LogLevel::Error);
    CS_TEST_CHECK(entries[1].context == QStringLiteral("context_data"));

    std::cout << "All HubLogger tests passed successfully.\n";
    return 0;
}
