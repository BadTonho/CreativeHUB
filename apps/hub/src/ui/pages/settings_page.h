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

private:
    void setupUi();
    void refreshCacheSize();

    QLineEdit* m_installPathEdit{nullptr};
    QCheckBox* m_autostartCheck{nullptr};
    QCheckBox* m_notificationsCheck{nullptr};
    QLineEdit* m_logPathEdit{nullptr};
    QLabel* m_cacheSizeLabel{nullptr};
    QLabel* m_cacheStatusNote{nullptr};
};

} // namespace creative_suite::hub
