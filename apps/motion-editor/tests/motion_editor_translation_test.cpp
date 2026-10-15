#include "ui/main_window/main_window.h"
#include "ui/translation_helpers.h"

#include <creative_suite/translations/translation_runtime.h>

#include <QAction>
#include <QApplication>
#include <QLabel>
#include <QMenuBar>
#include <QLocale>
#include <QTemporaryDir>

#include <filesystem>
#include <iostream>
#include <string>

namespace {

std::filesystem::path filesystemPath(const QString& path)
{
    const auto encoded = path.toUtf8();
    const auto* first = reinterpret_cast<const char8_t*>(encoded.constData());
    return std::filesystem::path(std::u8string(first, first + encoded.size()));
}

bool check(bool condition, const char* message)
{
    if (condition) return true;
    std::cerr << message << '\n';
    return false;
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    QTemporaryDir translation_storage;
    QTemporaryDir recovery_storage;
    if (!check(translation_storage.isValid() && recovery_storage.isValid(),
               "Could not create isolated translation test directories.")) {
        return 1;
    }

    creative_suite::translations::TranslationRuntime runtime;
    const auto portuguese = runtime.installForApplication(
        application, QStringLiteral("motion-editor"),
        QStringLiteral(":/translations/motion-editor/pt_BR.qm"),
        QLocale(QStringLiteral("pt_PT")), translation_storage.path());
    if (!check(portuguese.locale == QStringLiteral("pt_BR") && portuguese.bundled_loaded &&
                   !portuguese.downloaded_loaded,
               "The bundled Motion Studio pt-BR catalog was not selected offline.") ||
        !check(motion::ui::MotionUiTranslation::tr("No composition open") ==
                   QStringLiteral("Nenhuma composição aberta"),
               "Motion Studio UI text did not use the bundled pt-BR catalog.") ||
        !check(motion::ui::MotionUiTranslation::tr("Selected layer: %1").arg(
                   QStringLiteral("Texto 1")) == QStringLiteral("Camada selecionada: Texto 1"),
               "A translated placeholder was not preserved.")) {
        return 1;
    }

    {
        motion::ui::MainWindow window(nullptr, filesystemPath(recovery_storage.path()),
                                      "translation-test");
        const auto menus = window.menuBar()->actions();
        const auto* empty_state = window.findChild<QLabel*>(
            QStringLiteral("motion-empty-state"));
        if (!check(!menus.empty() && menus.front()->text() == QStringLiteral("Arquivo"),
                   "The Motion Studio main window was not translated before construction.") ||
            !check(empty_state != nullptr &&
                       empty_state->text() == QStringLiteral("Nenhuma composição aberta"),
                   "The Motion Studio empty-state label was not translated.")) {
            return 1;
        }
    }

    if (!check(motion::ui::MotionUiTranslation::tr("A future source string") ==
                   QStringLiteral("A future source string"),
               "Missing catalog entries must fall back to English source text.")) {
        return 1;
    }

    const auto english = runtime.installForApplication(
        application, QStringLiteral("motion-editor"),
        QStringLiteral(":/translations/motion-editor/pt_BR.qm"),
        QLocale(QStringLiteral("en_US")), translation_storage.path());
    if (!check(english.locale.isEmpty() && !english.bundled_loaded &&
                   motion::ui::MotionUiTranslation::tr("No composition open") ==
                       QStringLiteral("No composition open"),
               "An unsupported locale must keep Motion Studio in English.")) {
        return 1;
    }

    return 0;
}
