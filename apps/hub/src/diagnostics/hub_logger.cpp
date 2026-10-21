#include "hub_logger.h"

#include <QFile>
#include <QTextStream>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>

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
        constexpr qint64 max_file_bytes = 2 * 1024 * 1024;
        const QFileInfo current(m_logFilePath);
        if (current.exists() && current.size() >= max_file_bytes) {
            const auto previous_path = m_logFilePath + QStringLiteral(".1");
            QFile::remove(previous_path);
            if (!QFile::rename(m_logFilePath, previous_path)) {
                // Keep the in-memory entry even when the filesystem refuses rotation.
                return;
            }
        }
        QFile file(m_logFilePath);
        if (file.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream out(&file);
            QString levelStr;
            switch (level) {
                case LogLevel::Info: levelStr = QStringLiteral("INFO"); break;
                case LogLevel::Warning: levelStr = QStringLiteral("WARN"); break;
                case LogLevel::Error: levelStr = QStringLiteral("ERROR"); break;
            }
            const auto bounded_message = message.left(32 * 1024);
            const auto bounded_context = context.left(16 * 1024);
            out << entry.timestamp.toString(Qt::ISODate) << " [" << levelStr << "] ["
                << subsystem.left(256) << "::" << operation.left(256) << "] "
                << bounded_message;
            if (!context.isEmpty()) {
                out << " | context: " << bounded_context;
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
