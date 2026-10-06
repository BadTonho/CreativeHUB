#include <creative_suite/updater/update_service.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <cstring>

using creative_suite::updater::UpdateService;
using creative_suite::updater::UpdateServiceConfig;
using creative_suite::updater::UpdateState;

namespace {

class ControlledReply final : public QNetworkReply {
public:
    explicit ControlledReply(const QNetworkRequest& request) {
        setRequest(request);
        setUrl(request.url());
        setOperation(QNetworkAccessManager::GetOperation);
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    }

    void beginResponse(int status) {
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        emit metaDataChanged();
    }

    void pushData(const QByteArray& bytes) {
        m_body.append(bytes);
        emit readyRead();
    }

    void finishResponse() {
        setFinished(true);
        emit finished();
    }

    void finishWithError(NetworkError error, const QString& message) {
        setError(error, message);
        setFinished(true);
        emit finished();
    }

    void abort() override {
        setError(OperationCanceledError, QStringLiteral("Cancelled by test."));
        setFinished(true);
        emit finished();
    }

    [[nodiscard]] bool isSequential() const override { return true; }

    [[nodiscard]] qint64 bytesAvailable() const override {
        return m_body.size() - m_offset + QNetworkReply::bytesAvailable();
    }

protected:
    qint64 readData(char* output, qint64 max_size) override {
        const auto available = static_cast<qint64>(m_body.size()) - m_offset;
        if (available <= 0) return -1;
        const auto count = qMin(available, max_size);
        std::memcpy(output, m_body.constData() + m_offset, static_cast<size_t>(count));
        m_offset += count;
        return count;
    }

private:
    QByteArray m_body;
    qint64 m_offset{0};
};

struct NetworkHarness final {
    QVector<ControlledReply*> replies;
    QVector<QNetworkRequest> requests;

    std::function<QNetworkReply*(const QNetworkRequest&)> handler() {
        return [this](const QNetworkRequest& request) -> QNetworkReply* {
            requests.push_back(request);
            auto* reply = new ControlledReply(request);
            replies.push_back(reply);
            return reply;
        };
    }
};

QByteArray makeCatalog(const QByteArray& installer, const QByteArray& digest) {
    QJsonObject entry;
    entry.insert(QStringLiteral("version"), QStringLiteral("0.1.1"));
    entry.insert(QStringLiteral("installer_asset"), QStringLiteral("video-editor-setup.exe"));
    entry.insert(QStringLiteral("size_bytes"), installer.size());
    entry.insert(QStringLiteral("sha256"), QString::fromLatin1(digest));
    entry.insert(QStringLiteral("release_notes"), QStringLiteral("Update test release"));
    QJsonObject applications;
    applications.insert(QStringLiteral("video-editor"), entry);
    QJsonObject root;
    root.insert(QStringLiteral("schema_version"), 1);
    root.insert(QStringLiteral("suite_version"), QStringLiteral("2026.10.0"));
    root.insert(QStringLiteral("applications"), applications);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

UpdateServiceConfig makeConfig(const QString& directory) {
    UpdateServiceConfig config;
    config.app_id = QStringLiteral("video-editor");
    config.app_name = QStringLiteral("Video Editor");
    config.executable_name = QStringLiteral("creative-suite-video-editor.exe");
    config.current_version = QStringLiteral("0.1.0");
    config.installed = true;
    config.install_path = directory;
    config.catalog_url = QUrl(QStringLiteral("https://example.test/updates.json"));
    config.release_asset_base_url = QUrl(QStringLiteral("https://example.test/download/"));
    config.download_directory = directory;
    return config;
}

bool prepareDownload(UpdateService& service,
                     NetworkHarness& network,
                     const QByteArray& catalog) {
    service.checkForUpdates();
    if (network.replies.size() != 1) return false;
    network.replies[0]->beginResponse(200);
    network.replies[0]->pushData(catalog);
    network.replies[0]->finishResponse();
    if (service.state() != UpdateState::UpdateAvailable) return false;
    service.downloadUpdate();
    return network.replies.size() == 2;
}

} // namespace

class UpdateServiceTest final : public QObject {
    Q_OBJECT

private slots:
    void resumesAfterNetworkFailure();
    void cancellationPreservesPartialDownload();
    void incompleteInstallerIsNeverPublished();
    void invalidHashNeverProducesInstaller();
    void catalogNetworkFailureLeavesInstalledVersionUsable();
    void unchangedVersionDoesNotOfferUpdate();
    void onlyOneSuiteDownloadCanRunAtOnce();
};

void UpdateServiceTest::resumesAfterNetworkFailure() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray installer("complete-installer-bytes");
    const auto digest = QCryptographicHash::hash(installer, QCryptographicHash::Sha256).toHex();
    const auto catalog = makeCatalog(installer, digest);
    NetworkHarness network;
    UpdateService service(makeConfig(directory.path()), network.handler());
    QSignalSpy ready_spy(&service, &UpdateService::downloadReady);
    QSignalSpy failed_spy(&service, &UpdateService::operationFailed);

