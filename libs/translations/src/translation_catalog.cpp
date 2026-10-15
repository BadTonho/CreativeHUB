#include <creative_suite/translations/translation_catalog.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTranslator>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

namespace creative_suite::translations {
namespace {

constexpr int kCatalogSchemaVersion = 1;
constexpr qint64 kMaximumTranslationBytes = 16 * 1024 * 1024;

bool isKnownApplication(const QString& application_id) {
    return application_id == QStringLiteral("hub") ||
        application_id == QStringLiteral("video-editor") ||
        application_id == QStringLiteral("image-editor") ||
        application_id == QStringLiteral("motion-editor");
}

bool isSafeLocale(const QString& locale) {
    static const QRegularExpression pattern(QStringLiteral("^[a-z]{2}_[A-Z]{2}$"));
    return pattern.match(locale).hasMatch();
}

void setError(QString* error, const QString& message) {
    if (error != nullptr) {
        *error = message;
    }
}

bool parseEntry(const QJsonValue& value,
                TranslationCatalogEntry* entry,
                QString* error) {
    if (!value.isObject()) {
        setError(error, QStringLiteral("A translation entry must be an object."));
        return false;
    }

    const auto object = value.toObject();
    const auto application_id = object.value(QStringLiteral("application"));
    const auto locale = object.value(QStringLiteral("locale"));
    const auto path = object.value(QStringLiteral("path"));
    const auto size = object.value(QStringLiteral("size_bytes"));
    const auto hash = object.value(QStringLiteral("sha256"));

    if (!application_id.isString() || !locale.isString() ||
        !path.isString() || !size.isDouble() || !hash.isString()) {
        setError(error, QStringLiteral("A translation entry is missing required fields."));
        return false;
    }

    const QString app = application_id.toString();
    const QString language = locale.toString();
    const QString asset_path = path.toString();
    const double size_value = size.toDouble();
    const QString hash_text = hash.toString();

    if (!isKnownApplication(app)) {
        setError(error, QStringLiteral("The translation entry has an unknown application ID."));
        return false;
    }
    if (!isSafeLocale(language)) {
        setError(error, QStringLiteral("The translation entry has an invalid locale."));
        return false;
    }
    if (asset_path != app + QLatin1Char('/') + language + QStringLiteral(".qm")) {
        setError(error, QStringLiteral("The translation asset path is unsafe or inconsistent."));
        return false;
    }
    if (size_value < 1 || size_value > static_cast<double>(kMaximumTranslationBytes) ||
        size_value != static_cast<double>(static_cast<qint64>(size_value))) {
        setError(error, QStringLiteral("The translation size is invalid or exceeds the limit."));
        return false;
    }
    static const QRegularExpression hash_pattern(QStringLiteral("^[0-9a-f]{64}$"));
    if (!hash_pattern.match(hash_text).hasMatch()) {
        setError(error, QStringLiteral("The translation SHA-256 value is invalid."));
        return false;
    }

    entry->application_id = app;
    entry->locale = language;
    entry->path = asset_path;
    entry->size_bytes = static_cast<qint64>(size_value);
    entry->sha256 = QByteArray::fromHex(hash_text.toLatin1());
    return true;
}

} // namespace

bool parseTranslationCatalog(const QByteArray& json,
                             TranslationCatalog* catalog,
                             QString* error) {
    if (catalog == nullptr) {
        setError(error, QStringLiteral("No catalog output was provided."));
        return false;
    }
    *catalog = {};

    if (json.isEmpty() || json.size() > 256 * 1024) {
        setError(error, QStringLiteral("The translation catalog is empty or too large."));
        return false;
    }

    QJsonParseError parse_error;
    const auto document = QJsonDocument::fromJson(json, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
        setError(error, QStringLiteral("The translation catalog is not a valid JSON object."));
        return false;
    }

    const auto object = document.object();
    const auto schema = object.value(QStringLiteral("schema_version"));
    const auto entries_value = object.value(QStringLiteral("translations"));
    if (!schema.isDouble() || schema.toInt(-1) != kCatalogSchemaVersion) {
        setError(error, QStringLiteral("The translation catalog schema is unsupported."));
        return false;
    }
    if (!entries_value.isArray()) {
        setError(error, QStringLiteral("The translation catalog has no translation list."));
        return false;
    }

    const auto entries = entries_value.toArray();
    if (entries.size() > 128) {
        setError(error, QStringLiteral("The translation catalog contains too many entries."));
        return false;
    }

    TranslationCatalog parsed;
    parsed.schema_version = kCatalogSchemaVersion;
    for (const auto& value : entries) {
        TranslationCatalogEntry entry;
        if (!parseEntry(value, &entry, error)) {
            return false;
        }
        for (const auto& previous : parsed.translations) {
            if (previous.application_id == entry.application_id &&
                previous.locale == entry.locale) {
                setError(error, QStringLiteral("The translation catalog contains a duplicate entry."));
                return false;
            }
        }
        parsed.translations.push_back(std::move(entry));
    }

    *catalog = std::move(parsed);
    return true;
}

QString translationLocaleFor(const QLocale& locale) {
    if (locale.language() == QLocale::Portuguese) {
        return QStringLiteral("pt_BR");
    }
    return {};
}

QString sharedTranslationRoot() {
    const QString data_root = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (data_root.isEmpty()) {
        return {};
    }
    return QDir(data_root).filePath(QStringLiteral("CreativeSuite/translations"));
}

QString translationFilePath(const QString& root,
                            const QString& application_id,
                            const QString& locale) {
    if (!isKnownApplication(application_id) || !isSafeLocale(locale) || root.isEmpty()) {
        return {};
    }
    return QDir(root).filePath(application_id + QLatin1Char('/') + locale + QStringLiteral(".qm"));
}

bool verifyAndInstallTranslation(const TranslationCatalogEntry& entry,
                                 const QByteArray& data,
                                 const QString& root,
                                 QString* error) {
    if (!isKnownApplication(entry.application_id) || !isSafeLocale(entry.locale) ||
        entry.path != entry.application_id + QLatin1Char('/') + entry.locale + QStringLiteral(".qm")) {
        setError(error, QStringLiteral("The translation entry is invalid."));
        return false;
    }
    if (entry.size_bytes < 1 || entry.size_bytes > kMaximumTranslationBytes ||
        entry.sha256.size() != QCryptographicHash::hashLength(QCryptographicHash::Sha256) ||
        data.size() != entry.size_bytes) {
        setError(error, QStringLiteral("The downloaded translation has an unexpected size."));
        return false;
    }
    if (QCryptographicHash::hash(data, QCryptographicHash::Sha256) != entry.sha256) {
        setError(error, QStringLiteral("The downloaded translation SHA-256 does not match."));
        return false;
    }

    QTranslator probe;
    if (!probe.load(reinterpret_cast<const uchar*>(data.constData()), data.size())) {
        setError(error, QStringLiteral("The downloaded file is not a valid Qt translation."));
        return false;
    }

    const QString target_path = translationFilePath(root, entry.application_id, entry.locale);
    if (target_path.isEmpty()) {
        setError(error, QStringLiteral("The translation destination is invalid."));
        return false;
    }
    const QFileInfo target_info(target_path);
    if (!QDir().mkpath(target_info.absolutePath())) {
        setError(error, QStringLiteral("The translation directory could not be created."));
        return false;
    }

    QSaveFile file(target_path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(error, QStringLiteral("The translation file could not be opened for replacement."));
        return false;
    }
    if (file.write(data) != data.size()) {
        file.cancelWriting();
        setError(error, QStringLiteral("The translation file could not be fully written."));
        return false;
    }
    if (!file.commit()) {
        setError(error, QStringLiteral("The translation file could not be installed atomically."));
        return false;
    }
    return true;
}

} // namespace creative_suite::translations
