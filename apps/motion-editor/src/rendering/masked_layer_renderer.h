#pragma once
#include "preview_renderer.h"
#include "path_rasterizer.h"
#include <creative_suite/composition/shape_mask.h>
#include <creative_suite/composition/opengl_frame_compositor.h>
namespace motion::ui {
struct MaskedLayerFrame {
    creative_suite::media::RgbaFramePtr rgba;
    creative_suite::composition::OpenGlTextureFramePtr texture;
    creative_suite::composition::OpenGlCompositionTimings timings;
};
inline void addMaskTimings(creative_suite::composition::OpenGlCompositionTimings& total,const creative_suite::composition::OpenGlCompositionTimings& value) {
    total.upload_nanoseconds+=value.upload_nanoseconds;
    total.draw_submission_nanoseconds+=value.draw_submission_nanoseconds;
    total.readback_nanoseconds+=value.readback_nanoseconds;
    total.uploaded_bytes+=value.uploaded_bytes;
    total.readback_bytes+=value.readback_bytes;
    total.uploaded_layers+=value.uploaded_layers;
    total.producer_fence_submission_nanoseconds+=value.producer_fence_submission_nanoseconds;
    total.color_adjustment_submission_nanoseconds+=value.color_adjustment_submission_nanoseconds;
    total.color_adjustment_count+=value.color_adjustment_count;
    total.gaussian_blur_submission_nanoseconds+=value.gaussian_blur_submission_nanoseconds;
    total.gaussian_blur_count+=value.gaussian_blur_count;
    total.native_video_imports+=value.native_video_imports;
    total.native_video_conversion_nanoseconds+=value.native_video_conversion_nanoseconds;
}
// Worker-only orchestration of existing image/effect operations. No decoder ownership.
[[nodiscard]] bool hasLayerMasks(const PreviewLayerSnapshot&) noexcept;
[[nodiscard]] MaskedLayerFrame renderMaskedLayer(const PreviewLayerSnapshot&,
    creative_suite::media::RgbaFramePtr source, const PreviewRequest&,
    MotionMaskRasterCache&,
    creative_suite::composition::OpenGlFrameCompositor*,
    const std::function<bool()>& cancel,
    const creative_suite::animation::Transform2D* resolved_transform = nullptr,
    const creative_suite::composition::AffineTransform2D* resolved_world = nullptr, const PathRaster* path_raster = nullptr);
}
