#pragma once

#include <QByteArray>
#include <QList>
#include <QLocale>
#include <QString>

namespace creative_suite::translations {

struct TranslationCatalogEntry {
    QString application_id;
    QString locale;
    QString path;
    qint64 size_bytes = 0;
    QByteArray sha256;
};

struct TranslationCatalog {
    int schema_version = 0;
    QList<TranslationCatalogEntry> translations;
};

[[nodiscard]] bool parseTranslationCatalog(
    const QByteArray& json,
    TranslationCatalog* catalog,
    QString* error = nullptr);

[[nodiscard]] QString translationLocaleFor(const QLocale& locale);
[[nodiscard]] QString sharedTranslationRoot();
[[nodiscard]] QString translationFilePath(
    const QString& root,
    const QString& application_id,
    const QString& locale);

[[nodiscard]] bool verifyAndInstallTranslation(
    const TranslationCatalogEntry& entry,
    const QByteArray& data,
    const QString& root,
    QString* error = nullptr);

} // namespace creative_suite::translations
