#include "settings/renderer_startup.h"
#include "settings/renderer_preferences.h"
#include "settings/user_preferences.h"
#include "logging/logger.h"
#include <QApplication>
#include <QDialog>
#include <QCheckBox>
#include <QComboBox>
#include <QMessageBox>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>
#include <stdexcept>
namespace {
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void answer(bool accept,bool remember) {
    QTimer::singleShot(0,[=]{
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if(!dialog)return;
        if(auto* combo=dialog->findChild<QComboBox*>("rendererStartupComboBox"))combo->setCurrentIndex(combo->findData(int(settings::GraphicsBackend::Cpu)));
        if(auto* check=dialog->findChild<QCheckBox*>("rememberRendererCheckBox"))check->setChecked(remember);
        if(accept)dialog->accept();else dialog->reject();
    });
}
}
int main(int argc,char** argv) {
    QApplication app(argc,argv);QCoreApplication::setOrganizationName("CreativeSuiteTests");QCoreApplication::setApplicationName("RendererSelection");
    QSettings::setDefaultFormat(QSettings::IniFormat);QTemporaryDir temporary;
    if(!temporary.isValid())return 1;
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temporary.path());
    if(!logging::Logger::instance().initialize(temporary.path().toStdWString()))return 1;
    try {
        settings::RendererLaunchOptions choose;choose.choose=true;
        answer(true,true);auto selected=settings::initializeRenderer(choose);
        require(selected.proceed && settings::rendererPreference()==settings::GraphicsBackend::Cpu && !settings::askRendererAtStartup(),"Remembered startup selection failed.");
        require(app.property("graphicsBackend").toString()=="cpu","Effective renderer property is incorrect.");
        settings::RendererLaunchOptions override;override.backend=settings::GraphicsBackend::OpenGl;
        selected=settings::initializeRenderer(override);
        require(selected.proceed && settings::rendererPreference()==settings::GraphicsBackend::Cpu,"One-run override changed the remembered preference.");
        require(app.property("graphicsBackend").toString()=="opengl","Override was not selected.");
        answer(false,false);selected=settings::initializeRenderer(choose);require(!selected.proceed,"Cancelling the startup chooser opened the application.");
        require(settings::rendererPreference()==settings::GraphicsBackend::Cpu,"Cancellation changed stored renderer.");
        settings::setRendererPreference(settings::GraphicsBackend::OpenGl);answer(true,false);
        selected=settings::initializeRenderer(choose);
        require(selected.proceed && settings::askRendererAtStartup() && settings::rendererPreference()==settings::GraphicsBackend::OpenGl,"Temporary startup choice did not preserve saved preference.");
        qputenv("CREATIVE_SUITE_DISABLE_VULKAN","1");override.backend=settings::GraphicsBackend::Vulkan;
        QTimer::singleShot(0,[]{if(auto* dialog=qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))dialog->accept();});
        selected=settings::initializeRenderer(override);qunsetenv("CREATIVE_SUITE_DISABLE_VULKAN");
        require(selected.proceed && !selected.vulkan && app.property("graphicsBackend").toString()=="opengl","Unavailable Vulkan did not recover to OpenGL.");
        std::cout<<"Renderer selection, persistence, overrides, cancellation and recovery passed.\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
