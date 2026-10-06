#include "bucket_fill_tool.h"

#include <cmath>

namespace image_editor {

std::optional<QPoint> BucketFillTool::seedAt(
    const QPointF& document_position, const QSize& document_size) const noexcept {
    if (document_size.width() <= 0 || document_size.height() <= 0 ||
        !std::isfinite(document_position.x()) || !std::isfinite(document_position.y()) ||
        document_position.x() < 0.0 || document_position.y() < 0.0 ||
        document_position.x() >= document_size.width() ||
        document_position.y() >= document_size.height()) return std::nullopt;
    return QPoint(static_cast<int>(std::floor(document_position.x())),
                  static_cast<int>(std::floor(document_position.y())));
}

} // namespace image_editor
