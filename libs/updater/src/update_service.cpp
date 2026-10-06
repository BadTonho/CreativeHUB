#include <creative_suite/updater/update_service.h>

#include <creative_suite/diagnostics/logger.h>
#include <creative_suite/updater/update_dialog.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QNetworkReply>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>
#include <QToolButton>
#include <QUrlQuery>
#include <QWidget>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace creative_suite::updater {
namespace {

constexpr auto kGitHubReleaseDirectory =
    "https://github.com/BadTonho/AdobeShoppee/releases/latest/download/";
constexpr auto kCatalogAsset = "updates.json";
constexpr auto kInstallRegistryRoot =
    "HKEY_CURRENT_USER\\Software\\Tonho Studios\\Creative Suite\\Installations\\";

void logMessage(creative_suite::diagnostics::Level level,
                const QString& operation,
                const QString& message,
                const QString& context = {}) {
    auto& logger = creative_suite::diagnostics::Logger::instance();
    if (!logger.is_initialized()) {
        static_cast<void>(logger.initialize_default("creative-suite-updater"));
    }
    creative_suite::diagnostics::Context fields;
    if (!context.isEmpty()) {
        fields.emplace_back("context", context.toStdString());
    }
    logger.log(level, "updater", operation.toStdString(), message.toStdString(), fields);
}

QString safeAppId(const QString& app_id) {
    static const QRegularExpression safe_pattern(QStringLiteral("^[a-z0-9-]+$"));
    return safe_pattern.match(app_id).hasMatch() ? app_id : QString{};
}

QString appLockPath(const QString& app_id) {
    if (safeAppId(app_id).isEmpty()) return {};
    return QDir(sharedUpdateDirectory()).filePath(app_id + QStringLiteral(".running.lock"));
}

QString appPendingPath(const QString& app_id) {
    if (safeAppId(app_id).isEmpty()) return {};
    const auto pending_dir = QDir(sharedUpdateDirectory()).filePath(QStringLiteral("pending"));
    return QDir(pending_dir).filePath(app_id + QStringLiteral(".json"));
}

QByteArray hashFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const auto bytes = file.read(1024 * 1024);
        if (bytes.isEmpty() && file.error() != QFileDevice::NoError) return {};
        hash.addData(bytes);
    }
    return hash.result().toHex();
}

bool isWithinDirectory(const QString& file_path, const QString& root_path) {
    const QDir root(QDir::cleanPath(QFileInfo(root_path).absoluteFilePath()));
    const auto relative = root.relativeFilePath(QDir::cleanPath(QFileInfo(file_path).absoluteFilePath()));
    return relative != QStringLiteral("..") &&
        !relative.startsWith(QStringLiteral("../")) &&
        !QDir::isAbsolutePath(relative);
}

bool copyDirectoryContents(const QString& source_path, const QString& destination_path) {
    QDir source(source_path);
    if (!source.exists() || !QDir().mkpath(destination_path)) return false;
    const auto entries = source.entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
        QDir::Name);
    for (const auto& entry : entries) {
        if (entry.isSymLink()) return false;
        const auto destination = QDir(destination_path).filePath(entry.fileName());
        if (entry.isDir()) {
            if (!copyDirectoryContents(entry.absoluteFilePath(), destination)) return false;
        } else {
            QFile::remove(destination);
            if (!QFile::copy(entry.absoluteFilePath(), destination)) return false;
        }
    }
    return true;
}

#ifdef Q_OS_WIN
bool processUsesExecutable(const QString& install_path, const QString& executable_name) {
    if (install_path.isEmpty() || executable_name.isEmpty()) return false;
    const auto expected = QDir::toNativeSeparators(
        QDir::cleanPath(QDir(install_path).filePath(executable_name)));
    HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32W process{};
    process.dwSize = sizeof(process);
    bool running = false;
    if (::Process32FirstW(snapshot, &process)) {
        do {
            HANDLE handle = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process.th32ProcessID);
            if (!handle) continue;
            wchar_t buffer[32768]{};
            DWORD length = static_cast<DWORD>(std::size(buffer));
            const bool queried = ::QueryFullProcessImageNameW(handle, 0, buffer, &length) != FALSE;
            ::CloseHandle(handle);
            if (!queried) continue;
            const auto actual = QDir::toNativeSeparators(
                QDir::cleanPath(QString::fromWCharArray(buffer, static_cast<qsizetype>(length))));
            if (QString::compare(actual, expected, Qt::CaseInsensitive) == 0) {
                running = true;
                break;
            }
        } while (::Process32NextW(snapshot, &process));
    }
    ::CloseHandle(snapshot);
    return running;
}
#endif

} // namespace

