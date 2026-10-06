#include "hub_logger.h"

#include <QFile>
#include <QTextStream>
#include <QStandardPaths>
#include <QDir>

namespace creative_suite::hub {

HubLogger& HubLogger::instance() {
    static HubLogger s_instance;
    return s_instance;
}

HubLogger::HubLogger() {
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir dir(appData);
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }
    m_logFilePath = dir.filePath(QStringLiteral("creative-suite-hub.log"));
}

void HubLogger::setLogFilePath(const QString& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_logFilePath = path;
}

QString HubLogger::logFilePath() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_logFilePath;
}

void HubLogger::log(LogLevel level,
                    const QString& subsystem,
                    const QString& operation,
                    const QString& message,
                    const QString& context)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    LogEntry entry;
    entry.timestamp = QDateTime::currentDateTimeUtc();
    entry.level = level;
    entry.subsystem = subsystem;
    entry.operation = operation;
    entry.message = message;
    entry.context = context;

    if (m_entries.size() >= kMaxInMemoryEntries) {
        m_entries.erase(m_entries.begin());
    }
    m_entries.push_back(entry);

    if (!m_logFilePath.isEmpty()) {
        QFile file(m_logFilePath);
        if (file.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream out(&file);
            QString levelStr;
            switch (level) {
                case LogLevel::Info: levelStr = QStringLiteral("INFO"); break;
                case LogLevel::Warning: levelStr = QStringLiteral("WARN"); break;
                case LogLevel::Error: levelStr = QStringLiteral("ERROR"); break;
            }
            out << entry.timestamp.toString(Qt::ISODate) << " [" << levelStr << "] ["
                << subsystem << "::" << operation << "] "
                << message;
            if (!context.isEmpty()) {
                out << " | context: " << context;
            }
            out << "\n";
        }
    }
}

void HubLogger::logInfo(const QString& subsystem, const QString& operation, const QString& message, const QString& context) {
    log(LogLevel::Info, subsystem, operation, message, context);
}

void HubLogger::logWarning(const QString& subsystem, const QString& operation, const QString& message, const QString& context) {
    log(LogLevel::Warning, subsystem, operation, message, context);
}

void HubLogger::logError(const QString& subsystem, const QString& operation, const QString& message, const QString& context) {
    log(LogLevel::Error, subsystem, operation, message, context);
}

std::vector<LogEntry> HubLogger::entries() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_entries;
}

void HubLogger::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_entries.clear();
}

} // namespace creative_suite::hub
