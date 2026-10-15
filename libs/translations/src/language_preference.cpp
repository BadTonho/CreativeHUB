#include <creative_suite/translations/language_preference.h>

#include <creative_suite/translations/translation_catalog.h>

#include <QSettings>

#include <array>

namespace creative_suite::translations {
namespace {

constexpr std::array<const char*, 4> kApplicationIds{
    "hub", "video-editor", "image-editor", "motion-editor"};

bool isKnownApplication(const QString& application_id)
{
    for (const auto* id : kApplicationIds) {
        if (application_id == QLatin1String(id)) return true;
    }
    return false;
}

QString preferenceKey(const QString& application_id)
{
    return QStringLiteral("applications/%1/language").arg(application_id);
}

void setError(QString* error, const QString& value)
{
    if (error != nullptr) *error = value;
}

} // namespace

QString languagePreferenceValue(LanguagePreference preference)
{
    switch (preference) {
    case LanguagePreference::System: return QStringLiteral("system");
    case LanguagePreference::English: return QStringLiteral("en");
    case LanguagePreference::PortugueseBrazil: return QStringLiteral("pt_BR");
    }
    return QStringLiteral("system");
}

LanguagePreference languagePreferenceFromValue(const QString& value)
{
    if (value == QStringLiteral("en")) return LanguagePreference::English;
    if (value == QStringLiteral("pt_BR")) return LanguagePreference::PortugueseBrazil;
    return LanguagePreference::System;
}

LanguagePreference loadLanguagePreference(const QString& application_id)
{
    if (!isKnownApplication(application_id)) return LanguagePreference::System;
    QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                       QStringLiteral("CreativeSuite"),
                       QStringLiteral("LanguagePreferences"));
    const QString value = settings.value(preferenceKey(application_id),
                                         QStringLiteral("system")).toString();
    return languagePreferenceFromValue(value);
}

bool saveLanguagePreference(const QString& application_id,
                            LanguagePreference preference,
                            QString* error)
{
    if (!isKnownApplication(application_id)) {
        setError(error, QStringLiteral("The application ID is not supported."));
        return false;
    }
    if (preference != LanguagePreference::System &&
        preference != LanguagePreference::English &&
        preference != LanguagePreference::PortugueseBrazil) {
        setError(error, QStringLiteral("The language preference is invalid."));
        return false;
    }

    QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                       QStringLiteral("CreativeSuite"),
                       QStringLiteral("LanguagePreferences"));
    settings.setValue(preferenceKey(application_id), languagePreferenceValue(preference));
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        setError(error, QStringLiteral("Could not save the language preference (settings status %1).")
            .arg(static_cast<int>(settings.status())));
        return false;
    }
    if (error != nullptr) error->clear();
    return true;
}

QLocale localeForLanguagePreference(LanguagePreference preference,
                                    const QLocale& system_locale)
{
    switch (preference) {
    case LanguagePreference::System:
        return system_locale;
    case LanguagePreference::English:
        return QLocale(QLocale::English, QLocale::UnitedStates);
    case LanguagePreference::PortugueseBrazil:
        return QLocale(QStringLiteral("pt_BR"));
    }
    return system_locale;
}

QLocale localeForApplicationPreference(const QString& application_id,
                                       const QLocale& system_locale)
{
    return localeForLanguagePreference(
        loadLanguagePreference(application_id), system_locale);
}

bool anyApplicationUsesTranslationLocale(const QString& locale,
                                         const QLocale& system_locale)
{
    if (locale.isEmpty()) return false;
    for (const auto* id : kApplicationIds) {
        const QLocale effective_locale = localeForApplicationPreference(
            QString::fromLatin1(id), system_locale);
        if (translationLocaleFor(effective_locale) == locale) return true;
    }
    return false;
}

} // namespace creative_suite::translations
