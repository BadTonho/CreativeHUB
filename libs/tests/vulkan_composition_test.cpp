#include <creative_suite/composition/vulkan_frame_compositor.h>
#include <creative_suite/composition/opengl_frame_compositor.h>
#include <creative_suite/effects/gaussian_blur.h>
#include <creative_suite/system_monitor/performance_usage.h>
#include <QApplication>
#include <QWindow>
#include <QSurfaceFormat>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QSemaphore>
#include <thread>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace creative_suite;
using namespace creative_suite::composition;
namespace {
void require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
media::RgbaFrame fixture(int w,int h,bool alpha=true) {
    media::RgbaFrame f{w,h,w*4+8,{}};f.rgba_pixels.resize(size_t(f.stride)*h,231);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){auto* p=f.rgba_pixels.data()+size_t(y)*f.stride+x*4;
        p[0]=uint8_t((x*13+y*7)%256);p[1]=uint8_t((x*5+y*17)%256);p[2]=uint8_t((x*29+y*3)%256);
        p[3]=alpha?uint8_t((x*31+y*11)%256):255;}
    return f;
}
void parity(const media::RgbaFrame& a,const media::RgbaFrame& b,int tolerance=2) {
    require(a.width==b.width && a.height==b.height,"Vulkan output dimensions differ.");
    for(int y=0;y<a.height;++y)for(int x=0;x<a.width*4;++x){
        auto av=a.rgba_pixels[size_t(y)*a.stride+x],bv=b.rgba_pixels[size_t(y)*b.stride+x];
        if(std::abs(int(av)-int(bv))>(x%4==3?0:tolerance))throw std::runtime_error("Vulkan pixel mismatch at "+std::to_string(x/4)+","+std::to_string(y)+" channel "+std::to_string(x%4)+": "+std::to_string(av)+" vs "+std::to_string(bv));
    }
}
void correctness(const std::shared_ptr<VulkanDevice>& device) {
    VulkanFrameCompositor gpu(device);auto a=fixture(37,23,false),b=fixture(19,13);
    for(double rotation:{0.,23.,90.,-45.})for(double opacity:{0.,0.37,1.}){
        std::vector<CompositionLayer> layers{{&a},{&b}};layers[1].transform={0.43,0.61,0.78,rotation,opacity};
        auto cpu=FrameCompositor::compose(61,47,layers);auto result=gpu.compose(61,47,layers);
        if(rotation && !device->supportsPreciseRotation()){require(result.status==OpenGlCompositionStatus::Unsupported,"Rotation must recover without shaderFloat64.");continue;}
        require(result.status==OpenGlCompositionStatus::Complete,result.cause);parity(*cpu,*result.frame);
    }
    OpenGlImageOperation op;op.background.rgba=&b;op.kind=OpenGlImageOperationKind::Color;
    op.colors={{13,123,77},{-7,91,118}};
    auto expected=b;for(const auto& color:op.colors)require(effects::applyColorAdjustment(expected,color)==effects::ProcessingResult::Completed,"CPU color failed.");
    auto texture=gpu.processImage(op);require(texture.status==OpenGlCompositionStatus::Complete,texture.cause);
    auto result=gpu.readback(texture.frame);require(bool(result.frame),result.cause);parity(expected,*result.frame);
    op.kind=OpenGlImageOperationKind::GaussianBlur;op.blur_radius_pixels=3;
    expected=b;require(effects::applyGaussianBlur(expected,3)==effects::ProcessingResult::Completed,"CPU blur failed.");
    texture=gpu.processImage(op);require(bool(texture.frame),texture.cause);result=gpu.readback(texture.frame);parity(expected,*result.frame,0);
    auto coverage=fixture(b.width,b.height);op={};op.kind=OpenGlImageOperationKind::ApplyMask;op.background.rgba=&b;op.mask.rgba=&coverage;
    expected=b;require(applyMask(expected,coverage),"CPU mask failed.");texture=gpu.processImage(op);require(bool(texture.frame),texture.cause);
    result=gpu.readback(texture.frame);parity(expected,*result.frame,0);
    op.kind=OpenGlImageOperationKind::MaskedBlend;op.foreground.rgba=&expected;auto blended=expected;
    require(maskedBlend(b,blended,coverage),"CPU masked blend failed.");texture=gpu.processImage(op);require(bool(texture.frame),texture.cause);
    result=gpu.readback(texture.frame);parity(blended,*result.frame,0);
    require(gpu.compose(61,47,{{&a}},[]{return true;}).status==OpenGlCompositionStatus::Cancelled,"Cancelled work published output.");
    require(gpu.compose(-1,1,{}).status==OpenGlCompositionStatus::Unsupported,"Negative canvas was accepted.");
    std::vector<OpenGlTextureFramePtr> held;
    texture.frame.reset();for(int i=0;i<3;++i){auto f=gpu.composeTexture(61,47,{{&a}});require(bool(f.frame),f.cause);held.push_back(f.frame);}
    require(gpu.composeTexture(61,47,{{&a}}).status==OpenGlCompositionStatus::Busy,"Retained output limit was not enforced.");
    held.clear();OpenGlCompositionTimings timings;texture=gpu.composeTexture(61,47,{{&a}}, {}, &timings);
    require(bool(texture.frame),texture.cause);require(timings.readback_bytes==0,"Direct Vulkan delivery read back pixels.");
    require(texture.frame->backend()==GraphicsBackend::Vulkan && !texture.frame->bottomLeftOrigin(),"Vulkan lease metadata is wrong.");
    require(device->reservedBytes()<=256ULL*1024*1024,"Vulkan resource budget exceeded.");
    auto shared_budget=std::make_shared<OpenGlTexturePoolBudget>(64ULL*1024*1024,1);
    OpenGlTextureFramePtr retired;
    {VulkanFrameCompositor first(device,shared_budget->byteLimit(),shared_budget->targetLimit(),shared_budget);
        auto output=first.composeTexture(61,47,{{&a}});require(bool(output.frame),output.cause);retired=output.frame;}
    VulkanFrameCompositor second(device,shared_budget->byteLimit(),shared_budget->targetLimit(),shared_budget);
    require(second.composeTexture(61,47,{{&a}}).status==OpenGlCompositionStatus::Busy,"Retired Vulkan leases escaped the shared pool budget.");
    retired.reset();require(second.composeTexture(61,47,{{&a}}).status==OpenGlCompositionStatus::Complete,"Released shared Vulkan capacity was not reusable.");
    QWindow window;device->attachWindow(&window);window.resize(320,180);window.create();std::string cause;
    require(device->present(&window,texture.frame,false,cause),cause);
    QSemaphore locked,resume;OpenGlTextureCompositionResult threaded;
    std::thread worker([&]{VulkanFrameCompositor compositor(device);int calls=0;
        threaded=compositor.composeTexture(61,47,{{&a}},[&]{if(++calls==2){locked.release();resume.acquire();}return false;});});
    const bool acquired=locked.tryAcquire(1,3000);
    bool deferred=false;
    if(acquired){cause="previous";deferred=!device->present(&window,texture.frame,false,cause) && cause.empty();}
    resume.release();worker.join();
    require(acquired && deferred && bool(threaded.frame),"GUI presentation did not defer while the render worker owned the device.");
    window.resize(240,320);require(device->present(&window,texture.frame,true,cause),cause);device->releaseWindow(&window);
    auto retained=texture.frame;{VulkanFrameCompositor temporary(device);auto f=temporary.composeTexture(9,7,{});retained=f.frame;}
    require(retained && retained->valid(),"A lease did not retain its device after compositor destruction.");
    std::cout<<"native_vulkan_composition=passed direct_readback_bytes="<<timings.readback_bytes<<" reserved_bytes="<<device->reservedBytes()<<"\n";
}
void benchmark(const std::shared_ptr<VulkanDevice>& device) {
    using Clock=std::chrono::steady_clock;
    auto surface=OpenGlFrameCompositor::createSurface();
    for(auto size:{QSize(480,270),QSize(960,540),QSize(1920,1080)}){
        auto a=fixture(size.width(),size.height(),false),b=fixture(size.width()/2,size.height()/2);
        std::vector<CompositionLayer> layers{{&a},{&b},{&b}};layers[1].transform.rotation_degrees=23;layers[2].transform.position_x=0.7;
        for(int trial=1;trial<=3;++trial)for(const std::string backend:{"cpu","opengl","vulkan"}){
            std::unique_ptr<GpuFrameCompositor> gpu;
            if(backend=="vulkan")gpu=std::make_unique<VulkanFrameCompositor>(device);
            else if(backend=="opengl")gpu=std::make_unique<OpenGlFrameCompositor>(surface.get());
            std::vector<double> samples;OpenGlCompositionTimings total;
            system_monitor::PerformanceSampler sampler;static_cast<void>(sampler.sample());double cold_ms=0;
            for(int i=-3;i<12;++i){const auto start=Clock::now();
                if(gpu){OpenGlCompositionTimings timing;auto result=gpu->compose(size.width(),size.height(),layers,{},&timing);require(bool(result.frame),result.cause);
                    if(i>=0){total.uploaded_bytes+=timing.uploaded_bytes;total.readback_bytes+=timing.readback_bytes;}}
                else require(bool(FrameCompositor::compose(size.width(),size.height(),layers)),"CPU benchmark failed.");
                if(i==-3)cold_ms=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
                if(i>=0)samples.push_back(std::chrono::duration<double,std::milli>(Clock::now()-start).count());
            }
            double sum=0;for(auto value:samples)sum+=value;std::sort(samples.begin(),samples.end());
            const auto usage=sampler.sample();
            std::cout<<"benchmark backend="<<backend<<" width="<<size.width()<<" height="<<size.height()<<" trial="<<trial
                <<" average_ms="<<sum/samples.size()<<" p95_ms="<<samples.back()<<" uploaded_bytes="<<total.uploaded_bytes
                <<" readback_bytes="<<total.readback_bytes<<" reserved_gpu_bytes="<<(gpu?gpu->resourceUsage().texture_bytes:0)<<" working_set_bytes="<<usage.process_working_set_bytes.value_or(0)<<" process_cpu_percent="<<usage.process_cpu_percent.value_or(0)<<" cold_composition_ms="<<cold_ms<<"\n";
        }
    }
}
}
int main(int argc,char** argv) {
    QSurfaceFormat format;format.setVersion(3,2);format.setProfile(QSurfaceFormat::CoreProfile);QSurfaceFormat::setDefaultFormat(format);
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);QApplication app(argc,argv);
    const bool required=app.arguments().contains("--require-gpu");
    if(QGuiApplication::platformName()=="offscreen" || QGuiApplication::platformName()=="minimal")return required?1:77;
    const auto initialization=std::chrono::steady_clock::now();
    std::string cause;auto device=VulkanDevice::create(cause);
    const auto initialization_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-initialization).count();
    if(!device){std::cerr<<cause<<"\n";return required?1:77;}
    try{std::cout<<"device="<<device->name().toStdString()<<" initialization_ms="<<initialization_ms<<" precise_rotation="<<device->supportsPreciseRotation()<<"\n";
        correctness(device);if(app.arguments().contains("--benchmark"))benchmark(device);return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}
}
