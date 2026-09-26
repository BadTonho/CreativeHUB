#pragma once

#include "../media/video_frame.h"
#include "../timeline/timeline_transform.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace rendering {

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

struct CompositionLayer {
    const media::VideoFrame* frame = nullptr;
    timeline::Transform2D transform;
    AlphaCoveragePtr alpha_coverage;
    PreparedAlphaCoverageGeometryPtr prepared_alpha_geometry;
};

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
        const media::VideoFrame& frame);

    [[nodiscard]] static bool canUseAlphaCoverageFastPath(
        const CompositionLayer& layer) noexcept;

    [[nodiscard]] static PreparedAlphaCoverageGeometryPtr
        prepareAlphaCoverageGeometry(
            int canvas_width,
            int canvas_height,
            const CompositionLayer& layer,
            const PreparedAlphaCoverageGeometryPtr& previous = {});

    [[nodiscard]] static std::optional<media::VideoFrame> compose(
        int width,
        int height,
        const std::vector<CompositionLayer>& layers,
        FrameCompositionTimings* timings = nullptr);
};

} // namespace rendering
