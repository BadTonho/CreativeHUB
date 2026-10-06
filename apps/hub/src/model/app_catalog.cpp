#include "app_catalog.h"

#include <algorithm>

namespace creative_suite::hub {

AppCatalog::AppCatalog(QObject* parent)
    : QObject(parent)
{
    populateDefaultApps();
}

void AppCatalog::populateDefaultApps() {
    m_apps.clear();

    m_apps.emplace_back(
        QStringLiteral("video-editor"),
        QStringLiteral("Video Editor"),
        QStringLiteral("Edição audiovisual e pós-produção"),
        QStringLiteral("Um editor de vídeo completo e ágil, com linha do tempo multitrilha, mixagem de áudio, aceleração por GPU opcional, transições, sobreposição de texto e exportação para formatos comuns."),
        QStringLiteral(""),
        QStringLiteral(CREATIVE_SUITE_VERSION_VIDEO_EDITOR),
        QStringLiteral("creative-suite-video-editor.exe"),
        QStringLiteral(":/icons/video-editor.png"),
        AppStatus::NotInstalled,
        QStringList{
            QStringLiteral("Linha do tempo multitrilha com suporte fluido a vídeo e áudio"),
            QStringLiteral("Efeitos de transição, transformações de camada e keyframes"),
            QStringLiteral("Mixagem e monitoramento de canais de áudio com scrubbing responsivo"),
            QStringLiteral("Aceleração opcional por GPU na linha do tempo e exportação em até 4K UHD"),
            QStringLiteral("Salvamento automático, histórico resiliente e recuperação de projetos")
        },
        QStringLiteral(".csp (Creative Suite Project)")
    );

    m_apps.emplace_back(
        QStringLiteral("image-editor"),
        QStringLiteral("Image Editor"),
        QStringLiteral("Edição de imagens e gráficos em camadas"),
        QStringLiteral("Aplicativo de edição raster focada em manipulação de camadas, grupos, ferramentas de seleção inteligente, desenho de formas geométricas e suporte a camadas de imagens vinculadas."),
        QStringLiteral(""),
        QStringLiteral(CREATIVE_SUITE_VERSION_IMAGE_EDITOR),
        QStringLiteral("creative-suite-image-editor.exe"),
        QStringLiteral(":/icons/image-editor.png"),
        AppStatus::NotInstalled,
        QStringList{
            QStringLiteral("Pintura digital e edição raster organizada em camadas e grupos"),
            QStringLiteral("Máscaras ajustáveis com suporte a imagens vinculadas"),
            QStringLiteral("Ferramentas de seleção de objetos, formas vetoriais e texto"),
            QStringLiteral("Histórico bounded de desfazer/refazer com alta performance"),
            QStringLiteral("Exportação otimizada em formatos PNG e JPEG de alta qualidade")
        },
        QStringLiteral(".cimg (Creative Suite Image Document)")
    );

    m_apps.emplace_back(
        QStringLiteral("motion-editor"),
        QStringLiteral("Motion Studio"),
        QStringLiteral("Motion design e animação gráfica"),
        QStringLiteral("Ambiente especializado para motion design e composição avançada, combinando animação por keyframes, curvas Bezier precisas (Graph Editor), máscaras e exportação acelerada."),
        QStringLiteral(""),
        QStringLiteral(CREATIVE_SUITE_VERSION_MOTION_EDITOR),
        QStringLiteral("creative-suite-motion-editor.exe"),
        QStringLiteral(":/icons/motion-studio.png"),
        AppStatus::NotInstalled,
        QStringList{
            QStringLiteral("Composição dinâmica em camadas com suporte a mídias e formas"),
            QStringLiteral("Editor de curvas Bezier (Graph Editor) para controle fino de interpolação"),
            QStringLiteral("Efeitos encadeados de desfoque gaussiano e ajustes de cor"),
            QStringLiteral("Exportação de vídeo renderizado com alta fidelidade visual"),
            QStringLiteral("Estrutura independente e nativa compartilhando o núcleo da suíte")
        },
        QStringLiteral(".motion (Creative Suite Motion File)")
    );

    emit catalogReloaded();
}

std::optional<AppInfo> AppCatalog::findApp(const QString& id) const {
    auto it = std::find_if(m_apps.begin(), m_apps.end(), [&id](const AppInfo& app) {
        return app.id() == id;
    });
    if (it != m_apps.end()) {
        return *it;
    }
    return std::nullopt;
}

void AppCatalog::updateAppStatus(const QString& id, AppStatus status) {
    for (auto& app : m_apps) {
        if (app.id() == id) {
            app.setStatus(status);
            emit appUpdated(id);
            return;
        }
    }
}

void AppCatalog::updateAppVersion(const QString& id, const QString& installedVersion) {
    for (auto& app : m_apps) {
        if (app.id() == id) {
            app.setInstalledVersion(installedVersion);
            emit appUpdated(id);
            return;
        }
    }
}

void AppCatalog::updateLatestVersion(const QString& id, const QString& latestVersion) {
    for (auto& app : m_apps) {
        if (app.id() == id) {
            app.setLatestVersion(latestVersion);
            emit appUpdated(id);
            return;
        }
    }
}

} // namespace creative_suite::hub
