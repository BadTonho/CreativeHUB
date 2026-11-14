#include "../src/application/startup_integration.h"
#include "../../../cmake/test_support/test_check.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <iostream>

namespace startup = creative_suite::hub::startup_integration;

namespace {
QString readAll(const QString& path) {
    QFile file(path);
    const bool opened = file.open(QIODevice::ReadOnly);
    CS_TEST_CHECK(opened);
    return QString::fromUtf8(file.readAll());
}
} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    // Desktop Entry: plain paths are quoted; special characters are escaped per the specification.
    const QString plain = startup::desktopEntryContents(QStringLiteral("/opt/suite/creative-suite-hub"));
    CS_TEST_CHECK(plain.startsWith(QStringLiteral("[Desktop Entry]\n")));
    CS_TEST_CHECK(plain.contains(QStringLiteral("Exec=\"/opt/suite/creative-suite-hub\"\n")));
    CS_TEST_CHECK(plain.contains(QStringLiteral("Terminal=false\n")));
    const QString tricky = startup::desktopEntryContents(QStringLiteral("/home/a b/$x\"y`z%1"));
    CS_TEST_CHECK(tricky.contains(QStringLiteral("Exec=\"/home/a b/\\\\$x\\\\\"y\\\\`z%%1\"\n")));

    // LaunchAgent: label, RunAtLoad, and XML-escaped program path.
    const QString plist = startup::launchAgentContents(QStringLiteral("/Apps/R&D <Hub>/creative-suite-hub"));
    CS_TEST_CHECK(plist.contains(QStringLiteral("<string>creative-suite.hub</string>")));
    CS_TEST_CHECK(plist.contains(QStringLiteral("<key>RunAtLoad</key>\n    <true/>")));
    CS_TEST_CHECK(plist.contains(QStringLiteral("<string>/Apps/R&amp;D &lt;Hub&gt;/creative-suite-hub</string>")));

    // File round trip: create in a new nested folder, replace atomically, remove, remove again.
    QTemporaryDir root;
    CS_TEST_CHECK(root.isValid());
    const QString target = QDir(root.path()).filePath(QStringLiteral("config/autostart/hub.desktop"));
    QString error;
    CS_TEST_CHECK(startup::writeOrRemoveFile(target, plain, true, &error));
    CS_TEST_CHECK(readAll(target) == plain);
    CS_TEST_CHECK(startup::writeOrRemoveFile(target, tricky, true, &error));
    CS_TEST_CHECK(readAll(target) == tricky);
    CS_TEST_CHECK(startup::writeOrRemoveFile(target, QString(), false, &error));
    CS_TEST_CHECK(!QFile::exists(target));
    CS_TEST_CHECK(startup::writeOrRemoveFile(target, QString(), false, &error));

    // Failures report a cause instead of silently succeeding.
    error.clear();
    CS_TEST_CHECK(!startup::writeOrRemoveFile(QString(), plain, true, &error) && !error.isEmpty());
    const QString blocker = QDir(root.path()).filePath(QStringLiteral("blocker"));
    CS_TEST_CHECK(startup::writeOrRemoveFile(blocker, QStringLiteral("file"), true, &error));
    error.clear();
    CS_TEST_CHECK(!startup::writeOrRemoveFile(QDir(blocker).filePath(QStringLiteral("x/hub.desktop")),
                                              plain, true, &error) && !error.isEmpty());

    CS_TEST_CHECK(startup::isSupported());
    std::cout << "Startup integration tests passed\n";
    return 0;
}
