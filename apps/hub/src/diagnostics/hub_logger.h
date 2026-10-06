#pragma once

#include <QString>
#include <QDateTime>
#include <vector>
#include <mutex>

namespace creative_suite::hub {

enum class LogLevel {
    Info,
    Warning,
    Error
};

struct LogEntry {
    QDateTime timestamp;
    LogLevel level;
    QString subsystem;
    QString operation;
    QString message;
    QString context;
};

class HubLogger {
public:
    static HubLogger& instance();

    void log(LogLevel level,
             const QString& subsystem,
             const QString& operation,
             const QString& message,
             const QString& context = QString());

    void logInfo(const QString& subsystem, const QString& operation, const QString& message, const QString& context = QString());
    void logWarning(const QString& subsystem, const QString& operation, const QString& message, const QString& context = QString());
    void logError(const QString& subsystem, const QString& operation, const QString& message, const QString& context = QString());

    [[nodiscard]] std::vector<LogEntry> entries() const;
    void clear();

    void setLogFilePath(const QString& path);
    [[nodiscard]] QString logFilePath() const;

private:
    HubLogger();
    ~HubLogger() = default;

    HubLogger(const HubLogger&) = delete;
    HubLogger& operator=(const HubLogger&) = delete;

    mutable std::mutex m_mutex;
    std::vector<LogEntry> m_entries;
    QString m_logFilePath;
    static constexpr size_t kMaxInMemoryEntries = 1000;
};

} // namespace creative_suite::hub
