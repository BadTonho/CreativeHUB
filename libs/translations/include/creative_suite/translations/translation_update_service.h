#pragma once

#include <creative_suite/translations/translation_catalog.h>

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QUrl>

namespace creative_suite::translations {

struct TranslationUpdateConfig {
    QUrl manifest_url;
    QString storage_root;
    int request_timeout_ms = 30000;
};

enum class TranslationUpdateState {
    Idle,
    Checking,
    Downloading,
    Completed,
    Failed,
};

class TranslationUpdateService final : public QObject {
    Q_OBJECT

public:
    explicit TranslationUpdateService(TranslationUpdateConfig config,
                                      QObject* parent = nullptr);
    ~TranslationUpdateService() override;

    [[nodiscard]] TranslationUpdateState state() const noexcept { return m_state; }
    [[nodiscard]] bool isBusy() const noexcept;

    // Checks only the selected locale. HTTP is accepted only for loopback
    // hosts so tests can use a local server; production configuration uses HTTPS.
    bool checkForUpdates(const QString& locale);

    static TranslationUpdateConfig defaultConfig();

signals:
    void stateChanged(creative_suite::translations::TranslationUpdateState state);
    void translationInstalled(const QString& application_id, const QString& locale);
    void operationFailed(const QString& operation,
                         const QString& application_id,
                         const QString& cause);
    void finished(int installed_count, bool had_errors);

private:
    enum class RequestKind { Manifest, Asset };

    void setState(TranslationUpdateState state);
    void request(const QUrl& url, qint64 maximum_bytes, RequestKind kind);
    void onReplyReadyRead(QNetworkReply* reply);
    void onReplyFinished(QNetworkReply* reply);
    void handleManifest(const QByteArray& data);
    void requestNextAsset();
    void handleAsset(const QByteArray& data);
    void finish();
    void advanceAfterAssetFailure();
    void fail(const QString& operation,
              const QString& application_id,
              const QString& cause);

    TranslationUpdateConfig m_config;
    QNetworkAccessManager m_network;
    QNetworkReply* m_reply{nullptr};
    RequestKind m_request_kind{RequestKind::Manifest};
    QByteArray m_response;
    qint64 m_response_limit = 0;
    bool m_response_too_large = false;
    TranslationUpdateState m_state{TranslationUpdateState::Idle};
    QString m_locale;
    QList<TranslationCatalogEntry> m_pending;
    int m_next_asset = 0;
    int m_installed_count = 0;
    bool m_had_errors = false;
};

} // namespace creative_suite::translations

Q_DECLARE_METATYPE(creative_suite::translations::TranslationUpdateState)
