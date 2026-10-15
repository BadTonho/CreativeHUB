#pragma once

#include <QLocale>
#include <QString>

namespace creative_suite::translations {

enum class LanguagePreference {
    System,
    English,
    PortugueseBrazil,
};

// Preferences are stored per user in one suite-wide settings namespace, keyed
// by the stable application ID. Missing and invalid values mean System.
[[nodiscard]] LanguagePreference loadLanguagePreference(
    const QString& application_id);
[[nodiscard]] bool saveLanguagePreference(
    const QString& application_id,
    LanguagePreference preference,
    QString* error = nullptr);

[[nodiscard]] QString languagePreferenceValue(LanguagePreference preference);
[[nodiscard]] LanguagePreference languagePreferenceFromValue(const QString& value);
[[nodiscard]] QLocale localeForLanguagePreference(
    LanguagePreference preference,
    const QLocale& system_locale = QLocale::system());
[[nodiscard]] QLocale localeForApplicationPreference(
    const QString& application_id,
    const QLocale& system_locale = QLocale::system());

// Returns true when at least one supported application resolves to the given
// catalog locale under its saved preference and the current system locale.
[[nodiscard]] bool anyApplicationUsesTranslationLocale(
    const QString& locale,
    const QLocale& system_locale = QLocale::system());

} // namespace creative_suite::translations
