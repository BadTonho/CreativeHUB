#include "renderer_startup.h"
#include "renderer_preferences.h"
#include "logging/logger.h"
#include <creative_suite/composition/vulkan_frame_compositor.h>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QWindow>

namespace settings {
RendererStartup initializeRenderer() { return initializeRenderer(parseRendererArguments(QCoreApplication::arguments())); }
RendererStartup initializeRenderer(const RendererLaunchOptions& options) {
    namespace c=creative_suite::composition;
    if(!options.error.isEmpty()){
        logging::Logger::instance().log(logging::Level::Error,"rendering","parse_startup_arguments",options.error.toStdString());
        QMessageBox::warning(nullptr,QCoreApplication::translate("RendererSelection", "Graphics renderer"),options.error);return {false,{}};
    }
    auto requested=options.backend.value_or(rendererPreference());
    if(options.choose || (!options.backend && askRendererAtStartup())){
        QDialog dialog;dialog.setObjectName("rendererStartupDialog");dialog.setWindowTitle(QCoreApplication::translate("RendererSelection", "Graphics renderer"));
        auto* layout=new QVBoxLayout(&dialog);
        auto* description=new QLabel(QCoreApplication::translate("RendererSelection", "Choose the graphics API for preview and GPU export. GPU composition and hardware codecs keep their separate settings. Changes to this API require restarting the editor."));
        description->setWordWrap(true);layout->addWidget(description);
        auto* combo=new QComboBox;combo->setObjectName("rendererStartupComboBox");
        for(auto backend:{GraphicsBackend::Automatic,GraphicsBackend::OpenGl,GraphicsBackend::Vulkan,GraphicsBackend::Cpu})
            combo->addItem(rendererDisplayName(backend),int(backend));
        combo->setCurrentIndex(combo->findData(int(requested)));layout->addWidget(combo);
        auto* remember=new QCheckBox(QCoreApplication::translate("RendererSelection", "Remember my choice"));remember->setObjectName("rememberRendererCheckBox");
        remember->setEnabled(!options.backend);layout->addWidget(remember);
        auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);layout->addWidget(buttons);
        QObject::connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
        QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        if(dialog.exec()!=QDialog::Accepted)return {false,{}};
        requested=GraphicsBackend(combo->currentData().toInt());
        if(!options.backend){setAskRendererAtStartup(!remember->isChecked());if(remember->isChecked())setRendererPreference(requested);}
    }
    RendererStartup result;auto effective=requested==GraphicsBackend::Automatic?GraphicsBackend::OpenGl:requested;
    if(effective==GraphicsBackend::Vulkan){
        std::string cause;result.vulkan=c::VulkanDevice::create(cause);
        if(result.vulkan){
            c::setSessionVulkanDevice(result.vulkan);
            // Verify presentation before choosing Vulkan for the application session.
            auto probe_device = result.vulkan;
            QWindow probe;result.vulkan->attachWindow(&probe);probe.resize(16,16);probe.create();
            c::VulkanFrameCompositor compositor(result.vulkan);auto frame=compositor.composeTexture(1,1,{});
            if(frame.status!=c::OpenGlCompositionStatus::Complete || !result.vulkan->present(&probe,frame.frame,false,cause)){
                if(cause.empty())cause=frame.cause;
                result.vulkan->releaseWindow(&probe);frame.frame.reset();result.vulkan.reset();
            }else result.vulkan->releaseWindow(&probe);
        }
        if(!result.vulkan){
            c::setSessionVulkanDevice({});effective=GraphicsBackend::OpenGl;
            logging::Logger::instance().log(logging::Level::Error,"rendering","initialize_vulkan",cause,{{"fallback","opengl"}});
            QMessageBox::warning(nullptr,QCoreApplication::translate("RendererSelection", "Graphics renderer"),QCoreApplication::translate("RendererSelection", "Vulkan is unavailable. The editor will use OpenGL and recover with CPU when necessary.")+"\n\n"+QString::fromStdString(cause));
        }
    }
    c::setSessionGraphicsBackend(effective);
    qApp->setProperty("graphicsBackend",QString::fromLatin1(c::backendName(effective).data()));
    logging::Logger::instance().log(logging::Level::Info,"rendering","select_renderer","Graphics renderer selected.",
        {{"requested_backend",std::string(c::backendName(requested))},{"effective_backend",std::string(c::backendName(effective))},
         {"device",result.vulkan?result.vulkan->name().toStdString():""}});
    return result;
}
}
