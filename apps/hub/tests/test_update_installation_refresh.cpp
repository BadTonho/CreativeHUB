#include "../src/ui/main_window.h"
#include "../src/ui/cards/app_card_widget.h"
#include "../src/diagnostics/hub_logger.h"
#include "../src/model/activity_manager.h"
#include "../src/model/backup_manager.h"
#include "../../../cmake/test_support/test_check.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <cstring>
#include <iostream>

using namespace creative_suite::hub;
using namespace creative_suite::updater;

namespace {
class CatalogReply final : public QNetworkReply {
public:
    explicit CatalogReply(const QNetworkRequest& request) {
        setRequest(request);
        setUrl(request.url());
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    }
    void complete(const QByteArray& body) {
        m_body = body;
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
        setFinished(true);
        emit finished();
    }
    void abort() override {
        setError(OperationCanceledError, QStringLiteral("Cancelled by test"));
        setFinished(true);
        emit finished();
    }
    bool isSequential() const override { return true; }
protected:
    qint64 readData(char* data, qint64 maximum) override {
        const auto count = qMin(maximum, m_body.size() - m_offset);
        if (count <= 0) return -1;
        std::memcpy(data, m_body.constData() + m_offset, static_cast<size_t>(count));
        m_offset += count;
        return count;
    }
private:
    QByteArray m_body;
    qint64 m_offset{0};
};

void refresh(MainWindow& window) {
    const bool invoked = QMetaObject::invokeMethod(&window, "onRefreshApps", Qt::DirectConnection);
    CS_TEST_CHECK(invoked);
}

AppInfo displayedApp(MainWindow& window, const QString& id) {
    for (auto* card : window.findChildren<AppCardWidget*>()) {
        if (card->appInfo().id() == id) return card->appInfo();
    }
    CS_TEST_CHECK(false);
    return {};
}
}

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("hub-update-installation-refresh-test"));
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir root;
    CS_TEST_CHECK(root.isValid());
    HubLogger::instance().setLogFilePath(root.filePath("hub.log"));
    ActivityManager::instance().setStoragePath(root.filePath("activities.json"));
    BackupManager::instance().setBackupDirectory(root.filePath("backups"));

    const QByteArray bytes("verified simulated installer");
    const QByteArray hash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
    const QString installer = root.filePath("setup.exe");
    QFile package(installer);
    const bool opened = package.open(QIODevice::WriteOnly);
    CS_TEST_CHECK(opened);
    CS_TEST_CHECK(package.write(bytes) == bytes.size());
    package.close();

    QHash<QString, InstalledApplication> registrations;
    QHash<QString, UpdateService*> services;
    QHash<QString, CatalogReply*> replies;
    QVector<QStringList> launches;
    const QStringList ids{"video-editor", "image-editor", "motion-editor"};
    for (const auto& id : ids) {
        if (id == "image-editor") continue; // First installation during this session.
        registrations[id] = {id, "0.1.0", root.filePath(id + "/old"), id + ".exe"};
    }
    MainWindow::UpdateDependencies dependencies;
    dependencies.read_installation = [&](const QString& id) -> std::optional<InstalledApplication> {
        if (!registrations.contains(id)) return std::nullopt;
        return registrations.value(id);
    };
    dependencies.create_service = [&](UpdateServiceConfig config, QObject* owner) {
        const QString id = config.app_id;
        config.catalog_url = QUrl("https://example.test/catalog.json");
        config.release_asset_base_url = QUrl("https://example.test/releases/");
        config.download_directory = root.filePath("downloads/" + id);
        config.installed = registrations.contains(id);
        config.current_version = registrations.value(id).version;
        config.install_path = registrations.value(id).install_path;
        config.executable_name = registrations.contains(id) ? registrations.value(id).executable_name : id + ".exe";
        auto* service = new UpdateService(config, [&, id](const QNetworkRequest& request) {
            auto* reply = new CatalogReply(request);
            replies[id] = reply;
            return reply;
        }, owner);
        services[id] = service;
        return service;
    };
    dependencies.create_runtime = [&](QObject* owner) {
        return new UpdateRuntime("hub", root.path(),
            [&](const QString& program, const QStringList& arguments, const QString&) {
                CS_TEST_CHECK(program == installer);
                launches.push_back(arguments);
                return true;
            }, owner);
    };

    MainWindow window(std::move(dependencies));
    const auto completeCatalogs = [&](const QString& version) {
        QJsonObject apps;
        for (const auto& id : ids) {
            apps.insert(id, QJsonObject{{"version", version}, {"installer_asset", id + "-setup.exe"},
                {"size_bytes", bytes.size()}, {"sha256", QString::fromLatin1(hash)}});
        }
        const auto catalog = QJsonDocument(QJsonObject{{"schema_version", 1},
            {"suite_version", "2026.10.0"}, {"applications", apps}}).toJson();
        for (const auto& id : ids) replies[id]->complete(catalog);
    };
    completeCatalogs("0.1.1");
    // A genuine first install uses the interactive installer, before registration exists.
    auto* image = services["image-editor"];
    CS_TEST_CHECK(!image->config().installed);
    image->downloadReady(installer, *image->release());
    CS_TEST_CHECK(launches.size() == 1 && launches.back().isEmpty());

    for (const auto& id : ids) {
        registrations[id] = {id, "0.1.1", root.filePath(id + "/new"), id + "-new.exe"};
    }
    refresh(window);
    for (const auto& id : ids) {
        const auto& config = services[id]->config();
        CS_TEST_CHECK(config.installed);
        CS_TEST_CHECK(config.current_version == "0.1.1");
        CS_TEST_CHECK(config.install_path == registrations[id].install_path);
        CS_TEST_CHECK(config.executable_name == registrations[id].executable_name);
        CS_TEST_CHECK(displayedApp(window, id).installedVersion() == "0.1.1");
    }
    completeCatalogs("0.1.1");
    for (const auto& id : ids) CS_TEST_CHECK(services[id]->state() == UpdateState::UpToDate);

    refresh(window);
    completeCatalogs("0.1.2");
    for (const auto& id : ids) {
        auto* service = services[id];
        CS_TEST_CHECK(service->state() == UpdateState::UpdateAvailable);
        service->downloadReady(installer, *service->release());
        CS_TEST_CHECK(launches.back().contains("/SILENT"));
        CS_TEST_CHECK(launches.back().contains(QStringLiteral("/DIR=\"%1\"")
            .arg(QDir::toNativeSeparators(registrations[id].install_path))));
    }
    CS_TEST_CHECK(launches.size() == 4);

    // Registration changes between a download/check and launch must also be observed.
    registrations["video-editor"].version = "0.1.2";
    services["video-editor"]->downloadReady(installer, *services["video-editor"]->release());
    CS_TEST_CHECK(launches.size() == 4); // Already installed; do not launch again.
    registrations["image-editor"].install_path = root.filePath("relocated image");
    services["image-editor"]->downloadReady(installer, *services["image-editor"]->release());
    CS_TEST_CHECK(launches.back().contains(QStringLiteral("/DIR=\"%1\"")
        .arg(QDir::toNativeSeparators(registrations["image-editor"].install_path))));

    registrations.remove("image-editor");
    registrations["motion-editor"].version = "0.1.0"; // Rollback in this session.
    refresh(window);
    CS_TEST_CHECK(!image->config().installed);
    CS_TEST_CHECK(image->config().install_path.isEmpty());
    CS_TEST_CHECK(image->config().current_version.isEmpty());
    CS_TEST_CHECK(image->config().executable_name == "creative-suite-image-editor.exe");
    CS_TEST_CHECK(services["motion-editor"]->config().current_version == "0.1.0");
    CS_TEST_CHECK(displayedApp(window, "image-editor").installedVersion().isEmpty());
    CS_TEST_CHECK(displayedApp(window, "motion-editor").installedVersion() == "0.1.0");
    completeCatalogs("0.1.2");
    image->downloadReady(installer, *image->release());
    CS_TEST_CHECK(launches.back().isEmpty());

    registrations["image-editor"] = {"image-editor", "0.1.3", root.filePath("third image"), "third.exe"};
    image->downloadUpdate();
    CS_TEST_CHECK(image->state() == UpdateState::Downloading);
    auto* activeDownload = replies["image-editor"];
    refresh(window);
    CS_TEST_CHECK(image->state() == UpdateState::Downloading);
    CS_TEST_CHECK(replies["image-editor"] == activeDownload);
    CS_TEST_CHECK(displayedApp(window, "image-editor").status() == AppStatus::Downloading);
    CS_TEST_CHECK(image->config().current_version == "0.1.3");
    image->cancelDownload();

    // Reopening the actual details dialog checks the latest registration/catalog.
    QTimer::singleShot(0, &window, [&] {
        CS_TEST_CHECK(image->state() == UpdateState::Checking);
        CS_TEST_CHECK(image->config().install_path == registrations["image-editor"].install_path);
        const auto dialogs = window.findChildren<QDialog*>();
        CS_TEST_CHECK(!dialogs.isEmpty());
        for (auto* dialog : dialogs) dialog->reject();
    });
    const bool dialogOpened = QMetaObject::invokeMethod(&window, "onDownloadApp",
        Qt::DirectConnection, Q_ARG(QString, QString("image-editor")));
    CS_TEST_CHECK(dialogOpened);
    completeCatalogs("0.1.3");
    CS_TEST_CHECK(image->state() == UpdateState::UpToDate);
    // Invalid live version blocks setup and records the technical cause.
    const auto launchCount = launches.size();
    registrations["motion-editor"].version = "invalid";
    services["motion-editor"]->downloadReady(installer, *services["motion-editor"]->release());
    CS_TEST_CHECK(launches.size() == launchCount);
    QFile log(root.filePath("hub.log"));
    const bool logOpened = log.open(QIODevice::ReadOnly);
    CS_TEST_CHECK(logOpened);
    CS_TEST_CHECK(log.readAll().contains("The registered application version is invalid."));
    std::cout << "Hub installation refresh and installer arguments passed.\n";
}
