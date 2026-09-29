#pragma once

#include "model/composition_document.h"

#include <creative_suite/media/video_frame.h>
#include <creative_suite/media/video_playback.h>

#include <QThread>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace motion::ui {

struct PreviewLayerSnapshot {
    model::LayerId id = 0;
    model::LayerKind kind = model::LayerKind::Image;
    std::filesystem::path source_path;
    std::int64_t local_frame = 0;
    std::int64_t source_frame_count = 0;
    double source_frame_rate = 0.0;
    creative_suite::animation::Transform2D transform;
    creative_suite::animation::TransformKeyframes keyframes;
    model::LayerContent content;
    creative_suite::media::RgbaFramePtr still_frame;
};

struct PreviewRequest {
    model::CanvasSize canvas_size{};
    model::FrameRate frame_rate{};
    std::vector<PreviewLayerSnapshot> layers;
};

enum class PreviewRequestMode : std::uint8_t {
    Interactive,
    Playback,
};

// Serializes decoding on a worker thread and keeps decoder sessions local to
// that thread. Interactive requests cancel stale decodes; playback requests
// replace pending work without interrupting the in-flight playback decode.
class PreviewRenderer final : public QThread {
public:
    using ResultHandler = std::function<void(
        std::uint64_t,
        PreviewRequestMode,
        std::uint64_t,
        creative_suite::media::RgbaFramePtr)>;
    using CancellationPredicate = std::function<bool()>;
    using RenderFunction = std::function<creative_suite::media::RgbaFramePtr(
        const PreviewRequest&,
        const CancellationPredicate&)>;

    PreviewRenderer(QObject* result_receiver,
                    ResultHandler result_handler,
                    RenderFunction render_function = {});
    ~PreviewRenderer() override;

    [[nodiscard]] std::uint64_t submit(
        PreviewRequest request,
        PreviewRequestMode mode = PreviewRequestMode::Interactive);
    void resetSessions();
    [[nodiscard]] std::uint64_t generation() const noexcept;
    [[nodiscard]] bool canPresentResult(
        std::uint64_t request_generation,
        PreviewRequestMode mode,
        std::uint64_t cancellation_generation) const noexcept;
    void stopAndWait();

protected:
    void run() override;

private:
    [[nodiscard]] creative_suite::media::RgbaFramePtr render(
        const PreviewRequest& request,
        std::uint64_t cancellation_generation);
    void reportDecodeError(
        const std::filesystem::path& path,
        std::int64_t source_frame,
        std::string cause,
        std::optional<int> error_code = std::nullopt);

    QObject* result_receiver_ = nullptr;
    ResultHandler result_handler_;
    RenderFunction render_function_;
    struct PendingRequest {
        std::uint64_t generation = 0;
        std::uint64_t cancellation_generation = 0;
        PreviewRequestMode mode = PreviewRequestMode::Interactive;
        PreviewRequest request;
    };
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::optional<PendingRequest> pending_;
    std::optional<PreviewRequestMode> in_flight_mode_;
    std::uint64_t in_flight_generation_ = 0;
    bool reset_sessions_pending_ = false;
    bool stopping_ = false;
    std::atomic<std::uint64_t> generation_{0};
    std::atomic<std::uint64_t> cancellation_generation_{0};
    std::map<std::filesystem::path,
             std::unique_ptr<creative_suite::media::VideoPlaybackSession>> video_sessions_;
    struct CachedContentFrame {
        model::LayerContent content;
        creative_suite::media::RgbaFramePtr frame;
    };
    std::map<model::LayerId, CachedContentFrame> content_frames_;
};

} // namespace motion::ui
