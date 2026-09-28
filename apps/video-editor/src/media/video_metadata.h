#pragma once

#include "video_frame.h"
#include <creative_suite/media/video_metadata.h>

#include <filesystem>
#include <optional>
#include <string>

namespace media {

using MediaKind = creative_suite::media::MediaKind;
using AudioMetadata = creative_suite::media::AudioMetadata;
using VideoMetadata = creative_suite::media::VideoMetadata;
using MediaError = creative_suite::media::MediaError;

// Video Editor-only link state is deliberately kept out of the shared media
// catalog and is stored by the Video Editor project adapter.
struct LinkedImageReference {
    std::string id;
    std::filesystem::path document_path;
    std::filesystem::path published_output_path;

    friend bool operator==(const LinkedImageReference&, const LinkedImageReference&) = default;
};

} // namespace media
