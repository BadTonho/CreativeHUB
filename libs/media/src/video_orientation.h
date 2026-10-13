#pragma once

#include <creative_suite/media/media_error.h>
#include <creative_suite/media/video_frame.h>
extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
}
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace creative_suite::media::detail {
// FFmpeg display matrices map source coordinates into presentation coordinates.
// Normalize the resulting bounding box; container translation does not add padding.
struct VideoOrientation {
    double a = 1, b = 0, c = 0, d = 1;
    [[nodiscard]] bool identity() const noexcept { return a == 1 && b == 0 && c == 0 && d == 1; }
    static VideoOrientation fromBytes(const std::uint8_t* data, std::size_t size) {
        if (!data || size < 9 * sizeof(std::int32_t)) throw MediaError("Video display matrix is incomplete.");
        std::array<std::int32_t, 9> matrix{}; std::memcpy(matrix.data(), data, sizeof(matrix));
        const double w = matrix[8] / 1073741824.0;
        if (matrix[2] != 0 || matrix[5] != 0 || w == 0)
            throw MediaError("Perspective or singular video display matrices are unsupported.");
        VideoOrientation result{matrix[0] / (65536.0 * w), matrix[1] / (65536.0 * w),
            matrix[3] / (65536.0 * w), matrix[4] / (65536.0 * w)};
        const double determinant = result.a * result.d - result.b * result.c;
        if (!std::isfinite(determinant) || std::abs(determinant) < 1.0e-12)
            throw MediaError("Video display matrix has invalid geometry.");
        return result;
    }
    static VideoOrientation fromStream(const AVStream& stream) {
        const auto* data = av_packet_side_data_get(stream.codecpar->coded_side_data,
            stream.codecpar->nb_coded_side_data, AV_PKT_DATA_DISPLAYMATRIX);
        return data ? fromBytes(data->data, data->size) : VideoOrientation{};
    }
    [[nodiscard]] VideoOrientation forFrame(const AVFrame& frame) const {
        const auto* data = av_frame_get_side_data(&frame, AV_FRAME_DATA_DISPLAYMATRIX);
        return data ? fromBytes(data->data, data->size) : *this;
    }
    struct Bounds { double left, top; int width, height; };
    [[nodiscard]] Bounds bounds(int width, int height) const {
        if (width <= 0 || height <= 0) throw MediaError("Video orientation requires valid dimensions.");
        const std::array<double, 4> xs{0, a * width, c * height, a * width + c * height};
        const std::array<double, 4> ys{0, b * width, d * height, b * width + d * height};
        const auto [xmin, xmax] = std::minmax_element(xs.begin(), xs.end());
        const auto [ymin, ymax] = std::minmax_element(ys.begin(), ys.end());
        const double out_width = std::ceil(*xmax - *xmin - 1.0e-8);
        const double out_height = std::ceil(*ymax - *ymin - 1.0e-8);
        if (!std::isfinite(out_width) || !std::isfinite(out_height) || out_width < 1 || out_height < 1 ||
            out_width > std::numeric_limits<int>::max() / 4 || out_height > std::numeric_limits<int>::max() ||
            out_height > std::numeric_limits<std::size_t>::max() / (out_width * 4))
            throw MediaError("Oriented video dimensions exceed the supported range.");
        return {*xmin, *ymin, static_cast<int>(out_width), static_cast<int>(out_height)};
    }
    [[nodiscard]] VideoFramePtr apply(VideoFramePtr source) const {
        if (identity()) return source;
        const auto box = bounds(source->width, source->height);
        auto output = std::make_shared<RgbaFrame>();
        output->width = box.width; output->height = box.height; output->stride = box.width * 4;
        output->rgba_pixels.resize(static_cast<std::size_t>(output->stride) * box.height);
        const auto determinant = a * d - b * c;
        for (int y = 0; y < box.height; ++y) for (int x = 0; x < box.width; ++x) {
            const double px = x + .5 + box.left, py = y + .5 + box.top;
            const double sx = (d * px - c * py) / determinant;
            const double sy = (a * py - b * px) / determinant;
            if (sx < 0 || sy < 0 || sx >= source->width || sy >= source->height) continue;
            const auto input_offset = static_cast<std::size_t>(static_cast<int>(sy)) * source->stride + static_cast<int>(sx) * 4;
            const auto output_offset = static_cast<std::size_t>(y) * output->stride + x * 4;
            std::memcpy(output->rgba_pixels.data() + output_offset, source->rgba_pixels.data() + input_offset, 4);
        }
        return output;
    }
};
}
