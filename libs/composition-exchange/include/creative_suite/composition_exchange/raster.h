#pragma once
#include "document.h"
#include <creative_suite/media/video_frame.h>
namespace creative_suite::composition_exchange {
// Worker-only helpers. Returned straight RGBA owners contain no GUI resources.
[[nodiscard]] media::RgbaFrame transparent(int width, int height);
[[nodiscard]] media::RgbaFrame fit(const media::RgbaFrame &, int width, int height,
                                   bool intrinsic = false, Cancel = {});
[[nodiscard]] media::RgbaFrame textFrame(const Text &, const Presentation &, int width, int height);
[[nodiscard]] media::RgbaFrame transformFrame(const media::RgbaFrame &, int width, int height,
                                              const animation::Transform2D &, Profile,
                                              bool intrinsic = false, Cancel = {});
} // namespace creative_suite::composition_exchange
