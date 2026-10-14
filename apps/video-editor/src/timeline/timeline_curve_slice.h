#pragma once
#include "timeline_transform.h"
namespace timeline {
// Uses the timeline's existing De Casteljau slicing. Unchanged curves retain
// their original records; partial segments receive equivalent easing handles.
[[nodiscard]] TransformKeyframes sliceTransformCurves(const Transform2D &,
                                                      const TransformKeyframes &,
                                                      std::int64_t start, std::int64_t duration,
                                                      Transform2D &base);
} // namespace timeline
