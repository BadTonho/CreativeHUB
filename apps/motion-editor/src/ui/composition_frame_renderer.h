#pragma once

#include "preview_renderer.h"

#include <creative_suite/media/video_playback.h>

#include <map>
#include <memory>
#include <optional>
#include <set>

namespace motion::ui {

// Motion-owned frame evaluation shared by interactive preview and offline
// export. Each instance owns its decoder sessions and content cache and must
// be used from one worker thread at a time.
class CompositionFrameRenderer final {
public:
    using CancellationPredicate = std::function<bool()>;

    void reset();
    [[nodiscard]] creative_suite::media::RgbaFramePtr render(
        const PreviewRequest& request,
        const CancellationPredicate& should_cancel = {},
        bool fail_on_media_error = false);

private:
    void reportDecodeError(
        const std::filesystem::path& path,
        std::int64_t source_frame,
        std::string cause,
        std::optional<int> error_code = std::nullopt);

    std::map<std::filesystem::path,
             std::unique_ptr<creative_suite::media::VideoPlaybackSession>> video_sessions_;
    struct CachedContentFrame {
        model::LayerContent content;
        creative_suite::media::RgbaFramePtr frame;
    };
    std::map<model::LayerId, CachedContentFrame> content_frames_;
};

} // namespace motion::ui
