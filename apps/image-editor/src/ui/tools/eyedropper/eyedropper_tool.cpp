#include "eyedropper_tool.h"

#include <algorithm>
#include <cmath>

namespace image_editor {

std::optional<QColor> EyedropperTool::sample(
    const QImage& image, const QPointF& image_position) const {
    if (image.isNull() || image.width() <= 0 || image.height() <= 0)
        return std::nullopt;

    const int x = std::clamp(static_cast<int>(std::floor(image_position.x())),
                             0, image.width() - 1);
    const int y = std::clamp(static_cast<int>(std::floor(image_position.y())),
                             0, image.height() - 1);
    const QColor sampled = image.pixelColor(x, y);
    if (sampled.alpha() == 0) return std::nullopt;
    return sampled;
}

} // namespace image_editor
