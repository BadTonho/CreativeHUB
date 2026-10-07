#pragma once

#include <QImage>
#include <QPainterPath>
#include <QPoint>
#include <QPointF>
#include <QSize>
#include <QString>

#include <optional>

namespace image_editor {

class MagicWandTool final {
public:
    enum class Status { NoSelection, Selected, Rejected };

    struct Result {
        Status status = Status::NoSelection;
        QPainterPath path;
        QString rejection_reason;
    };

    [[nodiscard]] static std::optional<QPoint> seedAt(
        const QPointF& document_position, const QSize& document_size) noexcept;
    [[nodiscard]] Result select(const QImage& target, const QPoint& seed,
                                int tolerance) const;
};

} // namespace image_editor
