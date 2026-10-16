#include "ui/preview/preview_widget.h"
#include "rendering/vulkan_preview_surface.h"
#include "rendering/preview_performance_metrics.h"
#include "ui/workspace/workspace_page_transition.h"
#include "logging/logger.h"
#include "main_window/main_window.h"
#include <creative_suite/composition/vulkan_frame_compositor.h>
#include <creative_suite/composition/opengl_frame_compositor.h>
#include <creative_suite/system_monitor/performance_usage.h>
#include "rendering/opengl_preview_surface.h"
#include <QApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QVBoxLayout>
#include <QTest>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QSurfaceFormat>
#include <QSemaphore>
#include <QThread>
#include <atomic>
#include <chrono>
#include <thread>
#include <algorithm>
#include <iostream>
#include <stdexcept>
namespace c=creative_suite::composition;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void benchmarkPreview() {
    using Clock=std::chrono::steady_clock;
    const auto original=c::sessionGraphicsBackend();
    auto surface=c::OpenGlFrameCompositor::createSurface();
    for(const auto size:{QSize(480,270),QSize(960,540),QSize(1920,1080)}) {
        const auto fixture=[](int w,int h,bool opaque){
            media::VideoFrame f{w,h,w*4,{}};f.rgba_pixels.resize(size_t(w)*h*4);
            for(int y=0;y<h;++y)for(int x=0;x<w;++x){auto* p=f.rgba_pixels.data()+size_t(y)*f.stride+x*4;
                p[0]=uint8_t(x*13+y*7);p[1]=uint8_t(x*5+y*17);p[2]=uint8_t(x*29+y*3);p[3]=opaque?255:uint8_t(x*31+y*11);}
            return f;
        };
        auto background=fixture(size.width(),size.height(),true),overlay=fixture(size.width()/2,size.height()/2,false);
        std::vector<c::CompositionLayer> layers{{&background},{&overlay},{&overlay}};
        layers[1].transform.rotation_degrees=23;layers[2].transform.position_x=0.7;
        for(int trial=1;trial<=3;++trial)for(auto backend:{c::GraphicsBackend::Cpu,c::GraphicsBackend::OpenGl,c::GraphicsBackend::Vulkan}) {
            c::setSessionGraphicsBackend(backend);
            QWidget host;host.resize(800,500);auto* layout=new QVBoxLayout(&host);
            auto* preview=new PreviewWidget(&host);layout->addWidget(preview);host.show();
            if(!QTest::qWaitForWindowExposed(&host,200)){host.hide();host.show();}
            require(QTest::qWaitForWindowExposed(&host,3000),"Native benchmark window was not exposed.");
            QTest::qWait(30);
            auto* gl=preview->findChild<rendering::OpenGLPreviewSurface*>();
            if(backend==c::GraphicsBackend::OpenGl)require(gl && gl->textureDeliveryAvailable(),"OpenGL benchmark sharing is unavailable.");
            auto& metrics=rendering::PreviewPerformanceMetrics::instance();metrics.setEnabled(true);metrics.reset();
            QSemaphore ack;std::atomic_bool done=false,abort=false;std::exception_ptr failure;
            bool pending=false,presentation_seen=false;Clock::time_point queued_at;int index=0;quint64 trace=0;Clock::time_point requested;
            std::vector<double> samples,completion_samples;
            c::VulkanFrameCompositor collector(c::sessionVulkanDevice());
            c::OpenGlCompositionTimings transfers;Clock::time_point measured_start,measured_end;
            system_monitor::PerformanceSampler sampler;static_cast<void>(sampler.sample());
            std::thread worker([&]{
                try {
                    auto gpu=backend==c::GraphicsBackend::Cpu?std::unique_ptr<c::GpuFrameCompositor>{}:
                        c::createGpuFrameCompositor(surface.get(),c::OpenGlPrecisionPolicy::Automatic,QOpenGLContext::globalShareContext(),{},backend);
                    for(int frame=0;frame<15 && !abort;++frame) {
                        const auto started=Clock::now();rendering::PreviewFramePayload payload;
                        c::OpenGlCompositionTimings timing;
                        if(gpu) {
                            auto result=gpu->composeTexture(size.width(),size.height(),layers,{},&timing);
                            const auto deadline=Clock::now()+std::chrono::seconds(1);
                            while(result.status==c::OpenGlCompositionStatus::Busy && Clock::now()<deadline){
                                static_cast<void>(gpu->collectReleasedTextureFrames());QThread::msleep(1);
                                result=gpu->composeTexture(size.width(),size.height(),layers,{},&timing);
                            }
                            if(!result.frame)throw std::runtime_error(result.cause);
                            payload.gpu=std::move(result.frame);
                        } else {
                            auto rgba=c::FrameCompositor::compose(size.width(),size.height(),layers);
                            require(bool(rgba),"CPU preview benchmark composition failed.");
                            payload.rgba=std::make_shared<const media::VideoFrame>(std::move(*rgba));
                        }
                        if(frame>=3){transfers.uploaded_bytes+=timing.uploaded_bytes;transfers.readback_bytes+=timing.readback_bytes;}
                        const auto id=metrics.createFrameDeliveryTrace(1,frame);
                        const bool queued=QMetaObject::invokeMethod(&host,[&,payload=std::move(payload),id,frame,started]()mutable{
                            trace=id;index=frame;requested=started;pending=true;presentation_seen=false;preview->setFrame(std::move(payload),id);
                        },Qt::QueuedConnection);
                        require(queued,"Cannot submit a benchmark preview frame.");ack.acquire();
                    }
                }catch(...){failure=std::current_exception();}
                done=true;
            });
            while(!done || pending) {
                QCoreApplication::processEvents(QEventLoop::AllEvents,2);
                const auto snapshot=metrics.takeSnapshotAndReset();
                if(pending) {
                    const bool event_seen=backend==c::GraphicsBackend::Cpu
                        ? snapshot.frame_delivery.stage_counts[size_t(rendering::PreviewFrameDeliveryStage::CpuPainted)]>0
                        : backend==c::GraphicsBackend::OpenGl ? gl->lastPresentedDeliveryTraceId()==trace
                        : snapshot.frame_delivery.stage_counts[size_t(rendering::PreviewFrameDeliveryStage::QtFrameSwapped)]>0;
                    if(event_seen && !presentation_seen){presentation_seen=true;queued_at=Clock::now();}
                    bool painted=presentation_seen;
                    if(painted && backend==c::GraphicsBackend::Vulkan){
                        const auto collected=collector.collectReleasedTextureFrames();
                        require(collected.status==c::GpuCompositionStatus::Complete,"Vulkan benchmark completion polling failed.");
                        painted=c::sessionVulkanDevice()->diagnostics().pending==0;
                    }
                    if(painted || Clock::now()-requested>std::chrono::seconds(5)) {
                        if(!painted)abort=true;
                        else if(index>=3){
                            measured_end=Clock::now();if(index==3)measured_start=requested;
                            samples.push_back(std::chrono::duration<double,std::milli>(queued_at-requested).count());
                            completion_samples.push_back(std::chrono::duration<double,std::milli>(measured_end-requested).count());
                        }
                        if(index==14 || abort)preview->releaseGpuFrames();
                        pending=false;ack.release();
                    }
                }
                QThread::msleep(1);
            }
            worker.join();if(failure)std::rethrow_exception(failure);
            require(!abort && samples.size()==12,"Native preview benchmark did not present every measured frame.");
            const auto usage=sampler.sample();double completion_sum=0;for(auto value:completion_samples)completion_sum+=value;
            std::sort(completion_samples.begin(),completion_samples.end());double sum=0;for(auto value:samples)sum+=value;std::sort(samples.begin(),samples.end());
            std::cout<<"preview_benchmark renderer="<<c::backendName(backend)<<" width="<<size.width()<<" height="<<size.height()
                <<" trial="<<trial<<" average_ms="<<sum/samples.size()<<" p95_ms="<<samples.back()<<" presented_fps="<<samples.size()/std::chrono::duration<double>(measured_end-measured_start).count()
                <<" upload_bytes="<<transfers.uploaded_bytes<<" readback_bytes="<<transfers.readback_bytes
                <<" completion_average_ms="<<completion_sum/completion_samples.size()<<" completion_p95_ms="<<completion_samples.back()
                <<" working_set_bytes="<<usage.process_working_set_bytes.value_or(0)<<" process_cpu_percent="<<usage.process_cpu_percent.value_or(0)<<"\n";
            host.close();
        }
    }
    c::setSessionGraphicsBackend(original);
}
}
int main(int argc,char** argv) {
    QSurfaceFormat format;format.setVersion(3,2);format.setProfile(QSurfaceFormat::CoreProfile);QSurfaceFormat::setDefaultFormat(format);
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication app(argc,argv);QCoreApplication::setOrganizationName("CreativeSuiteTests");QCoreApplication::setApplicationName("VulkanPreview");
    QTemporaryDir temporary;QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temporary.path());
    if(!logging::Logger::instance().initialize(temporary.path().toStdWString()))return 1;
    if (QGuiApplication::platformName()=="offscreen" || QGuiApplication::platformName()=="minimal") return app.arguments().contains("--require-gpu") ? 1 : 77;
    std::string cause;auto device=c::VulkanDevice::create(cause);
    if(!device){std::cerr<<cause<<'\n';return app.arguments().contains("--require-gpu")?1:77;}
    c::setSessionVulkanDevice(device);c::setSessionGraphicsBackend(c::GraphicsBackend::Vulkan);
    try {
        c::VulkanFrameCompositor compositor(device);
        auto frame=compositor.composeTexture(64,36,{});require(bool(frame.frame),"Cannot prepare native Vulkan preview.");
        QWidget host;host.resize(480,320);auto* layout=new QVBoxLayout(&host);auto* preview=new PreviewWidget(&host);layout->addWidget(preview);
        preview->setDeliveryEpoch(7);int errors=0;QObject::connect(preview,&PreviewWidget::gpuFallbackRequested,[&]{++errors;});
        rendering::PreviewFramePayload payload;payload.gpu=frame.frame;payload.delivery_epoch=7;
        rendering::PreviewPerformanceMetrics::instance().setEnabled(true);rendering::PreviewPerformanceMetrics::instance().reset();
        preview->setFrame(payload);host.show();
        // Windows execute_process may inherit SW_HIDE for the first top-level show.
        if(!QTest::qWaitForWindowExposed(&host,200)){host.hide();host.show();}
        if(!QTest::qWaitForWindowExposed(&host,3000)){
            std::cerr << "The runner cannot expose a native preview window; run this executable directly with --require-gpu.\n";
            return app.arguments().contains("--require-gpu") ? 1 : 77;
        }
        require(preview->usesGpuPreview() && preview->textureDeliveryAvailable() && errors==0,"Vulkan preview was not active.");
        rendering::PreviewPerformanceSnapshot metrics;
        QElapsedTimer presentation_wait;presentation_wait.start();
        do {metrics=rendering::PreviewPerformanceMetrics::instance().takeSnapshotAndReset();
            if(metrics.gpu_presented_frames)break;QTest::qWait(5);
        } while(presentation_wait.elapsed()<3000);
        require(metrics.gpu_presented_frames>0,"No Vulkan image reached native presentation.");
        payload.delivery_epoch=6;preview->setFrame(payload);require(preview->currentPayload().delivery_epoch==7,"Stale delivery replaced the preview.");
        host.resize(640,480);preview->setGrayscaleEnabled(true);QTest::qWait(60);
        ui::WorkspacePageTransition transition;bool applied=false;
        transition.start(&host,ui::WorkspacePageId::Edit,ui::WorkspacePageId::Fusion,30,[&]{applied=true;},settings::WorkspacePageTransitionStyle::WorkspaceContent);
        QElapsedTimer transition_wait;transition_wait.start();
        while((!applied || transition.isRunning()) && transition_wait.elapsed()<3000)QTest::qWait(5);
        require(applied && !transition.isRunning() && errors==0,"Vulkan workspace transition failed.");
        preview->releaseGpuFrames();frame.frame.reset();host.close();
        require(device->valid(),"Preview shutdown lost the Vulkan device.");
        if(!app.arguments().contains("--benchmark")) {
            // Verify the selected session reaches the real application's views.
            MainWindow application_window;application_window.show();
            require(QTest::qWaitForWindowExposed(&application_window,3000),"The Vulkan application window was not exposed.");
            require(!application_window.findChildren<rendering::VulkanPreviewSurface*>().isEmpty(),
                "The application did not construct Vulkan preview surfaces.");
            application_window.close();
        }
        std::cout<<"native_vulkan_preview=passed presented_frames="<<metrics.gpu_presented_frames<<"\n";
        if(app.arguments().contains("--benchmark"))benchmarkPreview();
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
