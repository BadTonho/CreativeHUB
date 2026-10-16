#include "vulkan_preview_surface.h"
#include "preview_performance_metrics.h"
#include "logging/logger.h"
#include <creative_suite/composition/vulkan_frame_compositor.h>
#include <QWindow>
#include <QExposeEvent>
#include <QPlatformSurfaceEvent>
#include <QLabel>
#include <QStackedLayout>
#include <functional>

namespace rendering {
namespace c=creative_suite::composition;
class VulkanPreviewWindow final : public QWindow {
public:
    explicit VulkanPreviewWindow(std::shared_ptr<c::VulkanDevice> device):device_(std::move(device)) {device_->attachWindow(this);}
    ~VulkanPreviewWindow() override {device_->releaseWindow(this);}
    c::OpenGlTextureFramePtr frame;
    quint64 trace=0;
    bool grayscale=false;
    std::function<void(const QString&)> failure;
    void draw() {
        if(!isExposed() || !frame)return;
        std::string cause;
        if(!device_->present(this,frame,grayscale,cause,{0.1098F,0.1255F,0.1569F,1.0F})){
            if(cause.empty()){requestUpdate();return;}
            if(failure)failure(QString::fromStdString(cause));return;
        }
        auto& metrics=PreviewPerformanceMetrics::instance();metrics.recordGpuPresentedFrame();
        metrics.recordFrameDeliveryStage(trace,PreviewFrameDeliveryStage::GpuDrawn);
        metrics.recordFrameDeliveryStage(trace,PreviewFrameDeliveryStage::QtFrameSwapped);trace=0;
    }
protected:
    bool event(QEvent* event) override {
        if(event->type()==QEvent::UpdateRequest){draw();return true;}
        if(event->type()==QEvent::PlatformSurface && static_cast<QPlatformSurfaceEvent*>(event)->surfaceEventType()==QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed)
            device_->releaseWindow(this);
        return QWindow::event(event);
    }
    void exposeEvent(QExposeEvent*) override {if(isExposed())requestUpdate();}
    void resizeEvent(QResizeEvent*) override {requestUpdate();}
private:
    std::shared_ptr<c::VulkanDevice> device_;
};
VulkanPreviewSurface::VulkanPreviewSurface(QWidget* parent):QWidget(parent),device_(c::sessionVulkanDevice()) {
    stack_=new QStackedLayout(this);stack_->setContentsMargins(0,0,0,0);
    snapshot_=new QLabel(this);snapshot_->setAlignment(Qt::AlignCenter);stack_->addWidget(snapshot_);
    if(!device_)return;
    window_=new VulkanPreviewWindow(device_);container_=QWidget::createWindowContainer(window_,this);
    container_->setFocusPolicy(Qt::NoFocus);stack_->addWidget(container_);stack_->setCurrentWidget(container_);
    window_->failure=[this](const QString& cause){
        if(failed_)return;failed_=true;
        logging::Logger::instance().log(logging::Level::Error,"preview","vulkan_present",cause.toStdString());
        PreviewPerformanceMetrics::instance().recordGpuFailure();emit gpuFailure(cause,-1);
    };
}
VulkanPreviewSurface::~VulkanPreviewSurface(){if(window_){window_->failure={};window_->frame.reset();}delete container_;window_=nullptr;}
bool VulkanPreviewSurface::available() const noexcept {return window_ && device_->valid() && !failed_;}
void VulkanPreviewSurface::clearFrame(){if(window_)window_->frame.reset();}
void VulkanPreviewSurface::setFrame(const PreviewFramePayload& payload,quint64 trace) {
    if(!available()){if(window_ && !failed_)window_->failure(QStringLiteral("The Vulkan device is unavailable; using CPU presentation."));return;}
    auto frame=payload.gpu;
    if(!frame && payload.rgba){
        c::VulkanFrameCompositor upload(device_);
        auto result=upload.tryUpload(*payload.rgba);
        if(result.status!=c::OpenGlCompositionStatus::Complete){
            if(result.status==c::OpenGlCompositionStatus::Busy || result.status==c::OpenGlCompositionStatus::Unsupported)
                emit gpuFailure(QString::fromStdString(result.cause),0);
            else window_->failure(QString::fromStdString(result.cause));
            return;
        }
        frame=std::move(result.frame);
        auto& metrics=PreviewPerformanceMetrics::instance();metrics.recordViewerUploadBytes(uint64_t(payload.width())*payload.height()*4);
        metrics.recordFrameDeliveryStage(trace,PreviewFrameDeliveryStage::GpuUploaded);
    }else PreviewPerformanceMetrics::instance().recordFrameDeliveryStage(trace,PreviewFrameDeliveryStage::GpuTextureAccepted);
    if(window_->trace && window_->trace!=trace)PreviewPerformanceMetrics::instance().recordFrameDeliveryDrop(window_->trace,PreviewFrameDeliveryDropReason::PreviewOverwritten);
    window_->frame=std::move(frame);window_->trace=trace;
    if(!suspended_){stack_->setCurrentWidget(container_);window_->requestUpdate();}
}
void VulkanPreviewSurface::setGrayscaleEnabled(bool enabled){if(window_){window_->grayscale=enabled;window_->requestUpdate();}}
void VulkanPreviewSurface::suspendForTransition(bool suspend) {
    if(!window_ || suspended_==suspend)return;
    suspended_=suspend;
    if(suspend){
        c::VulkanFrameCompositor reader(device_);
        auto snapshot_frame=window_->frame;
        if(snapshot_frame && window_->grayscale){
            c::OpenGlImageOperation op;op.kind=c::OpenGlImageOperationKind::Color;
            op.background.texture=snapshot_frame;op.colors={{0,100,0}};
            auto converted=reader.processImage(op);if(converted.frame)snapshot_frame=std::move(converted.frame);
        }
        auto result=reader.readback(snapshot_frame);
        if(result.frame){const auto& f=*result.frame;QImage image(f.rgba_pixels.data(),f.width,f.height,f.stride,QImage::Format_RGBA8888);
            snapshot_->setPixmap(QPixmap::fromImage(image).scaled(size(),Qt::KeepAspectRatio,Qt::SmoothTransformation));}
        else snapshot_->clear();
        stack_->setCurrentWidget(snapshot_);
    }else{snapshot_->clear();stack_->setCurrentWidget(container_);window_->requestUpdate();}
}
}
