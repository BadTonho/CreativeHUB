#include <creative_suite/translations/translation_catalog.h>
#include <creative_suite/translations/translation_runtime.h>

#include <test_check.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QLocale>
#include <QTemporaryDir>
#include <QTranslator>

#include <algorithm>

using namespace creative_suite::translations;

namespace {

QByteArray readFile(const QString& path) {
    QFile file(path);
    CS_TEST_CHECK(file.open(QIODevice::ReadOnly));
    return file.readAll();
}

QByteArray manifestFor(const QString& path, qint64 size, const QString& hash,
                       int schema = 1) {
    return QByteArray("{\"schema_version\":") + QByteArray::number(schema) +
        ",\"translations\":[{\"application\":\"hub\",\"locale\":\"pt_BR\",\"path\":\"" +
        path.toUtf8() + "\",\"size_bytes\":" + QByteArray::number(size) +
        ",\"sha256\":\"" + hash.toUtf8() + "\"}]}";
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    CS_TEST_CHECK(argc == 1);

    const QByteArray bundled = readFile(QString::fromLocal8Bit(TRANSLATION_TEST_BUNDLED_QM));
    const QByteArray downloaded = readFile(QString::fromLocal8Bit(TRANSLATION_TEST_DOWNLOADED_QM));
    const QByteArray downloadedHash = QCryptographicHash::hash(downloaded, QCryptographicHash::Sha256);
    const QString downloadedHashHex = QString::fromLatin1(downloadedHash.toHex());

    CS_TEST_CHECK(translationLocaleFor(QLocale(QStringLiteral("pt_BR"))) == QStringLiteral("pt_BR"));
    CS_TEST_CHECK(translationLocaleFor(QLocale(QStringLiteral("pt_PT"))) == QStringLiteral("pt_BR"));
    CS_TEST_CHECK(translationLocaleFor(QLocale(QStringLiteral("pt_MZ"))) == QStringLiteral("pt_BR"));
    CS_TEST_CHECK(translationLocaleFor(QLocale(QStringLiteral("en_US"))).isEmpty());
    CS_TEST_CHECK(translationLocaleFor(QLocale(QStringLiteral("fr_FR"))).isEmpty());

    TranslationCatalog publishedCatalog;
    CS_TEST_CHECK(parseTranslationCatalog(readFile(QString::fromLocal8Bit(TRANSLATION_PUBLISHED_MANIFEST)),
                                          &publishedCatalog));
    const auto published = std::find_if(
        publishedCatalog.translations.cbegin(), publishedCatalog.translations.cend(),
        [](const TranslationCatalogEntry& candidate) {
            return candidate.application_id == QStringLiteral("hub") &&
                candidate.locale == QStringLiteral("pt_BR");
        });
    CS_TEST_CHECK(published != publishedCatalog.translations.cend());
    const QByteArray publishedQm = readFile(QString::fromLocal8Bit(TRANSLATION_PUBLISHED_HUB_QM));
    CS_TEST_CHECK(published->size_bytes == publishedQm.size());
    CS_TEST_CHECK(published->sha256 == QCryptographicHash::hash(publishedQm, QCryptographicHash::Sha256));

    TranslationCatalog catalog;
    QString error;
    const QByteArray validManifest = manifestFor(
        QStringLiteral("hub/pt_BR.qm"), downloaded.size(), downloadedHashHex);
    CS_TEST_CHECK(parseTranslationCatalog(validManifest, &catalog, &error));
    CS_TEST_CHECK(catalog.schema_version == 1);
    CS_TEST_CHECK(catalog.translations.size() == 1);
    const auto entry = catalog.translations.first();

    CS_TEST_CHECK(!parseTranslationCatalog(manifestFor(
        QStringLiteral("hub/pt_BR.qm"), downloaded.size(), downloadedHashHex, 2), &catalog, &error));
    CS_TEST_CHECK(!parseTranslationCatalog(manifestFor(
        QStringLiteral("../hub/pt_BR.qm"), downloaded.size(), downloadedHashHex), &catalog, &error));
    QByteArray unknownApplication = validManifest;
    unknownApplication.replace("\"application\":\"hub\"", "\"application\":\"other\"");
    CS_TEST_CHECK(!parseTranslationCatalog(unknownApplication, &catalog, &error));
    CS_TEST_CHECK(!parseTranslationCatalog(validManifest.left(validManifest.size() - 1), &catalog, &error));
    CS_TEST_CHECK(!parseTranslationCatalog(manifestFor(
        QStringLiteral("hub/pt_BR.qm"), 0, downloadedHashHex), &catalog, &error));
    CS_TEST_CHECK(!parseTranslationCatalog(manifestFor(
        QStringLiteral("hub/pt_BR.qm"), 16 * 1024 * 1024 + 1, downloadedHashHex), &catalog, &error));
    CS_TEST_CHECK(!parseTranslationCatalog(manifestFor(
        QStringLiteral("hub/pt_BR.qm"), downloaded.size(), QString(64, QLatin1Char('z'))), &catalog, &error));

    const QByteArray duplicateManifest = QByteArray("{\"schema_version\":1,\"translations\":[") +
        validManifest.mid(validManifest.indexOf('[') + 1,
                          validManifest.lastIndexOf(']') - validManifest.indexOf('[') - 1) + "," +
        validManifest.mid(validManifest.indexOf('[') + 1,
                          validManifest.lastIndexOf(']') - validManifest.indexOf('[') - 1) + "]}";
    CS_TEST_CHECK(!parseTranslationCatalog(duplicateManifest, &catalog, &error));
    CS_TEST_CHECK(!parseTranslationCatalog(QByteArray(256 * 1024 + 1, ' '), &catalog, &error));

    QTemporaryDir storage;
    CS_TEST_CHECK(storage.isValid());
    CS_TEST_CHECK(verifyAndInstallTranslation(entry, downloaded, storage.path(), &error));
    const QString installedPath = translationFilePath(
        storage.path(), QStringLiteral("hub"), QStringLiteral("pt_BR"));
    CS_TEST_CHECK(readFile(installedPath) == downloaded);

    const QByteArray invalidQm("not a Qt translation catalog");
    auto invalidQmEntry = entry;
    invalidQmEntry.size_bytes = invalidQm.size();
    invalidQmEntry.sha256 = QCryptographicHash::hash(invalidQm, QCryptographicHash::Sha256);
    CS_TEST_CHECK(!verifyAndInstallTranslation(invalidQmEntry, invalidQm, storage.path(), &error));
    CS_TEST_CHECK(readFile(installedPath) == downloaded);
    auto wrongHashEntry = entry;
    wrongHashEntry.sha256 = QByteArray(32, '\0');
    CS_TEST_CHECK(!verifyAndInstallTranslation(wrongHashEntry, downloaded, storage.path(), &error));
    auto wrongSizeEntry = entry;
    wrongSizeEntry.size_bytes += 1;
    CS_TEST_CHECK(!verifyAndInstallTranslation(wrongSizeEntry, downloaded, storage.path(), &error));
    CS_TEST_CHECK(!verifyAndInstallTranslation(entry, bundled, storage.path(), &error));
    CS_TEST_CHECK(readFile(installedPath) == downloaded);

    {
        TranslationRuntime runtime;
        const auto result = runtime.installForApplication(
            application, QStringLiteral("hub"),
            QString::fromLocal8Bit(TRANSLATION_TEST_BUNDLED_QM),
            QLocale(QStringLiteral("pt_PT")), storage.path());
        CS_TEST_CHECK(result.locale == QStringLiteral("pt_BR"));
        CS_TEST_CHECK(result.bundled_loaded);
        CS_TEST_CHECK(result.downloaded_loaded);
        CS_TEST_CHECK(QCoreApplication::translate("TestContext", "Shared") == QStringLiteral("Downloaded value"));
        CS_TEST_CHECK(QCoreApplication::translate("TestContext", "BundledOnly") == QStringLiteral("Bundled fallback"));
        CS_TEST_CHECK(QCoreApplication::translate("TestContext", "EnglishSource") == QStringLiteral("EnglishSource"));
    }

    QTemporaryDir bundledOnlyStorage;
    {
        TranslationRuntime runtime;
        const auto result = runtime.installForApplication(
            application, QStringLiteral("hub"),
            QString::fromLocal8Bit(TRANSLATION_TEST_BUNDLED_QM),
            QLocale(QStringLiteral("pt_BR")), bundledOnlyStorage.path());
        CS_TEST_CHECK(result.bundled_loaded);
        CS_TEST_CHECK(!result.downloaded_loaded);
        CS_TEST_CHECK(QCoreApplication::translate("TestContext", "Shared") == QStringLiteral("Bundled value"));
        CS_TEST_CHECK(QCoreApplication::translate("TestContext", "EnglishSource") == QStringLiteral("EnglishSource"));
    }

    return 0;
}
