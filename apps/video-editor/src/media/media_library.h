#pragma once

#include "video_metadata.h"
#include "video_frame.h"
#include <creative_suite/media/media_library.h>

namespace media {

using MediaItem = creative_suite::media::MediaItem;
using MediaMutationResult = creative_suite::media::MediaMutationResult;
using MediaLibrary = creative_suite::media::MediaLibrary;
inline constexpr auto default_bin = creative_suite::media::default_bin;

} // namespace media
