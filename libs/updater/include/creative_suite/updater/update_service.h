#pragma once

#include <creative_suite/updater/release_catalog.h>

#include <QFile>
#include <QLockFile>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <memory>
#include <functional>

class QNetworkReply;
class QTimer;
class QToolButton;
class QMainWindow;

namespace creative_suite::updater {

enum class UpdateState {
    Idle,
    Checking,
    UpdateAvailable,
    UpToDate,
    Downloading,
    ReadyToInstall,
    WaitingForApplicationToClose,
    InstallerStarted,
    Failed,
};

struct UpdateServiceConfig final {
    QString app_id;
    QString app_name;
    QString executable_name;
    QString current_version;
    QString install_path;
    bool installed{false};
    QUrl catalog_url;
    QUrl release_asset_base_url;
    QString download_directory;
};

struct InstalledApplication final {
    QString app_id;
    QString version;
    QString install_path;
    QString executable_name;
    QString rollback_path;
    bool rollback_available{false};
    bool needs_launch_check{false};
};

[[nodiscard]] UpdateServiceConfig defaultConfig(
    QString app_id,
    QString app_name,
    QString executable_name,
    QString current_version);
[[nodiscard]] QString sharedUpdateDirectory();
[[nodiscard]] std::optional<InstalledApplication> readInstalledApplication(
    const QString& app_id);
void markApplicationStartupHealthy(const QString& app_id);
[[nodiscard]] bool restorePreviousVersion(
    const QString& app_id,
    QString* error = nullptr);

class UpdateRuntime final : public QObject {
    Q_OBJECT

public:
    explicit UpdateRuntime(QString application_id, QObject* parent = nullptr);
    ~UpdateRuntime() override;

    [[nodiscard]] bool isApplicationRunning(
        const QString& app_id,
        const QString& install_path,
        const QString& executable_name) const;
    [[nodiscard]] bool installOrWait(
        const QString& target_app_id,
        const QString& install_path,
        const QString& executable_name,
        const QString& installer_path,
        const QByteArray& expected_sha256,
        bool installed);
    void resumePendingInstalls(const QStringList& managed_app_ids = {});

signals:
    void installationStateChanged(
        const QString& app_id,
        creative_suite::updater::UpdateState state);
    void installationFailed(const QString& app_id, const QString& message);

private:
    struct PendingInstall final {
        QString app_id;
        QString install_path;
        QString executable_name;
        QString installer_path;
        QByteArray sha256_hex;
        bool installed{false};
    };
    [[nodiscard]] QString lockPath(const QString& app_id) const;
    [[nodiscard]] QString pendingPath(const QString& app_id) const;
    [[nodiscard]] bool writePending(const PendingInstall& pending) const;
    [[nodiscard]] std::optional<PendingInstall> readPending(
        const QString& app_id) const;
    [[nodiscard]] bool launchInstaller(const PendingInstall& pending) const;
    void onApplicationAboutToQuit();
    void pollPendingInstall(const QString& app_id);

    QString m_application_id;
    std::unique_ptr<QLockFile> m_application_lock;
    QPointer<QTimer> m_pending_timer;
    QString m_polling_app_id;
};

class UpdateService final : public QObject {
    Q_OBJECT

public:
    explicit UpdateService(UpdateServiceConfig config, QObject* parent = nullptr);
    // Injected request transport keeps download, cancellation, and resume tests deterministic.
    UpdateService(UpdateServiceConfig config,
                  std::function<QNetworkReply*(const QNetworkRequest&)> network_get,
                  QObject* parent = nullptr);
    ~UpdateService() override;

    [[nodiscard]] const UpdateServiceConfig& config() const noexcept { return m_config; }
    [[nodiscard]] UpdateState state() const noexcept { return m_state; }
    [[nodiscard]] const std::optional<ReleaseEntry>& release() const noexcept { return m_release; }
    [[nodiscard]] QString downloadedInstallerPath() const { return m_downloaded_installer_path; }

    void checkForUpdates();
    void downloadUpdate();
    void cancelDownload();

signals:
    void stateChanged(creative_suite::updater::UpdateState state);
    void updateAvailable(const creative_suite::updater::ReleaseEntry& release);
    void downloadProgress(qint64 received_bytes, qint64 total_bytes);
    void downloadReady(const QString& installer_path,
                       const creative_suite::updater::ReleaseEntry& release);
    void operationFailed(const QString& message);

private:
    void setState(UpdateState state);
    void fail(QString operation, QString message, QString context = {});
    void prepareDownloadFile();
    void receiveDownloadData();
    void finishDownload();
    [[nodiscard]] bool verifyFile(const QString& path) const;
    void releaseDownloadLock();

    UpdateServiceConfig m_config;
    QNetworkAccessManager m_network;
    std::function<QNetworkReply*(const QNetworkRequest&)> m_network_get;
    QPointer<QNetworkReply> m_reply;
    std::optional<ReleaseEntry> m_release;
    UpdateState m_state{UpdateState::Idle};
    std::unique_ptr<QLockFile> m_download_lock;
    std::unique_ptr<QFile> m_download_file;
    QString m_partial_path;
    QString m_downloaded_installer_path;
    qint64 m_resume_offset{0};
    bool m_response_prepared{false};
    bool m_cancel_requested{false};
};

class UpdateCenter final : public QObject {
    Q_OBJECT

public:
    UpdateCenter(QMainWindow* window,
                 UpdateServiceConfig config,
                 QObject* parent = nullptr);

    [[nodiscard]] UpdateService* service() const noexcept { return m_service; }
    [[nodiscard]] UpdateRuntime* runtime() const noexcept { return m_runtime; }

private:
    void openUpdateDialog();

    QMainWindow* m_window{nullptr};
    UpdateServiceConfig m_config;
    UpdateRuntime* m_runtime{nullptr};
    UpdateService* m_service{nullptr};
    QToolButton* m_status_button{nullptr};
};

} // namespace creative_suite::updater

Q_DECLARE_METATYPE(creative_suite::updater::UpdateState)
Q_DECLARE_METATYPE(creative_suite::updater::ReleaseEntry)
