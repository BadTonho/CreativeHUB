#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <creative_suite/translations/language_preference.h>

class QComboBox;

namespace creative_suite::hub {

class SettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit SettingsPage(QWidget* parent = nullptr, bool enableStartupIntegration = true);

private slots:
    void onBrowseInstallPath();
    void onOpenLogFolder();

private:
    void setupUi();

    QLineEdit* m_installPathEdit{nullptr};
    QCheckBox* m_autostartCheck{nullptr};
    QCheckBox* m_notificationsCheck{nullptr};
    bool m_enableStartupIntegration{true};
    QLineEdit* m_logPathEdit{nullptr};
    QComboBox* m_languageCombo{nullptr};
    QLabel* m_languageNote{nullptr};
    creative_suite::translations::LanguagePreference m_savedLanguagePreference{
        creative_suite::translations::LanguagePreference::System};
};

} // namespace creative_suite::hub
