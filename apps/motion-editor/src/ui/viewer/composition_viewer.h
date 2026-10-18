#pragma once

#include "model/composition_document.h"
#include "rendering/preview_frame.h"

#include <creative_suite/media/video_frame.h>

#include <QPointF>
#include <QRectF>
#include <QWidget>

#include <functional>
#include <memory>
#include <optional>
#include <cstdint>

class QPaintEvent;
class QContextMenuEvent;
class QPainter;
class QResizeEvent;
class QMouseEvent;
class QKeyEvent;

namespace motion::ui {

class GpuCompositionSurface;

class CompositionViewer final : public QWidget {
public:
    explicit CompositionViewer(QWidget* parent = nullptr);
    ~CompositionViewer() override;
    void enableTexturePresentation(std::function<void(bool)> recovery_handler);
    void setPreviewFrame(PreviewFrame frame);
    void clearPreviewFrame();
    [[nodiscard]] bool texturePresentationAvailable() const noexcept;
    [[nodiscard]] std::uint64_t lastPresentedGeneration() const noexcept;
    // Worker validation is checked again at the actual paint boundary.
    void setFrameValidator(std::function<bool(const PreviewFrame&)> validator);
    // Also used by deterministic failure-injection regression.
    void recoverTexturePresentation(bool use_cpu);

    void setComposition(
        model::CanvasSize canvas_size,
        std::optional<QPointF> selected_layer_anchor);
    void setSelectedLayerAnchor(std::optional<QPointF> selected_layer_anchor);
    [[nodiscard]] std::optional<QPointF> selectedLayerAnchor() const noexcept { return selected_layer_anchor_; }
    void setRenderedFrame(creative_suite::media::RgbaFramePtr frame,
                          std::uint64_t request_generation = 0);
    [[nodiscard]] creative_suite::media::RgbaFramePtr renderedFrame() const noexcept;
    void setLayerContextMenuHandler(std::function<void(const QPoint&)> handler);
    // Displacements are canvas pixels from the initial press, in logical UI
    // coordinates. Callbacks never own media; false rejects/cancels the gesture.
    void setPanBehindHandlers(std::function<bool()> begin,
        std::function<bool(const QPointF&)> preview, std::function<void()> commit,
        std::function<void()> cancel);
    void setPanBehindEnabled(bool enabled);
    void cancelPanBehind();
    [[nodiscard]] bool panBehindActive() const noexcept { return pivot_pressed_; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool event(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    friend class GpuCompositionSurface;
    [[nodiscard]] QRectF canvasRect() const;
    [[nodiscard]] bool hitPivot(QPointF point) const;
    bool previewPanBehind(QPointF point);
    void releasePivotCapture();
    void paintBackground(QPainter& painter);
    void paintOverlay(QPainter& painter);
    void recordPaint(bool texture);
    GpuCompositionSurface* gpu_surface_ = nullptr;
    PreviewFrame preview_frame_;
    std::function<void(bool)> recovery_handler_;
    std::function<bool(const PreviewFrame&)> frame_validator_;
    std::uint64_t last_presented_generation_ = 0;
    bool texture_failed_ = false;

    model::CanvasSize canvas_size_{0, 0};
    std::optional<QPointF> selected_layer_anchor_;
    creative_suite::media::RgbaFramePtr rendered_frame_;
    std::uint64_t rendered_frame_generation_ = 0;
    bool rendered_frame_paint_pending_ = false;
    std::function<void(const QPoint&)> layer_context_menu_handler_;
    std::function<bool()> begin_pan_behind_;
    std::function<bool(const QPointF&)> preview_pan_behind_;
    std::function<void()> commit_pan_behind_, cancel_pan_behind_;
    bool pan_behind_enabled_ = false;
    bool pivot_pressed_ = false;
    bool pivot_dragged_ = false;
    QPointF pivot_press_;
    QRectF pivot_canvas_;
    QWidget* pivot_window_ = nullptr;
};

} // namespace motion::ui
