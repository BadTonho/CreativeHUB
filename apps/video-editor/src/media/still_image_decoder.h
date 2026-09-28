#pragma once

#include <creative_suite/media/still_image_decoder.h>

namespace media {

using StillImageDecoder = creative_suite::media::StillImageDecoder;
inline constexpr auto kStillImageFrameRate = creative_suite::media::kStillImageFrameRate;
inline constexpr auto kStillImageDurationSeconds = creative_suite::media::kStillImageDurationSeconds;
inline constexpr auto kStillImageFrameCount = creative_suite::media::kStillImageFrameCount;

} // namespace media
