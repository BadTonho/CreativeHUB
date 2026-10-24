#include "ui/effects/effects_catalog.h"

#include <creative_suite/translations/translation_runtime.h>
#include <creative_suite/translations/language_preference.h>

#include <QCoreApplication>
#include <QLocale>
#include <QSettings>
#include <QTemporaryDir>

#include <cstdio>

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    QTemporaryDir storage;
    QTemporaryDir preferences_directory;
    if (!storage.isValid() || !preferences_directory.isValid()) {
        std::fprintf(stderr, "Could not create isolated translation storage.\n");
        return 1;
    }
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       preferences_directory.path());
    using creative_suite::translations::LanguagePreference;
    using namespace creative_suite::translations;
    if (!saveLanguagePreference(QStringLiteral("video-editor"),
                                LanguagePreference::PortugueseBrazil)) {
        std::fprintf(stderr, "Could not save the explicit Video Editor language preference.\n");
        return 1;
    }

    TranslationRuntime runtime;
    const auto result = runtime.installForApplication(
        application, QStringLiteral("video-editor"),
        QStringLiteral(":/translations/video-editor/pt_BR.qm"),
        localeForApplicationPreference(QStringLiteral("video-editor"),
                                       QLocale(QStringLiteral("en_US"))),
        storage.path());
    if (result.locale != QStringLiteral("pt_BR") || !result.bundled_loaded) {
        std::fprintf(stderr, "The bundled pt-BR Video Editor catalog was not loaded.\n");
        return 1;
    }

    if (QCoreApplication::translate("RendererSelection", "Graphics renderer") !=
            QStringLiteral("Renderizador gr\u00e1fico")) {
        std::fprintf(stderr, "The startup renderer chooser did not use its pt-BR catalog entry.\n");
        return 1;
    }
    if (QCoreApplication::translate("RenderWorkspace", "Render Settings") !=
            QStringLiteral("Configurações de renderização")) {
        std::fprintf(stderr, "The Render workspace did not use its pt-BR catalog entry.\n");
        return 1;
    }
    if (QCoreApplication::translate("EditWorkspace", "Adjust %1").arg(100) !=
            QStringLiteral("Ajustar 100")) {
        std::fprintf(stderr, "A translated placeholder was not preserved.\n");
        return 1;
    }
    if (QCoreApplication::translate(
            "RenderWorkspace",
            "%n render job(s) failed. See the queue rows and application log for details.",
            nullptr, 2) != QStringLiteral(
                "Falharam 2 tarefas de renderização. Consulte as linhas da fila e o log do aplicativo para ver os detalhes.")) {
        std::fprintf(stderr, "The Render queue plural form was not loaded.\n");
        return 1;
    }
    if (::effects::translateCatalogLabel(QStringLiteral("Audio")) !=
            QStringLiteral("Áudio")) {
        std::fprintf(stderr, "Dynamic Effects catalog labels were not translated.\n");
        return 1;
    }
    if (QCoreApplication::translate("ColorWorkspace", "RGB Parade") !=
            QStringLiteral("Parada RGB") ||
        QCoreApplication::translate("QObject", "Curves") !=
            QStringLiteral("Curvas") ||
        QCoreApplication::translate("ColorWorkspace", "Hue vs Saturation") !=
            QStringLiteral("Matiz vs Saturação") ||
        QCoreApplication::translate("ColorWorkspace", "Enable tonal qualifier") !=
            QStringLiteral("Ativar qualificador tonal") ||
        QCoreApplication::translate("ColorWorkspace", "Matte preview") !=
            QStringLiteral("Prévia da máscara") ||
        QCoreApplication::translate("ColorWorkspace", "Compare Version") !=
            QStringLiteral("Comparar vers\u00e3o") ||
        QCoreApplication::translate("ColorWorkspace", "Grade Versions") !=
            QStringLiteral("Versões de correção") ||
        QCoreApplication::translate("ColorWorkspace", "Copy Grade") !=
            QStringLiteral("Copiar correção") ||
        QCoreApplication::translate("ColorWorkspace", "Offset") !=
            QStringLiteral("Deslocamento") ||
        QCoreApplication::translate("ColorWorkspace", "Compare") !=
            QStringLiteral("Comparar") ||
        QCoreApplication::translate("ColorWorkspace", "Off") !=
            QStringLiteral("Desativado") ||
        QCoreApplication::translate("ColorWorkspace", "Split-screen") !=
            QStringLiteral("Tela dividida") ||
        QCoreApplication::translate("ColorWorkspace", "Wipe") !=
            QStringLiteral("Cortina") ||
        QCoreApplication::translate("ColorWorkspace", "Wipe position") !=
            QStringLiteral("Posição da cortina") ||
        QCoreApplication::translate("ColorWorkspace", "No comparison reference selected") !=
            QStringLiteral("Nenhuma refer\u00eancia de compara\u00e7\u00e3o selecionada") ||
        QCoreApplication::translate("ColorWorkspace", "Comparison unavailable") !=
            QStringLiteral("Comparação indisponível") ||
        QCoreApplication::translate("MainWindow",
            "Gallery comparison is unavailable; the Viewer remains unchanged.") !=
            QStringLiteral("A comparação da Galeria está indisponível; o Viewer permanece inalterado.") ||
        ::effects::translateCatalogLabel(QStringLiteral("Lift red")) !=
            QStringLiteral("Eleva\u00e7\u00e3o vermelha")) {
        std::fprintf(stderr, "Primary Grade and scope labels were not translated.\n");
        return 1;
    }
    if (QCoreApplication::translate("RenderWorkspace", "A future source string") !=
            QStringLiteral("A future source string")) {
        std::fprintf(stderr, "Missing catalog entries must fall back to English source text.\n");
        return 1;
    }

    const auto english_result = runtime.installForApplication(
        application, QStringLiteral("video-editor"),
        QStringLiteral(":/translations/video-editor/pt_BR.qm"),
        localeForLanguagePreference(LanguagePreference::English,
                                    QLocale(QStringLiteral("pt_BR"))),
        storage.path());
    if (!english_result.locale.isEmpty() || english_result.bundled_loaded ||
        QCoreApplication::translate("RenderWorkspace", "Render Settings") !=
            QStringLiteral("Render Settings")) {
        std::fprintf(stderr, "An unsupported locale must use the English source strings.\n");
        return 1;
    }
    QSettings preferences(QSettings::IniFormat, QSettings::UserScope,
                          QStringLiteral("CreativeSuite"),
                          QStringLiteral("LanguagePreferences"));
    preferences.clear();
    preferences.sync();
    return 0;
}
