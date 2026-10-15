#include <creative_suite/translations/translation_update_service.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStandardPaths>

namespace creative_suite::translations {
namespace {

constexpr qint64 kMaximumManifestBytes = 256 * 1024;
constexpr qint64 kMaximumTranslationBytes = 16 * 1024 * 1024;
const QUrl kManifestUrl(
    QStringLiteral("https://raw.githubusercontent.com/BadTonho/CreativeHUB/main/languages/manifest.json"));

bool isLoopbackHttp(const QUrl& url) {
    if (url.scheme() != QStringLiteral("http")) {
        return false;
    }
    const QString host = url.host().toLower();
    return host == QStringLiteral("localhost") ||
        host == QStringLiteral("127.0.0.1") ||
        host == QStringLiteral("::1");
}

bool isAllowedUrl(const QUrl& url) {
    return url.isValid() && !url.isRelative() &&
        (url.scheme() == QStringLiteral("https") || isLoopbackHttp(url));
}

bool sameOrigin(const QUrl& left, const QUrl& right) {
    return left.scheme() == right.scheme() &&
        left.host().compare(right.host(), Qt::CaseInsensitive) == 0 &&
        left.port(left.scheme() == QStringLiteral("https") ? 443 : 80) ==
            right.port(right.scheme() == QStringLiteral("https") ? 443 : 80);
}

bool localFileMatches(const TranslationCatalogEntry& entry, const QString& root) {
    const QString path = translationFilePath(root, entry.application_id, entry.locale);
    if (path.isEmpty()) {
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() != entry.size_bytes) {
        return false;
    }
    return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256) == entry.sha256;
}

} // namespace

TranslationUpdateService::TranslationUpdateService(TranslationUpdateConfig config,
                                                   QObject* parent)
    : QObject(parent)
    , m_config(std::move(config))
    , m_network() {
    qRegisterMetaType<TranslationUpdateState>();
}

TranslationUpdateService::~TranslationUpdateService() {
    if (m_reply != nullptr) {
        m_reply->abort();
    }
}

bool TranslationUpdateService::isBusy() const noexcept {
    return m_state == TranslationUpdateState::Checking ||
        m_state == TranslationUpdateState::Downloading;
}

TranslationUpdateConfig TranslationUpdateService::defaultConfig() {
    TranslationUpdateConfig config;
    config.manifest_url = kManifestUrl;
    config.storage_root = sharedTranslationRoot();
    return config;
}

bool TranslationUpdateService::checkForUpdates(const QString& locale) {
    if (isBusy()) {
        return false;
    }

    static const QRegularExpression locale_pattern(QStringLiteral("^[a-z]{2}_[A-Z]{2}$"));
    if (!locale_pattern.match(locale).hasMatch()) {
        m_locale.clear();
        m_pending.clear();
        m_installed_count = 0;
        m_had_errors = false;
        setState(TranslationUpdateState::Completed);
        emit finished(0, false);
        return true;
    }
    if (!isAllowedUrl(m_config.manifest_url)) {
        fail(QStringLiteral("validate_manifest_url"), QString(),
             QStringLiteral("The translation manifest URL must use HTTPS."));
        finish();
        return true;
    }
    if (m_config.storage_root.isEmpty()) {
        fail(QStringLiteral("resolve_translation_storage"), QString(),
             QStringLiteral("The per-user translation directory is unavailable."));
        finish();
        return true;
    }

    m_locale = locale;
    m_pending.clear();
    m_next_asset = 0;
    m_installed_count = 0;
    m_had_errors = false;
    setState(TranslationUpdateState::Checking);
    request(m_config.manifest_url, kMaximumManifestBytes, RequestKind::Manifest);
    return true;
}

void TranslationUpdateService::setState(TranslationUpdateState state) {
    if (m_state == state) {
        return;
    }
    m_state = state;
    emit stateChanged(m_state);
}

void TranslationUpdateService::request(const QUrl& url,
                                       qint64 maximum_bytes,
                                       RequestKind kind) {
    if (m_reply != nullptr || !isAllowedUrl(url) || !sameOrigin(m_config.manifest_url, url)) {
        fail(QStringLiteral("validate_download_url"), QString(),
             QStringLiteral("The translation asset URL is invalid or outside the catalog origin."));
        if (kind == RequestKind::Manifest) {
            finish();
        } else {
            advanceAfterAssetFailure();
        }
        return;
    }

    QNetworkRequest network_request(url);
    network_request.setHeader(QNetworkRequest::UserAgentHeader,
                              QStringLiteral("CreativeSuiteTranslations/1"));
    network_request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                 QNetworkRequest::ManualRedirectPolicy);
    network_request.setTransferTimeout(m_config.request_timeout_ms);

    m_response.clear();
    m_response_limit = maximum_bytes;
    m_response_too_large = false;
    m_request_kind = kind;
    m_reply = m_network.get(network_request);
    connect(m_reply, &QIODevice::readyRead, this,
            [this, reply = m_reply] { onReplyReadyRead(reply); });
    connect(m_reply, &QNetworkReply::finished, this,
            [this, reply = m_reply] { onReplyFinished(reply); });
}

