#pragma once

#include "../brush/brush_tool.h"
#include "image_document_store.h"

#include <optional>

namespace image_editor {

class BlurTool final : public BrushTool {
public:
    [[nodiscard]] bool begin(const BrushToolContext& context, int radius);
    [[nodiscard]] bool move(const BrushToolContext& context);
    [[nodiscard]] std::optional<ImageBlurStrokeData> finish(
        const BrushToolContext& context);
    void cancel() noexcept;

    [[nodiscard]] ImageBlurStrokeData currentStroke() const;

private:
    int diameter_ = 12;
    int radius_ = 10;
    std::optional<QPainterPath> clipping_path_;
};

} // namespace image_editor
