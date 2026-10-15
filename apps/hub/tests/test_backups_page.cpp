#include "../src/ui/pages/backups_page.h"
#include "../src/model/backup_manager.h"
#include "../src/model/activity_manager.h"
#include "../src/diagnostics/hub_logger.h"
#include <creative_suite/translations/translation_runtime.h>
#include "../../../cmake/test_support/test_check.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLocale>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <iostream>

using namespace creative_suite::hub;

static void writeBytes(const QString& path, const QByteArray& bytes) {
    const bool directoryCreated = QDir().mkpath(QFileInfo(path).absolutePath());
    CS_TEST_CHECK(directoryCreated);
    QFile file(path);
    const bool opened = file.open(QIODevice::WriteOnly);
    CS_TEST_CHECK(opened);
    const auto written = file.write(bytes);
    CS_TEST_CHECK(written == bytes.size());
}

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("hub-backups-page-test"));
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir root;
    CS_TEST_CHECK(root.isValid());
    creative_suite::translations::TranslationRuntime translations;
    const auto translationResult = translations.installForApplication(
        app, QStringLiteral("hub"), QStringLiteral(":/translations/hub/pt_BR.qm"),
        QLocale(QStringLiteral("pt_BR")), root.path());
    CS_TEST_CHECK(translationResult.bundled_loaded);
    ActivityManager::instance().setStoragePath(root.filePath("activities.json"));
    HubLogger::instance().setLogFilePath(root.filePath("hub.log"));
    auto& manager = BackupManager::instance();
    manager.setBackupDirectory(root.filePath("vault"));
    const QStringList sources{root.filePath("owner & one/same.data"), root.filePath("owner two/same.data")};
    QStringList snapshots;
    for (const auto& source : sources) {
        writeBytes(source, "project bytes");
        QString output;
        QString error;
        const bool created = manager.createBackup(source, &output, &error);
        CS_TEST_CHECK(created);
        snapshots.append(output);
    }
    const QString legacy = QDir(manager.backupDirectory()).filePath("same_backup_20250101_120000.data");
    writeBytes(legacy, "legacy bytes");

    BackupsPage page;
    page.refreshBackupsList();
    const auto labels = page.findChildren<QLabel*>();
    for (qsizetype index = 0; index < sources.size(); ++index) {
        const QString identity = QFileInfo(sources.at(index)).canonicalFilePath();
        bool found = false;
        for (const auto* label : labels) {
            if (label->text() == identity && label->toolTip().contains(snapshots.at(index))) {
                CS_TEST_CHECK(label->textFormat() == Qt::PlainText);
                found = true;
            }
        }
        CS_TEST_CHECK(found);
    }
    bool legacyIdentified = false;
    for (const auto* label : labels) {
        if (label->text().contains(QStringLiteral("origem desconhecida")) && label->toolTip().contains(legacy))
            legacyIdentified = true;
    }
    CS_TEST_CHECK(legacyIdentified);
    std::cout << "Backups page distinguishes source paths and unknown legacy origins.\n";
    return 0;
}