    QVERIFY(prepareDownload(service, network, catalog));
    auto* interrupted = network.replies[1];
    const QByteArray partial("complete-");
    interrupted->beginResponse(200);
    interrupted->pushData(partial);
    interrupted->finishWithError(QNetworkReply::RemoteHostClosedError,
                                 QStringLiteral("Connection closed."));
    QVERIFY(service.state() == UpdateState::Failed);
    QCOMPARE(failed_spy.size(), 1);

    service.downloadUpdate();
    QCOMPARE(network.requests.size(), 3);
    QCOMPARE(network.requests[2].rawHeader("Range"),
             QByteArray("bytes=") + QByteArray::number(partial.size()) + '-');
    network.replies[2]->beginResponse(206);
    network.replies[2]->pushData(installer.mid(partial.size()));
    network.replies[2]->finishResponse();

    QVERIFY(service.state() == UpdateState::ReadyToInstall);
    QCOMPARE(ready_spy.size(), 1);
    QFile verified(service.downloadedInstallerPath());
    QVERIFY(verified.open(QIODevice::ReadOnly));
    QCOMPARE(verified.readAll(), installer);
}

void UpdateServiceTest::cancellationPreservesPartialDownload() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray installer("resumable-installer-data");
    const auto digest = QCryptographicHash::hash(installer, QCryptographicHash::Sha256).toHex();
    NetworkHarness network;
    UpdateService service(makeConfig(directory.path()), network.handler());
    QSignalSpy ready_spy(&service, &UpdateService::downloadReady);
    QVERIFY(prepareDownload(service, network, makeCatalog(installer, digest)));

    const QByteArray partial("resumable-");
    network.replies[1]->beginResponse(200);
    network.replies[1]->pushData(partial);
    service.cancelDownload();
    QVERIFY(service.state() == UpdateState::UpdateAvailable);
    QCOMPARE(QFileInfo(QDir(directory.path()).filePath(
                 QStringLiteral("video-editor-0.1.1.part"))).size(), partial.size());

    service.downloadUpdate();
    QCOMPARE(network.requests[2].rawHeader("Range"),
             QByteArray("bytes=") + QByteArray::number(partial.size()) + '-');
    network.replies[2]->beginResponse(206);
    network.replies[2]->pushData(installer.mid(partial.size()));
    network.replies[2]->finishResponse();
    QVERIFY(service.state() == UpdateState::ReadyToInstall);
    QCOMPARE(ready_spy.size(), 1);
}

void UpdateServiceTest::invalidHashNeverProducesInstaller() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray installer("installer-with-wrong-digest");
    const QByteArray incorrect_digest(64, 'a');
    NetworkHarness network;
    UpdateService service(makeConfig(directory.path()), network.handler());
    QSignalSpy ready_spy(&service, &UpdateService::downloadReady);
    QVERIFY(prepareDownload(service, network,
                           makeCatalog(installer, incorrect_digest)));

    network.replies[1]->beginResponse(200);
    network.replies[1]->pushData(installer);
    network.replies[1]->finishResponse();
    QVERIFY(service.state() == UpdateState::Failed);
    QCOMPARE(ready_spy.size(), 0);
    QVERIFY(!QFileInfo::exists(service.downloadedInstallerPath()));
    QVERIFY(!QFileInfo::exists(QDir(directory.path()).filePath(
        QStringLiteral("video-editor-0.1.1.part"))));
}

