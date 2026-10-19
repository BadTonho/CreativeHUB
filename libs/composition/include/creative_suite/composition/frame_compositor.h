#pragma once

#include <creative_suite/animation/animation.h>
#include <creative_suite/composition/affine_transform.h>
#include <creative_suite/composition/blend_mode.h>
#include <creative_suite/effects/effects.h>
#include <creative_suite/media/video_frame.h>

#include <cstddef>
#include <functional>
#include <cstdint>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

namespace creative_suite::composition {

// In-place straight RGBA operations. Coverage is the red channel at identical
// dimensions. Blend uses premultiplied alpha; zero/full coverage preserves bytes.
// False means invalid geometry or cancellation; partial results must be discarded.
[[nodiscard]] bool applyMask(media::RgbaFrame&, const media::RgbaFrame&,
    const std::function<bool()>& cancel = {});
[[nodiscard]] bool maskedBlend(const media::RgbaFrame& original, media::RgbaFrame& processed,
    const media::RgbaFrame& coverage, const std::function<bool()>& cancel = {});

class OpenGlTextureFrame;

struct AlphaSpan {
    int begin = 0;
    int end = 0;
};

struct AlphaCoverage {
    int width = 0;
    int height = 0;
    std::vector<std::vector<AlphaSpan>> rows;
};

using AlphaCoveragePtr = std::shared_ptr<const AlphaCoverage>;

struct PreparedPixelRange {
    int begin = 0;
    int end = -1;
};

// Reusable source sampling and alpha-span mapping for a fixed, unrotated layer
// geometry. Pixel data and opacity remain live inputs during composition.
struct PreparedAlphaCoverageGeometry {
    int source_width = 0;
    int source_height = 0;
    int source_stride = 0;
    int canvas_width = 0;
    int canvas_height = 0;
    double position_x = 0.0;
    double position_y = 0.0;
    double scale = 0.0;
    AlphaCoveragePtr alpha_coverage;
    int horizontal_begin = 0;
    int vertical_begin = 0;
    int vertical_end = -1;
    bool has_visible_pixels = false;
    std::vector<int> source_x_lookup;
    std::vector<int> source_y_lookup;
    std::vector<std::vector<PreparedPixelRange>> mapped_rows;

