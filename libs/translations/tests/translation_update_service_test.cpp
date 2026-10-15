#include <creative_suite/translations/translation_catalog.h>
#include <creative_suite/translations/translation_update_service.h>

#include <test_check.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QEventLoop>
#include <QFile>
#include <QHostAddress>
#include <QNetworkReply>
#include <QNetworkProxy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>


using namespace creative_suite::translations;

namespace {

QByteArray readFile(const QString& path) {
    QFile file(path);
    CS_TEST_CHECK(file.open(QIODevice::ReadOnly));
    return file.readAll();
}

QByteArray makeManifest(const QByteArray& payload) {
    return QByteArray("{\"schema_version\":1,\"translations\":[{\"application\":\"hub\",\"locale\":\"pt_BR\",\"path\":\"hub/pt_BR.qm\",\"size_bytes\":") +
        QByteArray::number(payload.size()) + ",\"sha256\":\"" +
        QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex() + "\"}]}";
}

class TranslationHttpFixture final : public QObject {
public:
    TranslationHttpFixture() {
        QObject::connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (m_server.hasPendingConnections()) {
                auto* socket = m_server.nextPendingConnection();
                QObject::connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                    const QByteArray request = socket->readAll();
                    const QByteArray firstLine = request.left(request.indexOf("\r\n"));
                    ++requestCount;
                    if (firstLine.startsWith("GET /languages/manifest.json ")) {
                        send(socket, manifestStatus, manifestBody, false);
                    } else if (firstLine.startsWith("GET /languages/hub/pt_BR.qm ")) {
                        send(socket, assetStatus, assetBody, truncateAsset);
                    } else {
                        send(socket, 404, {}, false);
                    }
                });
                QTimer::singleShot(0, socket, [socket] {
                    if (socket->bytesAvailable() > 0) {
                        emit socket->readyRead();
                    }
                });
            }
        });
        CS_TEST_CHECK(m_server.listen(QHostAddress::LocalHost, 0));
    }

    QUrl manifestUrl() const {
        return QUrl(QStringLiteral("http://127.0.0.1:%1/languages/manifest.json")
                        .arg(m_server.serverPort()));
    }

    QByteArray manifestBody;
    QByteArray assetBody;
    int manifestStatus{200};
    int assetStatus{200};
    int requestCount{0};
    bool truncateAsset{false};

private:
    static void send(QTcpSocket* socket, int status, const QByteArray& body, bool truncate) {
        const QByteArray statusText = status == 200 ? QByteArray("OK") : QByteArray("Failure");
        QByteArray response = "HTTP/1.1 " + QByteArray::number(status) + " " + statusText +
            "\r\nContent-Type: application/octet-stream\r\nContent-Length: " +
            QByteArray::number(body.size() + (truncate ? 20 : 0)) +
            "\r\nConnection: close\r\n\r\n" + body;
        socket->write(response);
        if (truncate) {
            socket->abort();
        } else {
            socket->disconnectFromHost();
        }
    }

    QTcpServer m_server;
};

struct CheckResult {
    int installed{0};
    bool hadErrors{false};
    bool timedOut{false};
};

CheckResult runCheck(TranslationUpdateService& service) {
    QEventLoop loop;
    CheckResult result;
    QObject::connect(&service, &TranslationUpdateService::finished, &loop,
                     [&loop, &result](int installed, bool hadErrors) {
        result.installed = installed;
        result.hadErrors = hadErrors;
        loop.quit();
    });
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, [&loop, &result] {
        result.timedOut = true;
        loop.quit();
    });
    timeout.start(5000);
    CS_TEST_CHECK(service.checkForUpdates(QStringLiteral("pt_BR")));
    loop.exec();
    return result;
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    CS_TEST_CHECK(argc == 1);
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
    const QByteArray validQm = readFile(QString::fromLocal8Bit(TRANSLATION_TEST_DOWNLOADED_QM));
    const QByteArray alternateQm = readFile(QString::fromLocal8Bit(TRANSLATION_TEST_BUNDLED_QM));

    TranslationHttpFixture fixture;
    QTemporaryDir storage;
    CS_TEST_CHECK(storage.isValid());
    TranslationUpdateConfig config;
    config.manifest_url = fixture.manifestUrl();
    config.storage_root = storage.path();
    config.request_timeout_ms = 2000;
    TranslationUpdateService service(config);

    fixture.assetBody = validQm;
    fixture.manifestBody = makeManifest(fixture.assetBody);
    auto result = runCheck(service);
    CS_TEST_CHECK(!result.timedOut);
    CS_TEST_CHECK(!result.hadErrors);
    CS_TEST_CHECK(result.installed == 1);
    CS_TEST_CHECK(fixture.requestCount == 2);

    const QString installedPath = translationFilePath(
        storage.path(), QStringLiteral("hub"), QStringLiteral("pt_BR"));
    CS_TEST_CHECK(readFile(installedPath) == validQm);

    result = runCheck(service);
    CS_TEST_CHECK(!result.timedOut);
    CS_TEST_CHECK(!result.hadErrors);
    CS_TEST_CHECK(result.installed == 0);
    CS_TEST_CHECK(fixture.requestCount == 3);

    const QByteArray invalidQm("invalid replacement catalog");
    fixture.assetBody = invalidQm;
    fixture.manifestBody = makeManifest(invalidQm);
    int failureSignals = 0;
    QObject::connect(&service, &TranslationUpdateService::operationFailed,
                     &application, [&failureSignals] { ++failureSignals; });
    result = runCheck(service);
    CS_TEST_CHECK(!result.timedOut);
    CS_TEST_CHECK(result.hadErrors);
    CS_TEST_CHECK(result.installed == 0);
    CS_TEST_CHECK(failureSignals == 1);
    CS_TEST_CHECK(readFile(installedPath) == validQm);

    fixture.manifestStatus = 503;
    result = runCheck(service);
    CS_TEST_CHECK(!result.timedOut);
    CS_TEST_CHECK(result.hadErrors);
    CS_TEST_CHECK(fixture.requestCount == 6);

    fixture.manifestStatus = 200;
    fixture.assetBody = alternateQm;
    fixture.manifestBody = makeManifest(alternateQm);
    fixture.truncateAsset = true;
    result = runCheck(service);
    CS_TEST_CHECK(!result.timedOut);
    CS_TEST_CHECK(result.hadErrors);
    CS_TEST_CHECK(readFile(installedPath) == validQm);

    return 0;
}
