#pragma once

#include <QCoreApplication>
#include <QString>
#include <QLocale>

#include <memory>

class QTranslator;

namespace creative_suite::translations {

struct TranslationLoadResult {
    QString locale;
    bool bundled_loaded = false;
    bool downloaded_loaded = false;
    QString error;
};

class TranslationRuntime final {
public:
    TranslationRuntime();
    ~TranslationRuntime();

    TranslationRuntime(const TranslationRuntime&) = delete;
    TranslationRuntime& operator=(const TranslationRuntime&) = delete;

    [[nodiscard]] TranslationLoadResult installForApplication(
        QCoreApplication& application,
        const QString& application_id,
        const QString& bundled_resource_path,
        const QLocale& locale = QLocale::system(),
        const QString& storage_root = {});

private:
    void removeInstalledTranslators() noexcept;

    QCoreApplication* m_application{nullptr};
    std::unique_ptr<QTranslator> m_bundled;
    std::unique_ptr<QTranslator> m_downloaded;
};

} // namespace creative_suite::translations
