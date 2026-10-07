#pragma once

#include "../image_document_store.h"

#include <QImage>
#include <QString>

namespace image_editor {

class ImageBlurStrokeRenderer final {
public:
    [[nodiscard]] static bool apply(QImage* image,
                                    const ImageBlurStrokeData& stroke,
                                    bool* changed = nullptr,
                                    QString* error = nullptr);
};

} // namespace image_editor
