#pragma once
#include "../model/node_graph.h"
#include <creative_suite/composition/shape_mask.h>
namespace fusion::nodes {
// Compatibility adapter; all consumers use the same bounded coverage generator.
class MaskRasterCache {
    creative_suite::composition::ShapeMaskRasterCache cache_;
public:
    static constexpr auto maximum_bytes = creative_suite::composition::ShapeMaskRasterCache::maximum_bytes;
    media::VideoFramePtr coverage(NodeType type, const MaskParameters& p, int w, int h,
        const std::function<bool()>& cancel = {}) {
        if(!isMaskNode(type))return {};
        return cache_.coverage(type==NodeType::RectangleMask ? creative_suite::composition::ShapeMaskKind::Rectangle : creative_suite::composition::ShapeMaskKind::Ellipse,p,w,h,cancel);
    }
};
}
