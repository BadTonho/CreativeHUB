#pragma once

#include "image_document_store.h"

namespace image_editor {

class LinearGradientTool final {
public:
    [[nodiscard]] bool beginGesture(const QPointF& start,
                                    const QSize& document_size,
                                    const QColor& color) noexcept;
    [[nodiscard]] bool updateGesture(const QPointF& end) noexcept;
    [[nodiscard]] std::optional<ImageLinearGradientData> finishGesture(
        const QPointF& end) noexcept;
    void cancelGesture() noexcept;

    [[nodiscard]] bool gestureActive() const noexcept { return active_; }
    [[nodiscard]] QPointF start() const noexcept { return start_; }
    [[nodiscard]] QPointF end() const noexcept { return end_; }
    [[nodiscard]] QColor color() const noexcept { return color_; }

private:
    [[nodiscard]] QPointF clampToDocument(const QPointF& point) const noexcept;

    QSize document_size_;
    QPointF start_;
    QPointF end_;
    QColor color_;
    bool active_ = false;
};

} // namespace image_editor
