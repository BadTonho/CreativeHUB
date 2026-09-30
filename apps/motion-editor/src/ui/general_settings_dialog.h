#pragma once

#include <QDialog>

class QCheckBox;

namespace motion::ui {

class GeneralSettingsDialog final : public QDialog {
    Q_OBJECT

public:
    explicit GeneralSettingsDialog(bool preview_metrics_enabled,
                                   QWidget* parent = nullptr);

    [[nodiscard]] bool previewMetricsEnabled() const noexcept;

signals:
    void previewMetricsEnabledChanged(bool enabled);

private:
    QCheckBox* preview_metrics_checkbox_ = nullptr;
};

} // namespace motion::ui
