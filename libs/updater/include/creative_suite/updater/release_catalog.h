#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QUrl>
#include <optional>
#include <vector>

namespace creative_suite::updater {

struct ReleaseEntry final {
    QString app_id;
    QString version;
    QString installer_asset;
    qint64 size_bytes{0};
    QByteArray sha256_hex;
    QString release_notes;
};

struct ReleaseCatalog final {
    int schema_version{0};
    QString suite_version;
    std::vector<ReleaseEntry> applications;

    [[nodiscard]] const ReleaseEntry* find(const QString& app_id) const noexcept;
    [[nodiscard]] static std::optional<ReleaseCatalog> fromJson(
        const QByteArray& bytes,
        QString* error = nullptr);
};

[[nodiscard]] std::optional<int> compareVersions(
    const QString& left,
    const QString& right) noexcept;
[[nodiscard]] QUrl resolveInstallerUrl(
    const QUrl& release_asset_base_url,
    const QString& asset_name);

} // namespace creative_suite::updater
