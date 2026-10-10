#include "model/changelog_reader.h"
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include "../../../cmake/test_support/test_check.h"
#include <iostream>

using namespace creative_suite::hub;

void createTestFile(const QString& path, const QString& content) {
    QFile file(path);
    bool ok = file.open(QIODevice::WriteOnly | QIODevice::Text);
    CS_TEST_CHECK(ok);
    file.write(content.toUtf8());
    file.close();
}

int main() {
    QTemporaryDir tempDir;
    CS_TEST_CHECK(tempDir.isValid());

    const QString changelogRoot = tempDir.path() + QStringLiteral("/Changelog");
    QDir().mkpath(changelogRoot + QStringLiteral("/video-editor"));
    QDir().mkpath(changelogRoot + QStringLiteral("/image-editor"));

    ChangelogReader reader(changelogRoot);
    CS_TEST_CHECK(reader.basePath() == changelogRoot);

    // 1. Initially empty directory
    CS_TEST_CHECK(!reader.hasChangelogs(QStringLiteral("video-editor")));
    CS_TEST_CHECK(reader.availableVersions(QStringLiteral("video-editor")).isEmpty());
    CS_TEST_CHECK(reader.loadChangelog(QStringLiteral("video-editor")).isEmpty());

    // 2. Populate with files, including .gitkeep and non-md files
    createTestFile(changelogRoot + QStringLiteral("/video-editor/.gitkeep"), QStringLiteral(""));
    createTestFile(changelogRoot + QStringLiteral("/video-editor/readme.txt"), QStringLiteral("Ignore me"));
    createTestFile(changelogRoot + QStringLiteral("/video-editor/0.1.0.md"), QStringLiteral("# Video Editor 0.1.0\nInitial release"));
    createTestFile(changelogRoot + QStringLiteral("/video-editor/0.2.0.md"), QStringLiteral("# Video Editor 0.2.0\nAdded transitions"));
    createTestFile(changelogRoot + QStringLiteral("/video-editor/0.10.0.md"), QStringLiteral("# Video Editor 0.10.0\nPerformance boost"));
    createTestFile(changelogRoot + QStringLiteral("/video-editor/1.0.0.md"), QStringLiteral("# Video Editor 1.0.0\nStable production ready"));

    createTestFile(changelogRoot + QStringLiteral("/image-editor/0.1.0.md"), QStringLiteral("# Image Editor 0.1.0\nLayer support"));

    // 3. Test availableVersions with semantic version sorting (descending)
    CS_TEST_CHECK(reader.hasChangelogs(QStringLiteral("video-editor")));
    const QStringList versions = reader.availableVersions(QStringLiteral("video-editor"));
    CS_TEST_CHECK(versions.size() == 4);
    CS_TEST_CHECK(versions[0] == QStringLiteral("1.0.0"));
    CS_TEST_CHECK(versions[1] == QStringLiteral("0.10.0"));
    CS_TEST_CHECK(versions[2] == QStringLiteral("0.2.0"));
    CS_TEST_CHECK(versions[3] == QStringLiteral("0.1.0"));

    // 4. Test loading specific version
    const QString v020 = reader.loadChangelog(QStringLiteral("video-editor"), QStringLiteral("0.2.0"));
    CS_TEST_CHECK(v020.contains(QStringLiteral("Added transitions")));

    // 5. Test loading version with 'v' prefix
    const QString v020Prefixed = reader.loadChangelog(QStringLiteral("video-editor"), QStringLiteral("v0.2.0"));
    CS_TEST_CHECK(v020Prefixed.contains(QStringLiteral("Added transitions")));

    // 6. Test default version loading (picks newest: 1.0.0)
    const QString newest = reader.loadChangelog(QStringLiteral("video-editor"));
    CS_TEST_CHECK(newest.contains(QStringLiteral("Stable production ready")));

    // 7. Test non-existent app and non-existent version
    CS_TEST_CHECK(!reader.hasChangelogs(QStringLiteral("motion-editor")));
    CS_TEST_CHECK(reader.availableVersions(QStringLiteral("motion-editor")).isEmpty());
    CS_TEST_CHECK(reader.loadChangelog(QStringLiteral("video-editor"), QStringLiteral("9.9.9")).isEmpty());

    // 8. Test single version app
    CS_TEST_CHECK(reader.hasChangelogs(QStringLiteral("image-editor")));
    const QStringList imgVersions = reader.availableVersions(QStringLiteral("image-editor"));
    CS_TEST_CHECK(imgVersions.size() == 1);
    CS_TEST_CHECK(imgVersions[0] == QStringLiteral("0.1.0"));
    CS_TEST_CHECK(reader.loadChangelog(QStringLiteral("image-editor")).contains(QStringLiteral("Layer support")));

    // 9. Default changelog path check
    const QString defaultPath = ChangelogReader::defaultChangelogPath();
    CS_TEST_CHECK(!defaultPath.isEmpty());

    std::cout << "All ChangelogReader tests passed successfully!" << std::endl;
    return 0;
}
