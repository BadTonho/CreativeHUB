#include "startup_integration.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

namespace creative_suite::hub::startup_integration {
namespace {

#if defined(Q_OS_WIN)
const QString kRunKey = QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run");
const QString kRunValue = QStringLiteral("CreativeSuiteHub");
#endif

QString xmlEscape(const QString& text) {
    QString escaped;
    for (const QChar ch : text) {
        if (ch == QLatin1Char('&')) escaped += QStringLiteral("&amp;");
        else if (ch == QLatin1Char('<')) escaped += QStringLiteral("&lt;");
        else if (ch == QLatin1Char('>')) escaped += QStringLiteral("&gt;");
        else escaped += ch;
    }
    return escaped;
}

// Desktop Entry spec: inside a quoted Exec argument, `"`, `` ` ``, `$` and `\` are backslash-escaped,
// and the string-value layer doubles every backslash again; `%` is doubled to avoid field codes.
QString quoteExecArgument(const QString& argument) {
    QString quoted = QStringLiteral("\"");
    for (const QChar ch : argument) {
        if (ch == QLatin1Char('\\')) quoted += QStringLiteral("\\\\\\\\");
        else if (ch == QLatin1Char('"') || ch == QLatin1Char('`') || ch == QLatin1Char('$'))
            quoted += QStringLiteral("\\\\") + ch;
        else if (ch == QLatin1Char('%')) quoted += QStringLiteral("%%");
        else quoted += ch;
    }
    return quoted + QLatin1Char('"');
}

} // namespace

QString desktopEntryContents(const QString& executablePath) {
    return QStringLiteral(
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=Creative Suite Hub\n"
        "Exec=%1\n"
        "Terminal=false\n"
        "X-GNOME-Autostart-enabled=true\n").arg(quoteExecArgument(executablePath));
}

QString launchAgentContents(const QString& executablePath) {
    return QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
        "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\">\n"
        "<dict>\n"
        "    <key>Label</key>\n"
        "    <string>creative-suite.hub</string>\n"
        "    <key>ProgramArguments</key>\n"
        "    <array>\n"
        "        <string>%1</string>\n"
        "    </array>\n"
        "    <key>RunAtLoad</key>\n"
        "    <true/>\n"
        "</dict>\n"
        "</plist>\n").arg(xmlEscape(executablePath));
}

bool writeOrRemoveFile(const QString& filePath, const QString& contents, bool enabled, QString* error) {
    const auto fail = [error](const QString& cause) {
        if (error) *error = cause;
        return false;
    };
    if (filePath.isEmpty()) return fail(QStringLiteral("No startup file location is available."));
    if (!enabled) {
        if (QFileInfo::exists(filePath) && !QFile::remove(filePath))
            return fail(QStringLiteral("Could not remove %1.").arg(filePath));
        return true;
    }
    if (!QDir().mkpath(QFileInfo(filePath).absolutePath()))
        return fail(QStringLiteral("Could not create the folder for %1.").arg(filePath));
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly))
        return fail(QStringLiteral("Could not open %1: %2").arg(filePath, file.errorString()));
    const QByteArray bytes = contents.toUtf8();
    if (file.write(bytes) != bytes.size() || !file.commit())
        return fail(QStringLiteral("Could not write %1: %2").arg(filePath, file.errorString()));
    return true;
}

QString integrationFilePath() {
#if defined(Q_OS_WIN)
    return {};
#elif defined(Q_OS_MACOS)
    return QDir::home().filePath(QStringLiteral("Library/LaunchAgents/creative-suite.hub.plist"));
#else
    const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (config.isEmpty()) return {};
    return QDir(config).filePath(QStringLiteral("autostart/creative-suite-hub.desktop"));
#endif
}

bool isSupported() { return true; }

bool isEnabled() {
#if defined(Q_OS_WIN)
    return QSettings(kRunKey, QSettings::NativeFormat).contains(kRunValue);
#else
    const QString path = integrationFilePath();
    return !path.isEmpty() && QFileInfo::exists(path);
#endif
}

bool setEnabled(bool enabled, const QString& executablePath, QString* error) {
#if defined(Q_OS_WIN)
    QSettings settings(kRunKey, QSettings::NativeFormat);
    if (enabled) {
        settings.setValue(kRunValue,
            QLatin1Char('"') + QDir::toNativeSeparators(executablePath) + QLatin1Char('"'));
    } else {
        settings.remove(kRunValue);
    }
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        if (error) *error = QStringLiteral("Could not update the current-user startup entry.");
        return false;
    }
    return true;
#elif defined(Q_OS_MACOS)
    return writeOrRemoveFile(integrationFilePath(), launchAgentContents(executablePath), enabled, error);
#else
    return writeOrRemoveFile(integrationFilePath(), desktopEntryContents(executablePath), enabled, error);
#endif
}

} // namespace creative_suite::hub::startup_integration
