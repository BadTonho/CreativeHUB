#pragma once

#include <creative_suite/animation/animation.h>
#include <creative_suite/media/video_frame.h>

#include <algorithm>
#include <array>
#include <memory>
#include <vector>
#include <cmath>
#include <functional>
#include <numbers>

namespace creative_suite::composition {

enum class ShapeMaskKind { Rectangle, Ellipse };
struct ShapeMaskParameters {
    double center_x = .5, center_y = .5, width = .5, height = .5;
    double rotation = 0, soft_edge = 0;
    bool invert = false;
    friend bool operator==(const ShapeMaskParameters&, const ShapeMaskParameters&) = default;
};
using ShapeMaskKeyframes = std::array<animation::ScalarKeyframes, 5>;
inline double shapeMaskValue(const ShapeMaskParameters& p, std::size_t i) noexcept {
    switch(i) { case 0:return p.center_x; case 1:return p.center_y; case 2:return p.width; case 3:return p.height; default:return p.rotation; }
}
inline void setShapeMaskValue(ShapeMaskParameters& p, std::size_t i, double v) noexcept {
    switch(i) { case 0:p.center_x=v;break; case 1:p.center_y=v;break; case 2:p.width=v;break; case 3:p.height=v;break; default:p.rotation=v;break; }
}
inline bool validShapeMask(const ShapeMaskParameters& p, const ShapeMaskKeyframes& keys = {}) noexcept {
    if (!std::isfinite(p.soft_edge) || p.soft_edge<0 || p.soft_edge>1) return false;
    for(std::size_t i=0;i<5;++i) {
        const double lo=i<2?-2:i<4?0:-3600, hi=i<2?3:i<4?4:3600;
        const auto v=shapeMaskValue(p,i);
        if(!std::isfinite(v)||v<lo||v>hi||!animation::validScalarKeyframes(keys[i]))return false;
        for(const auto& k:keys[i])if(k.value<lo||k.value>hi)return false;
    }
    return true;
}
inline ShapeMaskParameters evaluateShapeMask(const ShapeMaskParameters& base, const ShapeMaskKeyframes& keys, std::int64_t frame) {
    auto p=base;
    for(std::size_t i=0;i<5;++i)setShapeMaskValue(p,i,animation::evaluateScalar(shapeMaskValue(base,i),keys[i],frame));
    return p;
}
// Optional raster window in the receiver's stable logical pixel space.
// Zero logical dimensions preserve the legacy output-sized coordinate system.
struct MaskRasterSpace {
    int logical_width=0, logical_height=0;
    double origin_x=0, origin_y=0;
    friend bool operator==(const MaskRasterSpace&,const MaskRasterSpace&)=default;
};
// Evaluation-owned cache: at most four geometries / 64 MiB, never frame history.
// Immutable RGBA coverage uses the red channel; VIEW shows RGB with opaque alpha.
class ShapeMaskRasterCache {
    struct Entry { ShapeMaskKind type; ShapeMaskParameters parameters; media::VideoFramePtr frame; MaskRasterSpace space; };
    std::vector<Entry> entries_;
    std::size_t bytes_ = 0;
public:
    static constexpr std::size_t maximum_bytes = 64ULL * 1024 * 1024;
    [[nodiscard]] media::VideoFramePtr coverage(ShapeMaskKind type, const ShapeMaskParameters& p,
        int width, int height, const std::function<bool()>& cancel = {}, MaskRasterSpace space = {}) {
        if (cancel && cancel()) return {};
        if(!validShapeMask(p)||space.logical_width<0||space.logical_height<0||!std::isfinite(space.origin_x)||!std::isfinite(space.origin_y))return {};
        const auto logical_width=space.logical_width?space.logical_width:width;
        const auto logical_height=space.logical_height?space.logical_height:height;
        if ((type != ShapeMaskKind::Rectangle && type != ShapeMaskKind::Ellipse) || width <= 0 || height <= 0 || width > 16384 || height > 16384)
            return {};
        for (const auto& e : entries_)
            if (e.type == type && e.parameters == p && e.frame->width == width && e.frame->height == height && e.space==space)
                return e.frame;
        const auto bytes = static_cast<std::size_t>(width) * height * 4;
        if (bytes > maximum_bytes) return {};
        auto frame = std::make_shared<media::VideoFrame>(media::VideoFrame{width,height,width*4,
            std::vector<std::uint8_t>(bytes)});
        const auto angle = p.rotation * std::numbers::pi / 180.0;
        const auto c = std::cos(angle), s = std::sin(angle);
        const auto rx = p.width * logical_width / 2, ry = p.height * logical_height / 2;
        const auto soft = p.soft_edge * std::min(logical_width,logical_height);
        for (int y=0; y<height; ++y) {
            if (cancel && cancel()) return {};
            for (int x=0; x<width; ++x) {
                double coverage = 0;
                if (rx > 0 && ry > 0) for (double oy : {0.25,0.75}) for (double ox : {0.25,0.75}) {
                    const auto dx=x+ox+space.origin_x-p.center_x*logical_width, dy=y+oy+space.origin_y-p.center_y*logical_height;
                    const auto px=c*dx+s*dy, py=-s*dx+c*dy;
                    double distance;
                    if (type == ShapeMaskKind::Ellipse)
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
        bytes_+=bytes; entries_.push_back({type,p,frame,space});
        return frame;
    }
};
} // namespace creative_suite::composition
