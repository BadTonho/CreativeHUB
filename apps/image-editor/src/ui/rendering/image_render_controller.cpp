#include "image_render_controller.h"
#include "rendering/image_interactive_raster_cache.h"
#include "rendering/image_document_renderer.h"
#include "image_document_utils.h"
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QPointer>
#include <stdexcept>

namespace image_editor {
using namespace creative_suite::composition;
struct ImageRenderController::WorkerState {
    ImageInteractiveRasterCache cache;
    std::unique_ptr<OpenGlFrameCompositor> gpu;
    bool disabled = false;
    QString injected_failure;
    std::uint64_t gpu_byte_limit = 256ULL*1024*1024;
    QString thumbnail_document_id;
    ImageExportSnapshot thumbnail_snapshot;
    QHash<QString,QImage> thumbnails, mask_thumbnails;
    void reserveThumbnailSpace(const ImageExportSnapshot& snapshot) {
        std::uint64_t previous=0, count=snapshot.document.layers.size()+snapshot.document.groups.size();
        for(const auto& image:thumbnails) previous+=image.sizeInBytes();
        for(const auto& image:mask_thumbnails) previous+=image.sizeInBytes();
        for(const auto& layer:snapshot.document.layers) if(layer.mask) ++count;
        // Reserve both generations before rasterizing, including a former
        // document's thumbnails while its replacement is prepared.
        const auto reserved=std::min(2*std::max(previous,count*48*36*4),cache.maximum_bytes);
        cache.setByteLimit(cache.maximum_bytes-reserved);
    }
    void prepareThumbnails(const ImageExportSnapshot& snapshot, const QString& id, InteractiveFrame& frame,
        const std::atomic_bool* cancellation,const InteractiveComposition& composition) {
        bool reset = thumbnail_document_id != id || thumbnail_snapshot.source_image.cacheKey() != snapshot.source_image.cacheKey() ||
            thumbnail_snapshot.document.canvas_size != snapshot.document.canvas_size ||
            thumbnail_snapshot.document.operations != snapshot.document.operations ||
            thumbnail_snapshot.document.canvas_base_offset != snapshot.document.canvas_base_offset;
        if (!reset) {
            if (thumbnail_snapshot.raster_images.size() != snapshot.raster_images.size()) reset = true;
            else for (auto it=snapshot.raster_images.cbegin(); it!=snapshot.raster_images.cend(); ++it)
                if (thumbnail_snapshot.raster_images.value(it.key()).cacheKey()!=it.value().cacheKey()) { reset=true; break; }
        }
        QHash<QString,QImage> next, masks;
        const QSize size(48,36);
        for (const auto& layer : snapshot.document.layers) {
            if (cancellation->load()) return;
            const auto* before = findLayer(thumbnail_snapshot.document,layer.id);
            const bool unchanged = !reset && before && before->operations==layer.operations && before->mask==layer.mask &&
                before->outline==layer.outline && before->adjustment==layer.adjustment && before->clip_to_base==layer.clip_to_base &&
                (!layer.clip_to_base || thumbnail_snapshot.document==snapshot.document);
            next.insert(layer.id, unchanged && thumbnails.contains(layer.id) ? thumbnails.value(layer.id) :
                ImageDocumentRenderer::layerThumbnail(snapshot.document,snapshot.source_image,snapshot.raster_images,layer,size,cancellation));
            if (layer.mask) {
                if (unchanged && mask_thumbnails.contains(layer.id)) masks.insert(layer.id,mask_thumbnails.value(layer.id));
                else {
                    auto document = snapshot.document; document.layers = {layer};
                    masks.insert(layer.id,ImageDocumentRenderer::maskThumbnails(document,snapshot.source_image,snapshot.raster_images,size).value(layer.id));
                }
            }
        }
        for (const auto& group : snapshot.document.groups) {
            if (cancellation->load()) return;
            const auto* before = findGroup(thumbnail_snapshot.document,group.id);
            bool unchanged = !reset && before && *before==group;
            if (unchanged) for (const auto& child : group.layer_ids) {
                const auto* old = findLayer(thumbnail_snapshot.document,child); const auto* current=findLayer(snapshot.document,child);
                if (!old || !current || *old != *current) { unchanged=false; break; }
            }
            const bool adjusted=std::any_of(group.layer_ids.cbegin(),group.layer_ids.cend(),[&](const QString& child){const auto* layer=findLayer(snapshot.document,child);return layer && layer->adjustment;});
            if(!unchanged && adjusted){
                const auto node=std::find_if(composition.nodes.cbegin(),composition.nodes.cend(),[&](const auto& value){return value.id==group.id;});
                QImage image(size,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);
                if(node!=composition.nodes.cend()){
                    InteractiveComposition isolated;isolated.size=composition.size;isolated.adjustment_document=true;isolated.nodes={*node};isolated.nodes[0].blend_mode=BlendMode::Normal;
                    image=ImageInteractiveRasterCache::composite(isolated,cancellation).scaled(size,Qt::KeepAspectRatio,Qt::SmoothTransformation);
                }
                next.insert(group.id,image);
            }else next.insert(group.id, unchanged && thumbnails.contains(group.id) ? thumbnails.value(group.id) :
                ImageDocumentRenderer::groupThumbnail(snapshot.document,snapshot.raster_images,group,size,cancellation));
        }
        if (cancellation->load()) return;
        for (const auto& thumbnail : next)
            if (thumbnail.isNull()) throw std::runtime_error("Layer/group thumbnail rendering failed.");
        thumbnail_snapshot=snapshot; thumbnail_document_id=id; thumbnails=std::move(next); mask_thumbnails=std::move(masks);
        frame.thumbnails=thumbnails; frame.mask_thumbnails=mask_thumbnails;
        std::uint64_t thumbnail_bytes=0;
        for(const auto& image:thumbnails) thumbnail_bytes+=image.sizeInBytes();
        for(const auto& image:mask_thumbnails) thumbnail_bytes+=image.sizeInBytes();
        // Reserve the previous GUI thumbnail set while replacement is delivered.
        const auto reserved=std::min(thumbnail_bytes*2,cache.maximum_bytes);
        cache.setByteLimit(cache.maximum_bytes-reserved);
        frame.cpu_cache_bytes=cache.retainedBytes()+reserved;
    }
};
OpenGlRasterRequest makeGpuRasterRequest(const InteractiveComposition& composition) {
    OpenGlRasterRequest request; request.size = composition.size;
    for (const auto& source : composition.nodes) {
        OpenGlRasterNode node; node.group = source.group; node.opacity = source.opacity; node.blend_mode = source.blend_mode;
        for (const auto& layer : source.layers) {
            const auto input = [](const InteractiveRaster& value) {
                return OpenGlRasterInput{value.identity.toStdString(), value.pixels, value.previous_key, value.dirty, value.revision};
            };
            node.layers.push_back({input(layer.content), input(layer.mask), layer.opacity, layer.blend_mode,layer.adjustment});
        }
        for (const auto& op : source.transforms) {
            OpenGlRasterTransform transform;
            const QRect bounds = op.transform_bounds.isValid() && !op.transform_bounds.isEmpty()
                ? op.transform_bounds : QRect(QPoint(), composition.size);
            switch (op.kind) {
            case OperationKind::Crop: transform.clip = op.crop; break;
            case OperationKind::Rotate: {
                const double x = bounds.x() + bounds.width()/2.0, y = bounds.y() + bounds.height()/2.0;
                transform.matrix.translate(x,y); transform.matrix.rotate(op.quarter_turns*90.0); transform.matrix.translate(-x,-y); break;
            }
            case OperationKind::FlipHorizontal:
                transform.matrix.translate(bounds == QRect(QPoint(), composition.size) ? bounds.width() : 2*bounds.x()+bounds.width()-1, 0);
                transform.matrix.scale(-1,1); break;
            case OperationKind::FlipVertical:
                transform.matrix.translate(0, bounds == QRect(QPoint(), composition.size) ? bounds.height() : 2*bounds.y()+bounds.height()-1);
                transform.matrix.scale(1,-1); break;
            default: throw std::runtime_error("Unsupported raster group operation.");
            }
            node.transforms.push_back(transform);
        }
        request.nodes.push_back(std::move(node));
    }
    return request;
}
ImageRenderController::ImageRenderController(QObject* parent) : QObject(parent),
    state_(std::make_shared<WorkerState>()), latest_(std::make_shared<std::atomic<std::uint64_t>>(0)) {
    if (QGuiApplication::platformName() != QStringLiteral("offscreen") && QOpenGLContext::globalShareContext())
        surface_ = OpenGlFrameCompositor::createSurface();
    worker_ = new QObject;
    worker_->moveToThread(&thread_);
    connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater);
    thread_.start();
}
ImageRenderController::~ImageRenderController() {
    cancel();
    QMetaObject::invokeMethod(worker_, [state = state_] { state->gpu.reset(); state->cache.clear(); }, Qt::BlockingQueuedConnection);
    thread_.quit(); thread_.wait();
}
void ImageRenderController::cancel() {
    latest_->store(++sequence_); pending_.reset(); gesture_ = 0;
    if (running_cancellation_) running_cancellation_->store(true);
}
void ImageRenderController::setAutomatic(bool enabled) { automatic_ = enabled; cancel(); }
void ImageRenderController::injectGpuFailureForTesting(const QString& cause) {
    QMetaObject::invokeMethod(worker_, [state = state_, cause] { state->injected_failure = cause; }, Qt::QueuedConnection);
}
void ImageRenderController::setGpuByteLimitForTesting(std::uint64_t bytes) {
    QMetaObject::invokeMethod(worker_, [state = state_, bytes] {
        state->gpu.reset(); state->gpu_byte_limit=bytes;
    }, Qt::QueuedConnection);
}
void ImageRenderController::disableGpu(const QString& cause) {
    emit renderFailed(QStringLiteral("gpu_presentation"), cause);
    QMetaObject::invokeMethod(worker_, [state = state_] { state->disabled = true; }, Qt::QueuedConnection);
    cancel();
}
void ImageRenderController::request(ImageExportSnapshot snapshot, const QString& document_id, bool transient) {
    ++sequence_;
    if (!transient || requested_document_id_ != document_id) {
        document_revision_ = sequence_; gesture_ = 0; requested_document_id_ = document_id;
    }
    if (transient && gesture_ == 0) gesture_ = sequence_;
    Request request;
    request.snapshot = std::move(snapshot); request.document_id = document_id; request.sequence = sequence_;
    request.document_revision = document_revision_; request.gesture = transient ? gesture_ : 0;
    request.transient = transient; request.automatic = automatic_;
    request.submitted.start(); latest_->store(sequence_);
    if (running_cancellation_) running_cancellation_->store(true);
    if (running_) { pending_ = std::move(request); return; }
    start(std::move(request));
}
void ImageRenderController::start(Request request) {
    running_ = true;
    running_cancellation_ = request.cancellation;
    const QPointer<ImageRenderController> receiver(this);
    QOffscreenSurface* surface = surface_.get();
    QOpenGLContext* sharing = QOpenGLContext::globalShareContext();
    QMetaObject::invokeMethod(worker_, [receiver, state = state_, latest = latest_, surface, sharing, request = std::move(request)]() mutable {
        InteractiveFrame frame; frame.document_id = request.document_id; frame.sequence = request.sequence; frame.document_revision = request.document_revision; frame.gesture = request.gesture; frame.transient = request.transient;
        QString failure, operation, recovery, recovery_operation;
        const auto cancelled = [&] { return latest->load() != request.sequence; };
        try {
            if (!cancelled()) {
                state->reserveThumbnailSpace(request.snapshot);
                const auto composition = state->cache.prepare(request.snapshot, request.document_id, request.transient, request.cancellation.get());
                frame.size = composition.size; frame.cpu_cache_bytes = state->cache.retainedBytes();
                for(const auto& image:state->thumbnails) frame.cpu_cache_bytes+=2*image.sizeInBytes();
                for(const auto& image:state->mask_thumbnails) frame.cpu_cache_bytes+=2*image.sizeInBytes();
                if (!cancelled() && request.automatic && !state->disabled && !state->injected_failure.isEmpty()) {
                    state->disabled = true; operation = QStringLiteral("gpu_allocation_or_context"); failure = state->injected_failure;
                }
                if (!cancelled() && request.automatic && surface && !state->disabled) {
                    if (!state->gpu) state->gpu = std::make_unique<OpenGlFrameCompositor>(surface,
                        OpenGlPrecisionPolicy::Automatic, sharing, std::make_shared<OpenGlTexturePoolBudget>(state->gpu_byte_limit,3));
                    const auto composed = state->gpu->composeRasterTexture(makeGpuRasterRequest(composition), cancelled, &frame.timings);
                    if (composed.status == OpenGlCompositionStatus::Complete) {
                        frame.texture = composed.frame;
                    }
                    if (composed.status == OpenGlCompositionStatus::Unsupported) {
                        recovery_operation=QString::fromStdString(composed.operation);
                        recovery=QStringLiteral("CPU recovery: %1 (document %2, GPU byte limit %3)")
                            .arg(QString::fromStdString(composed.cause),request.document_id).arg(state->gpu_byte_limit);
                    }
                    if (composed.status == OpenGlCompositionStatus::Failed) {
                        state->disabled = true;
                        failure = QStringLiteral("%1 (OpenGL code %2, document %3, request %4)")
                            .arg(QString::fromStdString(composed.cause)).arg(composed.error_code).arg(request.document_id).arg(request.sequence);
                        operation = QString::fromStdString(composed.operation);
                    }
                }
                frame.gpu_bytes = state->gpu ? state->gpu->resourceUsage().texture_bytes : 0;
                if (!cancelled() && !frame.texture) frame.image = ImageInteractiveRasterCache::composite(composition, request.cancellation.get());
                if (!cancelled() && !request.transient && (!frame.image.isNull() || frame.texture))
                    state->prepareThumbnails(request.snapshot, request.document_id, frame, request.cancellation.get(),composition);
                if (!cancelled() && !frame.texture && frame.image.isNull()) {
                    operation = QStringLiteral("interactive_render"); failure = QStringLiteral("The document could not be rendered.");
                }
            }
        } catch (const std::exception& error) {
            state->disabled = true; operation = QStringLiteral("interactive_render"); failure = QString::fromUtf8(error.what());
        } catch (...) { operation = QStringLiteral("interactive_render"); failure = QStringLiteral("Unexpected rendering failure."); }
        if (!failure.isEmpty() && !cancelled() && !frame.texture && frame.image.isNull()) {
            // Recover a complete frame using the established CPU reference after
            // allocation, adapter, or context exceptions. Never publish partial work.
            state->disabled = true;
            try {
                frame.size = ImageDocumentRenderer::documentSize(request.snapshot.document, request.snapshot.source_image.size());
                frame.image = ImageDocumentRenderer::composite(request.snapshot.document, request.snapshot.source_image,
                    request.snapshot.raster_images, {}, request.cancellation.get()).convertToFormat(QImage::Format_ARGB32_Premultiplied);
            } catch (...) { failure += QStringLiteral(" CPU recovery also failed."); }
        }
        frame.latency_nanoseconds = request.submitted.nsecsElapsed();
        if (receiver) QMetaObject::invokeMethod(receiver, [receiver, frame = std::move(frame), failure, operation, recovery, recovery_operation]() mutable {
            if (!receiver) return;
            receiver->running_ = false; receiver->running_cancellation_.reset();
            if (receiver->latest_->load()==frame.sequence) {
                const QString key=recovery_operation+recovery;
                if (!recovery.isEmpty() && !frame.image.isNull() && receiver->last_recovery_key_!=key) {
                    receiver->last_recovery_key_=key; emit receiver->cpuRecoveryUsed(recovery_operation,recovery);
                } else if (frame.texture) receiver->last_recovery_key_.clear();
            }
            if (!failure.isEmpty()) emit receiver->renderFailed(operation, failure);
            if (receiver->latest_->load() == frame.sequence && (!frame.image.isNull() || frame.texture)) emit receiver->frameReady(frame);
            if (receiver->pending_) {
                auto next = std::move(*receiver->pending_); receiver->pending_.reset(); receiver->start(std::move(next));
            }
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}
} // namespace image_editor
