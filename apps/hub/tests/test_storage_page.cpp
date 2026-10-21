#include "../src/ui/pages/storage_page.h"
#include "../src/model/storage_manager.h"
#include "../src/model/activity_manager.h"
#include "../src/diagnostics/hub_logger.h"
#include <creative_suite/translations/translation_runtime.h>
#include "../../../cmake/test_support/test_check.h"

#include <QApplication>
#include <QAbstractButton>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QMessageBox>
#include <QLocale>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QElapsedTimer>
#include <QThread>
#include <iostream>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#endif

using namespace creative_suite::hub;

static void writeBytes(const QString& path, int size) {
    const bool created = QDir().mkpath(QFileInfo(path).absolutePath());
    CS_TEST_CHECK(created);
    QFile file(path);
    const bool opened = file.open(QIODevice::WriteOnly);
    CS_TEST_CHECK(opened);
    CS_TEST_CHECK(file.write(QByteArray(size, 'c')) == size);
}

static void clearCache(StoragePage& page, bool accept) {
    QTimer::singleShot(0, &page, [&page, accept] {
        const auto dialogs = page.findChildren<QMessageBox*>();
        CS_TEST_CHECK(dialogs.size() == 1);
        auto* dialog = dialogs.front();
        for (auto* button : dialog->buttons()) {
            if (dialog->buttonRole(button) == (accept ? QMessageBox::AcceptRole : QMessageBox::RejectRole)) {
                button->click();
                return;
            }
        }
        CS_TEST_CHECK(false);
    });
    const bool invoked = QMetaObject::invokeMethod(&page, "onClearCache", Qt::DirectConnection);
    CS_TEST_CHECK(invoked);
}

static bool hasLabel(const StoragePage& page, const QString& text) {
    for (auto* label : page.findChildren<QLabel*>()) {
        if (label->text().contains(text)) return true;
    }
    return false;
}

static bool waitForLabelText(const StoragePage& page, const QString& object_name,
                             const QString& expected) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 5000) {
        auto* label = page.findChild<QLabel*>(object_name);
        if (label && label->text() == expected) return true;
        QCoreApplication::processEvents();
        QThread::msleep(10);
    }
    return false;
}

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    application.setApplicationName("hub-storage-page-test");
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir root;
    CS_TEST_CHECK(root.isValid());
    creative_suite::translations::TranslationRuntime translations;
    const auto translationResult = translations.installForApplication(
        application, QStringLiteral("hub"), QStringLiteral(":/translations/hub/pt_BR.qm"),
        QLocale(QStringLiteral("pt_BR")), root.path());
    CS_TEST_CHECK(translationResult.bundled_loaded);
    HubLogger::instance().setLogFilePath(root.filePath("hub.log"));
    ActivityManager::instance().setStoragePath(root.filePath("activities.json"));
    ActivityManager::instance().clear();
    HubLogger::instance().clear();

    const QString cache = root.filePath("cache");
    const QString blocked = root.filePath("cache/nested/blocked.dat");
    writeBytes(blocked, 2000);
    writeBytes(root.filePath("cache/removable.dat"), 1000);
