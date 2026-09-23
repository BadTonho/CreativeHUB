#include "frame_compositor.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace rendering {
namespace {

struct Color {
    double red = 0.0;
    double green = 0.0;
    double blue = 0.0;
    double alpha = 0.0;
};

Color sampleNearest(const media::VideoFrame& frame, double x, double y) noexcept {
    if (frame.width <= 0 || frame.height <= 0 || frame.stride < frame.width * 4 ||
        frame.rgba_pixels.size() < static_cast<std::size_t>(frame.stride) * frame.height) {
        return {};
    }
    const auto source_x = std::clamp(
        static_cast<int>(std::floor(x)), 0, frame.width - 1);
    const auto source_y = std::clamp(
        static_cast<int>(std::floor(y)), 0, frame.height - 1);
    const auto* pixel = frame.rgba_pixels.data() +
        static_cast<std::size_t>(source_y) * frame.stride +
        static_cast<std::size_t>(source_x) * 4;
    return Color{
        pixel[0] / 255.0,
        pixel[1] / 255.0,
        pixel[2] / 255.0,
        pixel[3] / 255.0};
}

void blend(std::uint8_t* destination, const Color& source) noexcept {
    const double source_alpha = std::clamp(source.alpha, 0.0, 1.0);
    if (source_alpha <= 0.0) return;
    if (source_alpha >= 1.0) {
        destination[0] = static_cast<std::uint8_t>(std::lround(
            std::clamp(source.red, 0.0, 1.0) * 255.0));
        destination[1] = static_cast<std::uint8_t>(std::lround(
            std::clamp(source.green, 0.0, 1.0) * 255.0));
        destination[2] = static_cast<std::uint8_t>(std::lround(
            std::clamp(source.blue, 0.0, 1.0) * 255.0));
        destination[3] = 255;
        return;
    }
    const double destination_alpha = destination[3] / 255.0;
    const double output_alpha = source_alpha + destination_alpha * (1.0 - source_alpha);
    if (output_alpha <= 0.0) return;
    const auto output = [source_alpha, destination_alpha, output_alpha](double source_value,
                                                                         double destination_value) {
        return (source_value * source_alpha +
                destination_value * destination_alpha * (1.0 - source_alpha)) /
            output_alpha;
    };
    destination[0] = static_cast<std::uint8_t>(std::lround(std::clamp(output(
        source.red, destination[0] / 255.0), 0.0, 1.0) * 255.0));
    destination[1] = static_cast<std::uint8_t>(std::lround(std::clamp(output(
        source.green, destination[1] / 255.0), 0.0, 1.0) * 255.0));
    destination[2] = static_cast<std::uint8_t>(std::lround(std::clamp(output(
        source.blue, destination[2] / 255.0), 0.0, 1.0) * 255.0));
    destination[3] = static_cast<std::uint8_t>(std::lround(output_alpha * 255.0));
}

bool isOpaqueFrame(const media::VideoFrame& frame) noexcept {
    if (frame.width <= 0 || frame.height <= 0 || frame.stride < frame.width * 4 ||
        frame.rgba_pixels.size() < static_cast<std::size_t>(frame.stride) * frame.height) {
        return false;
    }
    for (int y = 0; y < frame.height; ++y) {
        const auto* row = frame.rgba_pixels.data() +
            static_cast<std::size_t>(y) * frame.stride;
        for (int x = 0; x < frame.width; ++x) {
            if (row[static_cast<std::size_t>(x) * 4 + 3] != 255) return false;
        }
    }
    return true;
}

bool isFullFrameIdentity(
    const media::VideoFrame& frame,
    int width,
    int height,
    const timeline::Transform2D& transform) noexcept {
    return frame.width == width && frame.height == height &&
        frame.stride == width * 4 &&
        transform.position_x == 0.5 && transform.position_y == 0.5 &&
        transform.scale == 1.0 && transform.rotation_degrees == 0.0 &&
        transform.opacity == 1.0;
}

struct PixelRange {
    int begin = 0;
    int end = -1;
};

std::optional<PixelRange> axisAlignedPixelRange(
    double center,
    double displayed_size,
    int limit) noexcept {
    if (displayed_size <= 0.0 || limit <= 0) return std::nullopt;
    const auto first = std::ceil(center - displayed_size * 0.5 - 0.5);
    const auto last = std::floor(center + displayed_size * 0.5 - 0.5);
    if (first > last || last < 0.0 || first > static_cast<double>(limit - 1)) {
        return std::nullopt;
    }
    return PixelRange{
        static_cast<int>(std::clamp(first, 0.0, static_cast<double>(limit - 1))),
        static_cast<int>(std::clamp(last, 0.0, static_cast<double>(limit - 1)))};
}

std::vector<int> buildSourceLookup(
    int begin,
    int end,
    double center,
    double displayed_size,
    int source_size) {
    std::vector<int> lookup;
    if (begin > end || displayed_size <= 0.0 || source_size <= 0) return lookup;
    lookup.reserve(static_cast<std::size_t>(end - begin + 1));
    for (int destination = begin; destination <= end; ++destination) {
        const double source =
            ((static_cast<double>(destination) + 0.5 - center) / displayed_size + 0.5) *
            static_cast<double>(source_size);
        lookup.push_back(std::clamp(
            static_cast<int>(std::floor(source)), 0, source_size - 1));
    }
    return lookup;
}

void composeAlphaCoverageLayer(
    media::VideoFrame& output,
    const CompositionLayer& layer) {
    const auto& frame = *layer.frame;
    const auto& coverage = *layer.alpha_coverage;
    if (layer.transform.opacity <= 0.0 || coverage.rows.empty()) return;

    const double fit = std::min(
        static_cast<double>(output.width) / frame.width,
        static_cast<double>(output.height) / frame.height);
    const double displayed_width = frame.width * fit * layer.transform.scale;
    const double displayed_height = frame.height * fit * layer.transform.scale;
    if (displayed_width <= 0.0 || displayed_height <= 0.0) return;

    const double center_x = layer.transform.position_x * output.width;
    const double center_y = layer.transform.position_y * output.height;
    const auto horizontal = axisAlignedPixelRange(
        center_x, displayed_width, output.width);
    const auto vertical = axisAlignedPixelRange(
        center_y, displayed_height, output.height);
    if (!horizontal.has_value() || !vertical.has_value()) return;

    const auto source_x_lookup = buildSourceLookup(
        horizontal->begin,
        horizontal->end,
        center_x,
        displayed_width,
        frame.width);
    const auto source_y_lookup = buildSourceLookup(
        vertical->begin,
        vertical->end,
        center_y,
        displayed_height,
        frame.height);
    if (source_x_lookup.empty() || source_y_lookup.empty()) return;

    std::vector<std::vector<PixelRange>> mapped_rows(coverage.rows.size());
    for (std::size_t source_y = 0; source_y < coverage.rows.size(); ++source_y) {
        for (const auto& span : coverage.rows[source_y]) {
            const auto first = std::lower_bound(
                source_x_lookup.begin(), source_x_lookup.end(), span.begin);
            const auto last = std::lower_bound(
                source_x_lookup.begin(), source_x_lookup.end(), span.end);
            if (first == last) continue;
            mapped_rows[source_y].push_back(PixelRange{
                horizontal->begin + static_cast<int>(first - source_x_lookup.begin()),
                horizontal->begin + static_cast<int>(last - source_x_lookup.begin()) - 1});
        }
    }

    for (int destination_y = vertical->begin;
         destination_y <= vertical->end;
         ++destination_y) {
        const auto source_y = source_y_lookup[
            static_cast<std::size_t>(destination_y - vertical->begin)];
        if (source_y < 0 || source_y >= static_cast<int>(mapped_rows.size())) continue;
        const auto* source_row = frame.rgba_pixels.data() +
            static_cast<std::size_t>(source_y) * frame.stride;
        for (const auto& range : mapped_rows[static_cast<std::size_t>(source_y)]) {
            for (int destination_x = range.begin;
                 destination_x <= range.end;
                 ++destination_x) {
                const auto source_x = source_x_lookup[
                    static_cast<std::size_t>(destination_x - horizontal->begin)];
                const auto* source_pixel = source_row +
                    static_cast<std::size_t>(source_x) * 4;
                auto color = Color{
                    source_pixel[0] / 255.0,
                    source_pixel[1] / 255.0,
                    source_pixel[2] / 255.0,
                    source_pixel[3] / 255.0};
                color.alpha *= layer.transform.opacity;
                auto* destination = output.rgba_pixels.data() +
                    static_cast<std::size_t>(destination_y) * output.stride +
                    static_cast<std::size_t>(destination_x) * 4;
                blend(destination, color);
            }
        }
    }
}

} // namespace