void TranslationUpdateService::onReplyReadyRead(QNetworkReply* reply) {
    if (reply == nullptr || reply != m_reply || m_response_too_large) {
        return;
    }
    const qint64 remaining = m_response_limit - m_response.size();
    const QByteArray chunk = reply->read(remaining + 1);
    if (m_response.size() + chunk.size() > m_response_limit) {
        m_response_too_large = true;
        reply->abort();
        return;
    }
    m_response.append(chunk);
}

void TranslationUpdateService::onReplyFinished(QNetworkReply* reply) {
    if (reply == nullptr || reply != m_reply) {
        if (reply != nullptr) {
            reply->deleteLater();
        }
        return;
    }
    onReplyReadyRead(reply);
    const auto kind = m_request_kind;
    const QByteArray data = m_response;
    const bool too_large = m_response_too_large;
    const auto network_error = reply->error();
    const QString network_error_text = reply->errorString();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QUrl final_url = reply->url();
    m_reply = nullptr;
    reply->deleteLater();

    if (too_large) {
        fail(kind == RequestKind::Manifest ? QStringLiteral("download_manifest")
                                           : QStringLiteral("download_translation"),
             kind == RequestKind::Asset && m_next_asset < m_pending.size()
                 ? m_pending.at(m_next_asset).application_id : QString(),
             QStringLiteral("The server response exceeds the configured size limit."));
        if (kind == RequestKind::Asset) {
            advanceAfterAssetFailure();
        } else {
            finish();
        }
        return;
    }
    if (network_error != QNetworkReply::NoError || status != 200 ||
        !sameOrigin(m_config.manifest_url, final_url)) {
        const QString cause = network_error != QNetworkReply::NoError
            ? network_error_text
            : QStringLiteral("The server returned an unexpected status or redirect.");
        fail(kind == RequestKind::Manifest ? QStringLiteral("download_manifest")
                                           : QStringLiteral("download_translation"),
             kind == RequestKind::Asset && m_next_asset < m_pending.size()
                 ? m_pending.at(m_next_asset).application_id : QString(), cause);
        if (kind == RequestKind::Asset) {
            advanceAfterAssetFailure();
        } else {
            finish();
        }
        return;
    }

    if (kind == RequestKind::Manifest) {
        handleManifest(data);
    } else {
        handleAsset(data);
    }
}

void TranslationUpdateService::handleManifest(const QByteArray& data) {
    TranslationCatalog catalog;
    QString error;
    if (!parseTranslationCatalog(data, &catalog, &error)) {
        fail(QStringLiteral("parse_manifest"), QString(), error);
        finish();
        return;
    }

    for (const auto& entry : catalog.translations) {
        if (entry.locale == m_locale && !localFileMatches(entry, m_config.storage_root)) {
            m_pending.push_back(entry);
        }
    }
    requestNextAsset();
}

void TranslationUpdateService::requestNextAsset() {
    while (m_next_asset < m_pending.size() &&
           localFileMatches(m_pending.at(m_next_asset), m_config.storage_root)) {
        ++m_next_asset;
    }
    if (m_next_asset >= m_pending.size()) {
        finish();
        return;
    }

    const auto& entry = m_pending.at(m_next_asset);
    const QUrl asset_url = m_config.manifest_url.resolved(QUrl(entry.path));
    setState(TranslationUpdateState::Downloading);
    request(asset_url, kMaximumTranslationBytes, RequestKind::Asset);
}

void TranslationUpdateService::handleAsset(const QByteArray& data) {
    if (m_next_asset >= m_pending.size()) {
        fail(QStringLiteral("install_translation"), QString(),
             QStringLiteral("The translation download state is inconsistent."));
        finish();
        return;
    }
    const auto entry = m_pending.at(m_next_asset);
    QString error;
    if (!verifyAndInstallTranslation(entry, data, m_config.storage_root, &error)) {
        fail(QStringLiteral("verify_translation"), entry.application_id, error);
    } else {
        ++m_installed_count;
        emit translationInstalled(entry.application_id, entry.locale);
    }
    ++m_next_asset;
    requestNextAsset();
}

void TranslationUpdateService::advanceAfterAssetFailure() {
    ++m_next_asset;
    requestNextAsset();
}

void TranslationUpdateService::finish() {
    setState(m_had_errors ? TranslationUpdateState::Failed
                          : TranslationUpdateState::Completed);
    emit finished(m_installed_count, m_had_errors);
}

void TranslationUpdateService::fail(const QString& operation,
                                    const QString& application_id,
                                    const QString& cause) {
    m_had_errors = true;
    emit operationFailed(operation, application_id, cause);
}

} // namespace creative_suite::translations
