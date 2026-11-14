#include <creative_suite/app_locator/app_locator.h>

#include "../../../cmake/test_support/test_check.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>

#include <iostream>

namespace app_locator = creative_suite::app_locator;

namespace {

QString executableFileName(const QString& base) {
#if defined(Q_OS_WIN)
    return base + QStringLiteral(".exe");
#else
    return base;
#endif
}

QString makeExecutable(const QString& directory, const QString& base) {
    QDir().mkpath(directory);
    const QString path = QDir(directory).filePath(executableFileName(base));
    QFile file(path);
    const bool opened = file.open(QIODevice::WriteOnly);
    CS_TEST_CHECK(opened);
    file.write("stub");
    file.close();
    file.setPermissions(file.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeUser);
    return QFileInfo(path).absoluteFilePath();
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("CreativeSuiteTest"));
    QCoreApplication::setApplicationName(QStringLiteral("app-locator-test"));

    QTemporaryDir root;
    CS_TEST_CHECK(root.isValid());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, QDir(root.path()).filePath("settings"));
    QSettings().clear();

    // Names and keys.
    CS_TEST_CHECK(app_locator::applicationBaseName("image-editor") == "creative-suite-image-editor");
    CS_TEST_CHECK(app_locator::applicationSettingsKey("image-editor") == "applications/image_editor_executable");
    CS_TEST_CHECK(app_locator::applicationSettingsKey("motion-editor") == "applications/motion_editor_executable");
    CS_TEST_CHECK(app_locator::executableCandidateNames(QString()).isEmpty());
    const auto from_exe = app_locator::executableCandidateNames("tool.exe");
    CS_TEST_CHECK(from_exe == (QStringList{"tool.exe", "tool", "tool.app/Contents/MacOS/tool"}));
    const auto from_bare = app_locator::executableCandidateNames("tool");
    CS_TEST_CHECK(from_bare == (QStringList{"tool", "tool.exe", "tool.app/Contents/MacOS/tool"}));

    // Development output tree: build/apps/<id>/Release, as launched from a sibling application.
    const QString apps = QDir(root.path()).filePath("apps");
    const QString video_dir = QDir(apps).filePath("video-editor/Release");
    QDir().mkpath(video_dir);
    CS_TEST_CHECK(app_locator::findApplicationExecutable("image-editor", video_dir).isEmpty() ||
                  !app_locator::findApplicationExecutable("image-editor", video_dir).startsWith(apps));

    const QString image_release = makeExecutable(QDir(apps).filePath("image-editor/Release"),
                                                 "creative-suite-image-editor");
    CS_TEST_CHECK(app_locator::findApplicationExecutable("image-editor", video_dir) == image_release);

    // An executable in the application's own directory wins over the development tree.
    const QString local = makeExecutable(video_dir, "creative-suite-image-editor");
    CS_TEST_CHECK(app_locator::findApplicationExecutable("image-editor", video_dir) == local);

    // A user-chosen executable wins over everything, but only while it still exists.
    const QString chosen = makeExecutable(QDir(root.path()).filePath("chosen"), "custom-image-editor");
    QSettings().setValue(app_locator::applicationSettingsKey("image-editor"), chosen);
    CS_TEST_CHECK(app_locator::findApplicationExecutable("image-editor", video_dir) == chosen);
    QFile::remove(chosen);
    CS_TEST_CHECK(app_locator::findApplicationExecutable("image-editor", video_dir) == local);

    // The Motion Studio ID resolves independently of the Image Editor.
    const QString motion = makeExecutable(QDir(apps).filePath("motion-editor/Debug"),
                                          "creative-suite-motion-editor");
    CS_TEST_CHECK(app_locator::findApplicationExecutable("motion-editor", video_dir) == motion);

    std::cout << "App locator tests passed\n";
    return 0;
}
