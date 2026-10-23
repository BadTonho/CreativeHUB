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
#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
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
    require(!device->supportsNativeExport(),"Ordinary Vulkan creation enabled interop implicitly.");
    const auto original=device->totalReservedBytes();auto lease=device->tryReserveExternal(1024*1024);
    require(lease && device->totalReservedBytes()==original+1024*1024,"External reservation was not charged.");
    auto copy=lease;lease.reset();require(device->totalReservedBytes()==original+1024*1024,"Copied reservation released early.");
    require(!device->tryReserveExternal(0) && !device->tryReserveExternal(256ULL*1024*1024),"Invalid reservation was admitted.");
    copy.reset();require(device->totalReservedBytes()==original,"Reservation did not retire.");
    const auto checkMemory=[&]{auto stats=device->diagnostics();
        require(stats.active_bytes+stats.pending_bytes+stats.cached_bytes+stats.external_bytes==stats.known_bytes,"Known Vulkan memory classes do not reconcile.");
        require(stats.known_bytes==device->totalReservedBytes() && stats.known_bytes<=256ULL*1024*1024,"Memory diagnostics disagree with admission.");};
    checkMemory();
    VulkanFrameCompositor gpu(device);auto a=fixture(37,23,false),b=fixture(19,13);
    OpenGlImageOperation glow;glow.kind=OpenGlImageOperationKind::Glow;glow.background={&b};
    require(gpu.processImage(glow).status==OpenGlCompositionStatus::Unsupported,
        "Vulkan must report Glow as unsupported so callers can fall back to CPU.");
    CompositionLayer alpha_layer{&b};
    const auto alpha_refusal = gpu.compose(19, 13, {alpha_layer}, {}, nullptr,
        CompositionOutputAlpha::Preserve);
    require(alpha_refusal.status == OpenGlCompositionStatus::Unsupported &&
        !alpha_refusal.frame,
        "Vulkan must refuse transparent output so callers can fall back to CPU.");
    const auto alpha_readback_refusal = gpu.submitAsyncReadback(19, 13, {alpha_layer}, {},
        CompositionOutputAlpha::Preserve);
    require(alpha_readback_refusal.status == OpenGlCompositionStatus::Unsupported,
        "Vulkan must refuse asynchronous transparent output before composition.");
    const auto alpha_cpu = FrameCompositor::compose(19, 13, {alpha_layer}, nullptr,
        CompositionOutputAlpha::Preserve);
    require(alpha_cpu && alpha_cpu->rgba_pixels[3] == 0,
        "CPU fallback must retain empty-area alpha after Vulkan refusal.");
    for(unsigned m=1;m<6;++m) {CompositionLayer blend{&a};blend.blend_mode=static_cast<BlendMode>(m);
        const auto refused=gpu.composeTexture(37,23,{blend});
        require(refused.status==OpenGlCompositionStatus::Unsupported&&!refused.frame,"Vulkan must explicitly refuse blend modes before rendering");}

    CompositionLayer affine{&a};affine.source_to_canvas=AffineTransform2D{1,0,.5,2,3,4};
    require(gpu.compose(61,47,{affine}).status==OpenGlCompositionStatus::Unsupported,
        "Vulkan silently ignored the affine override.");
    OpenGlImageOperation affine_stage;affine_stage.kind=OpenGlImageOperationKind::Transform;
    affine_stage.background={&a};affine_stage.output_width=61;affine_stage.output_height=47;affine_stage.source_to_canvas=affine.source_to_canvas;
    require(gpu.processImage(affine_stage).status==OpenGlCompositionStatus::Unsupported,
        "Vulkan silently ignored affine Transform geometry.");
    for(double rotation:{0.,23.,90.,-45.})for(double opacity:{0.,0.37,1.}){
        std::vector<CompositionLayer> layers{{&a},{&b}};layers[1].transform={0.43,0.61,0.78,rotation,opacity};
        auto cpu=FrameCompositor::compose(61,47,layers);auto result=gpu.compose(61,47,layers);
        if(rotation && !device->supportsPreciseRotation()){require(result.status==OpenGlCompositionStatus::Unsupported,"Rotation must recover without shaderFloat64.");continue;}
        require(result.status==OpenGlCompositionStatus::Complete,result.cause);parity(*cpu,*result.frame);checkMemory();
    }
    {auto empty=gpu.compose(41,29,{});require(empty.status==OpenGlCompositionStatus::Complete,empty.cause);parity(*FrameCompositor::compose(41,29,{}),*empty.frame);
        auto grade = effects::makeDefaultInstance("video.primary_grade");
        require(effects::setParameterValue(grade, "red_gain", 135) &&
            effects::setParameterValue(grade, "blue_gain", 76), "Vulkan grade setup failed.");
        const std::vector<effects::EffectInstance> stack{{"video.brightness",{{"amount",7}},true},
            grade,{"video.saturation",{{"amount",80}},true}};
        std::vector<CompositionLayer> layers{{&a},{&b}};layers[0].effect_stack=stack;
        auto actual=gpu.compose(61,47,layers);require(actual.status==OpenGlCompositionStatus::Complete,actual.cause);parity(*FrameCompositor::compose(61,47,layers),*actual.frame);checkMemory();}
    OpenGlImageOperation op;op.background.rgba=&b;op.kind=OpenGlImageOperationKind::Color;
    op.colors={{13,123,77,145,82,115},{-7,91,118}};
    auto expected=b;for(const auto& color:op.colors)require(effects::applyColorAdjustment(expected,color)==effects::ProcessingResult::Completed,"CPU color failed.");
    auto texture=gpu.processImage(op);require(texture.status==OpenGlCompositionStatus::Complete,texture.cause);
    auto result=gpu.readback(texture.frame);require(bool(result.frame),result.cause);parity(expected,*result.frame);
    op.kind=OpenGlImageOperationKind::GaussianBlur;op.blur_radius_pixels=3;
    expected=b;require(effects::applyGaussianBlur(expected,3)==effects::ProcessingResult::Completed,"CPU blur failed.");
    texture=gpu.processImage(op);require(bool(texture.frame),texture.cause);result=gpu.readback(texture.frame);parity(expected,*result.frame,0);
    for(int mode=0;mode<4;++mode) {
        OpenGlImageOperation matte;matte.kind=OpenGlImageOperationKind::ApplyTrackMatte;matte.background.rgba=&b;
        matte.track_matte_mode=static_cast<TrackMatteMode>(mode);
        require(gpu.processImage(matte).status==OpenGlCompositionStatus::Unsupported,"Vulkan must explicitly reject track matte operations");
    }
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
    {
        VulkanFrameCompositor asynchronous(device);
        unsigned prepared=0;
        require(asynchronous.prepareAsyncReadback(std::numeric_limits<int>::max(),2,std::numeric_limits<std::uint64_t>::max()).status==OpenGlCompositionStatus::Unsupported,
            "An invalid readback canvas reached the Vulkan allocator.");
        auto ready=asynchronous.prepareAsyncReadback(61,47,61ULL*47*4*2,2,&prepared);
        require(ready.status==OpenGlCompositionStatus::Complete && prepared==2,"Two bounded Vulkan readback slots were not prepared.");
        auto first=asynchronous.submitAsyncReadback(61,47,{{&a}});
        auto last=asynchronous.submitAsyncReadback(61,47,{{&b}});
        require(first.status==OpenGlCompositionStatus::Complete && last.status==OpenGlCompositionStatus::Complete,"Asynchronous submission failed.");
        const auto polled_first=asynchronous.pollAsyncReadback(first.ticket);
        require(polled_first.status==OpenGlCompositionStatus::Busy ||
                polled_first.status==OpenGlCompositionStatus::Complete,
            "Nonblocking Vulkan poll returned an invalid readback state.");
        require(asynchronous.pollAsyncReadback(last.ticket).status==OpenGlCompositionStatus::Busy,
            "Vulkan polling accepted an out-of-order ticket.");
        require(first.composition_timings.uploaded_bytes>0 && first.composition_timings.readback_bytes==0,"Asynchronous composition lost transfer diagnostics.");
        require(asynchronous.submitAsyncReadback(61,47,{{&a}}).status==OpenGlCompositionStatus::Busy,"Readback capacity was not bounded.");
        require(asynchronous.collectAsyncReadback(last.ticket).status==OpenGlCompositionStatus::Busy,"Out-of-order collection was accepted.");
        require(asynchronous.collectAsyncReadback(first.ticket,[]{return true;}).status==OpenGlCompositionStatus::Cancelled,"Pending readback ignored cancellation.");
        auto first_pixels=asynchronous.collectAsyncReadback(first.ticket);auto last_pixels=asynchronous.collectAsyncReadback(last.ticket);
        require(first_pixels.frame && last_pixels.frame,"Submitted Vulkan pixels were lost.");
        parity(*FrameCompositor::compose(61,47,{{&a}}),*first_pixels.frame);
        parity(*FrameCompositor::compose(61,47,{{&b}}),*last_pixels.frame);
        require(asynchronous.collectAsyncReadback(first.ticket).status==OpenGlCompositionStatus::Unsupported,"A released ticket was accepted.");
        auto batch=asynchronous.beginCommandBatch();
        auto before=device->diagnostics();
        OpenGlImageOperation copy;copy.background.rgba=&a;
        auto source=asynchronous.processImage(copy);require(bool(source.frame),source.cause);
        copy.background={nullptr,source.frame};copy.kind=OpenGlImageOperationKind::Color;copy.colors={{10,100,100}};
        auto effect=asynchronous.processImage(copy);require(bool(effect.frame),effect.cause);
        require(device->diagnostics().submissions==before.submissions,"An image operation submitted within an open batch.");
        require(batch && batch->finish().status==OpenGlCompositionStatus::Complete,"Explicit Vulkan batch submission failed.");
        require(device->diagnostics().submissions==before.submissions+1,"A graph batch did not share one submission.");
        auto input_ticket=asynchronous.prepareAsyncReadback(a.width,a.height,uint64_t(a.width)*a.height*4,2,&prepared);
        require(input_ticket.status==OpenGlCompositionStatus::Complete && prepared==1,"Staging memory did not reduce the slot count.");
        auto ticket=asynchronous.submitAsyncReadback(effect.frame);require(ticket.status==OpenGlCompositionStatus::Complete,"Texture readback submission failed.");
        require(asynchronous.collectAsyncReadback(ticket.ticket,{},true).status==OpenGlCompositionStatus::Failed,"Injected collection failure was ignored.");
        asynchronous.discardAsyncReadbacks();
        require(asynchronous.collectAsyncReadback(ticket.ticket).status==OpenGlCompositionStatus::Unsupported,"Discarded ticket remained live.");
        require(asynchronous.prepareAsyncReadback(a.width,a.height,uint64_t(a.width)*a.height*4,1).status==OpenGlCompositionStatus::Complete,"Readback reconfiguration failed.");
        auto replacement=asynchronous.submitAsyncReadback(effect.frame);
        require(replacement.status==OpenGlCompositionStatus::Complete && replacement.ticket!=ticket.ticket,"Reconfiguration reused an old ticket identity.");
        require(asynchronous.collectAsyncReadback(ticket.ticket).status==OpenGlCompositionStatus::Unsupported,"A stale ticket collected another frame after reconfiguration.");
        require(asynchronous.collectAsyncReadback(replacement.ticket).frame.has_value(),"The replacement readback was lost.");
    }
    QWindow window;device->attachWindow(&window);window.resize(320,180);window.create();std::string cause;
    auto presentation=device->presentFrame(&window,texture.frame,false);
    require(presentation.status==VulkanDevice::PresentStatus::Submitted,presentation.cause);
    require(device->presentFrame(nullptr,texture.frame,false).status==VulkanDevice::PresentStatus::Failed,
        "An invalid presentation window was accepted.");
    require(device->pollPresentation({},cause)==VulkanDevice::CompletionStatus::Failed && device->valid(),
        "An invalid receipt was accepted or disabled a valid device.");
    const auto receipt=presentation.receipt;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(device->pollPresentation(receipt,cause)==VulkanDevice::CompletionStatus::Pending && std::chrono::steady_clock::now()<deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    require(device->pollPresentation(receipt,cause)==VulkanDevice::CompletionStatus::Complete,cause);
    const auto submissions=device->diagnostics().submissions;
    for(int repeat=0;repeat<10;++repeat){
        auto deferred=device->presentFrame(&window,texture.frame,false,{0,0,0,1},true);
        require(deferred.status==VulkanDevice::PresentStatus::Deferred && deferred.deferral==VulkanDevice::PresentDeferral::AcquireBusy && deferred.cause.empty(),"Controlled acquisition did not defer.");
    }
    require(device->diagnostics().submissions==submissions,"Unavailable acquisition submitted empty GPU commands.");
    auto recovered=device->presentFrame(&window,texture.frame,false);
    require(recovered.status==VulkanDevice::PresentStatus::Submitted,recovered.cause);
    require(device->pollPresentation(receipt,cause)==VulkanDevice::CompletionStatus::Complete,"Context reuse invalidated an older receipt.");
    QSemaphore locked,resume;OpenGlTextureCompositionResult threaded;
    std::thread worker([&]{VulkanFrameCompositor compositor(device);int calls=0;
        threaded=compositor.composeTexture(61,47,{{&a}},[&]{if(++calls==3){locked.release();resume.acquire();}return false;});});
    const bool acquired=locked.tryAcquire(1,3000);
    bool deferred=false,upload_deferred=false,receipt_independent=false;
    if(acquired){VulkanFrameCompositor gui_upload(device);
        upload_deferred=gui_upload.tryUpload(a).status==OpenGlCompositionStatus::Busy;
        receipt_independent=device->pollPresentation(receipt,cause)==VulkanDevice::CompletionStatus::Complete;
        cause="previous";deferred=!device->present(&window,texture.frame,false,cause) && cause.empty();}
    resume.release();worker.join();
    require(acquired && deferred && upload_deferred && receipt_independent && bool(threaded.frame),"GUI presentation did not defer while the render worker owned the device.");
    const auto unrelated_pending=device->diagnostics().pending;
    require((unrelated_pending>0 || qEnvironmentVariableIntValue("CREATIVE_SUITE_VULKAN_SERIAL")==1) && device->pollPresentation(receipt,cause)==VulkanDevice::CompletionStatus::Complete &&
        device->diagnostics().pending==unrelated_pending,"Unrelated pending work affected this receipt or was globally drained.");
    window.resize(240,320);require(device->present(&window,texture.frame,true,cause),cause);device->releaseWindow(&window);
    require(device->pollPresentation(receipt,cause)==VulkanDevice::CompletionStatus::Complete,"Window retirement invalidated a completed receipt.");
    auto retained=texture.frame;{VulkanFrameCompositor temporary(device);auto f=temporary.composeTexture(9,7,{});retained=f.frame;}
    require(retained && retained->valid(),"A lease did not retain its device after compositor destruction.");
    {
        std::string reason;auto owned_device=VulkanDevice::create(reason);require(bool(owned_device),reason);
        std::weak_ptr<VulkanDevice> weak=owned_device;OpenGlTextureFramePtr lease;
        {VulkanFrameCompositor producer(owned_device);auto pending=producer.composeTexture(9,7,{});require(bool(pending.frame),pending.cause);lease=std::move(pending.frame);}
        VulkanDevice::PresentationReceipt weak_receipt;
        {
            QWindow receipt_window;owned_device->attachWindow(&receipt_window);receipt_window.resize(80,60);receipt_window.create();
            auto result=owned_device->presentFrame(&receipt_window,lease,false);
            require(result.status==VulkanDevice::PresentStatus::Submitted,result.cause);weak_receipt=result.receipt;
            owned_device->releaseWindow(&receipt_window);
        }
        owned_device.reset();require(!weak.expired(),"A submitted lease did not retain its device.");
        lease.reset();require(weak.expired(),"A submission or presentation receipt formed a device ownership cycle.");
    }
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
void primaryGradeOnly(const std::shared_ptr<VulkanDevice>& device) {
    VulkanFrameCompositor gpu(device);
    auto source = fixture(31, 19, false);
    auto grade = effects::makeDefaultInstance("video.primary_grade");
    require(effects::setParameterValue(grade, "red_gain", 145) &&
        effects::setParameterValue(grade, "green_gain", 82) &&
        effects::setParameterValue(grade, "blue_gain", 115) &&
        effects::setParameterValue(grade, "lift_red", -23) &&
        effects::setParameterValue(grade, "gamma_green", 138) &&
        effects::setParameterValue(grade, "offset_blue", 13) &&
        effects::setParameterValue(grade, "contrast", 127) &&
        effects::setParameterValue(grade, "pivot", 43) &&
        effects::setParameterValue(grade, "temperature", 34) &&
        effects::setParameterValue(grade, "tint", -19), "Vulkan grade fixture failed.");
    const std::array<effects::EffectInstance, 1> stack{grade};
    CompositionLayer layer{&source}; layer.effect_stack = stack;
    const auto cpu = FrameCompositor::compose(source.width, source.height, {layer});
    const auto gpu_frame = gpu.compose(source.width, source.height, {layer});
    require(cpu && gpu_frame.frame, gpu_frame.cause);
    parity(*cpu, *gpu_frame.frame);
    OpenGlImageOperation operation; operation.kind = OpenGlImageOperationKind::Color;
    operation.background.rgba = &source;
    effects::ColorAdjustmentParameters expanded{15, 125, 85, 145, 82, 115};
    expanded.lift_red = -23; expanded.lift_green = 11; expanded.lift_blue = 7;
    expanded.gamma_red = 72; expanded.gamma_green = 138; expanded.gamma_blue = 221;
    expanded.offset_red = 6; expanded.offset_green = -9; expanded.offset_blue = 13;
    expanded.pivot_percent = 43; expanded.temperature = 34; expanded.tint = -19;
    operation.colors = {expanded};
    auto expected = source;
    require(effects::applyColorAdjustment(expected, operation.colors.front()) ==
        effects::ProcessingResult::Completed, "Vulkan image grade CPU reference failed.");
    const auto texture = gpu.processImage(operation);
    require(bool(texture.frame), texture.cause);
    const auto readback = gpu.readback(texture.frame);
    require(bool(readback.frame), readback.cause);
    parity(expected, *readback.frame);
    operation.kind = OpenGlImageOperationKind::Adjustment;
    operation.foreground.rgba = &source;
    const auto adjustment = gpu.processImage(operation);
    require(bool(adjustment.frame), adjustment.cause);
    const auto adjustment_pixels = gpu.readback(adjustment.frame);
    require(bool(adjustment_pixels.frame), adjustment_pixels.cause);
    parity(expected, *adjustment_pixels.frame);
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
        if (app.arguments().contains("--color-only")) primaryGradeOnly(device);
        else correctness(device);
        if(app.arguments().contains("--benchmark"))benchmark(device);return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}
}
