#pragma once

#include "image_document_store.h"

#include <QImage>

namespace image_editor {

class ImageBucketFill final {
public:
    // Applies a non-destructive fill operation to the current operation replay
    // image. A false return means the operation could not be rendered safely.
    [[nodiscard]] static bool apply(QImage* image,
                                    const ImageBucketFillData& fill,
                                    bool* changed = nullptr,
                                    QString* error = nullptr);
};

} // namespace image_editor
