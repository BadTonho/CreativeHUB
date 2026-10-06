#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>

namespace creative_suite::hub {

class SettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit SettingsPage(QWidget* parent = nullptr);

private slots:
    void onBrowseInstallPath();
    void onOpenLogFolder();
    void onClearCache();
    void onOpenCacheFolder();
    void onBrowseBackupPath();
    void onOpenBackupFolder();

private:
    void setupUi();
    void refreshCacheSize();
    void refreshBackupStats();

    QLineEdit* m_installPathEdit{nullptr};
    QCheckBox* m_autostartCheck{nullptr};
    QCheckBox* m_notificationsCheck{nullptr};
    QLineEdit* m_logPathEdit{nullptr};
    QLabel* m_cacheSizeLabel{nullptr};
    QLabel* m_cacheStatusNote{nullptr};
    QLineEdit* m_backupPathEdit{nullptr};
    QLabel* m_backupStatsLabel{nullptr};
};

} // namespace creative_suite::hub
