// Native, opt-in measurement tool. Generated media and logs stay in a temp
// directory; stdout contains reproducible aggregate results, never media paths.
#include "rendering/preview_renderer.h"
#include "ui/viewer/composition_viewer.h"
#include <creative_suite/media/video_encoder.h>
#include <creative_suite/diagnostics/logger.h>
#include <creative_suite/system_monitor/performance_usage.h>
#include <QApplication>
#include <QElapsedTimer>
#include <QOpenGLContext>
#include <QOffscreenSurface>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QThread>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace motion::ui;
using namespace motion::model;
auto pattern(int w, int h, bool alpha = false) {
    auto f = std::make_shared<creative_suite::media::RgbaFrame>();
    f->width = w; f->height = h; f->stride = w * 4;
    f->rgba_pixels.resize(std::size_t(f->stride) * h);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        auto* p = f->rgba_pixels.data() + std::size_t(y) * f->stride + x * 4;
        p[0] = std::uint8_t(x % 256); p[1] = std::uint8_t(y % 256);
        p[2] = std::uint8_t(((x/32 + y/32) % 2) * 160 + 30);
        p[3] = alpha ? 140 : 255;
    }
    return f;
}
PreviewRequest fixture(const std::filesystem::path& video, int fps) {
    PreviewRequest r{{1920,1080}, {fps,1}, {}};
    PreviewLayerSnapshot bg; bg.id=1; bg.kind=LayerKind::Image;
    bg.still_frame=pattern(1920,1080);
    bg.effects={GaussianBlurEffect{true,10},ColorAdjustmentEffect{true,3,105,95}};
    r.layers.push_back(bg);
    PreviewLayerSnapshot clip; clip.id=2; clip.kind=LayerKind::Video;
    clip.source_path=video; clip.source_frame_count=300; clip.source_frame_rate=30;
    clip.transform.scale=.55; clip.transform.position_x=.65;
    r.layers.push_back(clip);
    PreviewLayerSnapshot still; still.id=3; still.kind=LayerKind::Image;
    still.still_frame=pattern(480,320,true); still.transform.scale=.3;
    still.transform.position_x=.25; r.layers.push_back(still);
    PreviewLayerSnapshot text; text.id=4; text.kind=LayerKind::Text;
    auto words=defaultTextLayerContent({1920,1080}); words.text="Motion GPU reference";
    words.font_size_pixels=48; words.box_width=700; words.box_height=100;
    text.content=words; text.transform.position_y=.2; r.layers.push_back(text);
    PreviewLayerSnapshot shape; shape.id=5; shape.kind=LayerKind::Shape;
    auto ellipse=defaultShapeLayerContent({1920,1080},ShapeKind::Ellipse);
    ellipse.width=180; ellipse.height=180; ellipse.fill_color={220,40,80,160};
    shape.content=ellipse; shape.transform.position_y=.75; r.layers.push_back(shape);
    return r;
}
void makeVideo(const std::filesystem::path& path) {
    creative_suite::media::VideoEncodingSettings settings;
    settings.output_path=path; settings.container_name="matroska";
    settings.video_encoder_name="ffv1"; settings.width=640; settings.height=360;
    settings.frame_rate_numerator=30; settings.frame_rate_denominator=1;
    creative_suite::media::VideoEncoder encoder(settings);
    auto pixels=pattern(640,360);
    for (int n=0;n<300;++n) {
        for (int y=0;y<360;++y) {
            auto* p=pixels->rgba_pixels.data()+std::size_t(y)*pixels->stride+(n%640)*4;
            p[0]=255; p[1]=255; p[2]=255;
        }
        encoder.writeVideo(*pixels,n);
    }
    encoder.finish();
}
bool waitFor(const std::function<bool()>& test) {
    QElapsedTimer t; t.start();
    while (!test() && t.elapsed()<10000) { QApplication::processEvents(); QThread::msleep(1); }
    return test();
}
void run(const std::filesystem::path& video,int fps,int mode,int trial) {
    auto& metrics=motion::diagnostics::PerformanceMetrics::instance();
    metrics.setEnabled(true); metrics.reset();
    auto request=fixture(video,fps);
    QObject receiver;
    auto surface=mode ? creative_suite::composition::OpenGlFrameCompositor::createSurface() : nullptr;
    CompositionViewer viewer; viewer.resize(1008,606); viewer.setComposition({1920,1080},std::nullopt);
    bool failed=false;
    if (mode==2) viewer.enableTexturePresentation([&](bool){failed=true;});
    viewer.show();
    if (mode==2 && !waitFor([&]{return viewer.texturePresentationAvailable();}))
        throw std::runtime_error("Native shared GPU viewer unavailable.");
    PreviewRenderer worker(&receiver, {}, {}, &metrics, mode!=0,surface.get(),
        [&](PreviewFrame f){ viewer.setPreviewFrame(std::move(f)); },
        mode==2 ? QOpenGLContext::globalShareContext():nullptr);
    viewer.setFrameValidator([&](const PreviewFrame& f){return worker.canPresentResult(
        f.generation,f.playback?PreviewRequestMode::Playback:PreviewRequestMode::Interactive,
        f.cancellation_generation);});
    system_monitor::PerformanceSampler sampler;
    QElapsedTimer timer; timer.start();
    int last=-1; bool measuring=false;
    while(timer.elapsed()<12000) {
        const auto ms=timer.elapsed();
        if (!measuring && ms>=2000) { metrics.reset(); (void)sampler.sample(); measuring=true; }
        const int frame=int(ms*fps/1000);
        if(frame!=last) {
            last=frame;
            for(auto& layer:request.layers) layer.local_frame=frame%(fps*10);
            request.layers[4].transform.position_x=.5+.15*std::sin(frame/double(fps));
            (void)worker.submit(request,PreviewRequestMode::Playback);
        }
        QApplication::processEvents(); QThread::msleep(1);
    }
    const auto resources=sampler.sample();
    const auto s=metrics.takeSnapshotAndReset();
    viewer.clearPreviewFrame(); worker.stopAndWait(); QApplication::processEvents();
    if(failed || !s || (mode && s->gpu_composition_frames==0) ||
       (mode==2 && (!s->delivery.texture_presented || s->gpu_composition_readback_bytes)))
        throw std::runtime_error("Measurement did not use its requested GPU route.");
    const auto& lat=s->timings[std::size_t(motion::diagnostics::PreviewTimingStage::RequestToViewerPaint)];
    const auto& render=s->timings[std::size_t(motion::diagnostics::PreviewTimingStage::FrameRender)];
    std::cout<<fps<<','<<(mode==0?"cpu":mode==1?"gpu_rgba":"gpu_texture")<<','<<trial<<','
        <<(s->delivery.texture_presented+s->delivery.rgba_presented)/10.0<<','
        <<(lat.count?lat.total_nanoseconds/double(lat.count)/1e6:0)<<','<<lat.p95_nanoseconds/1e6<<','
        <<(render.count?render.total_nanoseconds/double(render.count)/1e6:0)<<','
        <<s->coalesced_requests<<','<<s->stale_results<<','
        <<resources.process_cpu_percent.value_or(-1)<<','<<resources.process_working_set_bytes.value_or(0)<<','
        <<s->gpu_composition_uploaded_bytes<<','<<s->gpu_composition_readback_bytes<<','
        <<s->delivery.pool_peak_bytes<<','<<s->delivery.pool_peak_occupancy<<','
        <<s->delivery.busy_drops<<','<<s->gpu_composition_failures<<'\n'<<std::flush;
}
}
int main(int argc,char** argv) {
    QSurfaceFormat format; format.setVersion(3,2); format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(format); QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication app(argc,argv); QTemporaryDir temp;
    creative_suite::diagnostics::Logger::instance().initialize(std::filesystem::path(temp.path().toStdString()));
    try {
        const auto video=std::filesystem::path(temp.path().toStdString())/"synthetic.mkv";
        makeVideo(video);
        std::cout<<"fps,backend,trial,presented_fps,latency_avg_ms,latency_p95_ms,render_avg_ms,coalesced,stale,cpu_percent,working_set_bytes,upload_bytes,readback_bytes,pool_peak_bytes,pool_peak_targets,busy_drops,failures\n";
        for(int fps:{30,60}) for(int trial=1;trial<=3;++trial) for(int mode=0;mode<3;++mode) run(video,fps,mode,trial);
        return 0;
    } catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
