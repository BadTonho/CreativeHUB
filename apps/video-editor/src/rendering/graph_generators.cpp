#include <creative_suite/composition_exchange/raster.h>
#include "graph_generators.h"
#include "text_renderer.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace rendering {
namespace {
media::VideoFrame canvas(int width, int height) {
    if (width <= 0 || height <= 0 || width > std::numeric_limits<int>::max() / 4 ||
        static_cast<std::size_t>(height) > std::numeric_limits<std::size_t>::max() / (width * 4))
        throw std::runtime_error("Invalid Fusion generator canvas.");
    return {width, height, width * 4, std::vector<std::uint8_t>(static_cast<std::size_t>(width) * height * 4)};
}
media::VideoFrame fit(const media::VideoFrame& source, int width, int height, bool text,
    const std::function<bool()>& canceled) {
    auto result = canvas(width, height);
    const double scale = text ? 1.0 : std::min(double(width) / source.width, double(height) / source.height);
    const int fw = std::max(1, int(std::lround(source.width * scale)));
    const int fh = std::max(1, int(std::lround(source.height * scale)));
    const int left = (width - fw) / 2, top = (height - fh) / 2;
    for (int y = std::max(0, top); y < std::min(height, top + fh); ++y) {
        if (canceled && canceled()) return result;
        for (int x = std::max(0, left); x < std::min(width, left + fw); ++x) {
            const int sx = std::clamp(int((x - left) / scale), 0, source.width - 1);
            const int sy = std::clamp(int((y - top) / scale), 0, source.height - 1);
            std::copy_n(source.rgba_pixels.data() + std::size_t(sy) * source.stride + sx * 4, 4,
                result.rgba_pixels.data() + std::size_t(y) * result.stride + x * 4);
        }
    }
    return result;
}
}
void prepareGraphFrames(const fusion::nodes::NodeGraph& graph, fusion::nodes::InputFrames& inputs,
    int width, int height, bool independent, const std::function<bool()>& canceled, std::int64_t clock) {
    const bool portable = std::any_of(graph.nodes.begin(), graph.nodes.end(),
        [](const auto& node) { return node.presentation.has_value(); });
    if (portable) {
        const auto active = [clock](const auto& node) {
            return !node.presentation || (node.presentation->visible && clock >= node.presentation->start &&
                (node.presentation->duration == 0 || static_cast<long double>(clock)-node.presentation->start < node.presentation->duration));
        };
        std::uint64_t bytes = 0;
        for (auto it=inputs.begin(); it!=inputs.end();) {
            const auto* node = fusion::nodes::findNode(graph,it->first);
            if (node && !active(*node)) { it=inputs.erase(it); continue; }
            if (it->second) bytes += independent && (!node || !node->presentation || node->presentation->profile == creative_suite::composition_exchange::Profile::FusionCanvas)
                ? std::uint64_t(width)*height*4 : it->second->rgba_pixels.size();
            ++it;
        }
        for (const auto& node : graph.nodes) {
            if (!active(node)) continue;
            if (node.type == fusion::nodes::NodeType::Background || node.type == fusion::nodes::NodeType::Text) {
                const bool boxed = node.type == fusion::nodes::NodeType::Text && node.presentation && node.presentation->profile == creative_suite::composition_exchange::Profile::MotionLayer;
                bytes += boxed ? std::uint64_t(node.presentation->text_width)*node.presentation->text_height*4 : std::uint64_t(width)*height*4;
            }
        }
        if (canceled && canceled()) return;
        if (width <= 0 || height <= 0 || bytes > 256ULL*1024*1024)
            throw std::runtime_error("Interchange source preparation exceeds the 256 MiB frame budget.");
    }
    if (independent) for (auto& [id, frame] : inputs) {
        if (canceled && canceled()) return;
        const auto* node = fusion::nodes::findNode(graph,id);
        if (node && node->presentation && node->presentation->profile == creative_suite::composition_exchange::Profile::MotionLayer) continue;
        if (frame && frame->width > 0 && frame->height > 0 &&
            (frame->width != width || frame->height != height))
            frame = std::make_shared<const media::VideoFrame>(portable
                ? creative_suite::composition_exchange::fit(*frame, width, height, false, canceled)
                : fit(*frame, width, height, false, canceled));
    }
    for (const auto& node : graph.nodes) {
        if (canceled && canceled()) return;
        if (node.presentation && (!node.presentation->visible || clock < node.presentation->start || (node.presentation->duration > 0 && static_cast<long double>(clock)-node.presentation->start >= node.presentation->duration))) { inputs.erase(node.id); continue; }
        if (node.type == fusion::nodes::NodeType::Background) {
            auto frame = canvas(width, height);
            for (int y = 0; y < height; ++y) {
                if (canceled && canceled()) return;
                for (int x = 0; x < width; ++x)
                    std::copy_n(node.background_color.data(), 4,
                        frame.rgba_pixels.data() + std::size_t(y) * frame.stride + x * 4);
            }
            inputs[node.id] = std::make_shared<const media::VideoFrame>(std::move(frame));
        } else if (node.type == fusion::nodes::NodeType::Text) {
            auto text = node.presentation ? std::optional<media::VideoFrame>(creative_suite::composition_exchange::textFrame({node.text.content,node.text.font_family,node.text.font_size_pixels,int(node.text.alignment),node.text.color}, *node.presentation,width,height)) : renderText(node.text, width, height);
            if (!text) throw std::runtime_error("A Fusion Text node could not be rasterized.");
            inputs[node.id] = std::make_shared<const media::VideoFrame>(node.presentation && node.presentation->profile == creative_suite::composition_exchange::Profile::MotionLayer ? std::move(*text) : fit(*text, width, height, true, canceled));
        }
    }
}
} // namespace rendering
