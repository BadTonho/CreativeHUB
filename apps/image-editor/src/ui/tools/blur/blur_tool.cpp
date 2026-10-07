#include "blur_tool.h"

namespace image_editor {

bool BlurTool::begin(const BrushToolContext& context, int radius) {
    if (drawing() || radius < 0 || radius > ImageDocumentStore::kMaximumBlurRadius)
        return false;
    beginStroke(context);
    radius_ = radius;
    diameter_ = context.diameter;
    clipping_path_ = context.area_selection;
    return true;
}

bool BlurTool::move(const BrushToolContext& context) {
    if (!drawing()) return false;
    appendStrokePoint(context.image_position);
    updateCursor(context, true);
    return true;
}

std::optional<ImageBlurStrokeData> BlurTool::finish(
    const BrushToolContext& context) {
    if (!drawing()) return std::nullopt;
    ImageBlurStrokeData stroke;
    stroke.points = finishStroke(context);
    stroke.diameter = diameter_;
    stroke.radius = radius_;
    stroke.clipping_path = clipping_path_;
    radius_ = 0;
    diameter_ = 12;
    clipping_path_.reset();
    if (stroke.points.isEmpty()) return std::nullopt;
    return stroke;
}

void BlurTool::cancel() noexcept {
    cancelStroke();
    radius_ = 0;
    diameter_ = 12;
    clipping_path_.reset();
}

ImageBlurStrokeData BlurTool::currentStroke() const {
    ImageBlurStrokeData stroke;
    stroke.points = points();
    stroke.diameter = diameter_;
    stroke.radius = radius_;
    stroke.clipping_path = clipping_path_;
    return stroke;
}

} // namespace image_editor
