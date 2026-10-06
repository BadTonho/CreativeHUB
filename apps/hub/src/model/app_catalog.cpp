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
        QStringLiteral("Editor de vídeo multitrilha, efeitos visuais, áudio profissional e exportação."),
        QStringLiteral(""),
        QStringLiteral("0.1.6"),
        QStringLiteral("creative-suite-video-editor.exe"),
        QStringLiteral(":/icons/video-editor.png"),
        AppStatus::NotInstalled
    );

    m_apps.emplace_back(
        QStringLiteral("image-editor"),
        QStringLiteral("Image Editor"),
        QStringLiteral("Edição de imagens e gráficos em camadas"),
        QStringLiteral("Edição raster em camadas, máscaras, desenho de formas e formatos compatíveis."),
        QStringLiteral(""),
        QStringLiteral("0.1.3"),
        QStringLiteral("creative-suite-image-editor.exe"),
        QStringLiteral(":/icons/image-editor.png"),
        AppStatus::NotInstalled
    );

    m_apps.emplace_back(
        QStringLiteral("motion-editor"),
        QStringLiteral("Motion Studio"),
        QStringLiteral("Motion design e animação gráfica"),
        QStringLiteral("Composição em camadas, animação por curvas Bezier, keyframes e efeitos."),
        QStringLiteral(""),
        QStringLiteral("0.1.1"),
        QStringLiteral("creative-suite-motion-editor.exe"),
        QStringLiteral(":/icons/motion-studio.png"),
        AppStatus::NotInstalled
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

} // namespace creative_suite::hub