QString sharedUpdateDirectory() {
    auto root = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
    if (root.isEmpty()) root = QDir::tempPath();
    return QDir(root).filePath(QStringLiteral("Tonho Studios/Creative Suite/Updater"));
}

UpdateServiceConfig defaultConfig(
    QString app_id,
    QString app_name,
    QString executable_name,
    QString current_version) {
    const QUrl release_directory(QString::fromLatin1(kGitHubReleaseDirectory));
    UpdateServiceConfig config;
    config.app_id = std::move(app_id);
    config.app_name = std::move(app_name);
    config.executable_name = std::move(executable_name);
    config.current_version = std::move(current_version);
    config.catalog_url = resolveInstallerUrl(release_directory, QString::fromLatin1(kCatalogAsset));
    config.release_asset_base_url = release_directory;
    config.download_directory = sharedUpdateDirectory();
    const auto installed = readInstalledApplication(config.app_id);
    if (installed) {
        config.installed = true;
        config.install_path = installed->install_path;
        if (!installed->version.isEmpty()) config.current_version = installed->version;
        if (!installed->executable_name.isEmpty()) config.executable_name = installed->executable_name;
    }
    return config;
}

std::optional<InstalledApplication> readInstalledApplication(const QString& app_id) {
#ifdef Q_OS_WIN
    if (safeAppId(app_id).isEmpty()) return std::nullopt;
    const auto key = QString::fromLatin1(kInstallRegistryRoot) + app_id;
    QSettings settings(key, QSettings::NativeFormat);
    const auto install_path = settings.value(QStringLiteral("InstallPath")).toString();
    const auto version = settings.value(QStringLiteral("Version")).toString();
    const auto executable_name = settings.value(QStringLiteral("Executable")).toString();
    if (install_path.isEmpty() || !QFileInfo(install_path).isDir() || version.isEmpty()) {
        return std::nullopt;
    }
    const auto rollback_path = settings.value(QStringLiteral("RollbackPath")).toString();
    const bool rollback_available = settings.value(QStringLiteral("RollbackAvailable")).toBool();
    const bool needs_launch_check = settings.value(QStringLiteral("NeedsLaunchCheck")).toBool();
    return InstalledApplication{app_id, version, install_path, executable_name,
                                rollback_path, rollback_available, needs_launch_check};
#else
    static_cast<void>(app_id);
    return std::nullopt;
#endif
}

void markApplicationStartupHealthy(const QString& app_id) {
#ifdef Q_OS_WIN
    if (safeAppId(app_id).isEmpty()) return;
    QSettings settings(QString::fromLatin1(kInstallRegistryRoot) + app_id,
                       QSettings::NativeFormat);
    if (!settings.value(QStringLiteral("NeedsLaunchCheck")).toBool()) return;
    settings.setValue(QStringLiteral("NeedsLaunchCheck"), false);
    settings.setValue(QStringLiteral("UpdateState"), QStringLiteral("healthy"));
#else
    static_cast<void>(app_id);
#endif
}

