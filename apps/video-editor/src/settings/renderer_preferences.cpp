#include "renderer_preferences.h"
#include <QSettings>
#include <QCoreApplication>

namespace settings {
namespace {
std::optional<GraphicsBackend> parse(const QString& value) {
    if(value=="auto")return GraphicsBackend::Automatic;
    if(value=="opengl")return GraphicsBackend::OpenGl;
    if(value=="vulkan")return GraphicsBackend::Vulkan;
    if(value=="cpu")return GraphicsBackend::Cpu;
    return {};
}
}
GraphicsBackend rendererPreference() {
    return parse(QSettings().value(kRendererBackendKey,"auto").toString()).value_or(GraphicsBackend::Automatic);
}
void setRendererPreference(GraphicsBackend backend) {
    QSettings().setValue(kRendererBackendKey,QString::fromLatin1(creative_suite::composition::backendName(backend).data()));
}
bool askRendererAtStartup() {return QSettings().value(kAskRendererKey,true).toBool();}
void setAskRendererAtStartup(bool value) {QSettings().setValue(kAskRendererKey,value);}
QString rendererDisplayName(GraphicsBackend backend) {
    switch(backend){
    case GraphicsBackend::Automatic:return QCoreApplication::translate("RendererSelection","Automatic");
    case GraphicsBackend::OpenGl:return QStringLiteral("OpenGL");
    case GraphicsBackend::Vulkan:return QCoreApplication::translate("RendererSelection","Vulkan (Experimental)");
    case GraphicsBackend::Cpu:return QStringLiteral("CPU");
    }
    return {};
}
RendererLaunchOptions parseRendererArguments(const QStringList& args) {
    RendererLaunchOptions result;
    for(int i=1;i<args.size();++i){
        const auto& arg=args[i];
        if(arg=="--choose-renderer"){result.choose=true;continue;}
        if(arg!="--renderer" && !arg.startsWith("--renderer="))continue;
        QString value;
        if(arg=="--renderer"){if(i+1<args.size())value=args[++i];}
        else value=arg.mid(11);
        auto backend=parse(value);
        if(!backend || result.backend){result.error=QCoreApplication::translate("RendererSelection","Use --renderer=auto|opengl|vulkan|cpu once.");return result;}
        result.backend=backend;
    }
    return result;
}
}
