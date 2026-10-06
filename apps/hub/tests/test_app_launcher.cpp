#include "../src/application/app_launcher.h"
#include <cassert>
#include <iostream>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>

using namespace creative_suite::hub;

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    AppLauncher launcher;

    // With empty executable name
    assert(!launcher.findExecutable(QString()).has_value());

    // With temporary directory
    QTemporaryDir tempDir;
    assert(tempDir.isValid());

    const QString testExeName = QStringLiteral("dummy_test_app.exe");
    const QString testExePath = tempDir.filePath(testExeName);

    QFile file(testExePath);
    assert(file.open(QIODevice::WriteOnly));
    file.write("MZ_DUMMY_BINARY");
    file.close();

    // Check permissions on Windows/Linux
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);

    launcher.addSearchPath(tempDir.path());
    auto foundPath = launcher.findExecutable(testExeName);
    assert(foundPath.has_value());
    assert(*foundPath == testExePath);

    std::cout << "All AppLauncher tests passed successfully.\n";
    return 0;
}