AlphaCoveragePtr FrameCompositor::buildAlphaCoverage(
    const media::VideoFrame& frame) {
    if (frame.width <= 0 || frame.height <= 0 ||
        frame.stride < frame.width * 4 ||
        frame.rgba_pixels.size() < static_cast<std::size_t>(frame.stride) * frame.height) {
        return nullptr;
    }

    auto coverage = std::make_shared<AlphaCoverage>();
    coverage->width = frame.width;
    coverage->height = frame.height;
    coverage->rows.resize(static_cast<std::size_t>(frame.height));
    for (int y = 0; y < frame.height; ++y) {
        const auto* row = frame.rgba_pixels.data() +
            static_cast<std::size_t>(y) * frame.stride;
        auto& spans = coverage->rows[static_cast<std::size_t>(y)];
        int span_begin = -1;
        for (int x = 0; x < frame.width; ++x) {
            const bool visible = row[static_cast<std::size_t>(x) * 4 + 3] != 0;
            if (visible && span_begin < 0) {
                span_begin = x;
            } else if (!visible && span_begin >= 0) {
                spans.push_back(AlphaSpan{span_begin, x});
                span_begin = -1;
            }
        }
        if (span_begin >= 0) spans.push_back(AlphaSpan{span_begin, frame.width});
    }
    return coverage;
}

