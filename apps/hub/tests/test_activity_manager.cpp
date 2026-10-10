#include "../src/model/activity_manager.h"
#include "../../../cmake/test_support/test_check.h"
#include <iostream>
#include <QTemporaryDir>
#include <QThread>

using namespace creative_suite::hub;

int main() {
    QTemporaryDir tempDir;
    CS_TEST_CHECK(tempDir.isValid());

    const QString customJsonPath = tempDir.filePath(QStringLiteral("test_activities.json"));

    // 1. Creation and initial state
    {
        ActivityManager manager(customJsonPath);
        CS_TEST_CHECK(manager.activities().empty());
        CS_TEST_CHECK(manager.unreadCount() == 0);

        // Add activities
        manager.addActivity(QStringLiteral("Atualização Disponível"),
                            QStringLiteral("Versão 0.2.0 encontrada"),
                            QStringLiteral("update"));
        CS_TEST_CHECK(manager.activities().size() == 1);
        CS_TEST_CHECK(manager.unreadCount() == 1);
        CS_TEST_CHECK(manager.activities()[0].title == QStringLiteral("Atualização Disponível"));
        CS_TEST_CHECK(manager.activities()[0].category == QStringLiteral("update"));
        CS_TEST_CHECK(!manager.activities()[0].isRead);

        QThread::msleep(10);
        manager.addActivity(QStringLiteral("Projeto Aberto"),
                            QStringLiteral("trailer.csp foi iniciado"),
                            QStringLiteral("project"));
        CS_TEST_CHECK(manager.activities().size() == 2);
        CS_TEST_CHECK(manager.unreadCount() == 2);
        // Newest should be index 0
        CS_TEST_CHECK(manager.activities()[0].title == QStringLiteral("Projeto Aberto"));
        CS_TEST_CHECK(manager.activities()[1].title == QStringLiteral("Atualização Disponível"));

        // Mark all as read
        manager.markAllAsRead();
        CS_TEST_CHECK(manager.unreadCount() == 0);
        CS_TEST_CHECK(manager.activities()[0].isRead);
        CS_TEST_CHECK(manager.activities()[1].isRead);
    }

    // 2. Persistence reload
    {
        ActivityManager manager(customJsonPath);
        CS_TEST_CHECK(manager.activities().size() == 2);
        CS_TEST_CHECK(manager.unreadCount() == 0);
        CS_TEST_CHECK(manager.activities()[0].title == QStringLiteral("Projeto Aberto"));

        // Add a 3rd unread activity
        manager.addActivity(QStringLiteral("Backup Criado"),
                            QStringLiteral("Cópia de segurança salva"),
                            QStringLiteral("backup"));
        CS_TEST_CHECK(manager.activities().size() == 3);
        CS_TEST_CHECK(manager.unreadCount() == 1);

        // Clear all
        manager.clear();
        CS_TEST_CHECK(manager.activities().empty());
        CS_TEST_CHECK(manager.unreadCount() == 0);
    }

    // 3. Persistence after clear
    {
        ActivityManager manager(customJsonPath);
        CS_TEST_CHECK(manager.activities().empty());
        CS_TEST_CHECK(manager.unreadCount() == 0);
    }

    std::cout << "All ActivityManager tests passed successfully.\n";
    return 0;
}