bool restorePreviousVersion(const QString& app_id, QString* error) {
#ifdef Q_OS_WIN
    if (safeAppId(app_id).isEmpty()) {
        if (error) *error = QStringLiteral("The application identifier is invalid.");
        return false;
    }
    QSettings settings(QString::fromLatin1(kInstallRegistryRoot) + app_id,
                       QSettings::NativeFormat);
    const auto install_path = settings.value(QStringLiteral("InstallPath")).toString();
    const auto executable_name = settings.value(QStringLiteral("Executable")).toString();
    const auto rollback_path = settings.value(QStringLiteral("RollbackPath")).toString();
    const auto rollback_root = QDir(sharedUpdateDirectory()).filePath(QStringLiteral("rollback"));
    if (!settings.value(QStringLiteral("RollbackAvailable")).toBool() ||
        install_path.isEmpty() || rollback_path.isEmpty() ||
        !isWithinDirectory(rollback_path, rollback_root) ||
        !QFileInfo(rollback_path).isDir()) {
        if (error) *error = QStringLiteral("No valid previous application version is available.");
        return false;
    }
    QLockFile running_lock(appLockPath(app_id));
    if (!running_lock.tryLock(0) || processUsesExecutable(install_path, executable_name)) {
        if (error) *error = QStringLiteral("Close the application before restoring its previous version.");
        return false;
    }
    if (!copyDirectoryContents(rollback_path, install_path)) {
        logMessage(creative_suite::diagnostics::Level::Error,
                   QStringLiteral("restore_previous_version"),
                   QStringLiteral("Could not restore all files from the previous version."), app_id);
        if (error) *error = QStringLiteral("Some files could not be restored. The backup was kept for another attempt.");
        return false;
    }
    const auto previous_version = settings.value(QStringLiteral("RollbackVersion")).toString();
    if (!previous_version.isEmpty()) settings.setValue(QStringLiteral("Version"), previous_version);
    settings.setValue(QStringLiteral("RollbackAvailable"), false);
    settings.setValue(QStringLiteral("NeedsLaunchCheck"), false);
    settings.setValue(QStringLiteral("UpdateState"), QStringLiteral("restored"));
    logMessage(creative_suite::diagnostics::Level::Info,
               QStringLiteral("restore_previous_version"),
               QStringLiteral("Restored the previous application files."), app_id);
    return true;
#else
    static_cast<void>(app_id);
    if (error) *error = QStringLiteral("Application restoration is currently available only on Windows.");
    return false;
#endif
}

UpdateRuntime::UpdateRuntime(QString application_id, QObject* parent)
    : QObject(parent)
    , m_application_id(std::move(application_id)) {
    QDir().mkpath(sharedUpdateDirectory());
    const auto path = appLockPath(m_application_id);
    if (!path.isEmpty()) {
        m_application_lock = std::make_unique<QLockFile>(path);
        if (!m_application_lock->tryLock(0)) {
            logMessage(creative_suite::diagnostics::Level::Warning,
                       QStringLiteral("acquire_application_lock"),
                       QStringLiteral("Another process already holds this application's update lease."),
                       m_application_id);
        }
    }
    if (QCoreApplication::instance()) {
        connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit,
                this, &UpdateRuntime::onApplicationAboutToQuit);
    }
}

UpdateRuntime::~UpdateRuntime() = default;

QString UpdateRuntime::lockPath(const QString& app_id) const {
    return appLockPath(app_id);
}

QString UpdateRuntime::pendingPath(const QString& app_id) const {
    return appPendingPath(app_id);
}

bool UpdateRuntime::isApplicationRunning(
    const QString& app_id,
    const QString& install_path,
    const QString& executable_name) const {
    const auto path = lockPath(app_id);
    if (!path.isEmpty()) {
        QLockFile probe(path);
        if (!probe.tryLock(0)) return true;
        probe.unlock();
    }
#ifdef Q_OS_WIN
    return processUsesExecutable(install_path, executable_name);
#else
    static_cast<void>(install_path);
    static_cast<void>(executable_name);
    return false;
#endif
}