#ifdef Q_OS_WIN
    // Deny FILE_SHARE_DELETE, reproducing a real Windows cache-removal failure.
    const HANDLE lease = CreateFileW(reinterpret_cast<LPCWSTR>(blocked.utf16()),
        GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    CS_TEST_CHECK(lease != INVALID_HANDLE_VALUE);
    const QStringList paths{cache};
#else
    // Invalid cache roots are a deterministic failure independent of Unix UID/ACLs.
    const QStringList paths{cache, root.filePath("invalid-root.dat")};
    writeBytes(paths.back(), 2000);
#endif
    StoragePage page(paths);
    clearCache(page, false);
    CS_TEST_CHECK(QFileInfo::exists(blocked));
    CS_TEST_CHECK(ActivityManager::instance().activities().empty());
    CS_TEST_CHECK(HubLogger::instance().entries().empty());
    clearCache(page, true);
    CS_TEST_CHECK(!hasLabel(page, "com sucesso"));
    CS_TEST_CHECK(!ActivityManager::instance().activities().empty());
    CS_TEST_CHECK(ActivityManager::instance().activities().front().title != "Cache Liberado");
    bool errorLogged = false;
    for (const auto& entry : HubLogger::instance().entries()) {
        if (entry.level == LogLevel::Error && entry.subsystem == "StorageManager" &&
            !entry.context.isEmpty() && entry.message.contains("code")) errorLogged = true;
    }
    CS_TEST_CHECK(errorLogged);
#ifdef Q_OS_WIN
    CS_TEST_CHECK(QFileInfo::exists(blocked));
    CS_TEST_CHECK(!QFileInfo::exists(root.filePath("cache/removable.dat")));
    CS_TEST_CHECK(hasLabel(page, "Limpeza parcial"));
    CS_TEST_CHECK(hasLabel(page, "Foram liberados " + StorageManager::formatBytes(1000)));
    CS_TEST_CHECK(hasLabel(page, StorageManager::formatBytes(2000) + " permanecem"));
    CS_TEST_CHECK(ActivityManager::instance().activities().front().description.contains(
        "Foram liberados " + StorageManager::formatBytes(1000)));
    clearCache(page, true); // Total failure now that only the blocked file remains.
    CS_TEST_CHECK(hasLabel(page, "Falha na limpeza"));
    CS_TEST_CHECK(hasLabel(page, "Foram liberados 0 B"));
    CS_TEST_CHECK(ActivityManager::instance().activities().front().title == "Falha na limpeza do cache");
    CS_TEST_CHECK(CloseHandle(lease));
    clearCache(page, true);
    CS_TEST_CHECK(hasLabel(page, "Cache limpo com sucesso"));
    CS_TEST_CHECK(hasLabel(page, "Foram liberados " + StorageManager::formatBytes(2000)));
    CS_TEST_CHECK(ActivityManager::instance().activities().front().title == "Cache Liberado");
#else
    CS_TEST_CHECK(hasLabel(page, "não foi possível medir todos os arquivos"));
#endif
    StoragePage cleanPage(QStringList{cache});
    writeBytes(root.filePath("cache/normal.dat"), 1000);
    clearCache(cleanPage, true);
    CS_TEST_CHECK(hasLabel(cleanPage, "Cache limpo com sucesso"));
    CS_TEST_CHECK(hasLabel(cleanPage, "Foram liberados " + StorageManager::formatBytes(1000)));
    CS_TEST_CHECK(waitForLabelText(cleanPage, QStringLiteral("storageTotalSizeLabel"),
                                   StorageManager::formatBytes(0))); // Async overview refreshed after removal.
    const auto activityCount = ActivityManager::instance().activities().size();
    const auto logCount = HubLogger::instance().entries().size();
    clearCache(cleanPage, false);
    CS_TEST_CHECK(ActivityManager::instance().activities().size() == activityCount);
    CS_TEST_CHECK(HubLogger::instance().entries().size() == logCount);
    writeBytes(root.filePath("cache/empty-file.dat"), 0);
    clearCache(cleanPage, true);
    CS_TEST_CHECK(hasLabel(cleanPage, "Cache limpo com sucesso"));
    CS_TEST_CHECK(hasLabel(cleanPage, "Foram liberados 0 B"));
    CS_TEST_CHECK(ActivityManager::instance().activities().front().title == "Cache Liberado");
    clearCache(cleanPage, true);
    CS_TEST_CHECK(hasLabel(cleanPage, "já estava vazio"));
    CS_TEST_CHECK(ActivityManager::instance().activities().front().title == "Cache já vazio");
    std::cout << "StoragePage cache failure feedback passed.\n";
}
