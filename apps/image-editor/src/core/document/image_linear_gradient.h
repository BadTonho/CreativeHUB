#pragma once

#include "image_document_store.h"

#include <QImage>

namespace image_editor {

class ImageLinearGradient final {
public:
    [[nodiscard]] static bool apply(QImage* image,
                                    const ImageLinearGradientData& gradient,
                                    bool* changed = nullptr,
                                    QString* error = nullptr);
};

} // namespace image_editor
