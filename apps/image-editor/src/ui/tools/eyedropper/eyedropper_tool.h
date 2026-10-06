#pragma once

#include <QColor>
#include <QImage>
#include <QPointF>

#include <optional>

namespace image_editor {

class EyedropperTool final {
public:
    [[nodiscard]] std::optional<QColor> sample(
        const QImage& image, const QPointF& image_position) const;
};

} // namespace image_editor
