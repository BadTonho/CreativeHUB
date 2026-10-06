#include "../src/model/recent_projects_manager.h"
#include <cassert>
#include <iostream>
#include <QTemporaryDir>
#include <QFile>
#include <QThread>

using namespace creative_suite::hub;

int main() {
    QTemporaryDir tempDir;
    assert(tempDir.isValid());

    const QString customJsonPath = tempDir.filePath(QStringLiteral("test_recent_projects.json"));

    // 1. App extension detection
    assert(RecentProjectsManager::detectAppForFile(QStringLiteral("my_video.csp")) == QStringLiteral("video-editor"));
    assert(RecentProjectsManager::detectAppForFile(QStringLiteral("project.csve")) == QStringLiteral("video-editor"));
    assert(RecentProjectsManager::detectAppForFile(QStringLiteral("photo.cimg")) == QStringLiteral("image-editor"));
    assert(RecentProjectsManager::detectAppForFile(QStringLiteral("banner.csie")) == QStringLiteral("image-editor"));
    assert(RecentProjectsManager::detectAppForFile(QStringLiteral("animation.motion")) == QStringLiteral("motion-editor"));
    assert(RecentProjectsManager::detectAppForFile(QStringLiteral("title.csme")) == QStringLiteral("motion-editor"));
    assert(RecentProjectsManager::detectAppForFile(QStringLiteral("document.txt")).isEmpty());

    // 2. Manager initialization & persistence
    {
        RecentProjectsManager manager(customJsonPath);
        assert(manager.projects().empty());

        // Add dummy files
        const QString file1 = tempDir.filePath(QStringLiteral("project_alpha.csp"));
        const QString file2 = tempDir.filePath(QStringLiteral("photo_beta.cimg"));
        const QString file3 = tempDir.filePath(QStringLiteral("anim_gamma.motion"));

        {
            QFile f1(file1);
            f1.open(QIODevice::WriteOnly);
            f1.write("dummy data");
            f1.close();
        }

        manager.addOrUpdateProject(file1, QStringLiteral("video-editor"));
        assert(manager.projects().size() == 1);
        assert(manager.projects()[0].filePath == file1);
        assert(manager.projects()[0].name == QStringLiteral("project_alpha"));
        assert(manager.projects()[0].appId == QStringLiteral("video-editor"));
        assert(manager.projects()[0].fileSizeBytes > 0);

        QThread::msleep(10);
        manager.addOrUpdateProject(file2, QStringLiteral("image-editor"));
        assert(manager.projects().size() == 2);
        // file2 is most recent, so it should be at index 0
        assert(manager.projects()[0].filePath == file2);
        assert(manager.projects()[1].filePath == file1);

        QThread::msleep(10);
        // Re-adding file1 moves it to index 0
        manager.addOrUpdateProject(file1);
        assert(manager.projects().size() == 2);
        assert(manager.projects()[0].filePath == file1);
        assert(manager.projects()[1].filePath == file2);

        QThread::msleep(10);
        // Add file3
        manager.addOrUpdateProject(file3, QStringLiteral("motion-editor"));
        assert(manager.projects().size() == 3);
        assert(manager.projects()[0].filePath == file3);

        // Remove project
        manager.removeProject(file2);
        assert(manager.projects().size() == 2);
        for (const auto& p : manager.projects()) {
            assert(p.filePath != file2);
        }
    }

    // 3. Reload from file to ensure persistence
    {
        RecentProjectsManager manager(customJsonPath);
        assert(manager.projects().size() == 2);
        assert(manager.projects()[0].name == QStringLiteral("anim_gamma"));
        assert(manager.projects()[1].name == QStringLiteral("project_alpha"));

        manager.clear();
        assert(manager.projects().empty());
    }

    // 4. Verify clear persisted
    {
        RecentProjectsManager manager(customJsonPath);
        assert(manager.projects().empty());
    }

    std::cout << "All RecentProjectsManager tests passed successfully.\n";
    return 0;
}
