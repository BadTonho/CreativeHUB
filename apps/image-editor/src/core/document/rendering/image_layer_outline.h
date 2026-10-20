#pragma once
#include "image_document_store.h"
#include <QImage>
#include <atomic>

namespace image_editor {
// Operates on masked, premultiplied layer pixels before layer/group opacity.
// Null means cancelled/allocation failure. The input is never modified.
[[nodiscard]] QImage applyLayerOutline(const QImage& pixels,
    const ImageLayerOutlineData& outline, const std::atomic_bool* cancel = nullptr);
}
