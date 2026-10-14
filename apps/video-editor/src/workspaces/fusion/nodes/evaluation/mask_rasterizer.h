#pragma once

#include "../model/node_graph.h"
#include "media/video_frame.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <numbers>

namespace fusion::nodes {

// Evaluation-owned cache: at most four geometries / 64 MiB, never frame history.
// Immutable RGBA coverage uses the red channel; VIEW shows RGB with opaque alpha.
class MaskRasterCache {
    struct Entry { NodeType type; MaskParameters parameters; media::VideoFramePtr frame; };
    std::vector<Entry> entries_;
    std::size_t bytes_ = 0;
public:
    static constexpr std::size_t maximum_bytes = 64ULL * 1024 * 1024;
    [[nodiscard]] media::VideoFramePtr coverage(NodeType type, const MaskParameters& p,
        int width, int height, const std::function<bool()>& cancel = {}) {
        if (cancel && cancel()) return {};
        if (!isMaskNode(type) || width <= 0 || height <= 0 || width > 16384 || height > 16384)
            return {};
        for (const auto& e : entries_)
            if (e.type == type && e.parameters == p && e.frame->width == width && e.frame->height == height)
                return e.frame;
        const auto bytes = static_cast<std::size_t>(width) * height * 4;
        if (bytes > maximum_bytes) return {};
        auto frame = std::make_shared<media::VideoFrame>(media::VideoFrame{width,height,width*4,
            std::vector<std::uint8_t>(bytes)});
        const auto angle = p.rotation * std::numbers::pi / 180.0;
        const auto c = std::cos(angle), s = std::sin(angle);
        const auto rx = p.width * width / 2, ry = p.height * height / 2;
        const auto soft = p.soft_edge * std::min(width,height);
        for (int y=0; y<height; ++y) {
            if (cancel && cancel()) return {};
            for (int x=0; x<width; ++x) {
                double coverage = 0;
                if (rx > 0 && ry > 0) for (double oy : {0.25,0.75}) for (double ox : {0.25,0.75}) {
                    const auto dx=x+ox-p.center_x*width, dy=y+oy-p.center_y*height;
                    const auto px=c*dx+s*dy, py=-s*dx+c*dy;
                    double distance;
                    if (type == NodeType::EllipseMask)
                        distance=(std::hypot(px/rx,py/ry)-1)*std::min(rx,ry);
                    else {
                        const auto qx=std::abs(px)-rx, qy=std::abs(py)-ry;
                        distance=std::hypot(std::max(qx,0.0),std::max(qy,0.0))+
                            std::min(std::max(qx,qy),0.0);
                    }
                    const auto t = soft > 0 ? std::clamp(0.5-distance/soft,0.0,1.0) : (distance<=0 ? 1.0 : 0.0);
                    coverage += (soft > 0 ? t*t*(3-2*t) : t)*0.25;
                }
                if (p.invert) coverage=1-coverage;
                const auto value=static_cast<std::uint8_t>(std::lround(coverage*255));
                const auto index=static_cast<std::size_t>(y)*frame->stride+x*4;
                for (int channel=0; channel<3; ++channel) frame->rgba_pixels[index+channel]=value;
                frame->rgba_pixels[index+3]=255;
            }
        }
        while (!entries_.empty() && (entries_.size()>=4 || bytes_+bytes>maximum_bytes)) {
            bytes_-=entries_.front().frame->rgba_pixels.size(); entries_.erase(entries_.begin());
        }
        bytes_+=bytes; entries_.push_back({type,p,frame});
        return frame;
    }
};
} // namespace fusion::nodes
