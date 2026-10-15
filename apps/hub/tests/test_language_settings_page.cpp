#include "../src/ui/pages/settings_page.h"

#include <creative_suite/translations/language_preference.h>
#include <creative_suite/translations/translation_runtime.h>
#include "../../../cmake/test_support/test_check.h"

#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QSettings>
#include <QCoreApplication>
#include <QTemporaryDir>

int main(int argc, char* argv[])
{
    QTemporaryDir app_data_root;
    if (!app_data_root.isValid()) return 1;
    qputenv("APPDATA", app_data_root.path().toLocal8Bit());
    qputenv("LOCALAPPDATA", app_data_root.path().toLocal8Bit());
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("CreativeSuite"));
    QCoreApplication::setApplicationName(QStringLiteral("HubLanguageSettingsTest"));
    using namespace creative_suite::hub;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QTemporaryDir settings_root;
    QTemporaryDir catalog_root;
    CS_TEST_CHECK(settings_root.isValid());
    CS_TEST_CHECK(catalog_root.isValid());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       settings_root.path());
    QSettings preferences(QSettings::IniFormat, QSettings::UserScope,
                          QStringLiteral("CreativeSuite"),
                          QStringLiteral("LanguagePreferences"));
    preferences.clear();
    preferences.sync();

    using creative_suite::translations::LanguagePreference;
    using namespace creative_suite::translations;
    CS_TEST_CHECK(saveLanguagePreference(QStringLiteral("hub"),
                                         LanguagePreference::PortugueseBrazil));
    TranslationRuntime runtime;
    const auto loaded = runtime.installForApplication(
        application, QStringLiteral("hub"), QStringLiteral(":/translations/hub/pt_BR.qm"),
        localeForApplicationPreference(QStringLiteral("hub"), QLocale(QStringLiteral("en_US"))),
        catalog_root.path());
    CS_TEST_CHECK(loaded.bundled_loaded);

    SettingsPage page;
    auto* language = page.findChild<QComboBox*>(QStringLiteral("languagePreferenceComboBox"));
    auto* note = page.findChild<QLabel*>(QStringLiteral("languagePreferenceNote"));
    CS_TEST_CHECK(language != nullptr);
    CS_TEST_CHECK(note != nullptr);
    CS_TEST_CHECK(language->count() == 3);
    CS_TEST_CHECK(language->currentData().toInt() ==
                  static_cast<int>(LanguagePreference::PortugueseBrazil));
    const int english_index = language->findData(static_cast<int>(LanguagePreference::English));
    CS_TEST_CHECK(english_index >= 0);
    language->setCurrentIndex(english_index);
    CS_TEST_CHECK(loadLanguagePreference(QStringLiteral("hub")) == LanguagePreference::English);
    CS_TEST_CHECK(note->text().contains(QStringLiteral("próxima")));
    CS_TEST_CHECK(loadLanguagePreference(QStringLiteral("video-editor")) == LanguagePreference::System);
    return 0;
}
