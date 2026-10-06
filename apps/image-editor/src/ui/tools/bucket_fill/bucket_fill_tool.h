#pragma once

#include <QPoint>
#include <QPointF>
#include <QSize>

#include <optional>

namespace image_editor {

class BucketFillTool final {
public:
    [[nodiscard]] std::optional<QPoint> seedAt(
        const QPointF& document_position, const QSize& document_size) const noexcept;
};

} // namespace image_editor
