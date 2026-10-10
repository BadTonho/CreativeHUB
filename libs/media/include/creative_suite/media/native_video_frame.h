#pragma once

#include <creative_suite/media/video_frame.h>
#include <functional>
#include <memory>

namespace creative_suite::media {
enum class NativeVideoFormat { Nv12, Bgra8 };
// Borrowed Windows adapter handles, valid while the immutable frame is retained.
// Neither Windows nor FFmpeg headers cross the public media boundary.
struct D3D11VideoFrameView {
    void* device = nullptr;
    void* texture = nullptr;
    unsigned array_slice = 0;
    int width = 0, height = 0;
    NativeVideoFormat format = NativeVideoFormat::Nv12;
    bool full_range = false;
};
class NativeVideoFrame final {
public:
    ~NativeVideoFrame();
    [[nodiscard]] int width() const noexcept;
    [[nodiscard]] int height() const noexcept;
    [[nodiscard]] D3D11VideoFrameView d3d11_view() const;
    // Serializes device operations with FFmpeg's D3D11 context lock.
    void with_device_lock(const std::function<void()>& operation) const;
    [[nodiscard]] RgbaFramePtr download_rgba() const;
private:
    friend class VideoPlaybackSession;
    friend class NativeVideoFramePool;
    friend class VideoEncoder;
    [[nodiscard]] void* retained_frame() const noexcept;
    struct Impl;
    explicit NativeVideoFrame(std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl_;
};
using NativeVideoFramePtr = std::shared_ptr<const NativeVideoFrame>;
// Worker-owned bounded BGRA D3D11 pool for native encoding. A retained frame
// keeps its device alive; acquire returns empty while all slots are in use.
class NativeVideoFramePool final {
public:
    NativeVideoFramePool(int width, int height, unsigned maximum_frames = 3);
    ~NativeVideoFramePool();
    [[nodiscard]] NativeVideoFramePtr acquire();
    [[nodiscard]] std::uint64_t reserved_bytes() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
struct DecodedVideoFrame {
    RgbaFramePtr rgba;
    NativeVideoFramePtr native;
    [[nodiscard]] int width() const noexcept { return native ? native->width() : rgba ? rgba->width : 0; }
    [[nodiscard]] int height() const noexcept { return native ? native->height() : rgba ? rgba->height : 0; }
};
}
