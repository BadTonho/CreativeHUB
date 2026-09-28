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
#include <string>
#include <vector>

namespace motion::ui {

struct PreviewLayerSnapshot {
    model::LayerKind kind = model::LayerKind::Image;
    std::filesystem::path source_path;
    std::int64_t local_frame = 0;
    std::int64_t source_frame_count = 0;
    double source_frame_rate = 0.0;
    creative_suite::animation::Transform2D transform;
    creative_suite::media::RgbaFramePtr still_frame;
};

struct PreviewRequest {
    model::CanvasSize canvas_size{};
    model::FrameRate frame_rate{};
    std::vector<PreviewLayerSnapshot> layers;
};

// Serializes decoding on a worker thread and keeps decoder sessions local to
// that thread. New requests replace pending work and cancel stale decodes.
class PreviewRenderer final : public QThread {
public:
    using ResultHandler = std::function<void(
        std::uint64_t,
        creative_suite::media::RgbaFramePtr)>;

    PreviewRenderer(QObject* result_receiver, ResultHandler result_handler);
    ~PreviewRenderer() override;

    [[nodiscard]] std::uint64_t submit(PreviewRequest request);
    void resetSessions();
    [[nodiscard]] std::uint64_t generation() const noexcept;
    void stopAndWait();

protected:
    void run() override;

private:
    [[nodiscard]] creative_suite::media::RgbaFramePtr render(
        const PreviewRequest& request,
        std::uint64_t request_generation);
    void reportDecodeError(
        const std::filesystem::path& path,
        std::int64_t source_frame,
        std::string cause,
        std::optional<int> error_code = std::nullopt);

    QObject* result_receiver_ = nullptr;
    ResultHandler result_handler_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::optional<std::pair<std::uint64_t, PreviewRequest>> pending_;
    bool reset_sessions_pending_ = false;
    bool stopping_ = false;
    std::atomic<std::uint64_t> generation_{0};
    std::map<std::filesystem::path,
             std::unique_ptr<creative_suite::media::VideoPlaybackSession>> video_sessions_;
};

} // namespace motion::ui