void UpdateServiceTest::incompleteInstallerIsNeverPublished() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray installer("complete-installer-bytes");
    const auto digest = QCryptographicHash::hash(installer, QCryptographicHash::Sha256).toHex();
    NetworkHarness network;
    UpdateService service(makeConfig(directory.path()), network.handler());
    QSignalSpy ready_spy(&service, &UpdateService::downloadReady);
    QVERIFY(prepareDownload(service, network, makeCatalog(installer, digest)));

    network.replies[1]->beginResponse(200);
    network.replies[1]->pushData(installer.left(installer.size() - 4));
    network.replies[1]->finishResponse();

    QCOMPARE(service.state(), UpdateState::Failed);
    QCOMPARE(ready_spy.size(), 0);
    QVERIFY(!QFileInfo::exists(service.downloadedInstallerPath()));
    const QFileInfo partial(QDir(directory.path()).filePath(
        QStringLiteral("video-editor-0.1.1.part")));
    QVERIFY(partial.isFile());
    QVERIFY(partial.size() < installer.size());
}

void UpdateServiceTest::catalogNetworkFailureLeavesInstalledVersionUsable() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    NetworkHarness network;
    UpdateService service(makeConfig(directory.path()), network.handler());
    QSignalSpy ready_spy(&service, &UpdateService::downloadReady);
    QSignalSpy failed_spy(&service, &UpdateService::operationFailed);
    service.checkForUpdates();
    QCOMPARE(network.replies.size(), 1);
    network.replies[0]->finishWithError(QNetworkReply::HostNotFoundError,
                                        QStringLiteral("Host not found."));
    QVERIFY(service.state() == UpdateState::Failed);
    QCOMPARE(failed_spy.size(), 1);
    QCOMPARE(ready_spy.size(), 0);
    QVERIFY(QFileInfo(directory.path()).isDir());
}

void UpdateServiceTest::unchangedVersionDoesNotOfferUpdate() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray installer("installer");
    const auto digest = QCryptographicHash::hash(installer, QCryptographicHash::Sha256).toHex();
    NetworkHarness network;
    auto config = makeConfig(directory.path());
    config.current_version = QStringLiteral("0.1.1");
    UpdateService service(std::move(config), network.handler());
    QSignalSpy available_spy(&service, &UpdateService::updateAvailable);
    QVERIFY(!prepareDownload(service, network, makeCatalog(installer, digest)));

    QCOMPARE(service.state(), UpdateState::UpToDate);
    QCOMPARE(available_spy.size(), 0);
    QVERIFY(!service.release());
    QCOMPARE(network.requests.size(), 1);
}

void UpdateServiceTest::onlyOneSuiteDownloadCanRunAtOnce() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray installer("installer");
    const auto digest = QCryptographicHash::hash(installer, QCryptographicHash::Sha256).toHex();
    const auto catalog = makeCatalog(installer, digest);
    NetworkHarness first_network;
    NetworkHarness second_network;
    UpdateService first(makeConfig(directory.path()), first_network.handler());
    UpdateService second(makeConfig(directory.path()), second_network.handler());

    first.checkForUpdates();
    first_network.replies[0]->beginResponse(200);
    first_network.replies[0]->pushData(catalog);
    first_network.replies[0]->finishResponse();
    second.checkForUpdates();
    second_network.replies[0]->beginResponse(200);
    second_network.replies[0]->pushData(catalog);
    second_network.replies[0]->finishResponse();
    QCOMPARE(first.state(), UpdateState::UpdateAvailable);
    QCOMPARE(second.state(), UpdateState::UpdateAvailable);

    first.downloadUpdate();
    QCOMPARE(first.state(), UpdateState::Downloading);
    QSignalSpy failed_spy(&second, &UpdateService::operationFailed);
    second.downloadUpdate();
    QCOMPARE(second.state(), UpdateState::Failed);
    QCOMPARE(failed_spy.size(), 1);
    QCOMPARE(first_network.requests.size(), 2);
    QCOMPARE(second_network.requests.size(), 1);
    first.cancelDownload();
    QCOMPARE(first.state(), UpdateState::UpdateAvailable);
}

QTEST_GUILESS_MAIN(UpdateServiceTest)
#include "update_service_test.moc"
