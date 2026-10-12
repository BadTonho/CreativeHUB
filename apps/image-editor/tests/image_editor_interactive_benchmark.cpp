#include "image_document_session.h"
#include "../src/ui/rendering/image_render_controller.h"
#include "../src/ui/canvas/image_gpu_presenter.h"
#include <creative_suite/system_monitor/performance_usage.h>
#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QDateTime>
#include <QOffscreenSurface>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QPainter>
#include <QSurfaceFormat>
#include <QSysInfo>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <stdexcept>

using namespace image_editor;
namespace {
class CpuPresenter final : public QWidget {
public:
    QImage image;
    std::function<void()> presented;
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this); painter.fillRect(rect(),Qt::gray);
        painter.drawImage(rect(),image); painter.end();
        if(presented) presented();
    }
};
ImageExportSnapshot fixture(const QSize& size, bool masks, bool strokes) {
    ImageDocumentSession session;
    if(!session.createCanvas(size,Qt::transparent)) throw std::runtime_error("Cannot create benchmark canvas.");
    for(int layer=0;layer<4;++layer) {
        ImageShapeData shape; shape.start={0,0}; shape.end={double(size.width()-1),double(size.height()-1)};
        shape.stroke_enabled=false; shape.fill_color=QColor(40+layer*45,180-layer*25,70+layer*30,150);
        if(session.addShape(shape).isEmpty()) throw std::runtime_error("Cannot create benchmark layer.");
        const auto id=session.selectedLayerId();
        if(strokes) for(int i=0;i<80;++i) {
            const QPointF point((i*37+layer*50)%size.width(),(i*29+layer*50)%size.height());
            if(!session.applyPaintStroke({point,QPointF(std::min(point.x()+60.0,double(size.width()-1)),std::min(point.y()+40.0,double(size.height()-1)))},QColor(100,40,200,120),24))
                throw std::runtime_error("Cannot create benchmark stroke.");
        }
        if(masks) {
            if(!session.addLayerMask(id) || !session.applyLayerMaskStroke({{20,20},{double(size.width()-20),double(size.height()-20)}},Qt::black,80))
                throw std::runtime_error("Cannot create benchmark mask.");
        }
    }
    return session.exportSnapshot();
}
double percentile(std::vector<double> values,double fraction) {
    std::sort(values.begin(),values.end()); return values.at(std::size_t(std::ceil(values.size()*fraction))-1);
}
QJsonObject run(const ImageExportSnapshot& original,bool automatic,const QString& profile,int repeat) {
    ImageRenderController controller; controller.setAutomatic(automatic);
    CpuPresenter cpu; QWidget gpu_host; ImageGpuPresenter gpu(&gpu_host);
    cpu.resize(960,540); gpu.resize(960,540);
    gpu_host.resize(960,540);
    gpu.targetRect=[] {return QRectF(0,0,960,540);};
    QEventLoop loop; QTimer watchdog; watchdog.setSingleShot(true);
    QString failure; bool waiting=false,received=false,used_gpu=false;
    QElapsedTimer timer; std::vector<double> values,worker_values;
    std::uint64_t uploads=0,readbacks=0,peak_gpu=0,peak_ram=0,peak_cache=0;
    system_monitor::PerformanceSampler sampler;
    std::optional<double> peak_cpu;
    QElapsedTimer resource_interval; resource_interval.start();
    double weighted_cpu=0, cpu_interval_ns=0;
    const auto presented=[&] {
        if(!waiting || !received) return;
        values.push_back(timer.nsecsElapsed()/1e6); waiting=false; loop.quit();
    };
    cpu.presented=presented;
    QObject::connect(&gpu,&QOpenGLWidget::frameSwapped,&gpu,presented);
    gpu.failed=[&](const QString& cause){failure=cause;waiting=false;loop.quit();};
    QObject::connect(&watchdog,&QTimer::timeout,&loop,[&]{failure="Presentation timed out.";waiting=false;loop.quit();});
    QObject::connect(&controller,&ImageRenderController::renderFailed,&loop,[&](const QString&,const QString& cause){failure=cause;});
    QObject::connect(&controller,&ImageRenderController::frameReady,&loop,[&](const InteractiveFrame& frame) {
        if(automatic && !frame.texture) {
            failure=QStringLiteral("Automatic benchmark received CPU fallback (%1, request %2, GPU bytes %3).")
                .arg(profile).arg(frame.sequence).arg(frame.gpu_bytes);
            waiting=false; loop.quit(); return;
        }
        worker_values.push_back(frame.latency_nanoseconds/1e6);
        uploads+=frame.timings.uploaded_bytes;readbacks+=frame.timings.readback_bytes;
        peak_gpu=std::max(peak_gpu,frame.gpu_bytes); peak_cache=std::max(peak_cache,frame.cpu_cache_bytes); received=true;
        if(frame.texture) { used_gpu=true;gpu.setFrame(frame.texture);gpu.show();gpu_host.show();gpu.update(); }
        else {cpu.image=frame.image;cpu.show();cpu.update();}
    });
    for(int iteration=-4;iteration<30;++iteration) {
        auto snapshot=original;
        // Metadata changes exercise composition with identical raster caches.
        snapshot.document.layers[2].opacity=iteration%2==0?75:95;
        received=false;waiting=true;timer.start();watchdog.start(15000);
        controller.request(std::move(snapshot),profile+QString::number(repeat)); loop.exec();watchdog.stop();
        if(!failure.isEmpty()) throw std::runtime_error(failure.toStdString());
        if(iteration<0) {values.clear();worker_values.clear();uploads=readbacks=0;}
        const auto resources=sampler.sample();
        const auto interval=resource_interval.nsecsElapsed(); resource_interval.restart();
        if(resources.process_working_set_bytes) peak_ram=std::max(peak_ram,*resources.process_working_set_bytes);
        if(resources.process_cpu_percent) peak_cpu=std::max(peak_cpu.value_or(0),*resources.process_cpu_percent);
        if(iteration>=0 && resources.process_cpu_percent) {
            weighted_cpu+=*resources.process_cpu_percent*interval; cpu_interval_ns+=interval;
        }
    }
    gpu.setFrame({});gpu_host.hide();cpu.hide();QCoreApplication::processEvents();
    if(automatic && !used_gpu) throw std::runtime_error("Automatic benchmark used CPU fallback instead of GPU.");
    return {{"mode",automatic?"gpu":"cpu"},{"samples",int(values.size())},
        {"presentation_p50_ms",percentile(values,0.5)},{"presentation_p95_ms",percentile(values,0.95)},
        {"worker_p95_ms",percentile(worker_values,0.95)},
        {"uploaded_bytes",qint64(uploads)},{"readback_bytes",qint64(readbacks)},
        {"peak_cpu_raster_cache_bytes",qint64(peak_cache)},{"peak_known_gpu_texture_bytes",qint64(peak_gpu)},{"sampled_peak_working_set_bytes",qint64(peak_ram)},
        {"sampled_peak_process_cpu_percent",peak_cpu?QJsonValue(*peak_cpu):QJsonValue(QJsonValue::Null)},
        {"sampled_mean_process_cpu_percent",cpu_interval_ns?QJsonValue(weighted_cpu/cpu_interval_ns):QJsonValue(QJsonValue::Null)}};
}
}
int main(int argc,char** argv) {
    QSurfaceFormat format;format.setVersion(3,2);format.setProfile(QSurfaceFormat::CoreProfile);format.setSwapInterval(0);
    QSurfaceFormat::setDefaultFormat(format);QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication app(argc,argv);
    const QString output=argc>1?QString::fromLocal8Bit(argv[1]):QStringLiteral("interactive-performance.json");
    QJsonObject report{{"schema_version",1},{"os",QSysInfo::prettyProductName()},{"qt_version",qVersion()},
        {"architecture",QSysInfo::currentCpuArchitecture()},{"repeat_runs",3},{"warmups_per_mode",4},{"samples_per_mode",30},
        {"gpu_mode_requires_texture_per_frame",true},
        {"presentation_boundary","CPU paint completion / QOpenGLWidget frameSwapped; swap interval 0"},
        {"timestamp_utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}};
    try {
        QOpenGLContext context;context.setFormat(format);auto surface=creative_suite::composition::OpenGlFrameCompositor::createSurface();
        if(context.create() && surface && context.makeCurrent(surface.get())) {
            const auto* vendor=context.functions()->glGetString(GL_VENDOR),*renderer=context.functions()->glGetString(GL_RENDERER),*version=context.functions()->glGetString(GL_VERSION);
            report.insert("gpu_vendor",QString::fromLatin1(reinterpret_cast<const char*>(vendor)));
            report.insert("gpu_renderer",QString::fromLatin1(reinterpret_cast<const char*>(renderer)));
            report.insert("gl_version",QString::fromLatin1(reinterpret_cast<const char*>(version)));context.doneCurrent();
        }
        struct Profile {QString name;QSize size;bool masks,strokes;};
        const Profile profiles[]={{"reference",{1920,1080},false,false},{"mask-heavy",{1920,1080},true,false},
            {"stroke-heavy",{1920,1080},false,true},{"large-image",{3840,2160},false,false},
            {"mask-stroke-heavy",{1920,1080},true,true}};
        QJsonArray records;int improved=0;bool no_regression=true;
        for(const auto& profile:profiles) {
            const auto snapshot=fixture(profile.size,profile.masks,profile.strokes);
            double cpu=0,gpu=0;
            QJsonArray runs;
            for(int repeat=0;repeat<3;++repeat) {
                const auto c=run(snapshot,false,profile.name,repeat),g=run(snapshot,true,profile.name,repeat);
                cpu+=c.value("presentation_p95_ms").toDouble();gpu+=g.value("presentation_p95_ms").toDouble();
                runs.append(QJsonObject{{"cpu",c},{"gpu",g}});
            }
            cpu/=3;gpu/=3;const double gain=1-gpu/cpu;
            if(gain>=0.20) ++improved; if(gpu>cpu*1.10) no_regression=false;
            records.append(QJsonObject{{"profile",profile.name},{"width",profile.size.width()},{"height",profile.size.height()},
                {"runs",runs},{"cpu_mean_run_p95_ms",cpu},{"gpu_mean_run_p95_ms",gpu},{"p95_improvement_percent",gain*100}});
        }
        report.insert("profiles",records);report.insert("performance_gate_passed",improved>=2&&no_regression);
    } catch(const std::exception& error) {report.insert("error",QString::fromUtf8(error.what()));report.insert("performance_gate_passed",false);}
    QFile file(output);if(!file.open(QIODevice::WriteOnly)) return 2;
    file.write(QJsonDocument(report).toJson(QJsonDocument::Indented));
    return report.value("performance_gate_passed").toBool()?0:1;
}