bool UpdateRuntime::writePending(const PendingInstall& pending) const {
    const auto path = pendingPath(pending.app_id);
    if (path.isEmpty() || !isWithinDirectory(pending.installer_path, sharedUpdateDirectory())) {
        return false;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QJsonObject object;
    object.insert(QStringLiteral("schema_version"), 1);
    object.insert(QStringLiteral("app_id"), pending.app_id);
    object.insert(QStringLiteral("install_path"), pending.install_path);
    object.insert(QStringLiteral("executable_name"), pending.executable_name);
    object.insert(QStringLiteral("installer_path"), pending.installer_path);
    object.insert(QStringLiteral("sha256"), QString::fromLatin1(pending.sha256_hex));
    object.insert(QStringLiteral("installed"), pending.installed);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    if (file.write(QJsonDocument(object).toJson(QJsonDocument::Compact)) < 0) return false;
    return file.commit();
}

std::optional<UpdateRuntime::PendingInstall> UpdateRuntime::readPending(
    const QString& app_id) const {
    const auto path = pendingPath(app_id);
    if (path.isEmpty()) return std::nullopt;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return std::nullopt;
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return std::nullopt;
    const auto object = document.object();
    if (object.value(QStringLiteral("schema_version")).toInt() != 1 ||
        object.value(QStringLiteral("app_id")).toString() != app_id) {
        return std::nullopt;
    }
    PendingInstall pending;
    pending.app_id = app_id;
    pending.install_path = object.value(QStringLiteral("install_path")).toString();
    pending.executable_name = object.value(QStringLiteral("executable_name")).toString();
    pending.installer_path = object.value(QStringLiteral("installer_path")).toString();
    pending.sha256_hex = object.value(QStringLiteral("sha256")).toString().toLatin1().toLower();
    pending.installed = object.value(QStringLiteral("installed")).toBool();
    if (safeAppId(app_id).isEmpty() || pending.installer_path.isEmpty() ||
        !isWithinDirectory(pending.installer_path, sharedUpdateDirectory()) ||
        pending.sha256_hex.size() != 64 ||
        hashFile(pending.installer_path) != pending.sha256_hex ||
        (pending.installed && pending.install_path.isEmpty())) {
        logMessage(creative_suite::diagnostics::Level::Error,
                   QStringLiteral("read_pending_install"),
                   QStringLiteral("Pending update data failed validation."), app_id);
        return std::nullopt;
    }
    return pending;
}

bool UpdateRuntime::launchInstaller(const PendingInstall& pending) const {
    const auto pending_path = pendingPath(pending.app_id);
    if (pending_path.isEmpty() || !QFileInfo::exists(pending_path)) return true;

    QLockFile launch_lock(QDir(sharedUpdateDirectory()).filePath(
        pending.app_id + QStringLiteral(".installer-launch.lock")));
    if (!launch_lock.tryLock(0)) return true;

    const auto persisted = readPending(pending.app_id);
    if (!persisted) return false;
    if (persisted->installer_path != pending.installer_path ||
        persisted->sha256_hex != pending.sha256_hex ||
        persisted->install_path != pending.install_path) {
        logMessage(creative_suite::diagnostics::Level::Error,
                   QStringLiteral("launch_installer"),
                   QStringLiteral("The pending installer changed before it could be started."),
                   pending.app_id);
        return false;
    }
    if (!QFileInfo(persisted->installer_path).isFile() ||
        !isWithinDirectory(persisted->installer_path, sharedUpdateDirectory()) ||
        hashFile(persisted->installer_path) != persisted->sha256_hex) {
        logMessage(creative_suite::diagnostics::Level::Error,
                   QStringLiteral("launch_installer"),
                   QStringLiteral("The staged installer failed validation before launch."),
                   pending.app_id);
        return false;
    }
    QStringList arguments;
    if (persisted->installed) {
        arguments << QStringLiteral("/SILENT")
                  << QStringLiteral("/NORESTART")
                  << QStringLiteral("/DIR=\"%1\"").arg(QDir::toNativeSeparators(persisted->install_path));
    }
    const bool started = QProcess::startDetached(
        persisted->installer_path, arguments, sharedUpdateDirectory());
    if (started) {
        QFile::remove(pending_path);
        logMessage(creative_suite::diagnostics::Level::Info,
                   QStringLiteral("launch_installer"),
                   QStringLiteral("Started the full application installer."),
                   pending.app_id);
    }
    return started;
}

bool UpdateRuntime::installOrWait(
    const QString& target_app_id,
    const QString& install_path,
    const QString& executable_name,
    const QString& installer_path,
    const QByteArray& expected_sha256,
    bool installed) {
    PendingInstall pending{target_app_id, install_path, executable_name,
                           installer_path, expected_sha256.toLower(), installed};
    if (safeAppId(target_app_id).isEmpty() ||
        !isWithinDirectory(installer_path, sharedUpdateDirectory()) ||
        hashFile(installer_path) != pending.sha256_hex) {
        emit installationFailed(target_app_id,
                                QStringLiteral("The downloaded installer failed validation."));
        return false;
    }
    if (!writePending(pending)) {
        emit installationFailed(target_app_id,
                                QStringLiteral("Could not save the pending installation."));
        return false;
    }

    if (target_app_id == m_application_id) {
        emit installationStateChanged(target_app_id, UpdateState::WaitingForApplicationToClose);
        return true;
    }
    if (installed && isApplicationRunning(target_app_id, install_path, executable_name)) {
        emit installationStateChanged(target_app_id, UpdateState::WaitingForApplicationToClose);
        m_polling_app_id = target_app_id;
        if (!m_pending_timer) {
            m_pending_timer = new QTimer(this);
            m_pending_timer->setInterval(750);
            connect(m_pending_timer, &QTimer::timeout, this, [this] {
                if (!m_polling_app_id.isEmpty()) pollPendingInstall(m_polling_app_id);
            });
        }
        m_pending_timer->start();
        return true;
    }

    if (!launchInstaller(pending)) {
        logMessage(creative_suite::diagnostics::Level::Error,
                   QStringLiteral("launch_installer"),
                   QStringLiteral("Could not start the downloaded installer."), target_app_id);
        emit installationFailed(target_app_id,
                                QStringLiteral("Could not start the installer. Try again or run it from the download folder."));
        return false;
    }
    emit installationStateChanged(target_app_id, UpdateState::InstallerStarted);
    return true;
}

void UpdateRuntime::resumePendingInstalls(const QStringList& managed_app_ids) {
    QDir directory(QDir(sharedUpdateDirectory()).filePath(QStringLiteral("pending")));
    if (!directory.exists()) return;
    auto entries = managed_app_ids;
    if (entries.isEmpty()) {
        const auto files = directory.entryList({QStringLiteral("*.json")}, QDir::Files);
        for (const auto& file : files) entries.push_back(file.left(file.size() - 5));
    }
    for (const auto& app_id : entries) {
        const auto pending = readPending(app_id);
        if (!pending) continue;
        if (app_id == m_application_id) continue;
        if (pending->installed && isApplicationRunning(
                app_id, pending->install_path, pending->executable_name)) {
            m_polling_app_id = app_id;
            if (!m_pending_timer) {
                m_pending_timer = new QTimer(this);
                m_pending_timer->setInterval(750);
                connect(m_pending_timer, &QTimer::timeout, this, [this] {
                    if (!m_polling_app_id.isEmpty()) pollPendingInstall(m_polling_app_id);
                });
            }
            m_pending_timer->start();
            break;
        }
        if (launchInstaller(*pending)) {
            emit installationStateChanged(app_id, UpdateState::InstallerStarted);
            break;
        }
        logMessage(creative_suite::diagnostics::Level::Error,
                   QStringLiteral("resume_pending_install"),
                   QStringLiteral("Could not resume the pending installer."), app_id);
        emit installationFailed(app_id,
                                QStringLiteral("Could not resume the pending installer."));
    }
}

void UpdateRuntime::onApplicationAboutToQuit() {
    const auto pending = readPending(m_application_id);
    if (!pending) return;
    if (!launchInstaller(*pending)) {
        logMessage(creative_suite::diagnostics::Level::Error,
                   QStringLiteral("launch_self_update"),
                   QStringLiteral("Could not start the installer during application shutdown."),
                   m_application_id);
        emit installationFailed(m_application_id,
                                QStringLiteral("The installer could not be started. The current application remains installed."));
        return;
    }
    emit installationStateChanged(m_application_id, UpdateState::InstallerStarted);
}

void UpdateRuntime::pollPendingInstall(const QString& app_id) {
    const auto pending = readPending(app_id);
    if (!pending) {
        if (m_pending_timer) m_pending_timer->stop();
        m_polling_app_id.clear();
        return;
    }
    if (pending->installed && isApplicationRunning(
            app_id, pending->install_path, pending->executable_name)) {
        return;
    }
    if (!launchInstaller(*pending)) {
        logMessage(creative_suite::diagnostics::Level::Error,
                   QStringLiteral("launch_queued_installer"),
                   QStringLiteral("Could not start the installer after the application closed."), app_id);
        emit installationFailed(app_id,
                                QStringLiteral("The installer could not be started. You can retry from the Hub."));
        if (m_pending_timer) m_pending_timer->stop();
        m_polling_app_id.clear();
        return;
    }
    emit installationStateChanged(app_id, UpdateState::InstallerStarted);
}

UpdateService::UpdateService(UpdateServiceConfig config, QObject* parent)
    : QObject(parent)
    , m_config(std::move(config))
    , m_network_get([this](const QNetworkRequest& request) {
        return m_network.get(request);
    }) {
    qRegisterMetaType<UpdateState>();
    qRegisterMetaType<ReleaseEntry>();
}

UpdateService::UpdateService(UpdateServiceConfig config,
                             std::function<QNetworkReply*(const QNetworkRequest&)> network_get,
                             QObject* parent)
    : QObject(parent)
    , m_config(std::move(config))
    , m_network_get(std::move(network_get)) {
    if (!m_network_get) {
        m_network_get = [this](const QNetworkRequest& request) {
            return m_network.get(request);
        };
    }
    qRegisterMetaType<UpdateState>();
    qRegisterMetaType<ReleaseEntry>();
}

UpdateService::~UpdateService() {
    if (m_reply) m_reply->abort();
    releaseDownloadLock();
}

void UpdateService::setState(UpdateState state) {
    if (m_state == state) return;
    m_state = state;
    emit stateChanged(state);
}

void UpdateService::fail(QString operation, QString message, QString context) {
    logMessage(creative_suite::diagnostics::Level::Error, operation, message, context);
    releaseDownloadLock();
    setState(UpdateState::Failed);
    emit operationFailed(message);
}

void UpdateService::checkForUpdates() {
    if (m_reply) m_reply->abort();
    m_release.reset();
    setState(UpdateState::Checking);

    if (!m_config.catalog_url.isValid() || m_config.catalog_url.scheme() != QStringLiteral("https")) {
        fail(QStringLiteral("check_catalog"), QStringLiteral("The configured update catalog URL is invalid."), m_config.app_id);
        return;
    }
    QNetworkRequest request(m_config.catalog_url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("CreativeSuiteUpdater/1"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(30000);
    auto* reply = m_network_get(request);
    if (!reply) {
        fail(QStringLiteral("check_catalog"),
             QStringLiteral("The network service could not create a catalog request."),
             m_config.app_id);
        return;
    }
    m_reply = reply;
    connect(m_reply, &QNetworkReply::finished, this, [this] {
        auto* reply = m_reply.data();
        if (!reply) return;
        const auto network_error = reply->error();
        const auto network_message = reply->errorString();
        const auto status_code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto bytes = reply->readAll();
        m_reply.clear();
        reply->deleteLater();
        if (network_error != QNetworkReply::NoError || status_code < 200 || status_code >= 300) {
            fail(QStringLiteral("check_catalog"),
                 QStringLiteral("Could not retrieve update information: %1").arg(network_message),
                 m_config.catalog_url.toString());
            return;
        }
        QString parse_error;
        const auto catalog = ReleaseCatalog::fromJson(bytes, &parse_error);
        if (!catalog) {
            fail(QStringLiteral("parse_catalog"), parse_error, m_config.catalog_url.toString());
            return;
        }
        const auto* entry = catalog->find(m_config.app_id);
        if (!entry) {
            fail(QStringLiteral("find_catalog_entry"),
                 QStringLiteral("The update catalog does not contain this application."),
                 m_config.app_id);
            return;
        }
        if (m_config.installed) {
            const auto comparison = compareVersions(entry->version, m_config.current_version);
            if (!comparison) {
                fail(QStringLiteral("compare_versions"),
                     QStringLiteral("The installed or published version is invalid."),
                     m_config.app_id);
                return;
            }
            if (*comparison <= 0) {
                setState(UpdateState::UpToDate);
                return;
            }
        }
        m_release = *entry;
        setState(UpdateState::UpdateAvailable);
        emit updateAvailable(*m_release);
    });
}

void UpdateService::downloadUpdate() {
    if (!m_release) {
        fail(QStringLiteral("download_update"),
             QStringLiteral("No update is available to download."), m_config.app_id);
        return;
    }
    if (m_reply) return;
    QDir().mkpath(m_config.download_directory);
    m_download_lock = std::make_unique<QLockFile>(
        QDir(m_config.download_directory).filePath(QStringLiteral("suite-download.lock")));
    if (!m_download_lock->tryLock(0)) {
        m_download_lock.reset();
        setState(UpdateState::Failed);
        emit operationFailed(QStringLiteral("Another application update is downloading. Try again when it finishes."));
        return;
    }

    const auto stem = m_config.app_id + QLatin1Char('-') + m_release->version;
    m_partial_path = QDir(m_config.download_directory).filePath(stem + QStringLiteral(".part"));
    m_downloaded_installer_path = QDir(m_config.download_directory).filePath(
        stem + QStringLiteral("-setup.exe"));
    if (verifyFile(m_downloaded_installer_path)) {
        releaseDownloadLock();
        setState(UpdateState::ReadyToInstall);
        emit downloadReady(m_downloaded_installer_path, *m_release);
        return;
    }
    QFile::remove(m_downloaded_installer_path);

    QFileInfo partial_info(m_partial_path);
    m_resume_offset = partial_info.exists() ? partial_info.size() : 0;
    if (m_resume_offset >= m_release->size_bytes) {
        QFile::remove(m_partial_path);
        m_resume_offset = 0;
    }
    m_response_prepared = false;
    m_cancel_requested = false;
    m_download_file = std::make_unique<QFile>(m_partial_path);

    const auto url = resolveInstallerUrl(m_config.release_asset_base_url,
                                         m_release->installer_asset);
    if (!url.isValid() || url.scheme() != QStringLiteral("https")) {
        fail(QStringLiteral("download_update"), QStringLiteral("The installer URL is invalid."), url.toString());
        return;
    }
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("CreativeSuiteUpdater/1"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(0);
    if (m_resume_offset > 0) {
        request.setRawHeader("Range", QByteArray("bytes=") + QByteArray::number(m_resume_offset) + '-');
    }

    auto* reply = m_network_get(request);
    if (!reply) {
        fail(QStringLiteral("download_installer"),
             QStringLiteral("The network service could not create an installer request."),
             m_config.app_id);
        return;
    }
    m_reply = reply;
    connect(m_reply, &QNetworkReply::metaDataChanged,
            this, &UpdateService::prepareDownloadFile);
    connect(m_reply, &QIODevice::readyRead,
            this, &UpdateService::receiveDownloadData);
    connect(m_reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) {
                if (total > 0) emit downloadProgress(m_resume_offset + received, m_release->size_bytes);
            });
    connect(m_reply, &QNetworkReply::finished,
            this, &UpdateService::finishDownload);
    setState(UpdateState::Downloading);
    emit downloadProgress(m_resume_offset, m_release->size_bytes);
}

void UpdateService::prepareDownloadFile() {
    if (!m_reply || m_response_prepared) return;
    const auto status_code = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool append = m_resume_offset > 0 && status_code == 206;
    if (m_resume_offset > 0 && status_code != 206) {
        m_resume_offset = 0;
    }
    if (!m_download_file) m_download_file = std::make_unique<QFile>(m_partial_path);
    if (!m_download_file->open(append ? QIODevice::WriteOnly | QIODevice::Append
                                     : QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_reply->abort();
        return;
    }
    m_response_prepared = true;
}

void UpdateService::receiveDownloadData() {
    if (!m_reply) return;
    if (!m_response_prepared) prepareDownloadFile();
    if (!m_download_file || !m_download_file->isOpen()) return;
    const auto bytes = m_reply->readAll();
    if (!bytes.isEmpty() && m_download_file->write(bytes) != bytes.size()) {
        m_reply->abort();
        return;
    }
    emit downloadProgress(m_download_file->size(), m_release ? m_release->size_bytes : 0);
}

void UpdateService::finishDownload() {
    auto* reply = m_reply.data();
    if (!reply) return;
    if (!m_response_prepared) prepareDownloadFile();
    receiveDownloadData();
    const auto network_error = reply->error();
    const auto network_message = reply->errorString();
    const auto status_code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    m_reply.clear();
    reply->deleteLater();
    if (m_download_file && m_download_file->isOpen()) {
        m_download_file->flush();
        m_download_file->close();
    }
    if (m_cancel_requested) {
        m_cancel_requested = false;
        releaseDownloadLock();
        setState(UpdateState::UpdateAvailable);
        return;
    }
    if (network_error != QNetworkReply::NoError || status_code < 200 || status_code >= 300) {
        releaseDownloadLock();
        fail(QStringLiteral("download_installer"),
             QStringLiteral("The installer download did not finish: %1").arg(network_message),
             m_config.app_id);
        return;
    }
    if (!m_release || QFileInfo(m_partial_path).size() != m_release->size_bytes) {
        fail(QStringLiteral("validate_installer_size"),
             QStringLiteral("The installer download is incomplete."), m_partial_path);
        return;
    }
    if (!verifyFile(m_partial_path)) {
        QFile::remove(m_partial_path);
        fail(QStringLiteral("validate_installer_hash"),
             QStringLiteral("The installer did not match the release catalog."), m_config.app_id);
        return;
    }

    QFile::remove(m_downloaded_installer_path);
    if (!QFile::rename(m_partial_path, m_downloaded_installer_path)) {
        fail(QStringLiteral("publish_download"),
             QStringLiteral("Could not prepare the verified installer."), m_downloaded_installer_path);
        return;
    }
    releaseDownloadLock();
    setState(UpdateState::ReadyToInstall);
    emit downloadProgress(m_release->size_bytes, m_release->size_bytes);
    emit downloadReady(m_downloaded_installer_path, *m_release);
}

void UpdateService::cancelDownload() {
    if (!m_reply) return;
    m_cancel_requested = true;
    m_reply->abort();
}

bool UpdateService::verifyFile(const QString& path) const {
    if (!m_release) return false;
    const QFileInfo info(path);
    return info.isFile() && info.size() == m_release->size_bytes &&
        hashFile(path) == m_release->sha256_hex;
}

void UpdateService::releaseDownloadLock() {
    if (!m_download_lock) return;
    if (m_download_lock->isLocked()) m_download_lock->unlock();
    m_download_lock.reset();
}

UpdateCenter::UpdateCenter(QMainWindow* window,
                           UpdateServiceConfig config,
                           QObject* parent)
    : QObject(parent ? parent : window)
    , m_window(window)
    , m_config(std::move(config)) {
    if (!m_window) return;
    m_runtime = new UpdateRuntime(m_config.app_id, this);
    m_service = new UpdateService(m_config, this);
    m_status_button = new QToolButton(m_window);
    m_status_button->setText(QStringLiteral("Update available"));
    m_status_button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_status_button->setAutoRaise(true);
    m_status_button->setVisible(false);
    m_status_button->setObjectName(QStringLiteral("updateCenterStatusButton"));
    m_window->statusBar()->addPermanentWidget(m_status_button);
    connect(m_status_button, &QToolButton::clicked, this, &UpdateCenter::openUpdateDialog);

    if (auto* menu_bar = m_window->menuBar()) {
        QMenu* help_menu = nullptr;
        for (auto* action : menu_bar->actions()) {
            if (action->menu() && action->menu()->title().contains(QStringLiteral("Help"), Qt::CaseInsensitive)) {
                help_menu = action->menu();
                break;
            }
        }
        if (!help_menu) help_menu = menu_bar->addMenu(QStringLiteral("&Help"));
        auto* check_action = help_menu->addAction(QStringLiteral("Check for Updates..."));
        check_action->setObjectName(QStringLiteral("checkForUpdatesAction"));
        connect(check_action, &QAction::triggered, this, [this] {
            if (m_service && m_service->state() == UpdateState::Idle) m_service->checkForUpdates();
            openUpdateDialog();
        });
    }
    connect(m_service, &UpdateService::updateAvailable, this,
            [this](const ReleaseEntry& entry) {
                if (!m_status_button) return;
                m_status_button->setText(QStringLiteral("Update available · %1").arg(entry.version));
                m_status_button->setVisible(true);
            });
    connect(m_service, &UpdateService::downloadReady, this,
            [this](const QString& installer, const ReleaseEntry& entry) {
                if (!m_runtime || !m_service) return;
                const bool accepted = m_runtime->installOrWait(
                    entry.app_id,
                    m_config.install_path,
                    m_config.executable_name,
                    installer,
                    entry.sha256_hex,
                    m_config.installed);
                if (accepted && m_status_button) {
                    m_status_button->setText(QStringLiteral("Update ready · %1").arg(entry.version));
                    m_status_button->setVisible(true);
                }
            });

    if (m_config.installed) {
        QTimer::singleShot(0, m_service, &UpdateService::checkForUpdates);
        m_runtime->resumePendingInstalls({m_config.app_id});
    }
}

void UpdateCenter::openUpdateDialog() {
    if (!m_window || !m_service || !m_runtime) return;
    UpdateDialog dialog(m_service, m_runtime, m_window);
    dialog.exec();
}

} // namespace creative_suite::updater
