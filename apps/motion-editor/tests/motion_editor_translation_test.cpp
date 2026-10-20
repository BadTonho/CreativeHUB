#include "ui/main_window/main_window.h"
#include "ui/translation_helpers.h"
#include "ui/inspector/trim_paths_inspector.h"
#include <QCheckBox>
#include "ui/dialogs/general_settings_dialog.h"
#include "ui/viewer/composition_viewer.h"
#include "ui/inspector/inspector_widget.h"

#include <creative_suite/translations/language_preference.h>
#include <creative_suite/translations/translation_catalog.h>
#include <creative_suite/translations/translation_runtime.h>

#include <QAction>
#include <QApplication>
#include <QLabel>
#include <QComboBox>
#include <QMenuBar>
#include <QLocale>
#include <QSettings>
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
    QTemporaryDir preference_storage;
    if (!check(translation_storage.isValid() && recovery_storage.isValid() &&
                   preference_storage.isValid(),
               "Could not create isolated translation test directories.")) {
        return 1;
    }
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       preference_storage.path());
    using creative_suite::translations::LanguagePreference;
    using namespace creative_suite::translations;
    QSettings preferences(QSettings::IniFormat, QSettings::UserScope,
                          QStringLiteral("CreativeSuite"),
                          QStringLiteral("LanguagePreferences"));
    preferences.clear();
    preferences.sync();
    if (!check(saveLanguagePreference(QStringLiteral("motion-editor"),
                                     LanguagePreference::PortugueseBrazil),
               "The explicit Motion Studio language preference could not be saved.")) {
        return 1;
    }

    creative_suite::translations::TranslationRuntime runtime;
    const auto portuguese = runtime.installForApplication(
        application, QStringLiteral("motion-editor"),
        QStringLiteral(":/translations/motion-editor/pt_BR.qm"),
        localeForApplicationPreference(QStringLiteral("motion-editor"),
                                       QLocale(QStringLiteral("en_US"))),
        translation_storage.path());
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

    if(!check(motion::ui::MotionUiTranslation::tr("Pen")==QStringLiteral("Caneta")&&
              motion::ui::MotionUiTranslation::tr("Edit Selected Path")==QStringLiteral("Editar caminho selecionado")&&
              motion::ui::MotionUiTranslation::tr("Path Progress (%)")==QStringLiteral("Progresso do caminho (%)")&&
              motion::ui::MotionUiTranslation::tr("Mask Path #%1").arg(42)==QStringLiteral("Caminho da máscara #42"),"Pen/path instructions use Portuguese"))return 1;
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

    {
        motion::ui::GeneralSettingsDialog dialog(false);
        auto* language = dialog.findChild<QComboBox*>(
            QStringLiteral("motion-language-preference-combo"));
        auto* note = dialog.findChild<QLabel*>(
            QStringLiteral("motion-language-preference-note"));
        if (!check(language != nullptr && note != nullptr && language->count() == 3 &&
                       language->currentData().toInt() ==
                           static_cast<int>(LanguagePreference::PortugueseBrazil),
                   "Motion Studio Settings did not restore its language preference.") ||
            !check(note->text().contains(QStringLiteral("próxima")),
                   "Motion Studio Settings does not explain that language applies next launch.")) {
            return 1;
        }
        language->setCurrentIndex(language->findData(
            static_cast<int>(LanguagePreference::English)));
        if (!check(loadLanguagePreference(QStringLiteral("motion-editor")) ==
                       LanguagePreference::English &&
                       translationLocaleFor(localeForApplicationPreference(
                           QStringLiteral("motion-editor"),
                           QLocale(QStringLiteral("pt_BR")))).isEmpty(),
                   "Explicit English did not override a Portuguese system locale.")) {
            return 1;
        }
        language->setCurrentIndex(language->findData(
            static_cast<int>(LanguagePreference::PortugueseBrazil)));
    }

    if (!check(motion::ui::MotionUiTranslation::tr("Scale X (%)") == QStringLiteral("Escala X (%)") &&
                   motion::ui::MotionUiTranslation::tr("Scale Y (%)") == QStringLiteral("Escala Y (%)") &&
                   motion::ui::MotionUiTranslation::tr("Link scale axes") == QStringLiteral("Vincular eixos de escala") &&
                   motion::ui::MotionUiTranslation::tr("Scale X and Y (%)") == QStringLiteral("Escala X e Y (%)"),
               "Scale axis and uniform audio labels use pt-BR.")) return 1;
    if (!check(motion::ui::MotionUiTranslation::tr("New Null") == QStringLiteral("Nova Null") &&
                   motion::ui::MotionUiTranslation::tr("Parent & Link") == QStringLiteral("Pai e vínculo") &&
                   motion::ui::MotionUiTranslation::tr("No parent") == QStringLiteral("Sem pai"),
               "Parenting and Null labels use the bundled pt-BR catalog.")) return 1;
    if (!check(motion::ui::MotionUiTranslation::tr("A future source string") ==
                   QStringLiteral("A future source string"),
               "Missing catalog entries must fall back to English source text.")) {
        return 1;
    }
    if (!check(motion::ui::MotionUiTranslation::tr("Parent: %1").arg("Null [#2]") == QStringLiteral("Pai: Null [#2]") &&
                   motion::ui::MotionUiTranslation::tr("Invalid parent: %1").arg("Null [#2]") == QStringLiteral("Pai inválido: Null [#2]") &&
                   motion::ui::MotionUiTranslation::tr("Layer: %1\nParent & Link: %2").arg("Text [#1]", "Null [#2]") ==
                       QStringLiteral("Camada: Text [#1]\nPai e vínculo: Null [#2]") &&
                   motion::ui::MotionUiTranslation::tr("Drag to choose a parent. Release outside a layer to cancel.") ==
                       QStringLiteral("Arraste para escolher um pai. Solte fora de uma camada para cancelar."),
               "Pick-whip candidates, identities and instructions use pt-BR.")) return 1;
    if (!check(motion::ui::MotionUiTranslation::tr("Anchor X (px)") == QStringLiteral("Âncora X (px)") &&
                   motion::ui::MotionUiTranslation::tr("Anchor Y (px)") == QStringLiteral("Âncora Y (px)") &&
                   motion::ui::MotionUiTranslation::tr("Reposition Anchor Without Moving...") == QStringLiteral("Reposicionar pivô sem mover...") &&
                   motion::ui::MotionUiTranslation::tr("Pixels from the fitted layer center, before editable scale and rotation.") != QStringLiteral("Pixels from the fitted layer center, before editable scale and rotation."),
               "Anchor units, explanation and reposition action use the bundled pt-BR catalog.")) return 1;

    {
        if(!check(motion::ui::MotionUiTranslation::tr("Drop Shadow")==QStringLiteral("Sombra projetada")&&motion::ui::MotionUiTranslation::tr("Shadow Distance (px)")==QStringLiteral("Distância da sombra (px)"),"Drop Shadow uses pt-BR"))return 1;
        motion::ui::TrimPathsInspector trim;
        if(!check(motion::ui::MotionUiTranslation::tr("Trim Paths")==QStringLiteral("Aparar contorno")&&
            trim.findChild<QCheckBox*>("motion-trim-enabled")->text()==QStringLiteral("Ativar aparagem do contorno")&&
            motion::ui::MotionUiTranslation::tr("Trim Paths / Offset (degrees)")==QStringLiteral("Aparar contorno / Deslocamento (graus)"),"Trim Paths labels use pt-BR"))return 1;
        motion::ui::InspectorWidget blend_inspector;
        const auto* modes=blend_inspector.findChild<QComboBox*>(QStringLiteral("motion-blend-mode"));
        if(!check(modes && modes->count()==6 && modes->itemText(1)==QStringLiteral("Multiplicar") && modes->itemText(2)==QStringLiteral("Divisão") &&
            modes->itemText(3)==QStringLiteral("Sobrepor") && modes->itemText(4)==QStringLiteral("Adicionar") && modes->itemText(5)==QStringLiteral("Diferença") &&
            modes->toolTip().contains(QStringLiteral("transparentes")) && motion::ui::MotionUiTranslation::tr("Blend Mode")==QStringLiteral("Modo de mesclagem") &&
            motion::ui::MotionUiTranslation::tr("The blend mode could not be changed. Check the Motion Studio log for details.").startsWith(QStringLiteral("Não foi possível")),"Blend mode labels, instructions and failures use pt-BR."))return 1;
        const auto* matte=blend_inspector.findChild<QComboBox*>(QStringLiteral("motion-track-matte-mode"));
        const auto* matte_source=blend_inspector.findChild<QComboBox*>(QStringLiteral("motion-track-matte-source"));
        if(!check(matte && matte_source && matte->itemText(0)==QStringLiteral("Matte alfa") && matte->itemText(1)==QStringLiteral("Alfa invertido") &&
            matte->itemText(2)==QStringLiteral("Matte de luma") && matte->itemText(3)==QStringLiteral("Luma invertida") &&
            matte_source->toolTip().contains(QStringLiteral("ocultada")) && matte->toolTip().contains(QStringLiteral("fora da fonte")) &&
            motion::ui::MotionUiTranslation::tr("No matte")==QStringLiteral("Sem matte") &&
            motion::ui::MotionUiTranslation::tr("Track Matte")==QStringLiteral("Matte de camada") &&
            motion::ui::MotionUiTranslation::tr("The track matte could not be changed. Check the Motion Studio log for details.").startsWith(QStringLiteral("Não foi possível")),"Track matte labels, instructions and errors use pt-BR."))return 1;
        motion::ui::CompositionViewer viewer;
        if (!check(viewer.toolTip().contains(QStringLiteral("Arraste X, Y ou XY para ajustar a escala da camada. O vínculo do Inspector controla as proporções. O arraste para nos limites positivos; Escape cancela.")) &&
                   motion::ui::MotionUiTranslation::tr("The layer could not be scaled. Check the Motion Studio log for details.") == QStringLiteral("Não foi possível ajustar a escala da camada. Consulte o log do Motion Studio para obter detalhes.") &&
                   viewer.toolTip().startsWith(QStringLiteral("Arraste o pivô para reposicioná-lo sem mover a camada. Pressione Escape para cancelar.")) &&
                   viewer.toolTip().contains(QStringLiteral("Arraste a alça de quatro direções para mover a camada. Pressione Escape para cancelar.")) &&
                   viewer.toolTip().contains(QStringLiteral("Arraste a alça com seta curva para girar a camada. Voltas completas são preservadas. Pressione Escape para cancelar.")) &&
                   motion::ui::MotionUiTranslation::tr("The layer could not be rotated. Check the Motion Studio log for details.") ==
                       QStringLiteral("Não foi possível girar a camada. Consulte o log do Motion Studio para obter detalhes.") &&
                   motion::ui::MotionUiTranslation::tr("The layer could not be moved. Check the Motion Studio log for details.") ==
                       QStringLiteral("Não foi possível mover a camada. Consulte o log do Motion Studio para obter detalhes."),
                   "Pan Behind gesture instructions use pt-BR.") ||
            !check(motion::ui::MotionUiTranslation::tr("The pivot could not be repositioned. Check the Motion Studio log for details.") ==
                   QStringLiteral("Não foi possível reposicionar o pivô. Consulte o log do Motion Studio para obter detalhes."),
                   "Pan Behind technical failure text uses pt-BR.")) return 1;
    }
    const auto english = runtime.installForApplication(
        application, QStringLiteral("motion-editor"),
        QStringLiteral(":/translations/motion-editor/pt_BR.qm"),
        localeForLanguagePreference(LanguagePreference::English,
                                    QLocale(QStringLiteral("pt_BR"))),
        translation_storage.path());
    if (!check(english.locale.isEmpty() && !english.bundled_loaded &&
                   motion::ui::MotionUiTranslation::tr("No composition open") ==
                       QStringLiteral("No composition open"),
               "An unsupported locale must keep Motion Studio in English.")) {
        return 1;
    }

    {
        motion::ui::InspectorWidget blend_inspector;
        const auto* modes=blend_inspector.findChild<QComboBox*>(QStringLiteral("motion-blend-mode"));
        if(!check(modes && modes->itemText(1)==QStringLiteral("Multiply") && modes->itemText(2)==QStringLiteral("Screen") &&
            modes->itemText(3)==QStringLiteral("Overlay") && modes->itemText(4)==QStringLiteral("Add") && modes->itemText(5)==QStringLiteral("Difference") &&
            motion::ui::MotionUiTranslation::tr("Blend Mode")==QStringLiteral("Blend Mode"),"Blend modes fall back to English."))return 1;
        const auto* matte=blend_inspector.findChild<QComboBox*>(QStringLiteral("motion-track-matte-mode"));
        if(!check(matte && matte->itemText(0)==QStringLiteral("Alpha Matte") && matte->itemText(3)==QStringLiteral("Luma Inverted") &&
            motion::ui::MotionUiTranslation::tr("Track Matte")==QStringLiteral("Track Matte"),"Track mattes fall back to English."))return 1;
        motion::ui::CompositionViewer viewer;
        if (!check(viewer.toolTip().contains(QStringLiteral("Drag X, Y or XY to scale the layer. The Inspector link controls proportions. Positive limits stop the drag; Escape cancels.")) &&
                   motion::ui::MotionUiTranslation::tr("The layer could not be scaled. Check the Motion Studio log for details.") == QStringLiteral("The layer could not be scaled. Check the Motion Studio log for details.") &&
                   viewer.toolTip().startsWith(QStringLiteral("Drag the pivot to reposition it without moving the layer. Press Escape to cancel.")) &&
                   viewer.toolTip().contains(QStringLiteral("Drag the four-way handle to move the layer. Press Escape to cancel.")) &&
                   viewer.toolTip().contains(QStringLiteral("Drag the curved-arrow handle to rotate the layer. Full turns are preserved. Press Escape to cancel.")) &&
                   motion::ui::MotionUiTranslation::tr("The layer could not be rotated. Check the Motion Studio log for details.") ==
                       QStringLiteral("The layer could not be rotated. Check the Motion Studio log for details.") &&
                   motion::ui::MotionUiTranslation::tr("The layer could not be moved. Check the Motion Studio log for details.") ==
                       QStringLiteral("The layer could not be moved. Check the Motion Studio log for details."),
                   "Pan Behind instructions use English without a catalog.")) return 1;
    }
    if (!check(motion::ui::MotionUiTranslation::tr("Parent: %1").arg("Null [#2]") == QStringLiteral("Parent: Null [#2]") &&
                   motion::ui::MotionUiTranslation::tr("Invalid parent: %1").arg("Null [#2]") == QStringLiteral("Invalid parent: Null [#2]") &&
                   motion::ui::MotionUiTranslation::tr("Layer: %1\nParent & Link: %2").arg("Text [#1]", "Null [#2]") ==
                       QStringLiteral("Layer: Text [#1]\nParent & Link: Null [#2]") &&
                   motion::ui::MotionUiTranslation::tr("Drag to choose a parent. Release outside a layer to cancel.") ==
                       QStringLiteral("Drag to choose a parent. Release outside a layer to cancel."),
               "Pick-whip labels and instructions retain the English source text.")) return 1;
    if(!check(motion::ui::MotionUiTranslation::tr("Pen")==QStringLiteral("Pen")&&motion::ui::MotionUiTranslation::tr("Remove Animation Keeping Current Frame")==QStringLiteral("Remove Animation Keeping Current Frame"),"Pen/path English fallback"))return 1;
    preferences.clear();
    if(!check(motion::ui::MotionUiTranslation::tr("Trim Paths")==QStringLiteral("Trim Paths"),"Trim Paths English fallback"))return 1;
    if(!check(motion::ui::MotionUiTranslation::tr("Drop Shadow")==QStringLiteral("Drop Shadow"),"Drop Shadow English fallback"))return 1;
    preferences.sync();

    return 0;
}