bool FrameCompositor::canUseAlphaCoverageFastPath(
    const CompositionLayer& layer) noexcept {
    return layer.frame != nullptr &&
        layer.alpha_coverage != nullptr &&
        timeline::validTransform(layer.transform) &&
        layer.transform.rotation_degrees == 0.0 &&
        layer.frame->width > 0 &&
        layer.frame->height > 0 &&
        layer.frame->stride >= layer.frame->width * 4 &&
        layer.frame->rgba_pixels.size() >=
            static_cast<std::size_t>(layer.frame->stride) * layer.frame->height &&
        layer.alpha_coverage->width == layer.frame->width &&
        layer.alpha_coverage->height == layer.frame->height &&
        layer.alpha_coverage->rows.size() ==
            static_cast<std::size_t>(layer.frame->height);
}

std::optional<media::VideoFrame> FrameCompositor::compose(
    int width,
    int height,
    const std::vector<CompositionLayer>& layers) {
    if (width <= 0 || height <= 0) return std::nullopt;
    if (static_cast<std::size_t>(width) >
            std::numeric_limits<std::size_t>::max() / static_cast<std::size_t>(height) / 4) {
        return std::nullopt;
    }

    media::VideoFrame output;
    output.width = width;
    output.height = height;
    output.stride = width * 4;
    output.rgba_pixels.assign(
        static_cast<std::size_t>(output.stride) * output.height, 0);
    for (auto& pixel : output.rgba_pixels) pixel = 0;
    for (std::size_t index = 3; index < output.rgba_pixels.size(); index += 4) {
        output.rgba_pixels[index] = 255;
    }

    for (const auto& layer : layers) {
        if (layer.frame == nullptr || !timeline::validTransform(layer.transform) ||
            layer.frame->width <= 0 || layer.frame->height <= 0) {
            continue;
        }
        if (isFullFrameIdentity(*layer.frame, width, height, layer.transform) &&
            isOpaqueFrame(*layer.frame)) {
            std::memcpy(
                output.rgba_pixels.data(),
                layer.frame->rgba_pixels.data(),
                output.rgba_pixels.size());
            continue;
        }
        if (canUseAlphaCoverageFastPath(layer)) {
            composeAlphaCoverageLayer(output, layer);
            continue;
        }
        const double fit = std::min(
            static_cast<double>(width) / layer.frame->width,
            static_cast<double>(height) / layer.frame->height);
        const double displayed_width = layer.frame->width * fit * layer.transform.scale;
        const double displayed_height = layer.frame->height * fit * layer.transform.scale;
        if (displayed_width <= 0.0 || displayed_height <= 0.0) continue;
        const double center_x = layer.transform.position_x * width;
        const double center_y = layer.transform.position_y * height;
        const double angle = layer.transform.rotation_degrees * 3.14159265358979323846 / 180.0;
        const double cosine = std::cos(angle);
        const double sine = std::sin(angle);
        const double radius = std::hypot(displayed_width, displayed_height) * 0.5;
        const auto left = std::max(0, static_cast<int>(std::floor(center_x - radius - 1.0)));
        const auto right = std::min(width - 1, static_cast<int>(std::ceil(center_x + radius + 1.0)));
        const auto top = std::max(0, static_cast<int>(std::floor(center_y - radius - 1.0)));
        const auto bottom = std::min(height - 1, static_cast<int>(std::ceil(center_y + radius + 1.0)));
        for (int y = top; y <= bottom; ++y) {
            for (int x = left; x <= right; ++x) {
                const double dx = x + 0.5 - center_x;
                const double dy = y + 0.5 - center_y;
                const double unrotated_x = cosine * dx + sine * dy;
                const double unrotated_y = -sine * dx + cosine * dy;
                if (std::abs(unrotated_x) > displayed_width * 0.5 ||
                    std::abs(unrotated_y) > displayed_height * 0.5) {
                    continue;
                }
                const double source_x =
                    (unrotated_x / displayed_width + 0.5) * layer.frame->width;
                const double source_y =
                    (unrotated_y / displayed_height + 0.5) * layer.frame->height;
                auto color = sampleNearest(*layer.frame, source_x, source_y);
                color.alpha *= layer.transform.opacity;
                auto* destination = output.rgba_pixels.data() +
                    static_cast<std::size_t>(y) * output.stride +
                    static_cast<std::size_t>(x) * 4;
                blend(destination, color);
            }
        }
    }
    return output;
}

} // namespace rendering
