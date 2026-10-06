#pragma once

#include <QString>
#include <QStringList>

namespace creative_suite::hub {

/**
 * @brief Utility for reading and discovering release changelog markdown files
 *        from the structured Changelog/<app-id>/<version>.md directory tree.
 */
class ChangelogReader {
public:
    explicit ChangelogReader(QString customBasePath = QString());

    [[nodiscard]] QString basePath() const;
    void setBasePath(const QString& path);

    [[nodiscard]] QString changelogDirForApp(const QString& appId) const;
    [[nodiscard]] QStringList availableVersions(const QString& appId) const;
    [[nodiscard]] QString loadChangelog(const QString& appId, const QString& version = QString()) const;
    [[nodiscard]] bool hasChangelogs(const QString& appId) const;

    [[nodiscard]] static QString defaultChangelogPath();

private:
    QString m_basePath;
};

} // namespace creative_suite::hub
