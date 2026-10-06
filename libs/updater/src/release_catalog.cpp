#include <creative_suite/updater/release_catalog.h>

#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <limits>

namespace creative_suite::updater {
namespace {

void setError(QString* output, QString message) {
    if (output) *output = std::move(message);
}

bool isVersion(const QString& version) {
    static const QRegularExpression pattern(
        QStringLiteral(R"(^\d+\.\d+\.\d+$)"));
    return pattern.match(version).hasMatch();
}

bool isSafeAssetName(const QString& name) {
    if (name.isEmpty() || name == QStringLiteral(".") || name == QStringLiteral("..")) {
        return false;
    }
    return !name.contains(QLatin1Char('/')) &&
        !name.contains(QLatin1Char('\\')) &&
        !name.contains(QLatin1Char(':'));
}

bool isSafeAppId(const QString& app_id) {
    static const QRegularExpression pattern(QStringLiteral("^[a-z0-9-]+$"));
    return pattern.match(app_id).hasMatch();
}

std::optional<QVector<quint64>> parseVersion(const QString& value) noexcept {
    if (!isVersion(value)) return std::nullopt;
    const auto parts = value.split(QLatin1Char('.'));
    QVector<quint64> numbers;
    numbers.reserve(parts.size());
    for (const auto& part : parts) {
        bool ok = false;
        const auto number = part.toULongLong(&ok, 10);
        if (!ok) return std::nullopt;
        numbers.push_back(number);
    }
    return numbers;
}

} // namespace

const ReleaseEntry* ReleaseCatalog::find(const QString& app_id) const noexcept {
    const auto found = std::find_if(
        applications.cbegin(), applications.cend(),
        [&app_id](const ReleaseEntry& entry) { return entry.app_id == app_id; });
    return found == applications.cend() ? nullptr : &*found;
}

std::optional<ReleaseCatalog> ReleaseCatalog::fromJson(
    const QByteArray& bytes,
    QString* error) {
    QJsonParseError parse_error;
    const auto document = QJsonDocument::fromJson(bytes, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
        setError(error, QStringLiteral("The update catalog is not valid JSON."));
        return std::nullopt;
    }

    const auto root = document.object();
    const auto schema_version = root.value(QStringLiteral("schema_version"));
    const auto app_object = root.value(QStringLiteral("applications"));
    if (!schema_version.isDouble() || schema_version.toInt() != 1 ||
        !app_object.isObject()) {
        setError(error, QStringLiteral("The update catalog schema is unsupported."));
        return std::nullopt;
    }

    ReleaseCatalog catalog;
    catalog.schema_version = 1;
    catalog.suite_version = root.value(QStringLiteral("suite_version")).toString();
    const auto apps = app_object.toObject();
    if (apps.isEmpty()) {
        setError(error, QStringLiteral("The update catalog contains no applications."));
        return std::nullopt;
    }

    for (auto it = apps.begin(); it != apps.end(); ++it) {
        if (!isSafeAppId(it.key()) || !it.value().isObject()) {
            setError(error, QStringLiteral("An application entry is malformed."));
            return std::nullopt;
        }
        const auto object = it.value().toObject();
        ReleaseEntry entry;
        entry.app_id = it.key();
        entry.version = object.value(QStringLiteral("version")).toString();
        entry.installer_asset = object.value(QStringLiteral("installer_asset")).toString();
        const auto size_value = object.value(QStringLiteral("size_bytes"));
        const auto size_number = size_value.toDouble(-1.0);
        entry.sha256_hex = object.value(QStringLiteral("sha256")).toString().toLatin1().toLower();
        entry.release_notes = object.value(QStringLiteral("release_notes")).toString();

        static const QRegularExpression hash_pattern(QStringLiteral(R"(^[0-9a-f]{64}$)"));
        const bool valid_size = size_value.isDouble() && std::isfinite(size_number) &&
            std::floor(size_number) == size_number && size_number > 0.0 &&
            static_cast<long double>(size_number) <=
                static_cast<long double>(std::numeric_limits<qint64>::max());
        if (!isVersion(entry.version) || !isSafeAssetName(entry.installer_asset) ||
            !valid_size || !hash_pattern.match(QString::fromLatin1(entry.sha256_hex)).hasMatch()) {
            setError(error, QStringLiteral("Application '%1' has invalid release metadata.").arg(entry.app_id));
            return std::nullopt;
        }
        entry.size_bytes = static_cast<qint64>(size_number);
        catalog.applications.push_back(std::move(entry));
    }

    std::sort(catalog.applications.begin(), catalog.applications.end(),
        [](const ReleaseEntry& left, const ReleaseEntry& right) {
            return left.app_id < right.app_id;
        });
    return catalog;
}

std::optional<int> compareVersions(const QString& left, const QString& right) noexcept {
    const auto left_parts = parseVersion(left);
    const auto right_parts = parseVersion(right);
    if (!left_parts || !right_parts) return std::nullopt;

    const auto count = std::max(left_parts->size(), right_parts->size());
    for (qsizetype index = 0; index < count; ++index) {
        const auto left_number = index < left_parts->size() ? left_parts->at(index) : 0;
        const auto right_number = index < right_parts->size() ? right_parts->at(index) : 0;
        if (left_number < right_number) return -1;
        if (left_number > right_number) return 1;
    }
    return 0;
}

QUrl resolveInstallerUrl(const QUrl& release_asset_base_url, const QString& asset_name) {
    if (!release_asset_base_url.isValid() || !isSafeAssetName(asset_name)) return {};
    auto base = release_asset_base_url;
    auto path = base.path();
    if (!path.endsWith(QLatin1Char('/'))) path.append(QLatin1Char('/'));
    path.append(QString::fromLatin1(QUrl::toPercentEncoding(asset_name)));
    base.setPath(path);
    return base;
}

} // namespace creative_suite::updater
