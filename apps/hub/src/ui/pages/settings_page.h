#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>

namespace creative_suite::hub {

class SettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit SettingsPage(QWidget* parent = nullptr);

private slots:
    void onBrowseInstallPath();
    void onOpenLogFolder();

private:
    void setupUi();

    QLineEdit* m_installPathEdit{nullptr};
    QCheckBox* m_autostartCheck{nullptr};
    QCheckBox* m_notificationsCheck{nullptr};
    QLineEdit* m_logPathEdit{nullptr};
};

} // namespace creative_suite::hub
