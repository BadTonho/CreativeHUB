#include "ui/effects/effects_catalog.h"

#include <creative_suite/translations/translation_runtime.h>

#include <QCoreApplication>
#include <QLocale>
#include <QTemporaryDir>

#include <cstdio>

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    QTemporaryDir storage;
    if (!storage.isValid()) {
        std::fprintf(stderr, "Could not create isolated translation storage.\n");
        return 1;
    }

    creative_suite::translations::TranslationRuntime runtime;
    const auto result = runtime.installForApplication(
        application, QStringLiteral("video-editor"),
        QStringLiteral(":/translations/video-editor/pt_BR.qm"),
        QLocale(QStringLiteral("pt_PT")), storage.path());
    if (result.locale != QStringLiteral("pt_BR") || !result.bundled_loaded) {
        std::fprintf(stderr, "The bundled pt-BR Video Editor catalog was not loaded.\n");
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
    if (QCoreApplication::translate("RenderWorkspace", "A future source string") !=
            QStringLiteral("A future source string")) {
        std::fprintf(stderr, "Missing catalog entries must fall back to English source text.\n");
        return 1;
    }

    const auto english_result = runtime.installForApplication(
        application, QStringLiteral("video-editor"),
        QStringLiteral(":/translations/video-editor/pt_BR.qm"),
        QLocale(QStringLiteral("en_US")), storage.path());
    if (!english_result.locale.isEmpty() || english_result.bundled_loaded ||
        QCoreApplication::translate("RenderWorkspace", "Render Settings") !=
            QStringLiteral("Render Settings")) {
        std::fprintf(stderr, "An unsupported locale must use the English source strings.\n");
        return 1;
    }
    return 0;
}
