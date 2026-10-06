#pragma once

#include <QJsonObject>
#include <QString>

namespace image_editor {

class ImageEditorPerformanceLog final {
public:
    explicit ImageEditorPerformanceLog(QString log_directory = {});

    [[nodiscard]] bool append(const QJsonObject& entry,
                              QString* error = nullptr) const noexcept;
    [[nodiscard]] QString logPath() const;

    static constexpr qint64 maximum_file_size_bytes = 2 * 1024 * 1024;
    static constexpr int maximum_file_count = 3;

private:
    [[nodiscard]] bool rotateIfNeeded(qint64 incoming_bytes,
                                      QString* error) const;

    QString log_directory_;
};

} // namespace image_editor
