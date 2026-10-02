#pragma once

#include <creative_suite/composition/frame_compositor.h>

#include <functional>
#include <memory>
#include <string>

class QOffscreenSurface;

namespace creative_suite::composition {

enum class OpenGlCompositionStatus { Complete, Cancelled, Unsupported, Failed };
// CoreOnly also permits capability/fallback regression on drivers advertising
// extensions. Rotated requests require precise FP64 arithmetic in Automatic.
enum class OpenGlPrecisionPolicy { Automatic, CoreOnly };

struct OpenGlCompositionTimings {
    // CPU wall time: submission is not a GPU execution timer. Readback includes
    // the wait for submitted work and the conversion to top-down RGBA rows.
    std::uint64_t upload_nanoseconds = 0;
    std::uint64_t draw_submission_nanoseconds = 0;
    std::uint64_t readback_nanoseconds = 0;
    std::uint64_t uploaded_bytes = 0;
    std::uint64_t readback_bytes = 0;
    std::uint64_t uploaded_layers = 0;
};

struct OpenGlCompositionResult {
    OpenGlCompositionStatus status = OpenGlCompositionStatus::Failed;
    std::optional<media::RgbaFrame> frame;
    std::string operation;
    std::string cause;
    std::int64_t error_code = 0;
};

// Optional Qt/OpenGL adapter. The CPU compositor and frame contract remain Qt
// independent. Source pixels and the GUI-created surface are borrowed.
class OpenGlFrameCompositor final {
public:
    using CancellationPredicate = std::function<bool()>;

    // Call and destroy the returned surface on the GUI thread. Keep it alive
    // until every compositor using it has been destroyed on its worker thread.
    [[nodiscard]] static std::unique_ptr<QOffscreenSurface> createSurface();

    // Construct, compose, and destroy on one worker thread. Context and all GL
    // resources are created lazily there; no resource is shared with the viewer.
    explicit OpenGlFrameCompositor(QOffscreenSurface* surface,
        OpenGlPrecisionPolicy precision = OpenGlPrecisionPolicy::Automatic);
    ~OpenGlFrameCompositor();
    OpenGlFrameCompositor(const OpenGlFrameCompositor&) = delete;
    OpenGlFrameCompositor& operator=(const OpenGlFrameCompositor&) = delete;

    // Same raster contract as FrameCompositor. Invalid layers contribute no
    // pixels. Device/request limits return Unsupported; technical errors return
    // Failed. Neither status contains a partially composed frame.
    [[nodiscard]] OpenGlCompositionResult compose(
        int width, int height, const std::vector<CompositionLayer>& layers,
        const CancellationPredicate& cancel = {},
        OpenGlCompositionTimings* timings = nullptr);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace creative_suite::composition
