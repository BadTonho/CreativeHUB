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
media::RgbaFrame solidFixture(int w,int h,std::array<std::uint8_t,4> color) {
    media::RgbaFrame frame{w,h,w*4,{}};
    frame.rgba_pixels.resize(static_cast<std::size_t>(frame.stride)*h);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x)
        std::copy(color.begin(),color.end(),frame.rgba_pixels.begin()+
            static_cast<std::ptrdiff_t>(y*frame.stride+x*4));
    return frame;
}
bool pixelIs(const media::RgbaFrame& frame,int x,int y,
    std::array<int,3> minimums,std::array<int,3> maximums) {
    const auto* pixel=frame.rgba_pixels.data()+static_cast<std::size_t>(y)*frame.stride+x*4;
    for(int channel=0;channel<3;++channel)
        if(pixel[channel]<minimums[channel] || pixel[channel]>maximums[channel])return false;
    return true;
}
void parity(const media::RgbaFrame& a,const media::RgbaFrame& b,int tolerance=2,
            const char* context="Vulkan") {
    static int parity_call = 0;
    ++parity_call;
    require(a.width==b.width && a.height==b.height,"Vulkan output dimensions differ.");
    for(int y=0;y<a.height;++y)for(int x=0;x<a.width*4;++x){
        auto av=a.rgba_pixels[size_t(y)*a.stride+x],bv=b.rgba_pixels[size_t(y)*b.stride+x];
        if(std::abs(int(av)-int(bv))>(x%4==3?0:tolerance))throw std::runtime_error(std::string(context)+" parity call "+std::to_string(parity_call)+" pixel mismatch at "+std::to_string(x/4)+","+std::to_string(y)+" channel "+std::to_string(x%4)+": "+std::to_string(av)+" vs "+std::to_string(bv));
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
        auto curves = effects::makeDefaultInstance("video.curves");
        curves.video_curves->rgb.points = {{0, 0}, {92, 124}, {255, 255}};
        curves.video_curves->targeted[0] = {{0, 143}, {255, 114}};
        curves.video_curves->targeted[1] = {{0, 128}, {255, 203}};
        curves.video_curves->targeted[2] = {{0, 128}, {255, 76}};
        curves.video_curves->targeted[3] = {{0, 151}, {255, 128}};
        curves.video_curves->targeted[4] = {{0, 128}, {255, 171}};
        curves.video_curves->qualifier.enabled = true;
        curves.video_curves->qualifier.hue_min = 338.0;
        curves.video_curves->qualifier.hue_max = 24.0;
        curves.video_curves->qualifier.hue_softness = 8.0;
        curves.video_curves->qualifier.saturation_min = 12.0;
        curves.video_curves->qualifier.saturation_max = 95.0;
        curves.video_curves->qualifier.saturation_softness = 5.0;
        curves.video_curves->qualifier.luminance_min = 8.0;
        curves.video_curves->qualifier.luminance_max = 94.0;
        curves.video_curves->qualifier.luminance_softness = 4.0;
        require(effects::setParameterValue(grade, "red_gain", 135) &&
            effects::setParameterValue(grade, "blue_gain", 76), "Vulkan grade setup failed.");
        const std::vector<effects::EffectInstance> stack{{"video.brightness",{{"amount",7}},true},
            grade, curves,{"video.saturation",{{"amount",80}},true}};
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
    const auto current_pixels=solidFixture(160,90,{225,32,24,255});
    const auto reference_pixels=solidFixture(160,90,{20,45,225,255});
    const auto current_texture=gpu.composeTexture(160,90,{{&current_pixels}});
    const auto reference_texture=gpu.composeTexture(160,90,{{&reference_pixels}});
    require(current_texture.frame && reference_texture.frame,
        "Vulkan comparison pixel fixtures failed to compose.");
    texture.frame=current_texture.frame;
    QWindow window;device->attachWindow(&window);window.resize(320,180);window.create();std::string cause;
    FrameComparisonOptions retain_output;
    retain_output.retain_rendered_frame=true;
    auto presentation=device->presentFrame(&window,texture.frame,false,
        {0,0,0,1},false,retain_output);
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
    const auto readPresented=[&](const VulkanDevice::PresentationResult& result) {
        require(bool(result.rendered_frame),"Vulkan presentation did not retain its GPU output.");
        const auto pixels=gpu.readback(result.rendered_frame);
        require(bool(pixels.frame),pixels.cause);
        return *pixels.frame;
    };
    auto off_pixels=readPresented(presentation);
    require(pixelIs(off_pixels,32,45,{200,0,0},{255,80,80}) &&
        pixelIs(off_pixels,128,45,{200,0,0},{255,80,80}),
        "Vulkan Off comparison changed the current Viewer pixels.");

    const auto comparison_reference = reference_texture;
    for (const auto [mode, fraction] : std::array{
            std::pair{FrameComparisonMode::SplitScreen, 0.5F},
            std::pair{FrameComparisonMode::Wipe, 0.0F},
            std::pair{FrameComparisonMode::Wipe, 0.5F},
            std::pair{FrameComparisonMode::Wipe, 1.0F}}) {
        const FrameComparisonOptions comparison{mode, comparison_reference.frame, fraction};
        auto retained_comparison=comparison;
        retained_comparison.retain_rendered_frame=true;
        const auto compared = device->presentFrame(&window, texture.frame, false,
            {0, 0, 0, 1}, false, retained_comparison);
        require(compared.status == VulkanDevice::PresentStatus::Submitted, compared.cause);
        const auto compare_deadline = std::chrono::steady_clock::now() +
            std::chrono::seconds(3);
        auto compare_status = device->pollPresentation(compared.receipt, cause);
        while (compare_status == VulkanDevice::CompletionStatus::Pending &&
            std::chrono::steady_clock::now() < compare_deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            compare_status = device->pollPresentation(compared.receipt, cause);
        }
        require(compare_status == VulkanDevice::CompletionStatus::Complete, cause);
        const auto compared_pixels=readPresented(compared);
        if(mode==FrameComparisonMode::SplitScreen) {
            require(pixelIs(compared_pixels,32,45,{0,0,200},{80,100,255}) &&
                pixelIs(compared_pixels,128,45,{200,0,0},{255,80,80}) &&
                pixelIs(compared_pixels,32,4,{0,0,0},{35,35,35}),
                "Vulkan split-screen order or aspect-fit pixels are incorrect.");
        } else if(fraction==0.0F) {
            require(pixelIs(compared_pixels,32,45,{200,0,0},{255,80,80}) &&
                pixelIs(compared_pixels,128,45,{200,0,0},{255,80,80}),
                "Vulkan wipe at zero percent must show only the current Viewer.");
        } else if(fraction==0.5F) {
            require(pixelIs(compared_pixels,32,45,{0,0,200},{80,100,255}) &&
                pixelIs(compared_pixels,128,45,{200,0,0},{255,80,80}),
                "Vulkan wipe at fifty percent has incorrect reference/current pixels.");
        } else {
            require(pixelIs(compared_pixels,32,45,{0,0,200},{80,100,255}) &&
                pixelIs(compared_pixels,128,45,{0,0,200},{80,100,255}),
                "Vulkan wipe at one hundred percent must show only the reference.");
        }
    }
    const auto submissions=device->diagnostics().submissions;
    for(int repeat=0;repeat<10;++repeat){
        auto deferred=device->presentFrame(&window,texture.frame,false,{0,0,0,1},true);
        require(deferred.status==VulkanDevice::PresentStatus::Deferred && deferred.deferral==VulkanDevice::PresentDeferral::AcquireBusy && deferred.cause.empty(),"Controlled acquisition did not defer.");
    }
    require(device->diagnostics().submissions==submissions,"Unavailable acquisition submitted empty GPU commands.");
    auto recovered=device->presentFrame(&window,texture.frame,false);
    require(recovered.status==VulkanDevice::PresentStatus::Submitted,recovered.cause);
    require(!recovered.rendered_frame,
        "Default Vulkan presentation unexpectedly retained a rendered-frame wrapper.");
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
        effects::setParameterValue(grade, "tint", -19) &&
        effects::setParameterValue(grade, "log_shadows_red", 38) &&
        effects::setParameterValue(grade, "log_midtones_green", -22) &&
        effects::setParameterValue(grade, "log_highlights_blue", 17), "Vulkan grade fixture failed.");
    auto curves_effect = effects::makeDefaultInstance("video.curves");
    curves_effect.video_curves->rgb.points = {{0, 0}, {92, 124}, {255, 255}};
    curves_effect.video_curves->targeted[0] = {{0, 129}, {255, 127}};
    curves_effect.video_curves->targeted[1] = {{0, 128}, {255, 129}};
    curves_effect.video_curves->targeted[2] = {{0, 128}, {255, 127}};
    curves_effect.video_curves->targeted[3] = {{0, 129}, {255, 128}};
    curves_effect.video_curves->targeted[4] = {{0, 128}, {255, 129}};
    curves_effect.video_curves->qualifier.enabled = true;
    curves_effect.video_curves->qualifier.hue_min = 338.0;
    curves_effect.video_curves->qualifier.hue_max = 24.0;
    curves_effect.video_curves->qualifier.hue_softness = 8.0;
    curves_effect.video_curves->qualifier.saturation_min = 12.0;
    curves_effect.video_curves->qualifier.saturation_max = 95.0;
    curves_effect.video_curves->qualifier.saturation_softness = 5.0;
    curves_effect.video_curves->qualifier.luminance_min = 8.0;
    curves_effect.video_curves->qualifier.luminance_max = 94.0;
    curves_effect.video_curves->qualifier.luminance_softness = 4.0;
    const auto neutral_targeted = effects::VideoCurvesParameters{}.targeted;
    const std::array<effects::CurvesPointList, 5> targeted_stress{{
        {{0, 143}, {255, 114}}, {{0, 128}, {255, 203}},
        {{0, 128}, {255, 76}}, {{0, 151}, {255, 128}},
        {{0, 128}, {255, 171}}}};
    for (std::size_t targeted_index = 0;
         targeted_index < curves_effect.video_curves->targeted.size(); ++targeted_index) {
        auto isolated = curves_effect;
        isolated.video_curves->rgb = effects::CurvesParameters{};
        isolated.video_curves->targeted = neutral_targeted;
        isolated.video_curves->targeted[targeted_index] =
            targeted_stress[targeted_index];
        isolated.video_curves->qualifier.enabled = false;
        const std::array<effects::EffectInstance, 1> isolated_stack{isolated};
        CompositionLayer isolated_layer{&source};
        isolated_layer.effect_stack = isolated_stack;
        const auto isolated_cpu = FrameCompositor::compose(
            source.width, source.height, {isolated_layer});
        const auto isolated_gpu = gpu.compose(
            source.width, source.height, {isolated_layer});
        require(isolated_cpu && isolated_gpu.frame,
            "Vulkan isolated targeted Curves pass failed: " + isolated_gpu.cause);
        parity(*isolated_cpu, *isolated_gpu.frame, 1,
            ("Vulkan isolated targeted Curves family " +
             std::to_string(targeted_index)).c_str());
    }
    const std::array<effects::EffectInstance, 1> curves_only_stack{curves_effect};
    const auto curves_only_passes = effects::colorAdjustmentPasses(curves_only_stack);
    require(curves_only_passes && curves_only_passes->size() == 1 &&
                curves_only_passes->front().video_curves,
            "The Vulkan Curves graph node must create one typed GPU color pass.");
    CompositionLayer direct_curves_only_layer{&source};
    direct_curves_only_layer.gpu_color_adjustments = *curves_only_passes;
    CompositionLayer curves_only_layer{&source};
    curves_only_layer.effect_stack = curves_only_stack;
    const auto curves_only_cpu = FrameCompositor::compose(
        source.width, source.height, {curves_only_layer});
    const auto direct_curves_only = gpu.compose(
        source.width, source.height, {direct_curves_only_layer});
    require(curves_only_cpu && direct_curves_only.frame, direct_curves_only.cause);
    parity(*curves_only_cpu, *direct_curves_only.frame, 1, "Vulkan direct Curves-only");
    const auto curves_only_gpu = gpu.compose(
        source.width, source.height, {curves_only_layer});
    require(curves_only_cpu && curves_only_gpu.frame, curves_only_gpu.cause);
    parity(*curves_only_cpu, *curves_only_gpu.frame, 1, "Vulkan effect-stack Curves-only");
    const std::array<effects::EffectInstance, 2> stack{grade, curves_effect};
    CompositionLayer layer{&source}; layer.effect_stack = stack;
    const auto cpu = FrameCompositor::compose(source.width, source.height, {layer});
    const auto gpu_frame = gpu.compose(source.width, source.height, {layer});
    require(cpu && gpu_frame.frame, gpu_frame.cause);
    parity(*cpu, *gpu_frame.frame, 1, "Vulkan Primary Grade + Curves");
    OpenGlImageOperation operation; operation.kind = OpenGlImageOperationKind::Color;
    operation.background.rgba = &source;
    effects::ColorAdjustmentParameters expanded{15, 125, 85, 145, 82, 115};
    expanded.lift_red = -23; expanded.lift_green = 11; expanded.lift_blue = 7;
    expanded.gamma_red = 72; expanded.gamma_green = 138; expanded.gamma_blue = 221;
    expanded.offset_red = 6; expanded.offset_green = -9; expanded.offset_blue = 13;
    expanded.pivot_percent = 43; expanded.temperature = 34; expanded.tint = -19;
    expanded.log_shadows_red = 38; expanded.log_midtones_green = -22;
    expanded.log_highlights_blue = 17;
    effects::VideoCurvesParameters curves;
    curves.rgb.points = {{0, 0}, {104, 137}, {255, 255}};
    curves.targeted[0] = {{0, 129}, {255, 127}};
    curves.targeted[1] = {{0, 128}, {255, 129}};
    curves.targeted[2] = {{0, 128}, {255, 127}};
    curves.targeted[3] = {{0, 129}, {255, 128}};
    curves.targeted[4] = {{0, 128}, {255, 129}};
    curves.qualifier.enabled = true;
    curves.qualifier.hue_min = 340.0;
    curves.qualifier.hue_max = 20.0;
    curves.qualifier.hue_softness = 6.0;
    curves.qualifier.saturation_min = 8.0;
    curves.qualifier.saturation_max = 98.0;
    curves.qualifier.luminance_min = 5.0;
    curves.qualifier.luminance_max = 97.0;
    effects::ColorAdjustmentParameters curve_adjustment;
    curve_adjustment.video_curves = curves;
    auto refinement_source = fixture(31, 19, true);
    const std::array<std::array<double, 4>, 4> refinement_controls{{
        {30.0, 0.0, 100.0, 0.0},
        {0.0, 12.0, 91.0, -27.0},
        {0.0, 0.0, 100.0, 0.0},
        {65.0, 8.0, 89.0, 23.0}}};
    for (std::size_t index = 0; index < refinement_controls.size(); ++index) {
        auto spatial_refinement = curve_adjustment;
        const auto& controls = refinement_controls[index];
        spatial_refinement.video_curves->qualifier.denoise_percent = controls[0];
        spatial_refinement.video_curves->qualifier.clean_black_percent = controls[1];
        spatial_refinement.video_curves->qualifier.clean_white_percent = controls[2];
        spatial_refinement.video_curves->qualifier.in_out_percent = controls[3];
        spatial_refinement.video_curves->qualifier.blur_radius_pixels =
            index == 2 || index == 3 ? 2.0 : 0.0;
        auto expected_refinement = refinement_source;
        require(effects::applyColorAdjustment(expected_refinement, spatial_refinement) ==
                    effects::ProcessingResult::Completed,
                "Vulkan CPU qualifier refinement reference failed.");
        OpenGlImageOperation refined_operation;
        refined_operation.kind = OpenGlImageOperationKind::Color;
        refined_operation.background.rgba = &refinement_source;
        refined_operation.colors = {spatial_refinement};
        const auto refined_result = gpu.processImage(refined_operation);
        require(refined_result.status == OpenGlCompositionStatus::Complete &&
                    refined_result.frame,
                "Vulkan must process spatial Curves refinement on the GPU: " +
                    refined_result.cause);
        const auto refined_pixels = gpu.readback(refined_result.frame);
        require(bool(refined_pixels.frame),
                "Vulkan qualifier refinement readback failed: " + refined_pixels.cause);
        parity(expected_refinement, *refined_pixels.frame, 1,
            "Vulkan qualifier refinement and alpha preservation");
    }

    auto composed_refinement = curve_adjustment;
    composed_refinement.video_curves->qualifier.denoise_percent = 65.0;
    composed_refinement.video_curves->qualifier.clean_black_percent = 8.0;
    composed_refinement.video_curves->qualifier.clean_white_percent = 89.0;
    composed_refinement.video_curves->qualifier.in_out_percent = 23.0;
    composed_refinement.video_curves->qualifier.blur_radius_pixels = 2.0;
    auto composed_refinement_source = source;
    require(effects::applyColorAdjustment(composed_refinement_source,
                composed_refinement) == effects::ProcessingResult::Completed,
            "Vulkan CPU composed qualifier refinement reference failed.");
    CompositionLayer refined_layer{&source};
    refined_layer.gpu_color_adjustments = {composed_refinement};
    const auto refined_frame = gpu.compose(source.width, source.height, {refined_layer});
    const auto expected_refined_frame = FrameCompositor::compose(
        source.width, source.height, {{&composed_refinement_source}});
    require(refined_frame.status == OpenGlCompositionStatus::Complete &&
                refined_frame.frame && expected_refined_frame,
            "Vulkan composition must evaluate refined Curves on the GPU: " +
                refined_frame.cause);
    parity(*expected_refined_frame, *refined_frame.frame, 1,
        "Vulkan composed qualifier refinement");
    OpenGlImageOperation cancelled_refinement;
    cancelled_refinement.kind = OpenGlImageOperationKind::Color;
    cancelled_refinement.background.rgba = &source;
    cancelled_refinement.colors = {composed_refinement};
    int refinement_cancel_checks = 0;
    const auto cancelled_refinement_result = gpu.processImage(cancelled_refinement,
        [&] { return ++refinement_cancel_checks >= 3; });
    require(cancelled_refinement_result.status == OpenGlCompositionStatus::Cancelled &&
                !cancelled_refinement_result.frame,
            "Vulkan matte passes must honor cancellation without publishing a partial frame.");
    auto second_expanded = expanded;
    second_expanded.log_shadows_blue = -33;
    second_expanded.log_midtones_red = 18;
    second_expanded.log_highlights_green = 27;
    operation.colors = {expanded, second_expanded, curve_adjustment};
    auto expected = source;
    for (const auto& parameters : operation.colors)
        require(effects::applyColorAdjustment(expected, parameters) ==
            effects::ProcessingResult::Completed, "Vulkan image grade CPU reference failed.");
    const auto texture = gpu.processImage(operation);
    require(bool(texture.frame), texture.cause);
    const auto readback = gpu.readback(texture.frame);
    require(bool(readback.frame), readback.cause);
    parity(expected, *readback.frame, 1);
    operation.kind = OpenGlImageOperationKind::Adjustment;
    operation.foreground.rgba = &source;
    const auto adjustment = gpu.processImage(operation);
    require(bool(adjustment.frame), adjustment.cause);
    const auto adjustment_pixels = gpu.readback(adjustment.frame);
    require(bool(adjustment_pixels.frame), adjustment_pixels.cause);
    parity(expected, *adjustment_pixels.frame, 1);
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
