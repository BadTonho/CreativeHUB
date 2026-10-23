#pragma once

#include "export/motion_video_export.h"

#include <QDialog>
#include <QString>

#include <filesystem>
#include <optional>
#include <string>

class QLineEdit;
class QLabel;
class QDialogButtonBox;

namespace motion::ui {

class MotionPngSequenceExportDialog final : public QDialog {
public:
    MotionPngSequenceExportDialog(
        QString initial_parent_directory,
        QString initial_directory_name,
        QWidget* parent = nullptr);

    [[nodiscard]] std::optional<MotionPngSequenceSettings> exportSettings() const;

private:
    void browseParentDirectory();
    void updateAcceptState();

    QLineEdit* parent_directory_ = nullptr;
    QLineEdit* directory_name_ = nullptr;
    QLabel* note_ = nullptr;
    QDialogButtonBox* buttons_ = nullptr;
};

} // namespace motion::ui
