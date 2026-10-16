#pragma once
#include <creative_suite/composition/graphics_backend.h>
#include <QStringList>
#include <optional>

namespace settings {
using GraphicsBackend = creative_suite::composition::GraphicsBackend;
inline constexpr char kRendererBackendKey[] = "rendering/backend";
inline constexpr char kAskRendererKey[] = "rendering/ask_at_startup";
GraphicsBackend rendererPreference();
void setRendererPreference(GraphicsBackend);
bool askRendererAtStartup();
void setAskRendererAtStartup(bool);
struct RendererLaunchOptions {
    std::optional<GraphicsBackend> backend;
    bool choose = false;
    QString error;
};
RendererLaunchOptions parseRendererArguments(const QStringList&);
QString rendererDisplayName(GraphicsBackend);
}
