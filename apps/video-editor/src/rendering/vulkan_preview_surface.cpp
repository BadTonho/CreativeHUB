#include "vulkan_preview_surface.h"
#include "preview_performance_metrics.h"
#include "vulkan_preview_schedule.h"
#include "logging/logger.h"
#include <creative_suite/composition/vulkan_frame_compositor.h>
#include <QWindow>
#include <QExposeEvent>
#include <QPlatformSurfaceEvent>
#include <QLabel>
#include <QStackedLayout>
#include <functional>
#include <QTimer>

namespace rendering {
namespace c=creative_suite::composition;
class VulkanPreviewWindow final : public QWindow {
public:
    explicit VulkanPreviewWindow(std::shared_ptr<c::VulkanDevice> device):device_(std::move(device)) {
        device_->attachWindow(this);
        connect(this,&QWindow::screenChanged,this,[this]{schedule_.invalidate();requestScheduledUpdate();});
        poll_.setInterval(1);
        connect(&poll_,&QTimer::timeout,this,[this]{pollCompletion();});
    }
    ~VulkanPreviewWindow() override {
        poll_.stop();clear();device_->releaseWindow(this);
    }
    quint64 completed=0,queued=0;
    VulkanPreviewSurface::Diagnostics stats;
    bool grayscale=false;
    std::function<void(const QString&)> failure;
    const c::GpuTextureFramePtr& snapshotFrame() const {return schedule_.lastSubmitted().image;}
    void setFrame(c::GpuTextureFramePtr image,quint64 trace,quint64 epoch) {
        drop(schedule_.replace({std::move(image),trace,epoch}),PreviewFrameDeliveryDropReason::PreviewOverwritten);
        requestScheduledUpdate();
    }
    void setEpoch(quint64 epoch) {
        if(epoch_==epoch)return;
        epoch_=epoch;completed=queued=0;
        drop(schedule_.setEpoch(epoch),PreviewFrameDeliveryDropReason::StaleGeneration);
        drop(schedule_.retireInFlightTrace(),PreviewFrameDeliveryDropReason::StaleGeneration);
    }
    void clear(PreviewFrameDeliveryDropReason reason=PreviewFrameDeliveryDropReason::Shutdown) {
        drop(schedule_.clear(),reason);
        drop(schedule_.retireInFlightTrace(),reason);
        update_pending_=false;
    }
    void setGrayscale(bool enabled) {
        if(grayscale==enabled)return;
        grayscale=enabled;schedule_.invalidate();requestScheduledUpdate();
    }
    void suspend(bool value) {
        suspended_=value;schedule_.setSuspended(value);
        if(value){poll_.stop();update_pending_=false;}
        else {pollCompletion();requestScheduledUpdate();}
    }
    void draw() {
        update_pending_=false;
        if(halted_ || !isExposed() || !schedule_.ready())return;
        const auto trace=schedule_.pendingTrace();
        auto result=device_->presentFrame(this,schedule_.candidate().image,grayscale,{0.1098F,0.1255F,0.1569F,1.0F});
        if(result.status==c::VulkanDevice::PresentStatus::Deferred){
            ++stats.deferred;
            // Zero extent is resumed only by expose/resize, not a retry timer.
            if(result.deferral!=c::VulkanDevice::PresentDeferral::ZeroExtent)requestScheduledUpdate();
            return;
        }
        if(result.status==c::VulkanDevice::PresentStatus::Failed){fail(result.cause);return;}
        receipt_=std::move(result.receipt);schedule_.submitted();++stats.submitted;
        if(trace)queued=trace;
        auto& metrics=PreviewPerformanceMetrics::instance();metrics.recordGpuPresentedFrame();
        metrics.recordFrameDeliveryStage(trace,PreviewFrameDeliveryStage::VulkanPresentQueued);
        poll_.start();
    }
protected:
    bool event(QEvent* event) override {
        if(event->type()==QEvent::UpdateRequest){draw();return true;}
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
        if(event->type()==QEvent::DevicePixelRatioChange){schedule_.invalidate();requestScheduledUpdate();}
#endif
        if(event->type()==QEvent::PlatformSurface && static_cast<QPlatformSurfaceEvent*>(event)->surfaceEventType()==QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed){
            poll_.stop();update_pending_=false;device_->releaseWindow(this);
        }
        return QWindow::event(event);
    }
    void exposeEvent(QExposeEvent*) override {
        update_pending_=false;
        if(!isExposed()){poll_.stop();return;}
        schedule_.invalidate();pollCompletion();requestScheduledUpdate();
    }
    void resizeEvent(QResizeEvent*) override {
        if(visual_size_==size() && visual_scale_==devicePixelRatio())return;
        visual_size_=size();visual_scale_=devicePixelRatio();schedule_.invalidate();requestScheduledUpdate();
    }
private:
    void drop(quint64 trace,PreviewFrameDeliveryDropReason reason) {
        if(!trace)return;++stats.dropped;
        PreviewPerformanceMetrics::instance().recordFrameDeliveryDrop(trace,reason);
    }
    void requestScheduledUpdate() {
        if(halted_ || suspended_ || !isExposed() || !schedule_.ready() || update_pending_)return;
        update_pending_=true;requestUpdate();
    }
    void pollCompletion() {
        if(halted_ || suspended_ || !isExposed()){poll_.stop();return;}
        if(!schedule_.inFlight()){poll_.stop();return;}
        std::string cause;const auto status=device_->pollPresentation(receipt_,cause);
        if(status==c::VulkanDevice::CompletionStatus::Pending){if(!poll_.isActive())poll_.start();return;}
        poll_.stop();
        if(status==c::VulkanDevice::CompletionStatus::Failed){fail(cause);return;}
        const auto trace=schedule_.completed();receipt_={};++stats.completed;
        if(trace){completed=trace;PreviewPerformanceMetrics::instance().recordFrameDeliveryStage(trace,PreviewFrameDeliveryStage::VulkanGpuCompleted);}
        requestScheduledUpdate();
    }
    void fail(const std::string& cause) {
        halted_=true;poll_.stop();clear(PreviewFrameDeliveryDropReason::GpuFailure);if(failure)failure(QString::fromStdString(cause));
    }
    std::shared_ptr<c::VulkanDevice> device_;
    VulkanPreviewSchedule schedule_;
    c::VulkanDevice::PresentationReceipt receipt_;
    QTimer poll_;
    QSize visual_size_;
    qreal visual_scale_=0;
    quint64 epoch_=0;
    bool update_pending_=false,suspended_=false,halted_=false;
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
VulkanPreviewSurface::~VulkanPreviewSurface(){if(window_){window_->failure={};window_->clear();}delete container_;window_=nullptr;}
VulkanPreviewSurface::Diagnostics VulkanPreviewSurface::diagnostics() const noexcept {return window_?window_->stats:Diagnostics{};}
quint64 VulkanPreviewSurface::lastCompletedDeliveryTraceId() const noexcept {return window_?window_->completed:0;}
quint64 VulkanPreviewSurface::lastQueuedDeliveryTraceId() const noexcept {return window_?window_->queued:0;}
bool VulkanPreviewSurface::available() const noexcept {return window_ && device_->valid() && !failed_;}
void VulkanPreviewSurface::clearFrame(){if(window_)window_->clear();}
void VulkanPreviewSurface::setDeliveryEpoch(quint64 epoch) {if(window_)window_->setEpoch(epoch);}
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
    window_->setFrame(std::move(frame),trace,payload.delivery_epoch);
    if(!suspended_)stack_->setCurrentWidget(container_);
}
void VulkanPreviewSurface::setGrayscaleEnabled(bool enabled){if(window_)window_->setGrayscale(enabled);}
void VulkanPreviewSurface::suspendForTransition(bool suspend) {
    if(!window_ || suspended_==suspend)return;
    suspended_=suspend;window_->suspend(suspend);
    if(suspend){
        c::VulkanFrameCompositor reader(device_);
        auto snapshot_frame=window_->snapshotFrame();
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
    }else{snapshot_->clear();stack_->setCurrentWidget(container_);}
}
}
