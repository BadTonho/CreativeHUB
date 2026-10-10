#include "../src/application/app_launcher.h"
#include "../../../cmake/test_support/test_check.h"
#include <iostream>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>

using namespace creative_suite::hub;

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    AppLauncher launcher;

    // With empty executable name
    CS_TEST_CHECK(!launcher.findExecutable(QString()).has_value());

    // With temporary directory
    QTemporaryDir tempDir;
    CS_TEST_CHECK(tempDir.isValid());

    const QString testExeName = QStringLiteral("dummy_test_app.exe");
    const QString testExePath = tempDir.filePath(testExeName);

    QFile file(testExePath);
    const bool opened = file.open(QIODevice::WriteOnly);
    CS_TEST_CHECK(opened);
    file.write("MZ_DUMMY_BINARY");
    file.close();

    // Check permissions on Windows/Linux
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);

    launcher.addSearchPath(tempDir.path());
    auto foundPath = launcher.findExecutable(testExeName);
    CS_TEST_CHECK(foundPath.has_value());
    CS_TEST_CHECK(*foundPath == testExePath);

    std::cout << "All AppLauncher tests passed successfully.\n";
    return 0;
}
