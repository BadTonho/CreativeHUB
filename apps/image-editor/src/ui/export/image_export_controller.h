#pragma once

#include "image_exporter.h"

class QWidget;

namespace image_editor {

class ImageExportController final {
public:
    [[nodiscard]] static ImageExportResult run(
        QWidget* parent,
        ImageExportSnapshot snapshot,
        QString output_path,
        ImageExportOptions options = {});
};

} // namespace image_editor
