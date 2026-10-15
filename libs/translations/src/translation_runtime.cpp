#include <creative_suite/translations/translation_runtime.h>

#include <creative_suite/diagnostics/logger.h>
#include <creative_suite/translations/translation_catalog.h>

#include <QFileInfo>
#include <QTranslator>

namespace creative_suite::translations {
namespace {

bool isKnownApplication(const QString& application_id) {
    return application_id == QStringLiteral("hub") ||
        application_id == QStringLiteral("video-editor") ||
        application_id == QStringLiteral("image-editor") ||
        application_id == QStringLiteral("motion-editor");
}

void logLoadFailure(const QString& application_id,
                    const QString& operation,
                    const QString& path,
                    const QString& cause) {
    auto& logger = creative_suite::diagnostics::Logger::instance();
    if (!logger.is_initialized()) {
        static_cast<void>(logger.initialize_default(application_id.toStdString()));
    }
    logger.log(
        creative_suite::diagnostics::Level::Warning,
        "translations",
        operation.toStdString(),
        cause.toStdString(),
        {{"application", application_id.toStdString()}, {"path", path.toStdString()}});
}

} // namespace

TranslationRuntime::TranslationRuntime() = default;

TranslationRuntime::~TranslationRuntime() {
    removeInstalledTranslators();
}

void TranslationRuntime::removeInstalledTranslators() noexcept {
    if (m_application != nullptr) {
        if (m_downloaded) {
            m_application->removeTranslator(m_downloaded.get());
        }
        if (m_bundled) {
            m_application->removeTranslator(m_bundled.get());
        }
    }
    m_downloaded.reset();
    m_bundled.reset();
    m_application = nullptr;
}

TranslationLoadResult TranslationRuntime::installForApplication(
    QCoreApplication& application,
    const QString& application_id,
    const QString& bundled_resource_path,
    const QLocale& locale,
    const QString& storage_root) {
    removeInstalledTranslators();

    TranslationLoadResult result;
    result.locale = translationLocaleFor(locale);
    if (!isKnownApplication(application_id) || result.locale.isEmpty()) {
        return result;
    }

    m_application = &application;

    m_bundled = std::make_unique<QTranslator>();
    if (m_bundled->load(bundled_resource_path)) {
        result.bundled_loaded = application.installTranslator(m_bundled.get());
    } else {
        m_bundled.reset();
    }

    const QString external_path = translationFilePath(
        storage_root.isEmpty() ? sharedTranslationRoot() : storage_root,
        application_id, result.locale);
    if (!external_path.isEmpty() && QFileInfo::exists(external_path)) {
        m_downloaded = std::make_unique<QTranslator>();
        if (m_downloaded->load(external_path)) {
            // Qt searches translators in reverse installation order, so the
            // downloaded catalog is installed after the bundled fallback.
            result.downloaded_loaded = application.installTranslator(m_downloaded.get());
        } else {
            result.error = QStringLiteral("The downloaded translation could not be loaded.");
            logLoadFailure(application_id, QStringLiteral("load_downloaded_catalog"),
                           external_path, result.error);
            m_downloaded.reset();
        }
    }

    if (result.bundled_loaded || result.downloaded_loaded) {
        return result;
    }
    m_application = nullptr;
    m_bundled.reset();
    m_downloaded.reset();
    return result;
}

} // namespace creative_suite::translations
