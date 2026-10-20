#pragma once

#include <QString>

#include <cstddef>

namespace image_editor {

class ImageEditorLogger final {
public:
    explicit ImageEditorLogger(QString log_directory = {});

    void logError(const QString& operation,
                  const QString& cause,
                  const QString& path = {},
                  int error_code = 0) const noexcept;

    // Expected resource-capacity recovery is informational, not an error.
    void logRecovery(const QString& operation, const QString& cause) const noexcept;
    [[nodiscard]] QString logPath() const;

private:
    void writeEntry(const QString& severity, const QString& operation,
                    const QString& cause, const QString& path, int error_code) const noexcept;
    void rotateIfNeeded() const noexcept;

    QString log_directory_;
    static constexpr qint64 kMaximumLogSize = 2 * 1024 * 1024;
    static constexpr int kMaximumLogFiles = 3;
};

} // namespace image_editor
