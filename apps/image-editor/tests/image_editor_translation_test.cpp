#include "translation_helpers.h"

#include <creative_suite/translations/translation_runtime.h>
#include <creative_suite/translations/language_preference.h>

#include <test_check.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QLocale>
#include <QTemporaryDir>
#include <QFileInfo>
#include <QSettings>

#include <creative_suite/translations/translation_catalog.h>

using creative_suite::translations::TranslationRuntime;

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    QTemporaryDir storage;
    QTemporaryDir preferences_directory;
    CS_TEST_CHECK(storage.isValid());
    CS_TEST_CHECK(preferences_directory.isValid());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       preferences_directory.path());
    using creative_suite::translations::LanguagePreference;
    CS_TEST_CHECK(creative_suite::translations::saveLanguagePreference(
        QStringLiteral("image-editor"), LanguagePreference::PortugueseBrazil));

    {
        const QString installed_translation =
            creative_suite::translations::translationFilePath(
                storage.path(), QStringLiteral("image-editor"),
                QStringLiteral("pt_BR"));
        CS_TEST_CHECK(QDir().mkpath(QFileInfo(installed_translation).absolutePath()));
        QFile downloaded_fixture(QString::fromLocal8Bit(
            IMAGE_EDITOR_TRANSLATION_TEST_DOWNLOADED_QM));
        CS_TEST_CHECK(downloaded_fixture.open(QIODevice::ReadOnly));
        QFile installed_file(installed_translation);
        CS_TEST_CHECK(installed_file.open(QIODevice::WriteOnly));
        CS_TEST_CHECK(installed_file.write(downloaded_fixture.readAll()) > 0);
        installed_file.close();

        TranslationRuntime runtime;
        const auto result = runtime.installForApplication(
            application, QStringLiteral("image-editor"),
            QStringLiteral(":/translations/image-editor/pt_BR.qm"),
            creative_suite::translations::localeForApplicationPreference(
                QStringLiteral("image-editor"), QLocale(QStringLiteral("en_US"))),
            storage.path());
        CS_TEST_CHECK(result.locale == QStringLiteral("pt_BR"));
        CS_TEST_CHECK(result.bundled_loaded);
        CS_TEST_CHECK(result.downloaded_loaded);
        CS_TEST_CHECK(QCoreApplication::translate("ImageEditorUI", "Open Image") ==
                      QStringLiteral("Imagem obtida"));
        CS_TEST_CHECK(QCoreApplication::translate("ImageEditorUI", "Create New Canvas") ==
                      QStringLiteral("Criar nova tela"));
        CS_TEST_CHECK(QCoreApplication::translate("ImageEditorUI", "Shortcut for %1")
                          .arg(QStringLiteral("Move")) == QStringLiteral("Atalho para Move"));
        CS_TEST_CHECK(image_editor::ui::translateMessage(
                          QStringLiteral("The paint brush diameter must be between 1 and 1024 pixels.")) ==
                      QStringLiteral("O diâmetro do pincel deve estar entre 1 e 1024 pixels."));
        CS_TEST_CHECK(QCoreApplication::translate("ImageEditorUI", "Text not in catalog") ==
                      QStringLiteral("Text not in catalog"));
    }

    QTemporaryDir bundled_only_storage;
    CS_TEST_CHECK(bundled_only_storage.isValid());
    {
        TranslationRuntime runtime;
        const auto result = runtime.installForApplication(
            application, QStringLiteral("image-editor"),
            QStringLiteral(":/translations/image-editor/pt_BR.qm"),
            QLocale(QStringLiteral("pt_BR")), bundled_only_storage.path());
        CS_TEST_CHECK(result.bundled_loaded);
        CS_TEST_CHECK(!result.downloaded_loaded);
        CS_TEST_CHECK(QCoreApplication::translate("ImageEditorUI", "Outline") == QStringLiteral("Contorno"));
        CS_TEST_CHECK(QCoreApplication::translate("ImageEditorUI", "Remove Outline") == QStringLiteral("Remover contorno"));
        CS_TEST_CHECK(QCoreApplication::translate("ImageEditorUI", "Open Image") ==
                      QStringLiteral("Abrir imagem"));
    }

    CS_TEST_CHECK(QCoreApplication::translate("ImageEditorUI", "Open Image") ==
                  QStringLiteral("Open Image"));
    {
        TranslationRuntime runtime;
        const auto result = runtime.installForApplication(
            application, QStringLiteral("image-editor"),
            QStringLiteral(":/translations/image-editor/pt_BR.qm"),
            QLocale(QStringLiteral("fr_FR")), storage.path());
        CS_TEST_CHECK(result.locale.isEmpty());
        CS_TEST_CHECK(!result.bundled_loaded);
        CS_TEST_CHECK(QCoreApplication::translate("ImageEditorUI", "Open Image") ==
                      QStringLiteral("Open Image"));
    }
    QSettings preferences(QSettings::IniFormat, QSettings::UserScope,
                          QStringLiteral("CreativeSuite"),
                          QStringLiteral("LanguagePreferences"));
    preferences.clear();
    preferences.sync();
    return 0;
}