    [[nodiscard]] bool matches(
        int target_canvas_width,
        int target_canvas_height,
        const struct CompositionLayer& layer) const noexcept;
};

using PreparedAlphaCoverageGeometryPtr =
    std::shared_ptr<const PreparedAlphaCoverageGeometry>;

enum class CompositionRasterPath : std::uint8_t {
    Unprocessed,
    FullFrameCopy,
    AlphaCoverage,
    AxisAligned,
    Rotated,
    GeneralFallback,
};

struct FullFrameCopyEligibility {
    bool source_dimensions_match = false;
    bool source_stride_matches = false;
    bool position_x_centered = false;
    bool position_y_centered = false;
    bool scale_is_one = false;
    bool rotation_is_zero = false;
    bool opacity_is_one = false;
    bool alpha_check_performed = false;
    bool source_pixels_opaque = false;
};

// Parameters for the shared OpenGL preview effects. radius_pixels follows
// Motion's existing sigma parameter; the renderer rounds it to the same box
// radius used by the CPU implementation.
struct GpuGaussianBlurParameters {
    double radius_pixels = 0.0;
};

using GpuCompositionEffect = std::variant<effects::ColorAdjustmentParameters,
    GpuGaussianBlurParameters>;

struct CompositionLayer {
    // Borrowed for the duration of compose(); frame pixels use RGBA8 straight
    // alpha, with no color-space conversion performed by the compositor.
    const media::RgbaFrame* frame = nullptr;
    animation::Transform2D transform;
    AlphaCoveragePtr alpha_coverage;
    PreparedAlphaCoverageGeometryPtr prepared_alpha_geometry;
    // Optional ordered Color Adjustment passes for OpenGL preview composition.
    // The CPU compositor ignores this list; callers must provide an already
    // processed frame when rendering through the CPU path.
    std::vector<effects::ColorAdjustmentParameters> gpu_color_adjustments;
    // Ordered stack used when effects must run before composition (for example
    // when Gaussian Blur is interleaved with Color Adjustment). The CPU
    // compositor ignores this list; callers supply a processed frame on CPU.
    std::vector<GpuCompositionEffect> gpu_effects;
    // Unprocessed built-in clip effects. Both compositors consume this stack
    // exactly once; GPU recovery uses the original immutable source frame.
    std::span<const effects::EffectInstance> effect_stack;
    // GPU-native source. The geometry frame can contain only dimensions/stride;
    // CPU recovery must materialize pixels before submitting this layer.
    std::shared_ptr<const OpenGlTextureFrame> texture_frame;
    std::shared_ptr<const media::NativeVideoFrame> native_frame;
    // Overrides transform geometry and aspect fit; transform.opacity still applies.
    std::optional<AffineTransform2D> source_to_canvas;
    BlendMode blend_mode = BlendMode::Normal;
};

// Transparent nearest-neighbor Transform-stage raster, without an extra fit.
// Invalid geometry throws; cancellation returns nullopt and publishes no pixels.
[[nodiscard]] std::optional<media::RgbaFrame> transformAffineFrame(const media::RgbaFrame&,
    int width,int height,const AffineTransform2D&,double opacity,
    const std::function<bool()>& cancel = {});

struct CompositionLayerTimings {
    std::uint64_t setup_nanoseconds = 0;
    std::uint64_t raster_blend_nanoseconds = 0;
    std::uint64_t fast_path_copy_nanoseconds = 0;
    bool blend_lookup_built = false;
    std::uint64_t blend_lookup_build_nanoseconds = 0;
    std::uint64_t blend_lookup_active_block_nanoseconds = 0;
    std::uint64_t blend_lookup_pixel_count = 0;
    std::uint64_t blend_lookup_active_block_count = 0;
    bool prepared_alpha_geometry_used = false;
    CompositionRasterPath raster_path = CompositionRasterPath::Unprocessed;
    FullFrameCopyEligibility full_frame_copy_eligibility;
};

struct FrameCompositionTimings {
    int canvas_width = 0;
    int canvas_height = 0;
    std::uint64_t layer_list_setup_nanoseconds = 0;
    std::uint64_t output_buffer_create_nanoseconds = 0;
    std::uint64_t output_background_fill_nanoseconds = 0;
    std::vector<CompositionLayerTimings> layers;
};

class FrameCompositor final {
public:
    [[nodiscard]] static AlphaCoveragePtr buildAlphaCoverage(
        const media::RgbaFrame& frame);

    [[nodiscard]] static bool canUseAlphaCoverageFastPath(
        const CompositionLayer& layer) noexcept;

    [[nodiscard]] static PreparedAlphaCoverageGeometryPtr
        prepareAlphaCoverageGeometry(
            int canvas_width,
            int canvas_height,
            const CompositionLayer& layer,
            const PreparedAlphaCoverageGeometryPtr& previous = {});

    // Source frames are aspect-fit to the canvas, then scaled and rotated about
    // their centers. Sampling is nearest-neighbor. Layers blend source-over,
    // back-to-front in vector order. Non-Normal modes blend over transparent
    // premultiplied accumulation before flattening over black. Normal-only
    // scenes retain the legacy path. Returned RGBA8 is always opaque. Invalid
    // blend-mode values return nullopt, never silently substitute Normal.
    // Non-positive or unrepresentable canvas dimensions return nullopt.
    // Layers with a null frame, non-positive source dimensions, invalid
    // transforms, or unusable RGBA storage contribute no pixels. Allocation
    // exceptions may propagate to callers.
    [[nodiscard]] static std::optional<media::RgbaFrame> compose(
        int width,
        int height,
        const std::vector<CompositionLayer>& layers,
        FrameCompositionTimings* timings = nullptr);
};

} // namespace creative_suite::composition
