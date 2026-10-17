#include "ui/preview/preview_widget.h"
#include "rendering/vulkan_preview_surface.h"
#include "rendering/vulkan_preview_schedule.h"
#include "rendering/offline_export_renderer.h"
#include <QFileInfo>
#include <QImage>
#include <QColor>
#include "rendering/preview_performance_metrics.h"
#include "ui/workspace/workspace_page_transition.h"
#include "logging/logger.h"
#include "main_window/main_window.h"
#include <creative_suite/composition/vulkan_frame_compositor.h>
#include <creative_suite/composition/opengl_frame_compositor.h>
#include <creative_suite/system_monitor/performance_usage.h>
#include "rendering/opengl_preview_surface.h"
#include <QApplication>
#include <QWindow>
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
#include <cmath>
#include <iostream>
#include <stdexcept>
namespace c=creative_suite::composition;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void validateSchedule(const c::GpuTextureFramePtr& image) {
    rendering::VulkanPreviewSchedule schedule;
    schedule.setEpoch(7);
    require(schedule.replace({image,1,7})==0 && schedule.ready(),"First frame did not become ready.");
    schedule.submitted();
    for(int repeat=0;repeat<100;++repeat)require(!schedule.ready(),"Repeated update resubmitted a clean revision.");
    require(schedule.replace({image,2,7})==0 && !schedule.ready(),"A second presentation escaped the in-flight limit.");
    require(schedule.replace({image,3,7})==2 && schedule.candidate().trace==3,"Pending frames were not coalesced.");
    require(schedule.lastSubmitted().trace==1,"A pending frame replaced the transition snapshot.");
    require(schedule.completed()==1 && schedule.ready(),"Completion lost the pending revision.");
    schedule.submitted();require(schedule.completed()==3 && !schedule.ready(),"Latest frame was not submitted exactly once.");
    schedule.invalidate();require(schedule.ready(),"A visual change did not redraw the last frame.");
    schedule.submitted();require(schedule.completed()==0 && !schedule.ready(),"A visual redraw credited the original trace twice.");
    schedule.replace({image,4,7});schedule.submitted();schedule.replace({image,5,7});
    require(schedule.setEpoch(8)==5 && schedule.completed()==0 && !schedule.ready(),"A stale generation was submitted or credited.");
    schedule.replace({image,6,8});schedule.setSuspended(true);
    require(!schedule.ready(),"A suspended viewer submitted work.");
    schedule.setSuspended(false);require(schedule.ready(),"A resumed viewer lost its pending frame.");
    schedule.submitted();schedule.invalidate();schedule.setSuspended(true);
    require(schedule.completed()==6 && !schedule.ready(),"Suspension corrupted an in-flight completion.");
    schedule.setSuspended(false);require(schedule.ready(),"Resume did not invalidate the visual revision.");
    schedule.replace({image,7,8});require(schedule.clear()==7 && !schedule.ready(),"Clear did not cancel retries.");
}
template<class Predicate> void waitFor(Predicate predicate,const char* message) {
    QElapsedTimer timer;timer.start();while(!predicate() && timer.elapsed()<3000)QTest::qWait(1);
    require(predicate(),message);
}
void validateNativeScheduling(PreviewWidget* preview,QWidget& host,const rendering::PreviewFramePayload& payload) {
    auto* surface=preview->findChild<rendering::VulkanPreviewSurface*>();require(surface,"Missing native Vulkan surface.");
    waitFor([&]{auto s=surface->diagnostics();return s.submitted>0 && s.submitted==s.completed;},"Presentation did not complete.");
    QTest::qWait(20);const auto before=surface->diagnostics().submitted;
    for(int repeat=0;repeat<100;++repeat)for(auto* window:QGuiApplication::allWindows())
        if(window->surfaceType()==QSurface::VulkanSurface){QEvent update(QEvent::UpdateRequest);QCoreApplication::sendEvent(window,&update);}
    QTest::qWait(20);require(surface->diagnostics().submitted==before,"Repeated Qt updates resubmitted an unchanged frame.");
    preview->setGrayscaleEnabled(true);
    waitFor([&]{return surface->diagnostics().submitted>before;},"Grayscale change did not redraw.");
    waitFor([&]{auto s=surface->diagnostics();return s.submitted==s.completed;},"Grayscale redraw did not complete.");
    const auto gray=surface->diagnostics().submitted;preview->setGrayscaleEnabled(true);QTest::qWait(20);
    require(surface->diagnostics().submitted==gray,"Unchanged grayscale resubmitted a frame.");
    host.resize(host.width()+10,host.height()+10);
    waitFor([&]{return surface->diagnostics().submitted>gray;},"Resize did not redraw.");
    surface->suspendForTransition(true);const auto suspended=surface->diagnostics().submitted;
    preview->setFrame(payload);QTest::qWait(20);
    require(surface->diagnostics().submitted==suspended,"Suspension permitted a new submission.");
    surface->suspendForTransition(false);
    waitFor([&]{return surface->diagnostics().submitted>suspended;},"Resume lost its pending frame.");
    surface->suspendForTransition(true);const auto old_generation=surface->diagnostics().submitted;preview->setFrame(payload);preview->setDeliveryEpoch(8);
    surface->suspendForTransition(false);QTest::qWait(20);
    require(surface->diagnostics().submitted==old_generation,"A stale pending frame survived the generation change.");
    const auto retired=surface->diagnostics().submitted;preview->releaseGpuFrames();QTest::qWait(20);
    require(surface->diagnostics().submitted==retired,"Clear retained a queued retry.");
    preview->setDeliveryEpoch(7);preview->setFrame(payload);
    host.hide();QTest::qWait(20);preview->setFrame(payload);const auto hidden=surface->diagnostics().submitted;
    QTest::qWait(20);require(surface->diagnostics().submitted==hidden,"A hidden Viewer submitted new work.");
    host.show();require(QTest::qWaitForWindowExposed(&host,3000),"Hidden Viewer did not expose again.");
    waitFor([&]{return surface->diagnostics().submitted>hidden;},"Expose did not resume the latest pending frame.");
}
void validateConcurrentViewers(const c::GpuTextureFramePtr& frame,const QString& directory,bool native=false,bool measure=false,QSize extent={},int trial=0) {
    QWidget host;host.resize(700,300);auto* layout=new QVBoxLayout(&host);
    auto* first=new PreviewWidget(&host);auto* second=new PreviewWidget(&host);layout->addWidget(first);layout->addWidget(second);
    host.show();require(QTest::qWaitForWindowExposed(&host,3000),"Two-viewer host was not exposed.");
    auto* first_vk=first->findChild<rendering::VulkanPreviewSurface*>();
    auto* second_vk=second->findChild<rendering::VulkanPreviewSurface*>();
    QImage image(320,180,QImage::Format_RGBA8888);image.fill(QColor(30,80,190));
    const auto source=directory+"/concurrent.png";require(image.save(source),"Cannot save concurrent export source.");
    rendering::RenderJob job;job.id=9201;job.settings.output_path=directory+"/concurrent.mkv";
    job.settings.container_name="matroska";job.settings.video_encoder_name="ffv1";
    job.settings.width=320;job.settings.height=180;job.settings.export_audio=false;
    job.settings.gpu_composition_enabled=true;job.settings.graphics_backend=c::GraphicsBackend::Vulkan;
    if(native || measure){job.settings.video_encoder_name="h264_nvenc";job.settings.width=measure?extent.width():1280;job.settings.height=measure?extent.height():720;
        job.settings.video_bitrate_mbps=std::round(10.0*job.settings.width*job.settings.height/(1920.0*1080)*10)/10;}
    const int frames=measure?210:12;
    if(measure)qputenv("CREATIVE_SUITE_NVENC_NATIVE_LATENCY","1");
    if(measure && !native)qputenv("CREATIVE_SUITE_VULKAN_NVENC_RGBA","1");else qunsetenv("CREATIVE_SUITE_VULKAN_NVENC_RGBA");
    project::ProjectTrack track;track.track_id=1;project::ProjectClip clip;
    clip.source_path=source.toStdWString();clip.kind=timeline::ClipKind::Image;clip.duration_frames=frames;track.clips.push_back(clip);
    job.project_snapshot.timeline_tracks.push_back(track);
    QSemaphore ack;std::atomic_bool done=false,cancel=false;bool pending=false;quint64 first_trace=0,second_trace=0;
    int presented=0;std::exception_ptr failure;rendering::OfflineExportMetrics export_metrics;
    rendering::OfflineExportOptions options;
    options.metrics_callback=[&](const auto& value){export_metrics=value;};
    options.native_maximum_frames=measure && extent.width()==3840?1:2;
    using Clock=std::chrono::steady_clock;Clock::time_point queued,measured_start;int current=0;std::vector<double> samples;
    const auto delivered=[&](std::int64_t index){
        QMetaObject::invokeMethod(&host,[&,index]{
            auto& metrics=rendering::PreviewPerformanceMetrics::instance();
            first_trace=metrics.createFrameDeliveryTrace(1,index);second_trace=metrics.createFrameDeliveryTrace(1,index);
            rendering::PreviewFramePayload payload;payload.gpu=frame;
            queued=Clock::now();current=int(index);if(index==30)measured_start=queued;
            first->setFrame(payload,first_trace);second->setFrame(payload,second_trace);pending=true;
        },Qt::QueuedConnection);
        ack.acquire();
    };
    if(native)options.prepared_native_frame_callback=[&](const auto&,std::int64_t index){delivered(index);};
    else options.prepared_frame_callback=[&](const auto&,std::int64_t index){delivered(index);};
    std::thread worker([&]{try{rendering::OfflineExportRenderer::render(job,cancel,{},options);}catch(...){failure=std::current_exception();}done=true;});
    QElapsedTimer deadline;deadline.start();
    while(!done && deadline.elapsed()<(measure?60000:15000)){
        QCoreApplication::processEvents(QEventLoop::AllEvents,2);
        if(pending && first_vk->lastCompletedDeliveryTraceId()==first_trace && second_vk->lastCompletedDeliveryTraceId()==second_trace){
            if(measure && current>=30)samples.push_back(std::chrono::duration<double,std::milli>(Clock::now()-queued).count());
            ++presented;pending=false;ack.release();
        }
        QThread::msleep(1);
    }
    const bool timed_out=!done;cancel=true;ack.release();worker.join();if(failure)std::rethrow_exception(failure);
    const bool pipeline=native?(export_metrics.native_encoded_frames==std::uint64_t(frames) && export_metrics.native_slots==options.native_maximum_frames && export_metrics.readback_bytes==0):qEnvironmentVariableIntValue("CREATIVE_SUITE_VULKAN_SERIAL")==1 ? export_metrics.async_readback_submissions==0 :
        export_metrics.async_readback_slots==options.native_maximum_frames && export_metrics.peak_pending_frames==options.native_maximum_frames;
    require(!timed_out && pipeline && presented==frames && export_metrics.encoded_frames==std::uint64_t(frames) && export_metrics.gpu_frames==std::uint64_t(frames) &&
            export_metrics.gpu_failures==0,
            "Concurrent viewers/export lost frames, stalled, or lost the asynchronous export path.");
    require(QFileInfo::exists(job.settings.output_path),"Concurrent export did not publish its destination.");
    if(measure){
        require(samples.size()==180,"Viewer benchmark samples changed.");double sum=0;for(auto value:samples)sum+=value;std::sort(samples.begin(),samples.end());
        std::cout<<"viewer_benchmark mode="<<(native?"native":"rgba")<<" width="<<extent.width()<<" height="<<extent.height()<<" trial="<<trial
            <<" completion_mean_ms="<<sum/180<<" completion_p95_ms="<<samples[170]<<" total_ms="<<std::chrono::duration<double,std::milli>(Clock::now()-measured_start).count()
            <<" frames_per_viewer="<<presented<<" native_frames="<<export_metrics.native_encoded_frames<<" readback_bytes="<<export_metrics.readback_bytes<<" pending="<<export_metrics.peak_pending_frames<<" known_peak="<<export_metrics.peak_known_gpu_bytes<<"\n"<<std::flush;
    }
    qunsetenv("CREATIVE_SUITE_VULKAN_NVENC_RGBA");qunsetenv("CREATIVE_SUITE_NVENC_NATIVE_LATENCY");
    first->releaseGpuFrames();second->releaseGpuFrames();host.close();
    std::cout<<"native_vulkan_concurrent_viewers=passed viewers=2 frames_per_viewer="<<presented<<" native="<<native<<" export_frames="<<export_metrics.encoded_frames<<"\n";
}
void validateCloseInFlight(const c::GpuTextureFramePtr& frame) {
    {
        QWidget host;host.resize(300,180);auto* layout=new QVBoxLayout(&host);auto* preview=new PreviewWidget(&host);layout->addWidget(preview);
        host.show();require(QTest::qWaitForWindowExposed(&host,3000),"Closing viewer was not exposed.");
        // PreviewWidget starts on its CPU placeholder until the first payload.
        rendering::PreviewFramePayload payload;payload.gpu=frame;preview->setFrame(payload);
        waitFor([]{for(auto* window:QGuiApplication::allWindows())
            if(window->isExposed() && window->surfaceType()==QSurface::VulkanSurface)return true;
            return false;},"Closing Vulkan child was not exposed.");
        auto* surface=preview->findChild<rendering::VulkanPreviewSurface*>();
        waitFor([&]{auto s=surface->diagnostics();return s.submitted>0 && s.submitted==s.completed;},"Closing viewer warm-up did not complete.");
        preview->setFrame(payload);
        for(auto* window:QGuiApplication::allWindows())if(window->isExposed() && window->surfaceType()==QSurface::VulkanSurface){
            QEvent update(QEvent::UpdateRequest);QCoreApplication::sendEvent(window,&update);
        }
        const auto stats=surface->diagnostics();
        require(stats.submitted>stats.completed,"Close fixture did not retain a tracked presentation.");
        // Destroy before processing any completion timer event.
    }
    require(c::sessionVulkanDevice()->valid(),"Closing an in-flight viewer lost the device.");
}
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
            auto* vk=preview->findChild<rendering::VulkanPreviewSurface*>();
            auto* gl=preview->findChild<rendering::OpenGLPreviewSurface*>();
            if(backend==c::GraphicsBackend::OpenGl)require(gl && gl->textureDeliveryAvailable(),"OpenGL benchmark sharing is unavailable.");
            auto& metrics=rendering::PreviewPerformanceMetrics::instance();metrics.setEnabled(true);metrics.reset();
            QSemaphore ack;std::atomic_bool done=false,abort=false;std::exception_ptr failure;
            bool pending=false,presentation_seen=false;Clock::time_point queued_at;int index=0;quint64 trace=0;Clock::time_point requested;
            std::vector<double> samples,completion_samples;
            const auto device_before=c::sessionVulkanDevice()->diagnostics();
            c::OpenGlCompositionTimings transfers;Clock::time_point measured_start,measured_end;
            system_monitor::PerformanceSampler sampler;static_cast<void>(sampler.sample());
            std::thread worker([&]{
                try {
                    auto gpu=backend==c::GraphicsBackend::Cpu?std::unique_ptr<c::GpuFrameCompositor>{}:
                        c::createGpuFrameCompositor(surface.get(),c::OpenGlPrecisionPolicy::Automatic,QOpenGLContext::globalShareContext(),{},backend);
                    for(int frame=0;frame<210 && !abort;++frame) {
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
                        if(frame>=30){transfers.uploaded_bytes+=timing.uploaded_bytes;transfers.readback_bytes+=timing.readback_bytes;}
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
                        : vk->lastQueuedDeliveryTraceId()==trace;
                    if(event_seen && !presentation_seen){presentation_seen=true;queued_at=Clock::now();}
                    bool painted=presentation_seen;
                    if(painted && backend==c::GraphicsBackend::Vulkan){
                        painted=vk->lastCompletedDeliveryTraceId()==trace;
                    }
                    if(painted || Clock::now()-requested>std::chrono::seconds(5)) {
                        if(!painted)abort=true;
                        else if(index>=30){
                            measured_end=Clock::now();if(index==30)measured_start=requested;
                            samples.push_back(std::chrono::duration<double,std::milli>(queued_at-requested).count());
                            completion_samples.push_back(std::chrono::duration<double,std::milli>(measured_end-requested).count());
                        }
                        if(index==209 || abort)preview->releaseGpuFrames();
                        pending=false;ack.release();
                    }
                }
                QThread::msleep(1);
            }
            worker.join();if(failure)std::rethrow_exception(failure);
            require(!abort && samples.size()==180,"Native preview benchmark did not present every measured frame.");
            const auto device_after=c::sessionVulkanDevice()->diagnostics();
            const auto viewer=vk?vk->diagnostics():rendering::VulkanPreviewSurface::Diagnostics{};
            const auto usage=sampler.sample();double completion_sum=0;for(auto value:completion_samples)completion_sum+=value;
            std::sort(completion_samples.begin(),completion_samples.end());double sum=0;for(auto value:samples)sum+=value;std::sort(samples.begin(),samples.end());
            std::cout<<"preview_benchmark renderer="<<c::backendName(backend)<<" width="<<size.width()<<" height="<<size.height()
                <<" trial="<<trial<<" average_ms="<<sum/samples.size()<<" p95_ms="<<samples[size_t(std::ceil(samples.size()*0.95))-1]<<" presented_fps="<<samples.size()/std::chrono::duration<double>(measured_end-measured_start).count()
                <<" upload_bytes="<<transfers.uploaded_bytes<<" readback_bytes="<<transfers.readback_bytes
                <<" completion_average_ms="<<completion_sum/completion_samples.size()<<" completion_p95_ms="<<completion_samples[size_t(std::ceil(completion_samples.size()*0.95))-1]
                <<" viewer_submissions="<<viewer.submitted<<" viewer_completions="<<viewer.completed<<" deferrals="<<viewer.deferred<<" drops="<<viewer.dropped
                <<" submissions="<<device_after.submissions-device_before.submissions<<" reserved_bytes="<<c::sessionVulkanDevice()->reservedBytes()
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
    std::string cause;c::VulkanDevice::CreateOptions creation;creation.try_native_export=true;auto device=c::VulkanDevice::create(cause,creation);
    if(!device){std::cerr<<cause<<'\n';return app.arguments().contains("--require-gpu")?1:77;}
    c::setSessionVulkanDevice(device);c::setSessionGraphicsBackend(c::GraphicsBackend::Vulkan);
    try {
        c::VulkanFrameCompositor compositor(device);
        auto frame=compositor.composeTexture(64,36,{});require(bool(frame.frame),"Cannot prepare native Vulkan preview.");
        if(!app.arguments().contains("--benchmark"))validateSchedule(frame.frame);
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
        if(!app.arguments().contains("--benchmark"))validateNativeScheduling(preview,host,payload);
        payload.delivery_epoch=6;preview->setFrame(payload);require(preview->currentPayload().delivery_epoch==7,"Stale delivery replaced the preview.");
        host.resize(640,480);preview->setGrayscaleEnabled(true);QTest::qWait(60);
        ui::WorkspacePageTransition transition;bool applied=false;
        transition.start(&host,ui::WorkspacePageId::Edit,ui::WorkspacePageId::Fusion,30,[&]{applied=true;},settings::WorkspacePageTransitionStyle::WorkspaceContent);
        QElapsedTimer transition_wait;transition_wait.start();
        while((!applied || transition.isRunning()) && transition_wait.elapsed()<3000)QTest::qWait(5);
        require(applied && !transition.isRunning() && errors==0,"Vulkan workspace transition failed.");
        auto retained_frame=frame.frame;preview->releaseGpuFrames();frame.frame.reset();host.close();
        require(device->valid(),"Preview shutdown lost the Vulkan device.");
        if(!app.arguments().contains("--benchmark")) {
            validateConcurrentViewers(retained_frame,temporary.path());
            if(app.arguments().contains("--native-bridge"))validateConcurrentViewers(retained_frame,temporary.path(),true);
            if(app.arguments().contains("--native-bridge-benchmark"))for(const auto extent:{QSize(1920,1080),QSize(2560,1440),QSize(3840,2160)})
                for(int trial=1;trial<=3;++trial)for(bool native:{false,true})validateConcurrentViewers(retained_frame,temporary.path(),native,true,extent,trial);
            validateCloseInFlight(retained_frame);
            // Verify the selected session reaches the real application's views.
            MainWindow application_window;application_window.show();
            require(QTest::qWaitForWindowExposed(&application_window,3000),"The Vulkan application window was not exposed.");
            require(!application_window.findChildren<rendering::VulkanPreviewSurface*>().isEmpty(),
                "The application did not construct Vulkan preview surfaces.");
            application_window.close();
        }
        std::cout<<"native_vulkan_preview=passed presented_frames="<<metrics.gpu_presented_frames<<"\n";
        if(app.arguments().contains("--benchmark")){retained_frame.reset();benchmarkPreview();}
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
