#pragma once

#include "image_raster_import.h"

class QWidget;

namespace image_editor {

enum class ImageImportPurpose {
    Layers,
    Relink,
};

class ImageImportController final {
public:
    [[nodiscard]] static RasterImportResult run(
        QWidget* parent,
        const QStringList& paths,
        ImageImportPurpose purpose = ImageImportPurpose::Layers);
};

} // namespace image_editor
