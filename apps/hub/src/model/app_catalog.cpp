#include "app_catalog.h"

#include <QCoreApplication>
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
        QCoreApplication::translate("AppCatalog", "Video editing and post-production"),
        QCoreApplication::translate("AppCatalog", "A fast, full-featured video editor with a multitrack timeline, audio mixing, optional GPU acceleration, transitions, text overlays, and export to common formats."),
        QStringLiteral(""),
        QStringLiteral(CREATIVE_SUITE_VERSION_VIDEO_EDITOR),
        QStringLiteral("creative-suite-video-editor.exe"),
        QStringLiteral(":/icons/video-editor.png"),
        AppStatus::NotInstalled,
        QStringList{
            QCoreApplication::translate("AppCatalog", "Smooth multitrack video and audio timeline"),
            QCoreApplication::translate("AppCatalog", "Transition effects, layer transforms, and keyframes"),
            QCoreApplication::translate("AppCatalog", "Audio mixing and channel monitoring with responsive scrubbing"),
            QCoreApplication::translate("AppCatalog", "Optional GPU acceleration for the timeline and exports up to 4K UHD"),
            QCoreApplication::translate("AppCatalog", "Autosave, resilient history, and project recovery")
        },
        QStringLiteral(".csp (Creative Suite Project)")
    );

    m_apps.emplace_back(
        QStringLiteral("image-editor"),
        QStringLiteral("Image Editor"),
        QCoreApplication::translate("AppCatalog", "Layered image and graphics editing"),
        QCoreApplication::translate("AppCatalog", "A raster editor focused on layers, groups, smart selection tools, geometric shapes, and linked image layers."),
        QStringLiteral(""),
        QStringLiteral(CREATIVE_SUITE_VERSION_IMAGE_EDITOR),
        QStringLiteral("creative-suite-image-editor.exe"),
        QStringLiteral(":/icons/image-editor.png"),
        AppStatus::NotInstalled,
        QStringList{
            QCoreApplication::translate("AppCatalog", "Digital painting and raster editing with layers and groups"),
            QCoreApplication::translate("AppCatalog", "Adjustable masks with support for linked images"),
            QCoreApplication::translate("AppCatalog", "Object selection, vector shapes, and text tools"),
            QCoreApplication::translate("AppCatalog", "High-performance undo and redo history"),
            QCoreApplication::translate("AppCatalog", "Optimized export to high-quality PNG and JPEG")
        },
        QStringLiteral(".cimg (Creative Suite Image Document)")
    );

    m_apps.emplace_back(
        QStringLiteral("motion-editor"),
        QStringLiteral("Motion Studio"),
        QCoreApplication::translate("AppCatalog", "Motion design and graphic animation"),
        QCoreApplication::translate("AppCatalog", "A workspace for motion design and advanced compositing, with keyframe animation, precise Bezier curves (Graph Editor), masks, and accelerated export."),
        QStringLiteral(""),
        QStringLiteral(CREATIVE_SUITE_VERSION_MOTION_EDITOR),
        QStringLiteral("creative-suite-motion-editor.exe"),
        QStringLiteral(":/icons/motion-studio.png"),
        AppStatus::NotInstalled,
        QStringList{
            QCoreApplication::translate("AppCatalog", "Dynamic layered compositions with media and shapes"),
            QCoreApplication::translate("AppCatalog", "Bezier curve editor (Graph Editor) for precise interpolation control"),
            QCoreApplication::translate("AppCatalog", "Chained Gaussian blur and color adjustment effects"),
            QCoreApplication::translate("AppCatalog", "High-fidelity rendered video export"),
            QCoreApplication::translate("AppCatalog", "Independent native application built on the shared suite core")
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
