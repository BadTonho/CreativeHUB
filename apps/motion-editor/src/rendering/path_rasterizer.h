#pragma once
#include "model/composition_document.h"
#include <QPainterPath>
#include <creative_suite/media/video_frame.h>
#include <functional>
#include <optional>
namespace motion::ui {
struct PathRaster {
    creative_suite::media::RgbaFramePtr frame;
    double origin_x = 0, origin_y = 0;
    creative_suite::composition::MaskRasterSpace space;
};
[[nodiscard]] QPainterPath painterPath(const model::PathGeometry &, double sx = 1, double sy = 1);
[[nodiscard]] std::optional<PathRaster> rasterizePath(const model::ShapeLayerContent &,
                                                      const model::PathGeometry &, double effect_margin = 0,
                                                      const std::function<bool()> &cancel = {},
                                                      model::TrimPathRange trim = {},
                                                      const model::PathLengthTable* measurement = nullptr);
// Per-evaluation cache, shared by primitive and Bezier coverage, four entries / 64 MiB.
class MotionMaskRasterCache {
    struct Entry {
        model::MaskId id;
        creative_suite::composition::ShapeMaskKind kind;
        creative_suite::composition::ShapeMaskParameters parameters;
        std::optional<model::PathGeometry> path;
        int width, height;
        creative_suite::composition::MaskRasterSpace space;
        creative_suite::media::RgbaFramePtr frame;
    };
    std::vector<Entry> entries_;
    std::size_t bytes_ = 0;

  public:
    [[nodiscard]] creative_suite::media::RgbaFramePtr
    coverage(const model::Mask &, std::int64_t frame, int width, int height,
             const std::function<bool()> &cancel = {},
             creative_suite::composition::MaskRasterSpace space = {},
             const model::PathGeometry *override_path = nullptr);
};
} // namespace motion::ui
