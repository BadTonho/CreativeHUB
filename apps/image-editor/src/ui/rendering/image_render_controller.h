#pragma once
#include "import_export/image_exporter.h"
#include <creative_suite/composition/opengl_frame_compositor.h>
#include <QObject>
#include <QThread>
#include <QElapsedTimer>
#include <atomic>
#include <memory>

class QOffscreenSurface;
namespace image_editor {
struct InteractiveComposition;
[[nodiscard]] creative_suite::composition::OpenGlRasterRequest makeGpuRasterRequest(const InteractiveComposition&);
struct InteractiveFrame {
    QString document_id;
    std::uint64_t sequence = 0;
    std::uint64_t document_revision = 0, gesture = 0;
    QSize size;
    QHash<QString,QImage> thumbnails, mask_thumbnails;
    QImage image;
    creative_suite::composition::OpenGlTextureFramePtr texture;
    bool transient = false;
    qint64 latency_nanoseconds = 0;
    creative_suite::composition::OpenGlCompositionTimings timings;
    std::uint64_t gpu_bytes = 0, cpu_cache_bytes = 0;
};
class ImageRenderController final : public QObject {
    Q_OBJECT
public:
    explicit ImageRenderController(QObject* parent = nullptr);
    ~ImageRenderController() override;
    void request(ImageExportSnapshot snapshot, const QString& document_id, bool transient = false);
    void cancel();
    void setAutomatic(bool enabled);
    void disableGpu(const QString& cause);
    // Deterministic allocation/context failure seam for recovery regression tests.
    void injectGpuFailureForTesting(const QString& cause);
    [[nodiscard]] bool busy() const noexcept { return running_; }
    [[nodiscard]] bool hasPendingRequest() const noexcept { return pending_.has_value(); }
    [[nodiscard]] std::uint64_t sequence() const noexcept { return sequence_; }
signals:
    void frameReady(const image_editor::InteractiveFrame& frame);
    void renderFailed(const QString& operation, const QString& cause);
private:
    struct WorkerState;
    struct Request {
        ImageExportSnapshot snapshot;
        QString document_id;
        std::uint64_t sequence = 0, document_revision = 0, gesture = 0;
        bool transient = false, automatic = true;
        QElapsedTimer submitted;
        std::shared_ptr<std::atomic_bool> cancellation = std::make_shared<std::atomic_bool>(false);
    };
    void start(Request request);
    QThread thread_;
    QObject* worker_ = nullptr;
    std::unique_ptr<QOffscreenSurface> surface_;
    std::shared_ptr<WorkerState> state_;
    std::shared_ptr<std::atomic<std::uint64_t>> latest_;
    std::optional<Request> pending_;
    std::shared_ptr<std::atomic_bool> running_cancellation_;
    std::uint64_t sequence_ = 0, document_revision_ = 0, gesture_ = 0;
    QString requested_document_id_;
    bool running_ = false, automatic_ = true;
};
} // namespace image_editor
