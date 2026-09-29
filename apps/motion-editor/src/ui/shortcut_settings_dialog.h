#pragma once

#include <creative_suite/shortcuts/shortcut_manager.h>

#include <QDialog>

#include <vector>

class QLabel;
class QKeySequenceEdit;

namespace motion::ui {

class ShortcutSettingsDialog final : public QDialog {
public:
    explicit ShortcutSettingsDialog(
        const std::vector<creative_suite::shortcuts::ShortcutEntry>& entries,
        QWidget* parent = nullptr);

    [[nodiscard]] std::vector<creative_suite::shortcuts::ShortcutAssignment>
    assignments() const;

protected:
    void accept() override;

private:
    void resetToDefaults();
    void showValidationMessage(const QString& message);

    std::vector<creative_suite::shortcuts::ShortcutEntry> entries_;
    std::vector<QKeySequenceEdit*> editors_;
    QLabel* validation_label_ = nullptr;
};

} // namespace motion::ui
