#include "../src/model/activity_manager.h"
#include <cassert>
#include <iostream>
#include <QTemporaryDir>
#include <QThread>

using namespace creative_suite::hub;

int main() {
    QTemporaryDir tempDir;
    assert(tempDir.isValid());

    const QString customJsonPath = tempDir.filePath(QStringLiteral("test_activities.json"));

    // 1. Creation and initial state
    {
        ActivityManager manager(customJsonPath);
        assert(manager.activities().empty());
        assert(manager.unreadCount() == 0);

        // Add activities
        manager.addActivity(QStringLiteral("Atualização Disponível"),
                            QStringLiteral("Versão 0.2.0 encontrada"),
                            QStringLiteral("update"));
        assert(manager.activities().size() == 1);
        assert(manager.unreadCount() == 1);
        assert(manager.activities()[0].title == QStringLiteral("Atualização Disponível"));
        assert(manager.activities()[0].category == QStringLiteral("update"));
        assert(!manager.activities()[0].isRead);

        QThread::msleep(10);
        manager.addActivity(QStringLiteral("Projeto Aberto"),
                            QStringLiteral("trailer.csp foi iniciado"),
                            QStringLiteral("project"));
        assert(manager.activities().size() == 2);
        assert(manager.unreadCount() == 2);
        // Newest should be index 0
        assert(manager.activities()[0].title == QStringLiteral("Projeto Aberto"));
        assert(manager.activities()[1].title == QStringLiteral("Atualização Disponível"));

        // Mark all as read
        manager.markAllAsRead();
        assert(manager.unreadCount() == 0);
        assert(manager.activities()[0].isRead);
        assert(manager.activities()[1].isRead);
    }

    // 2. Persistence reload
    {
        ActivityManager manager(customJsonPath);
        assert(manager.activities().size() == 2);
        assert(manager.unreadCount() == 0);
        assert(manager.activities()[0].title == QStringLiteral("Projeto Aberto"));

        // Add a 3rd unread activity
        manager.addActivity(QStringLiteral("Backup Criado"),
                            QStringLiteral("Cópia de segurança salva"),
                            QStringLiteral("backup"));
        assert(manager.activities().size() == 3);
        assert(manager.unreadCount() == 1);

        // Clear all
        manager.clear();
        assert(manager.activities().empty());
        assert(manager.unreadCount() == 0);
    }

    // 3. Persistence after clear
    {
        ActivityManager manager(customJsonPath);
        assert(manager.activities().empty());
        assert(manager.unreadCount() == 0);
    }

    std::cout << "All ActivityManager tests passed successfully.\n";
    return 0;
}
