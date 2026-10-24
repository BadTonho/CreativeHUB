#pragma once
#include "preview_frame_payload.h"
#include <creative_suite/composition/vulkan_frame_compositor.h>
#include <QWidget>
#include <memory>
class QLabel;
class QStackedLayout;
namespace creative_suite::composition {class VulkanDevice;}
namespace rendering {
class VulkanPreviewWindow;
class VulkanPreviewSurface final : public QWidget {
    Q_OBJECT
public:
    explicit VulkanPreviewSurface(QWidget* parent=nullptr);
    ~VulkanPreviewSurface() override;
    void setFrame(const PreviewFramePayload&,quint64 trace=0);
    void clearFrame();
    void setDeliveryEpoch(quint64);
    void setGrayscaleEnabled(bool);
    void setComparisonReferenceImage(QImage);
    void setComparisonReferenceFrame(const PreviewFramePayload&);
    void setComparison(creative_suite::composition::FrameComparisonMode, float);
    void suspendForTransition(bool);
    bool available() const noexcept;
    struct Diagnostics { quint64 submitted=0, completed=0, deferred=0, dropped=0; };
    Diagnostics diagnostics() const noexcept;
    quint64 lastCompletedDeliveryTraceId() const noexcept;
    quint64 lastQueuedDeliveryTraceId() const noexcept;
signals:
    void gpuFailure(const QString&,qint64);
    void wipePositionDragged(float);
    void comparisonUnavailable(const QString&,qint64);
private:
    std::shared_ptr<creative_suite::composition::VulkanDevice> device_;
    VulkanPreviewWindow* window_=nullptr;
    QWidget* container_=nullptr;
    QLabel* snapshot_=nullptr;
    QStackedLayout* stack_=nullptr;
    bool suspended_=false,failed_=false;
    creative_suite::composition::OpenGlTextureFramePtr reference_frame_;
    creative_suite::composition::FrameComparisonMode comparison_mode_=
        creative_suite::composition::FrameComparisonMode::Off;
    float reference_fraction_=0.5F;
};
}
