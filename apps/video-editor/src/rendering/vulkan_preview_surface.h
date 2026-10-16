#pragma once
#include "preview_frame_payload.h"
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
    void setGrayscaleEnabled(bool);
    void suspendForTransition(bool);
    bool available() const noexcept;
signals:
    void gpuFailure(const QString&,qint64);
private:
    std::shared_ptr<creative_suite::composition::VulkanDevice> device_;
    VulkanPreviewWindow* window_=nullptr;
    QWidget* container_=nullptr;
    QLabel* snapshot_=nullptr;
    QStackedLayout* stack_=nullptr;
    bool suspended_=false,failed_=false;
};
}
