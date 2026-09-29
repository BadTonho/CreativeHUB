#pragma once

#include "composition_document.h"

#include <creative_suite/media/media_library.h>

#include <filesystem>
#include <string>
#include <vector>

namespace motion::model {

struct MotionMediaEntryData {
    std::filesystem::path source_path;
    creative_suite::media::MediaKind kind = creative_suite::media::MediaKind::Video;
    std::string display_name;
    std::string bin_path = std::string(creative_suite::media::default_bin);

    friend bool operator==(const MotionMediaEntryData&, const MotionMediaEntryData&) = default;
};

// Serializable document state. Decoded frames, thumbnails, selection, and
// timeline navigation state intentionally remain outside the native document.
struct MotionProjectData {
    CompositionSettings composition;
    std::vector<CompositionLayer> layers;
    std::vector<std::string> bins{std::string(creative_suite::media::default_bin)};
    std::vector<MotionMediaEntryData> media;

    friend bool operator==(const MotionProjectData&, const MotionProjectData&) = default;
};

} // namespace motion::model
