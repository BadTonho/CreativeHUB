#include "../src/application/app_launcher.h"
#include "../src/model/app_catalog.h"
#include "../../../cmake/test_support/test_check.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <iostream>

using namespace creative_suite::hub;

static void createExecutable(const QString& path) {
    const bool createdDirectory = QDir().mkpath(QFileInfo(path).absolutePath());
    CS_TEST_CHECK(createdDirectory);
    QFile file(path);
    const bool opened = file.open(QIODevice::WriteOnly);
    CS_TEST_CHECK(opened);
    const auto written = file.write("MZ_DUMMY_BINARY");
    CS_TEST_CHECK(written == 15);
    file.close();
    const bool permissionsSet = file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    CS_TEST_CHECK(permissionsSet);
}

static QString fixtureExecutable(const AppInfo& editor, bool bundle, bool extension) {
    const QString stem = editor.executableName().left(editor.executableName().size() - 4);
    if (bundle) {
        return stem + QStringLiteral(".app/Contents/MacOS/") + stem;
    }
    return extension ? editor.executableName() : stem;
}

static void verifyBuildLayout(const QString& configuration, bool bundle, bool extension,
                              bool editorBundle = false) {
    std::cout << "Checking layout: " << configuration.toStdString() << ", bundle=" << bundle
              << ", extension=" << extension << std::endl;
    QTemporaryDir tree;
    CS_TEST_CHECK(tree.isValid());
    const QString configSuffix = configuration.isEmpty() ? QString() : '/' + configuration;
    const QString hubDirectory = tree.filePath(QStringLiteral("build/apps/hub") + configSuffix
        + (bundle ? QStringLiteral("/creative-suite-hub.app/Contents/MacOS") : QString()));
    const bool hubDirectoryCreated = QDir().mkpath(hubDirectory);
    CS_TEST_CHECK(hubDirectoryCreated);
    AppLauncher launcher(hubDirectory);
    AppCatalog catalog;

    for (const auto& editor : catalog.apps()) {
        const QString editorDirectory = tree.filePath(QStringLiteral("build/apps/") + editor.id());
        const QString relativeExecutable = fixtureExecutable(editor, editorBundle, extension);
        const QString expected = editorDirectory + configSuffix + '/' + relativeExecutable;
        createExecutable(expected);

        // Wrong-depth and alternate-configuration binaries must not win.
        const QString wrongDepth = tree.filePath(QStringLiteral("build/apps/hub/")
            + editor.id() + configSuffix + '/' + relativeExecutable);
        createExecutable(wrongDepth);
        const QString fallbackConfiguration = configuration == QStringLiteral("Release")
            ? QStringLiteral("Debug") : QStringLiteral("Release");
        const QString fallback = editorDirectory + '/' + fallbackConfiguration + '/' + relativeExecutable;
        createExecutable(fallback);
        auto found = launcher.findExecutable(editor.executableName());
        CS_TEST_CHECK(found.has_value());
        CS_TEST_CHECK(QFileInfo(*found).absoluteFilePath() == expected);
        CS_TEST_CHECK(launcher.isInstalled(editor));

        const bool removed = QFile::remove(expected);
        CS_TEST_CHECK(removed);
        found = launcher.findExecutable(editor.executableName());
        CS_TEST_CHECK(found.has_value());
        CS_TEST_CHECK(QFileInfo(*found).absoluteFilePath() == fallback);

        // Co-located and explicitly registered paths retain their priority.
        const QString coLocated = hubDirectory + '/' + relativeExecutable;
        createExecutable(coLocated);
        found = launcher.findExecutable(editor.executableName());
        CS_TEST_CHECK(found.has_value());
        CS_TEST_CHECK(QFileInfo(*found).absoluteFilePath() == coLocated);
        const QString installedDirectory = tree.filePath(QStringLiteral("installed/") + editor.id());
        const QString installed = installedDirectory + '/' + relativeExecutable;
        createExecutable(installed);
        launcher.addSearchPath(installedDirectory);
        launcher.addSearchPath(installedDirectory);
        CS_TEST_CHECK(launcher.searchPaths().count(installedDirectory) == 1);
        found = launcher.findExecutable(editor.executableName());
        CS_TEST_CHECK(found.has_value());
        CS_TEST_CHECK(QFileInfo(*found).absoluteFilePath() == installed);
    }
}

static void verifyBuiltApplications(const QStringList& arguments) {
    // CMake supplies actual producer target paths; no local install registration is used.
    CS_TEST_CHECK(arguments.size() >= 5 && (arguments.size() - 3) % 2 == 0);
    AppLauncher launcher(arguments.at(2));
    AppCatalog catalog;
    for (int index = 3; index < arguments.size(); index += 2) {
        const auto editor = catalog.findApp(arguments.at(index));
        CS_TEST_CHECK(editor.has_value());
        const QFileInfo expected(arguments.at(index + 1));
        CS_TEST_CHECK(expected.isFile() && expected.isExecutable());
        const auto found = launcher.findExecutable(editor->executableName());
        CS_TEST_CHECK(found.has_value());
        CS_TEST_CHECK(QFileInfo(*found).canonicalFilePath() == expected.canonicalFilePath());
        CS_TEST_CHECK(launcher.isInstalled(*editor));
    }
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    if (app.arguments().value(1) == QStringLiteral("--built-apps")) {
        verifyBuiltApplications(app.arguments());
        std::cout << "Hub discovery matches the enabled editor build outputs.\n";
        return 0;
    }

    for (const auto& configuration : QStringList{QString(), QStringLiteral("Release"),
            QStringLiteral("Debug"), QStringLiteral("RelWithDebInfo"), QStringLiteral("MinSizeRel"),
            QStringLiteral("CustomConfig")}) {
        verifyBuildLayout(configuration, false, true);
#ifdef Q_OS_WIN
        // Windows cannot mark extensionless files executable. Still exercise
        // root resolution from a Hub bundle with native executable fixtures.
        verifyBuildLayout(configuration, true, true);
#else
        verifyBuildLayout(configuration, false, false);
        verifyBuildLayout(configuration, true, false, true);
#endif
    }

    AppLauncher launcher;
    launcher.setSearchPaths({});
    CS_TEST_CHECK(!launcher.findExecutable(QString()).has_value());
    CS_TEST_CHECK(!launcher.findExecutable(QStringLiteral("missing-app.exe")).has_value());
    AppCatalog catalog;
    for (const auto& editor : catalog.apps()) {
        CS_TEST_CHECK(!launcher.isInstalled(editor));
    }
    QTemporaryDir tempDir;
    CS_TEST_CHECK(tempDir.isValid());
    const QString testExeName = QStringLiteral("dummy_test_app.exe");
    const QString testExePath = tempDir.filePath(testExeName);
    createExecutable(testExePath);
    launcher.setSearchPaths({tempDir.path()});
    const auto foundPath = launcher.findExecutable(testExeName);
    CS_TEST_CHECK(foundPath.has_value());
    CS_TEST_CHECK(*foundPath == testExePath);
    // A directory bearing the executable name is not an application.
    const bool directoryCreated = QDir().mkpath(tempDir.filePath(QStringLiteral("directory.exe")));
    CS_TEST_CHECK(directoryCreated);
    CS_TEST_CHECK(!launcher.findExecutable(QStringLiteral("directory.exe")).has_value());

    std::cout << "All AppLauncher tests passed successfully.\n";
    return 0;
}
