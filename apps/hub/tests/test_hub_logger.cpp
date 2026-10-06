#include "../src/diagnostics/hub_logger.h"
#include <cassert>
#include <iostream>
#include <QTemporaryFile>

using namespace creative_suite::hub;

int main() {
    auto& logger = HubLogger::instance();
    logger.clear();

    QTemporaryFile tempLog;
    assert(tempLog.open());
    const QString logPath = tempLog.fileName();
    tempLog.close();

    logger.setLogFilePath(logPath);

    logger.logInfo(QStringLiteral("SubsystemA"), QStringLiteral("Op1"), QStringLiteral("Message 1"));
    logger.logError(QStringLiteral("SubsystemB"), QStringLiteral("Op2"), QStringLiteral("Error Message"), QStringLiteral("context_data"));

    auto entries = logger.entries();
    assert(entries.size() == 2);
    assert(entries[0].level == LogLevel::Info);
    assert(entries[0].subsystem == QStringLiteral("SubsystemA"));
    assert(entries[1].level == LogLevel::Error);
    assert(entries[1].context == QStringLiteral("context_data"));

    std::cout << "All HubLogger tests passed successfully.\n";
    return 0;
}
